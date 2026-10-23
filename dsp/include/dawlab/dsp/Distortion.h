#pragma once
// Distortion.h —— 第 07 章：hard clip、foldback、fuzz、rectify 與 bitcrusher。
//
// 第 06 章的 Saturation 是「溫和地把波形壓圓」；這一章的 Distortion 則是刻意把波形
// 折斷、折返、切掉一半、或是把解析度砍掉。訊號流程：
//
//   in ──┬──────────────── dry（延遲 latency，和 wet 對齊）───────────────────────────┐
//        │                                                                           ├─ mix ─→ out
//        └─ drive ─┬→ [ Oversampler ↑ → shaper → ↓ ]（HardClip/Foldback/Fuzz/Rectify）  │
//                  └→ [ sample & hold → quantize → 延遲 latency ]（Bitcrush）           │
//                        依 type 選一條（切換時 crossfade）→ DC blocker（Fuzz/Rectify）  │
//                        → tone lowpass → output gain ──────────────────────────────────┘
//
// 設計重點：
// - 所有 memoryless 的曲線都是 static 函式，可以單獨拿來畫圖、做練習、寫測試。
// - HardClip / Foldback / Fuzz / Rectify 會產生無窮多諧波，一定要在 oversampled 的
//   取樣率上做，否則高次諧波會 aliasing 回聽得到的頻帶；Bitcrush 的 aliasing 本身就是效果，
//   所以在原始取樣率處理。兩條路徑的 latency 都補到一樣（host 只需要一個固定的 latency）。
// - 兩條路徑（以及 DC blocker）**每個 block 都會跑**，只是依 type 決定用哪一個的輸出。
//   這樣切換 type 時，新路徑的 FIR / 延遲線裡裝的就是「剛剛的」輸入，而不是很久以前的舊音訊；
//   再加上 kTypeFadeMs 的 crossfade，切換 type 不會有 click（代價：Bitcrush 時 oversampler 也在空轉）。
// - 可以在 audio thread 呼叫的只有 process()/processSample()/setXxx()/reset()，
//   prepare() 是唯一會 allocate 的地方。

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>
#include <dawlab/dsp/Biquad.h>
#include <dawlab/dsp/OnePole.h>
#include <dawlab/dsp/Oversampler.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <memory>
#include <vector>

namespace dawlab::dsp {

enum class DistortionType { HardClip, Foldback, Fuzz, Rectify, Bitcrush };

// Rectify 的兩種做法：full-wave 取絕對值（音高升八度），half-wave 把負半週切掉（基頻還在）
enum class RectifyMode { FullWave, HalfWave };

inline const char* distortionTypeName(DistortionType type) noexcept {
    switch (type) {
    case DistortionType::HardClip: return "HardClip";
    case DistortionType::Foldback: return "Foldback";
    case DistortionType::Fuzz: return "Fuzz";
    case DistortionType::Rectify: return "Rectify";
    case DistortionType::Bitcrush: return "Bitcrush";
    }
    return "?";
}

namespace detail {

// 固定長度的整數延遲：用來讓 dry 訊號（以及不經過 oversampler 的 bitcrush 路徑）
// 對齊 oversampler 造成的 latency，這樣 mix 旋鈕轉到中間時不會出現 comb filtering。
class FixedDelay {
public:
    void prepare(int delaySamples) {
        delay_ = std::max(0, delaySamples);
        buffer_.assign(static_cast<size_t>(std::max(1, delay_)), 0.0f);
        pos_ = 0;
    }
    void reset() noexcept {
        std::fill(buffer_.begin(), buffer_.end(), 0.0f);
        pos_ = 0;
    }
    float process(float x) noexcept {
        if (delay_ == 0)
            return x;
        const float y = buffer_[pos_]; // delay_ 個 sample 之前寫進去的值
        buffer_[pos_] = x;
        if (++pos_ >= buffer_.size())
            pos_ = 0;
        return y;
    }

private:
    int delay_ = 0;
    std::vector<float> buffer_;
    size_t pos_ = 0;
};

// 每個 block 做一次線性插值的參數（最簡單的「避免 zipper noise」做法）：
// 一個 block（512 samples ≈ 10 ms）內從 current 走到 target。
struct BlockRamp {
    float current = 1.0f;
    float target = 1.0f;

