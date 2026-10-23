#pragma once
// 第 12 章：Algorithmic reverb —— 8 線 FDN（Feedback Delay Network）
//
// 架構（每個 sample）：
//
//   in L/R ──► pre-delay ──► input diffusion（4 級 allpass）──┐
//                                                              ▼
//        ┌─────────────────────────────────────────────┐   + ──► delay_i ──► v_i ─┐
//        │   feedback：u = A·v  （A = Householder 矩陣，lossless）                 │
//        │   u_i → damping lowpass_i → × g_i（decay）→ 寫回 delay_i               │
//        └───────────────────────────────────────────────────────────────────────┘
//   out L = Σ (±) v_even , out R = Σ (±) v_odd  ──► width（mid/side）──► mix with dry
//
// 公式對照：
//   g_i = 10^(-3 · L_i / (RT60 · fs))            每條線每繞一圈衰減的量（和 comb 一樣）
//   A   = I - (2/N)·1·1ᵀ                          Householder 反射矩陣，Aᵀ A = I（能量守恆）
//   damping：one-pole lowpass（第 02 章 OnePole）放在迴路裡 → 高頻每繞一圈多衰減一點 → 高頻 RT60 較短
//
// 另一種常見架構 Freeverb（8 parallel comb + 4 series allpass）在 README 說明，
// 它的元件（CombFilter / AllpassDiffuser）在 ReverbBuildingBlocks.h。
//
// 使用第 09 章的 FractionalDelayLine（delay 線，可做平滑的 size 變化與 modulation）與
// 第 02 章的 OnePole（damping）。

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>
#include <dawlab/dsp/FractionalDelayLine.h>
#include <dawlab/dsp/OnePole.h>
#include <dawlab/dsp/ReverbBuildingBlocks.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace dawlab::dsp {

class Reverb {
public:
    static constexpr int kNumLines = 8;         // FDN 的 delay 線數量
    static constexpr int kNumInputDiffusers = 4; // 每個 channel 的輸入 allpass 級數
    static constexpr float kMaxPreDelayMs = 250.0f;
    static constexpr float kMaxModulationMs = 2.0f; // 內建 modulation 的最大深度（練習 4 會再深入）

    // ---- 標準介面 ---------------------------------------------------------------
    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        (void)maxBlockSize;
        sampleRate_ = sampleRate;
        numChannels_ = std::clamp(numChannels, 1, 2);

        // delay 線：最長 = 最大 size 時的長度 + 取質數的餘裕 + modulation 餘裕 + 插值餘裕
        maxModulationSamples_ = msToSamples(kMaxModulationMs, sampleRate);
        maxLineDelay_ = msToSamples(kBaseDelaysMs[kNumLines - 1] * sizeToScale(1.0f), sampleRate) + 64 + maxModulationSamples_ + 4;
        lines_.prepare(maxLineDelay_, kNumLines);
        preDelay_.prepare(msToSamples(kMaxPreDelayMs, sampleRate) + 4, 2);
        damping_.prepare(sampleRate, 1, kNumLines);
        damping_.setType(OnePole::Type::Lowpass);

        for (int ch = 0; ch < 2; ++ch)
            for (int k = 0; k < kNumInputDiffusers; ++k) {
                AllpassDiffuser& ap = diffusers_[static_cast<size_t>(ch)][static_cast<size_t>(k)];
                // 左右聲道的 diffuser 長度略有不同（×1.0 / ×1.07），讓 stereo 更寬
                const double ms = kDiffuserMs[k] * (ch == 0 ? 1.0 : 1.07);
                ap.prepare(msToSamples(ms, sampleRate) + 1);
                ap.setDelaySamples(msToSamples(ms, sampleRate));
                ap.setGain(0.5f);
            }