    void snap(float v) noexcept { current = target = v; }
    float incrementFor(int numSamples) const noexcept {
        return numSamples > 0 ? (target - current) / static_cast<float>(numSamples) : 0.0f;
    }
};

} // namespace detail

class Distortion {
public:
    // Fuzz 曲線的三個常數：dead zone（gating 的來源）、正負半週不同的 clip 高度（asymmetric → 偶次諧波）
    static constexpr float kFuzzDeadZone = 0.1f;
    static constexpr float kFuzzPositiveCeiling = 1.0f;
    static constexpr float kFuzzNegativeCeiling = 0.6f;
    // tone 旋鈕轉到這個頻率以上就完全 bypass 後級 lowpass
    static constexpr float kToneBypassHz = 20000.0f;
    // DC blocker（Fuzz / Rectify 會產生直流）的 highpass 截止頻率
    static constexpr float kDcBlockerHz = 10.0f;
    // oversampling 倍率上限 2^3 = 8×（第 06 章 Oversampler 支援到 8×）
    static constexpr int kMaxOversamplingLog2 = 3;
    // 切換 type 時，舊 type 與新 type 輸出之間的 crossfade 長度
    static constexpr double kTypeFadeMs = 10.0;

    // ---- 標準介面 ---------------------------------------------------------------------------

    // 在 prepare() 之前呼叫：oversampling 倍率 = 2^log2（0 = 不做 oversampling）。預設 2（4×）。
    void setOversamplingFactorLog2(int factorLog2) noexcept {
        osLog2_ = std::clamp(factorLog2, 0, kMaxOversamplingLog2);
    }

    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        sampleRate_ = sampleRate;
        // maxBlockSize ≤ 0 是不合理的輸入；至少配置 1，process() 也會以這個長度切塊
        maxBlockSize_ = std::max(1, maxBlockSize);
        numChannels_ = numChannels;

        if (osLog2_ > 0) {
            oversampler_ = std::make_unique<Oversampler>(osLog2_);
            oversampler_->prepare(sampleRate, maxBlockSize_, numChannels);
            latency_ = oversampler_->getLatencySamples();
        } else {
            oversampler_.reset();
            latency_ = 0;
        }

        dcBlocker_.prepare(sampleRate, maxBlockSize_, numChannels);
        dcBlocker_.setType(OnePole::Type::Highpass);
        dcBlocker_.setCutoff(kDcBlockerHz);
        tone_.prepare(sampleRate, maxBlockSize_, numChannels);
        toneHzApplied_ = -1.0f; // 強迫下一個 block 重算 tone 係數
        prepared_ = true;

        fadeLength_ = std::max(1, msToSamples(kTypeFadeMs, sampleRate));
        snapTypeFade();

        dryDelay_.assign(static_cast<size_t>(numChannels), detail::FixedDelay());
        wetDelay_.assign(static_cast<size_t>(numChannels), detail::FixedDelay());
        for (auto& d : dryDelay_)
            d.prepare(latency_);
        for (auto& d : wetDelay_)
            d.prepare(latency_);
        dryBuffer_.setSize(numChannels, maxBlockSize_);
        crushBuffer_.setSize(numChannels, maxBlockSize_);
        holdValue_.assign(static_cast<size_t>(numChannels), 0.0f);
        holdCounter_.assign(static_cast<size_t>(numChannels), 0);

        driveGain_.snap(dbToGain(driveDb_));
        outputGain_.snap(dbToGain(outputGainDb_));
        mixRamp_.snap(mix_);
    }

    void reset() noexcept {
        if (oversampler_)
            oversampler_->reset();
        dcBlocker_.reset();
        tone_.reset();
        for (auto& d : dryDelay_)
            d.reset();
        for (auto& d : wetDelay_)
            d.reset();
        std::fill(holdValue_.begin(), holdValue_.end(), 0.0f);
        std::fill(holdCounter_.begin(), holdCounter_.end(), 0);
        driveGain_.snap(driveGain_.target);
        outputGain_.snap(outputGain_.target);
        mixRamp_.snap(mixRamp_.target);
        snapTypeFade();
    }

    // ---- 參數 ---------------------------------------------------------------------------------

    // process() 會在下一個 block 開始一段 kTypeFadeMs 的 crossfade（不會 click）；
    // crossfade 進行中又改 type 的話，等這段 crossfade 走完才開始下一段。
    // prepare() / reset() 之後、第一個 block 之前呼叫的話不 fade，直接生效。
    void setType(DistortionType type) noexcept { type_ = type; }
    void setRectifyMode(RectifyMode mode) noexcept { rectifyMode_ = mode; }

    // 進 shaper 之前的增益（0..60 dB）。drive 越大，越多波形撞到 clip 區。
    void setDriveDb(float db) noexcept {
        driveDb_ = std::clamp(db, 0.0f, 60.0f);
        driveGain_.target = dbToGain(driveDb_);
    }

    // 後級 lowpass（tone 旋鈕）：失真後的高次諧波太刺時把它們削掉。≥ 20 kHz = bypass。
    void setToneHz(float hz) noexcept { toneHz_ = std::clamp(hz, 200.0f, kToneBypassHz); }

    void setMix(float mix) noexcept {
        mix_ = std::clamp(mix, 0.0f, 1.0f);
        mixRamp_.target = mix_;
    }

    void setOutputGainDb(float db) noexcept {
        outputGainDb_ = std::clamp(db, -60.0f, 24.0f);
        outputGain_.target = dbToGain(outputGainDb_);
    }

    // Bitcrush：量化位元數（允許小數，例如 5.5 bit）與 sample & hold 的倍率（1 = 不降取樣率）
    // 注意 bits < 2 時正的 code 只剩 0（見 quantize()），輸出不對稱、帶 DC。
    void setBitDepth(float bits) noexcept { bitDepth_ = std::clamp(bits, 1.0f, 24.0f); }
    void setDownsampleFactor(int factor) noexcept { downsampleFactor_ = std::clamp(factor, 1, 64); }

    DistortionType getType() const noexcept { return type_; }
    RectifyMode getRectifyMode() const noexcept { return rectifyMode_; }
    float getDriveDb() const noexcept { return driveDb_; }
    float getToneHz() const noexcept { return toneHz_; }
    float getMix() const noexcept { return mix_; }
    float getOutputGainDb() const noexcept { return outputGainDb_; }
    float getBitDepth() const noexcept { return bitDepth_; }
    int getDownsampleFactor() const noexcept { return downsampleFactor_; }
    // prepare 之後回報「實際生效」的倍率（與 getLatencySamples() 一致）；prepare 之後才呼叫
    // setOversamplingFactorLog2() 的新值要等下一次 prepare 才生效。prepare 之前回報設定值。
    int getOversamplingFactor() const noexcept {
        if (!prepared_)
            return 1 << osLog2_;
        return oversampler_ ? oversampler_->getFactor() : 1;
    }

    // 不論 type 為何都一樣（bitcrush 路徑與 dry 路徑都補了同樣的延遲）
    int getLatencySamples() const noexcept { return latency_; }

    // ---- 靜態曲線（memoryless）：輸入已經乘過 drive 的 u ------------------------------------------

    // hard clip：y = clamp(u, -1, 1)。drive → ∞ 時趨近方波（THD → 48.3%）。
    static float hardClip(float u) noexcept { return std::clamp(u, -1.0f, 1.0f); }

    // foldback / wavefolding：超過 ±1 的部分不是切掉，而是「折回來」。
    // 等價於把 u 送進週期 4 的三角波：u ∈ [-1, 1] 時 y = u；u ∈ [1, 3] 時 y = 2 - u；u ∈ [3, 5] 時 y = u - 4 ……
    static float foldback(float u) noexcept {
        float t = std::fmod(u + 1.0f, 4.0f); // 平移後對週期取餘數，範圍 (-4, 4)
        if (t < 0.0f)
            t += 4.0f; // 範圍 [0, 4)
        return t < 2.0f ? t - 1.0f : 3.0f - t;
    }

    // fuzz：模擬偏壓不正常的高增益電晶體級。
    //   1. |u| < dead zone 時完全不導通 → 音尾變小時會突然「關門」（gating / sputter）
    //   2. 正半週最高到 +1.0、負半週最低到 -0.6（asymmetric → 偶次諧波 + DC，之後要 DC block）
    static float fuzz(float u) noexcept {
        const float a = std::abs(u);
        if (a <= kFuzzDeadZone)
            return 0.0f;
        const float v = a - kFuzzDeadZone; // 從 dead zone 邊界開始連續，不會跳
        return u > 0.0f ? std::min(v, kFuzzPositiveCeiling) : -std::min(v, kFuzzNegativeCeiling);
    }