        // 參數平滑的係數（約 30 ms 時間常數）
        smoothCoef_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.03 * sampleRate)));
        lfoIncrement_ = static_cast<float>(kTwoPi * modulationRateHz_ / sampleRate);

        // 與 sample rate 有關的參數全部重算（setter 可能在 prepare 之前就被呼叫過）
        updateDelayTargets();
        updateDecayGains();
        setDampingHz(dampingHz_);
        setPreDelayMs(preDelayMs_);
        setModulationDepthMs(modulationDepthMs_);
        reset();
    }

    void reset() noexcept {
        lines_.reset();
        preDelay_.reset();
        damping_.reset();
        for (auto& chDiffusers : diffusers_)
            for (auto& ap : chDiffusers)
                ap.reset();
        lineOut_.fill(0.0f);
        lfoPhase_ = 0.0f;
        // 平滑中的參數直接跳到目標值：reset 之後沒有任何聲音殘留，跳值不會有 click，
        // 也避免「prepare 之後才設定參數」時，第一段輸出出現 mix / size / pre-delay 慢慢滑入的現象。
        currentDelay_ = targetDelay_;
        currentPreDelay_ = targetPreDelay_;
        currentMix_ = mix_;
        currentWidth_ = width_;
        modulationScale_ = freeze_ ? 0.0f : 1.0f;
    }

    // ---- 參數（合約）---------------------------------------------------------------
    // 所有 float setter 都先擋掉 NaN：std::clamp(NaN, lo, hi) 會原樣回傳 NaN，一個 NaN 參數
    // （例如 host automation 出錯）就會讓輸出永久變 NaN，連 reset() 都救不回來。收到 NaN 時保留上一個有效值。

    // RT60（秒）：殘響衰減 60 dB 所需時間。0.1 .. 60
    void setDecaySeconds(float seconds) noexcept {
        if (std::isnan(seconds))
            return;
        decaySeconds_ = std::clamp(seconds, 0.1f, 60.0f);
        updateDecayGains();
    }
    // 直達音到殘響開始的間隔（0 .. 250 ms），會平滑變化
    void setPreDelayMs(float ms) noexcept {
        if (std::isnan(ms))
            return;
        preDelayMs_ = std::clamp(ms, 0.0f, kMaxPreDelayMs);
        targetPreDelay_ = static_cast<float>(preDelayMs_ * 0.001 * sampleRate_);
    }
    // damping lowpass 的 cutoff；>= 20000 視為不 damping
    void setDampingHz(float hz) noexcept {
        if (std::isnan(hz))
            return;
        dampingHz_ = std::clamp(hz, 200.0f, 20000.0f);
        dampingBypassed_ = dampingHz_ >= 20000.0f;
        damping_.setCutoff(std::min(dampingHz_, static_cast<float>(sampleRate_ * 0.45)));
    }
    // 0..1：delay 線長度 ×(0.5 .. 1.5)，越大越像大空間（modal 密度更高、early 更稀疏）
    void setSize(float size) noexcept {
        if (std::isnan(size))
            return;
        size_ = std::clamp(size, 0.0f, 1.0f);
        updateDelayTargets();
        updateDecayGains();
    }
    // 0 = mono（L=R），1 = 完整 stereo
    void setWidth(float width) noexcept {
        if (std::isnan(width))
            return;
        width_ = std::clamp(width, 0.0f, 1.0f);
    }
    // 0 = 全 dry，1 = 全 wet（線性混合）
    void setMix(float mix) noexcept {
        if (std::isnan(mix))
            return;
        mix_ = std::clamp(mix, 0.0f, 1.0f);
    }
    // freeze：g_i = 1、關閉 damping、不再餵入新訊號、modulation 淡出（約 30 ms）→ 尾巴無限延長。
    // modulation 一定要停：在小數位置做 linear interpolation 等於一個 lowpass，每繞一圈都會損失一點，
    // 凍結的尾巴就會慢慢變暗、變小；讀取位置回到整數長度，迴路才嚴格無損。
    void setFreeze(bool shouldFreeze) noexcept {
        freeze_ = shouldFreeze;
        updateDecayGains();
    }

    // ---- 額外參數 ---------------------------------------------------------------
    // 內建的 delay 線 modulation（深度 ms、速率 Hz）；預設 0 = 關閉。練習 4 會討論它對 metallic ringing 的影響。
    void setModulationDepthMs(float ms) noexcept {
        if (std::isnan(ms))
            return;
        modulationDepthMs_ = std::clamp(ms, 0.0f, kMaxModulationMs);
        modulationDepth_ = static_cast<float>(modulationDepthMs_ * 0.001 * sampleRate_);
    }
    void setModulationRateHz(float hz) noexcept {
        if (std::isnan(hz))
            return;
        modulationRateHz_ = std::clamp(hz, 0.01f, 5.0f);
        lfoIncrement_ = static_cast<float>(kTwoPi * modulationRateHz_ / sampleRate_);
    }

    float getDecaySeconds() const noexcept { return decaySeconds_; }
    float getPreDelayMs() const noexcept { return preDelayMs_; }
    float getDampingHz() const noexcept { return dampingHz_; }
    float getSize() const noexcept { return size_; }
    float getWidth() const noexcept { return width_; }
    float getMix() const noexcept { return mix_; }
    bool isFrozen() const noexcept { return freeze_; }
    // 目前第 i 條 delay 線的長度（samples），給範例印表格用
    float getLineDelaySamples(int i) const noexcept { return targetDelay_[static_cast<size_t>(i)]; }
    // 第 i 條線的 decay gain g_i
    float getLineGain(int i) const noexcept { return lineGain_[static_cast<size_t>(i)]; }

    // ---- 處理 ---------------------------------------------------------------------
    // in-place；支援 1 或 2 個 channel（mono 輸入時輸出也是 mono：取 L 的殘響）
    void process(AudioBlock block) noexcept {
        const int nch = std::min(block.getNumChannels(), 2);
        const int n = block.getNumSamples();
        if (nch == 0)
            return;
        float* left = block.getChannelPointer(0);
        float* right = nch > 1 ? block.getChannelPointer(1) : nullptr;

        for (int i = 0; i < n; ++i) {
            const float dryL = left[i];
            const float dryR = right ? right[i] : dryL;
            float wetL, wetR;
            processSampleStereo(dryL, dryR, wetL, wetR);

            // mix / width 平滑（避免 zipper noise）
            currentMix_ += smoothCoef_ * (mix_ - currentMix_);
            currentWidth_ += smoothCoef_ * (width_ - currentWidth_);

            // width：mid/side
            const float mid = 0.5f * (wetL + wetR);
            const float side = 0.5f * (wetL - wetR) * currentWidth_;
            wetL = mid + side;
            wetR = mid - side;

            left[i] = dryL * (1.0f - currentMix_) + wetL * currentMix_;
            if (right)
                right[i] = dryR * (1.0f - currentMix_) + wetR * currentMix_;
        }
    }

    // 只算 wet（不含 dry、不含 width/mix），給想自己做 send/return 的人用
    void processSampleStereo(float inL, float inR, float& wetL, float& wetR) noexcept {
        // 1. pre-delay（平滑變化的 fractional delay）
        currentPreDelay_ += smoothCoef_ * (targetPreDelay_ - currentPreDelay_);
        preDelay_.push(0, inL);
        preDelay_.push(1, inR);
        float dL = preDelay_.readLinear(0, currentPreDelay_);
        float dR = preDelay_.readLinear(1, currentPreDelay_);

        // 2. input diffusion：4 級 series allpass，把一個脈衝打散成一小團
        for (auto& ap : diffusers_[0])
            dL = ap.processSample(dL);
        for (auto& ap : diffusers_[1])
            dR = ap.processSample(dR);

        // freeze 時不再餵入新訊號
        const float inputGain = freeze_ ? 0.0f : kInputGain;
        dL *= inputGain;
        dR *= inputGain;

        // 3. 讀出每條 delay 線的輸出 v_i（含 modulation、含平滑的長度變化）
        lfoPhase_ += lfoIncrement_;
        if (lfoPhase_ >= static_cast<float>(kTwoPi))
            lfoPhase_ -= static_cast<float>(kTwoPi);
        // freeze 時 modulation 平滑淡出到 0（直接歸零會讓讀取位置跳動 → click），解除 freeze 再淡入。
        // 接近終點時直接吸附到 0 / 1：指數平滑永遠到不了終點，殘留的一點點小數位置也會造成損失。
        modulationScale_ += smoothCoef_ * ((freeze_ ? 0.0f : 1.0f) - modulationScale_);
        if (freeze_ && modulationScale_ < 1.0e-4f)
            modulationScale_ = 0.0f;
        else if (!freeze_ && modulationScale_ > 0.9999f)
            modulationScale_ = 1.0f;
        const float depth = modulationDepth_ * modulationScale_;
        float sum = 0.0f;
        for (int k = 0; k < kNumLines; ++k) {
            const size_t i = static_cast<size_t>(k);
            // 平滑滑向目標長度。float 的指數平滑在幾千 samples 的長度會「卡住」：差距小於約 0.1–0.2 sample 時，
            // 增量 smoothCoef_·diff 小於半個 ulp，加上去數值不變，讀取位置就永遠停在小數點上
            // （linear interpolation = lowpass → freeze 時迴路不再無損）。所以加不動時直接吸附到目標。
            const float nextDelay = currentDelay_[i] + smoothCoef_ * (targetDelay_[i] - currentDelay_[i]);
            currentDelay_[i] = nextDelay == currentDelay_[i] ? targetDelay_[i] : nextDelay;
            // 每條線用不同相位的 LFO（k·45°），避免所有線一起「抖」
            const float lfo = depth > 0.0f ? depth * std::sin(lfoPhase_ + static_cast<float>(k) * 0.7853982f) : 0.0f;
            // 這裡是「先讀、後寫」：上一次 push 的是 u[n-1]，readLinear(d) 回傳 u[n-1-d]，
            // 所以要讀 d = L - 1 才會得到 v[n] = u[n - L]（迴路長度剛好是 L，g_i 的公式才精確）。
            const float v = lines_.readLinear(k, currentDelay_[i] - 1.0f + lfo);
            lineOut_[i] = v;
            sum += v;
        }

        // 4. Householder 回授：u_i = v_i - (2/N)·Σ v_j，然後 damping + decay gain，再加輸入寫回
        const float scale = 2.0f / static_cast<float>(kNumLines);
        for (int k = 0; k < kNumLines; ++k) {
            const size_t i = static_cast<size_t>(k);
            float u = lineOut_[i] - scale * sum;
            if (!dampingBypassed_ && !freeze_)
                u = damping_.processSample(k, u);
            u *= lineGain_[i];
            u += (k % 2 == 0) ? dL : dR; // 偶數線吃左、奇數線吃右
            lines_.push(k, reverb_detail::flushDenormal(u));
        }

        // 5. 輸出：左右各取 4 條線，正負交錯，讓 L/R 去相關
        wetL = lineOut_[0] - lineOut_[2] + lineOut_[4] - lineOut_[6];
        wetR = lineOut_[1] - lineOut_[3] + lineOut_[5] - lineOut_[7];
    }