    // rectify：full-wave = |u|（基頻消失、只剩偶次諧波 → 高八度），half-wave = max(u, 0)。
    // 輸出限制在 [0, 1]，超過的部分 clip。
    static float rectify(float u, RectifyMode mode = RectifyMode::FullWave) noexcept {
        const float r = mode == RectifyMode::FullWave ? std::abs(u) : std::max(u, 0.0f);
        return std::min(r, 1.0f);
    }

    // 量化成 bits 位元（mid-tread，四捨五入到最近的階）：step = 2 / 2^bits。
    // 整數 code 限制在 [-M, M - 1]，M = floor(2^(bits-1))，跟真正的整數 PCM 一樣（16 bit = -32768..32767）。
    // bits 是小數時 M 取整數，輸出才會一直落在 step 的整數倍上（5.5 bit：M = 22，code = -22..21）。
    // bits < 2 時 M = 1，code 只有 -1 與 0：正半週全部變 0，輸出不對稱、帶 DC（真正 1 bit 二補數也是如此）。
    static float quantize(float u, float bits) noexcept {
        const float levels = std::exp2(bits);
        const float step = 2.0f / levels;
        const float maxCode = std::floor(levels * 0.5f);
        const float code = std::clamp(std::floor(u / step + 0.5f), -maxCode, maxCode - 1.0f);
        return code * step;
    }

    // 方便畫 transfer curve 用：Rectify 用 full-wave，Bitcrush 用 8 bit（S&H 有狀態，無法用靜態函式表示）
    static float shape(DistortionType type, float u) noexcept {
        switch (type) {
        case DistortionType::HardClip: return hardClip(u);
        case DistortionType::Foldback: return foldback(u);
        case DistortionType::Fuzz: return fuzz(u);
        case DistortionType::Rectify: return rectify(u, RectifyMode::FullWave);
        case DistortionType::Bitcrush: return quantize(u, 8.0f);
        }
        return u;
    }

    // N bits 量化全幅正弦波的理論 SNR：6.02·N + 1.76 dB（+ 20·log10(A) 當振幅是 A）
    static double theoreticalSnrDb(double bits, double amplitude = 1.0) noexcept {
        return 6.0206 * bits + 1.7609 + 20.0 * std::log10(amplitude);
    }

    // ---- 處理 ---------------------------------------------------------------------------------

    // host 偶爾會給比 prepare 時更大的 block（不合規，但真的會發生）：內部 buffer 都依 maxBlockSize 配置，
    // 所以切成 maxBlockSize 的小塊處理（與第 06 章 Saturation 相同的策略）。各小塊之間 FIR、filter、S&H 的狀態都會延續；
    // 唯一的差別是 drive / output / mix 的 BlockRamp 會在第一小塊就走完。
    void process(AudioBlock block) noexcept {
        if (numChannels_ <= 0)
            return; // 還沒 prepare()
        const int total = block.getNumSamples();
        for (int start = 0; start < total; start += maxBlockSize_)
            processChunk(block.getSubBlock(start, std::min(maxBlockSize_, total - start)));
    }

    // 單一 sample 的版本：只給「不需要 oversampling」的情境（例如離線分析、bitcrush）。
    // 注意：這個版本不經過 oversampler、也不做 dry/wet 延遲對齊（latency = 0），
    // 參數不做 ramp、切換 type 也不 crossfade（直接用目前的 type）。tone 的變更會套用。
    float processSample(int channel, float x) noexcept {
        updateToneCoefficients(); // 只做一次 float 比較；tone 有變才重算係數
        float y = shapeStateful(channel, x * driveGain_.target);
        if (type_ == DistortionType::Fuzz || type_ == DistortionType::Rectify)
            y = dcBlocker_.processSample(channel, y);
        if (!toneBypass_)
            y = tone_.processSample(channel, y);
        return x * (1.0f - mix_) + y * outputGain_.target * mix_;
    }

private:
    void processChunk(AudioBlock block) noexcept {
        const int n = block.getNumSamples();
        const int nch = std::min(block.getNumChannels(), numChannels_);
        if (n <= 0 || nch <= 0)
            return;
        assert(n <= maxBlockSize_);
        block = block.getChannelSubset(0, nch); // 比 prepare 時多出來的 channel 原封不動

        updateToneCoefficients();

        // 1. dry 備份（延遲 latency 個 sample，跟 wet 對齊）
        for (int ch = 0; ch < nch; ++ch) {
            const float* x = block.getChannelPointer(ch);
            float* d = dryBuffer_.getWritePointer(ch);
            auto& delay = dryDelay_[static_cast<size_t>(ch)];
            for (int i = 0; i < n; ++i)
                d[i] = delay.process(x[i]);
        }

        // 2. type crossfade 的狀態：沒有在 fade 時 fromType_ == toType_，權重 w = 1
        //    prepare() / reset() 後的第一個 block 沒有「舊聲音」可以 fade，直接採用目前的 type
        //    （常見寫法是 prepare() 之後才 setType()，不應該因此多一段 fade）。
        if (!processedSinceReset_) {
            snapTypeFade();
            processedSinceReset_ = true;
        } else if (fadeRemaining_ == 0 && type_ != toType_) {
            startTypeFade(type_);
        }
        const bool fading = fadeRemaining_ > 0;
        const float fadeDone = static_cast<float>(fadeLength_ - fadeRemaining_); // 這段 fade 已經走了幾個 sample
        const float invFade = 1.0f / static_cast<float>(fadeLength_);
        // 第 i 個 sample（可以是小數：oversampled 的 sample）的新 type 權重 w(i)：0 → 1 線性
        auto fadeWeight = [fading, fadeDone, invFade](float i) noexcept {
            return fading ? std::min(1.0f, (fadeDone + i + 1.0f) * invFade) : 1.0f;
        };

        const float g0 = driveGain_.current;

        // 3a. Bitcrush 路徑（原始取樣率）：不論目前 type 都跑，狀態才不會凍結在很久以前
        {
            const float inc = driveGain_.incrementFor(n);
            for (int ch = 0; ch < nch; ++ch) {
                const float* x = block.getChannelPointer(ch);
                float* c = crushBuffer_.getWritePointer(ch);
                auto& delay = wetDelay_[static_cast<size_t>(ch)];
                for (int i = 0; i < n; ++i) // 沒經過 oversampler，自己補同樣的 latency
                    c[i] = delay.process(crush(ch, x[i] * (g0 + inc * static_cast<float>(i))));
            }
        }

        // 3b. 波形 shaper 路徑（HardClip / Foldback / Fuzz / Rectify），結果直接寫回 block。
        //     同一條路徑內換 type（例如 HardClip → Foldback）時，在 shaper 的輸出端 crossfade 兩條曲線。
        const bool shaperFading = osFromType_ != osToType_;
        if (oversampler_) {
            // 在 oversampled 取樣率處理：諧波有更多空間，不會 alias 回來
            AudioBlock up = oversampler_->upsample(block);
            const int un = up.getNumSamples();
            const float inc = driveGain_.incrementFor(un);
            const float invFactor = 1.0f / static_cast<float>(oversampler_->getFactor());
            for (int ch = 0; ch < nch; ++ch) {
                float* x = up.getChannelPointer(ch);
                for (int i = 0; i < un; ++i) {
                    const float u = x[i] * (g0 + inc * static_cast<float>(i));
                    const float yTo = shapeMemoryless(osToType_, u);
                    x[i] = shaperFading
                               ? lerp(shapeMemoryless(osFromType_, u), yTo, fadeWeight(static_cast<float>(i) * invFactor))
                               : yTo;
                }
            }
            oversampler_->downsample(block);
        } else {
            // 不做 oversampling（setOversamplingFactorLog2(0)）：直接在原始取樣率 shape
            const float inc = driveGain_.incrementFor(n);
            for (int ch = 0; ch < nch; ++ch) {
                float* x = block.getChannelPointer(ch);
                for (int i = 0; i < n; ++i) {
                    const float fi = static_cast<float>(i);
                    const float u = x[i] * (g0 + inc * fi);
                    const float yTo = shapeMemoryless(osToType_, u);
                    x[i] = shaperFading ? lerp(shapeMemoryless(osFromType_, u), yTo, fadeWeight(fi)) : yTo;
                }
            }
        }
        driveGain_.current = driveGain_.target;

        // 3c. 選路徑：cw = Bitcrush 路徑佔的比例（fade 時在兩條路徑之間線性移動）
        const float crushFrom = fromType_ == DistortionType::Bitcrush ? 1.0f : 0.0f;
        const float crushTo = toType_ == DistortionType::Bitcrush ? 1.0f : 0.0f;
        if (fading || crushTo > 0.0f) {
            for (int ch = 0; ch < nch; ++ch) {
                float* x = block.getChannelPointer(ch);
                const float* c = crushBuffer_.getReadPointer(ch);
                for (int i = 0; i < n; ++i) {
                    const float cw = lerp(crushFrom, crushTo, fadeWeight(static_cast<float>(i)));
                    x[i] = lerp(x[i], c[i], cw);
                }
            }
        }

        // 3d. DC blocker：asymmetric（Fuzz）與 rectify 都會產生直流。
        //     每個 block 都跑（狀態才不會凍結），但只有 Fuzz / Rectify 採用它的輸出；
        //     其他 type 完全不受影響（例如 bitcrush 的 SNR 量測需要 wet 與 dry 逐 sample 對齊）。
        const float dcFrom = needsDcBlocker(fromType_) ? 1.0f : 0.0f;
        const float dcTo = needsDcBlocker(toType_) ? 1.0f : 0.0f;
        for (int ch = 0; ch < nch; ++ch) {
            float* x = block.getChannelPointer(ch);
            for (int i = 0; i < n; ++i) {
                const float blocked = dcBlocker_.processSample(ch, x[i]);
                if (fading)
                    x[i] = lerp(x[i], blocked, lerp(dcFrom, dcTo, fadeWeight(static_cast<float>(i))));
                else if (dcTo > 0.0f)
                    x[i] = blocked;
            }
        }
        advanceTypeFade(n);

        // 4. tone（後級 lowpass）
        if (!toneBypass_) {
            for (int ch = 0; ch < nch; ++ch) {
                float* x = block.getChannelPointer(ch);
                for (int i = 0; i < n; ++i)
                    x[i] = tone_.processSample(ch, x[i]);
            }
        }

        // 5. output gain 與 dry/wet mix
        const float o0 = outputGain_.current, oInc = outputGain_.incrementFor(n);
        const float m0 = mixRamp_.current, mInc = mixRamp_.incrementFor(n);
        for (int ch = 0; ch < nch; ++ch) {
            float* x = block.getChannelPointer(ch);
            const float* d = dryBuffer_.getReadPointer(ch);
            for (int i = 0; i < n; ++i) {
                const float fi = static_cast<float>(i);
                const float m = m0 + mInc * fi;
                x[i] = d[i] * (1.0f - m) + x[i] * (o0 + oInc * fi) * m;
            }
        }
        outputGain_.current = outputGain_.target;
        mixRamp_.current = mixRamp_.target;
    }