private:
    // size → delay 長度倍率 0.5 .. 1.5
    static float sizeToScale(float size) noexcept { return 0.5f + size; }

    void updateDelayTargets() noexcept {
        const float scale = sizeToScale(size_);
        for (int k = 0; k < kNumLines; ++k) {
            const size_t i = static_cast<size_t>(k);
            // 轉成 samples 後取下一個質數：讓各線長度互質（README「為什麼要互質」）
            int samples = nextPrime(msToSamples(kBaseDelaysMs[k] * scale, sampleRate_));
            if (maxLineDelay_ > 0) // prepare 之後才知道上限：留下 modulation 與插值的空間
                samples = std::min(samples, maxLineDelay_ - maxModulationSamples_ - 4);
            targetDelay_[i] = static_cast<float>(samples);
        }
    }

    // g_i = 10^(-3·L_i / (RT60·fs))；freeze 時 g_i = 1
    void updateDecayGains() noexcept {
        for (int k = 0; k < kNumLines; ++k) {
            const size_t i = static_cast<size_t>(k);
            lineGain_[i] = freeze_ ? 1.0f
                                   : static_cast<float>(std::pow(10.0, -3.0 * targetDelay_[i] / (decaySeconds_ * sampleRate_)));
        }
    }

    // 8 條線的基礎長度（ms，size = 0.5 時）：大致均勻分布在 30 .. 75 ms，且不成整數比
    static constexpr std::array<double, kNumLines> kBaseDelaysMs = {29.7, 37.1, 41.1, 43.7, 53.3, 59.9, 67.3, 73.1};
    // 輸入 diffuser 長度（ms）：取自 Dattorro 1997 的 input diffusion 設定（換算成 ms）
    static constexpr std::array<double, kNumInputDiffusers> kDiffuserMs = {4.77, 3.60, 12.73, 9.31};
    static constexpr float kInputGain = 0.35f;

    double sampleRate_ = 48000.0;
    int numChannels_ = 2;
    int maxLineDelay_ = 0;
    int maxModulationSamples_ = 0;

    FractionalDelayLine lines_;    // kNumLines 條 delay 線
    FractionalDelayLine preDelay_; // 2 條（L/R）
    OnePole damping_;              // kNumLines 個 channel 的 lowpass
    std::array<std::array<AllpassDiffuser, kNumInputDiffusers>, 2> diffusers_{};

    std::array<float, kNumLines> targetDelay_{};
    std::array<float, kNumLines> currentDelay_{};
    std::array<float, kNumLines> lineGain_{};
    std::array<float, kNumLines> lineOut_{};

    float decaySeconds_ = 2.0f;
    float preDelayMs_ = 0.0f;
    float dampingHz_ = 6000.0f;
    bool dampingBypassed_ = false;
    float size_ = 0.5f;
    float width_ = 1.0f;
    float mix_ = 0.3f;
    bool freeze_ = false;

    float targetPreDelay_ = 0.0f, currentPreDelay_ = 0.0f;
    float currentMix_ = 0.3f, currentWidth_ = 1.0f;
    float smoothCoef_ = 0.001f;

    float modulationDepthMs_ = 0.0f;
    float modulationDepth_ = 0.0f; // samples
    float modulationRateHz_ = 0.5f;
    float lfoPhase_ = 0.0f, lfoIncrement_ = 0.0f;
    float modulationScale_ = 1.0f; // 1 = modulation 全開；freeze 時平滑降到 0
};

} // namespace dawlab::dsp