    static bool needsDcBlocker(DistortionType t) noexcept {
        return t == DistortionType::Fuzz || t == DistortionType::Rectify;
    }

    // 和 static shape() 一樣，但 Rectify 使用目前的 rectifyMode_（HalfWave 在 oversampled 路徑也要生效）
    float shapeMemoryless(DistortionType t, float u) const noexcept {
        switch (t) {
        case DistortionType::HardClip: return hardClip(u);
        case DistortionType::Foldback: return foldback(u);
        case DistortionType::Fuzz: return fuzz(u);
        case DistortionType::Rectify: return rectify(u, rectifyMode_);
        case DistortionType::Bitcrush: return quantize(u, bitDepth_); // 不會用到：Bitcrush 走 crush()
        }
        return u;
    }

    // processSample() 用：需要狀態的 bitcrush（sample & hold）與其他曲線在這裡分派
    float shapeStateful(int ch, float u) noexcept {
        return type_ == DistortionType::Bitcrush ? crush(ch, u) : shapeMemoryless(type_, u);
    }

    // ---- type crossfade ----
    // fromType_ → toType_：整體輸出（用哪條路徑、DC blocker 用不用）在這兩個 type 之間 fade。
    // osFromType_ → osToType_：shaper 路徑內部用的曲線。Bitcrush 不在 shaper 路徑上，
    // 所以 Bitcrush 時 shaper 路徑繼續用「上一個非 Bitcrush 的 type」空轉，狀態一直是新的。
    static float lerp(float a, float b, float t) noexcept { return a + (b - a) * t; }

    void snapTypeFade() noexcept {
        fromType_ = toType_ = type_;
        if (type_ != DistortionType::Bitcrush)
            osToType_ = type_;
        osFromType_ = osToType_;
        fadeRemaining_ = 0;
        processedSinceReset_ = false;
    }

    void startTypeFade(DistortionType newType) noexcept {
        fromType_ = toType_;
        toType_ = newType;
        if (newType != DistortionType::Bitcrush) {
            // 從 Bitcrush 切過來時，shaper 路徑的權重從 0 開始，直接換曲線即可；
            // 否則在舊曲線與新曲線之間 crossfade
            osFromType_ = fromType_ == DistortionType::Bitcrush ? newType : osToType_;
            osToType_ = newType;
        }
        fadeRemaining_ = fadeLength_;
    }

    void advanceTypeFade(int numSamples) noexcept {
        if (fadeRemaining_ == 0)
            return;
        fadeRemaining_ = std::max(0, fadeRemaining_ - numSamples);
        if (fadeRemaining_ == 0) {
            fromType_ = toType_;
            osFromType_ = osToType_;
        }
    }

    // sample & hold：每 downsampleFactor_ 個 sample 才「看」一次輸入，其餘時間維持上一個值，
    // 然後再量化。等效取樣率 = sampleRate / factor，S&H 的階梯會產生 imaging / aliasing。
    float crush(int ch, float u) noexcept {
        const auto i = static_cast<size_t>(ch);
        if (holdCounter_[i] == 0)
            holdValue_[i] = u;
        if (++holdCounter_[i] >= downsampleFactor_)
            holdCounter_[i] = 0;
        return quantize(holdValue_[i], bitDepth_);
    }

    void updateToneCoefficients() noexcept {
        if (toneHz_ == toneHzApplied_)
            return;
        toneHzApplied_ = toneHz_;
        const bool wasBypassed = toneBypass_;
        toneBypass_ = toneHz_ >= kToneBypassHz;
        if (!toneBypass_ && wasBypassed)
            tone_.reset(); // bypass 期間 biquad 沒在跑，狀態是很久以前的：清掉，不要把舊音訊吐出來
        if (!toneBypass_)
            tone_.setCoefficients(BiquadCoefficients::makeLowpass(sampleRate_, static_cast<double>(toneHz_), 0.7071));
    }

    DistortionType type_ = DistortionType::HardClip;
    RectifyMode rectifyMode_ = RectifyMode::FullWave;
    float driveDb_ = 0.0f;
    float toneHz_ = kToneBypassHz;
    float toneHzApplied_ = -1.0f;
    bool toneBypass_ = true;
    float mix_ = 1.0f;
    float outputGainDb_ = 0.0f;
    float bitDepth_ = 8.0f;
    int downsampleFactor_ = 1;
    int osLog2_ = 2;
    bool prepared_ = false; // prepare() 呼叫過了沒（getOversamplingFactor() 用）

    DistortionType fromType_ = DistortionType::HardClip, toType_ = DistortionType::HardClip;
    DistortionType osFromType_ = DistortionType::HardClip, osToType_ = DistortionType::HardClip;
    int fadeLength_ = 1;
    int fadeRemaining_ = 0;
    bool processedSinceReset_ = false;

    double sampleRate_ = 48000.0;
    int maxBlockSize_ = 0;
    int numChannels_ = 0;
    int latency_ = 0;

    std::unique_ptr<Oversampler> oversampler_;
    OnePole dcBlocker_;
    Biquad tone_;
    std::vector<detail::FixedDelay> dryDelay_, wetDelay_;
    AudioBuffer dryBuffer_, crushBuffer_;
    std::vector<float> holdValue_;
    std::vector<int> holdCounter_;
    detail::BlockRamp driveGain_, outputGain_, mixRamp_;
};

} // namespace dawlab::dsp
