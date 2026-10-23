#pragma once
// 第 11 章：LoudnessMeter —— ITU-R BS.1770-4 / EBU R128 的 loudness 量測（LUFS）。
//
// 流程（每個 channel 各自做前兩步）：
//   1. K-weighting：兩級 biquad（第 02 章 Biquad）
//        stage 1：高頻 shelf（+4 dB、約 1.68 kHz 以上），模擬頭部的聲學效應
//        stage 2：highpass（約 38 Hz），拿掉聽不太到的極低頻
//   2. mean square：每個 400 ms 的 block（gating block）算 z_i = (1/T) Σ y_i[n]^2
//      block 之間 overlap 75%，也就是每 100 ms 產生一個新的 block
//   3. 把各 channel 加權相加後換成 LUFS：
//        L = −0.691 + 10·log10( Σ_i G_i · z_i )
//      G_i：L、R、C = 1.0；Ls、Rs = 1.41（+1.5 dB）；LFE 不計
//   4. momentary  = 最近 400 ms（一個 block）
//      short-term = 最近 3 s（30 個 100 ms 子區塊的平均）
//      integrated = 所有 block 經過兩道 gate 後的平均：
//          absolute gate：丟掉 < −70 LUFS 的 block
//          relative gate：丟掉 < (通過 absolute gate 的平均 − 10 LU) 的 block
//
// 實作上我們以 100 ms 為單位累積 mean square（子區塊），400 ms block = 最近 4 個子區塊，
// 3 s = 最近 30 個子區塊，這樣每個 sample 只要做一次平方累加。
//
// integrated 的 gating 需要「所有 block 的 loudness」——若用 vector 存會在 audio thread allocate。
// 這裡用直方圖（histogram）：把 [−70, +10) LUFS 以 0.1 LU 切成 800 格，每格記錄 block 數與
// mean-square 總和；relative gate 以格的中心值判斷，誤差最多 0.05 LU，而且完全不 allocate。
//
// 所有 getter 都讀 std::atomic（relaxed），UI thread 可以直接讀。

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>
#include <dawlab/dsp/Biquad.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

namespace dawlab::dsp {

class LoudnessMeter {
public:
    static constexpr float kAbsoluteGateLufs = -70.0f;
    static constexpr float kRelativeGateLu = -10.0f;
    static constexpr double kBlockSeconds = 0.4;   // gating block 長度
    static constexpr double kStepSeconds = 0.1;    // 75% overlap → 每 100 ms 一個新 block
    static constexpr int kBlockSteps = 4;          // 400 ms / 100 ms
    static constexpr int kShortTermSteps = 30;     // 3 s / 100 ms
    static constexpr float kLufsOffset = -0.691f;  // 補償 K-weighting 在 1 kHz 的增益（約 +0.691 dB）
    static constexpr int kMaxChannels = 8;

    // ---- K-weighting 係數 ----------------------------------------------------------------
    // ITU-R BS.1770-4 的 Table 1 / Table 2 只列出 48 kHz 的係數（a0 已正規化為 1）：
    //
    //   stage 1（pre-filter，高頻 shelf）  b0 =  1.53512485958697  b1 = -2.69169618940638  b2 = 1.19839281085285
    //                                     a1 = -1.69065929318241  a2 =  0.73248077421585
    //   stage 2（RLB weighting，highpass） b0 =  1.0               b1 = -2.0               b2 = 1.0
    //                                     a1 = -1.99004745483398  a2 =  0.99007225036621
    //
    // 規範沒有給其他取樣率的係數，必須從「類比原型」重新設計：
    //
    //   stage 1：H1(s) = (Vh·s² + Vb·s/Q + 1) / (s² + s/Q + 1)   Vh = 10^(G/20)，Vb = Vh^0.49967
    //            f0 = 1681.974 Hz，G = 3.9998 dB，Q = 0.70718
    //            s → 0（DC）增益 1、s → ∞（高頻）增益 Vh ≈ +4 dB：這是 high shelf
    //   stage 2：H2(s) = s² / (s² + s/Q + 1)                     f0 = 38.135 Hz，Q = 0.50033
    //
    //   s 已對 ω0 = 2π f0 正規化。bilinear transform 用 s = (1/K)·(1 − z⁻¹)/(1 + z⁻¹)，K = tan(π f0 / fs)
    //   （這個 K 讓 f0 剛好映射到 f0，不會被頻率扭曲移走）。代入 H1 後上下同乘 K²(1 + z⁻¹)²：
    //     分子 = Vh(1 − z⁻¹)² + Vb(K/Q)(1 − z⁻¹)(1 + z⁻¹) + K²(1 + z⁻¹)²
    //     分母 =   (1 − z⁻¹)² +    (K/Q)(1 − z⁻¹)(1 + z⁻¹) + K²(1 + z⁻¹)²
    //   展開成 z⁻¹ 的多項式就是 makeKWeightingStage1() 裡的 b0 = (Vh + Vb·K/Q + K²)/a0 等式子。
    //
    // 這組原型參數是從 48 kHz 標準係數反推出來的（libebur128 也用同一組）；代入 48 kHz 會還原出上表
    // （誤差 < 1e-10，範例 loudness_verify 會驗證），代入 44.1 / 96 kHz 則得到同一條曲線。
    // 注意 stage 1 不是 RBJ cookbook 的 shelf：RBJ 的 corner 定義在「增益一半」處，兩者參數不能互換。
    static constexpr double kStage1Hz = 1681.974450955533;
    static constexpr double kStage1Q = 0.7071752369554196;
    static constexpr double kStage1GainDb = 3.999843853973347;
    static constexpr double kStage1VbExponent = 0.4996667741545416;
    static constexpr double kStage2Hz = 38.13547087602444;
    static constexpr double kStage2Q = 0.5003270373238773;

    // 48 kHz 的標準係數表（b0, b1, b2, a1, a2），給驗證與教學用
    static constexpr double kStage1Table48k[5] = {1.53512485958697, -2.69169618940638, 1.19839281085285,
                                                  -1.69065929318241, 0.73248077421585};
    static constexpr double kStage2Table48k[5] = {1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621};

    static BiquadCoefficients makeKWeightingStage1(double sampleRate) noexcept {
        const double K = std::tan(kPi * kStage1Hz / sampleRate); // bilinear transform 的頻率映射
        const double Vh = std::pow(10.0, kStage1GainDb / 20.0);
        const double Vb = std::pow(Vh, kStage1VbExponent);
        const double a0 = 1.0 + K / kStage1Q + K * K;
        BiquadCoefficients c;
        c.b0 = (Vh + Vb * K / kStage1Q + K * K) / a0;
        c.b1 = 2.0 * (K * K - Vh) / a0;
        c.b2 = (Vh - Vb * K / kStage1Q + K * K) / a0;
        c.a1 = 2.0 * (K * K - 1.0) / a0;
        c.a2 = (1.0 - K / kStage1Q + K * K) / a0;
        return c;
    }

    // 規範的 Table 2 分子固定是 1, -2, 1（沒有除以 a0），所以高頻增益是 a0 ≈ +0.043 dB 而不是精確的 0 dB；
    // 我們照規範做，這一點點增益已經包含在 -0.691 的補償裡。
    static BiquadCoefficients makeKWeightingStage2(double sampleRate) noexcept {
        const double K = std::tan(kPi * kStage2Hz / sampleRate);
        const double a0 = 1.0 + K / kStage2Q + K * K;
        BiquadCoefficients c;
        c.b0 = 1.0;
        c.b1 = -2.0;
        c.b2 = 1.0;
        c.a1 = 2.0 * (K * K - 1.0) / a0;
        c.a2 = (1.0 - K / kStage2Q + K * K) / a0;
        return c;
    }

    // mean square（已加權相加）→ LUFS
    static float powerToLufs(double weightedMeanSquare) noexcept {
        if (weightedMeanSquare <= 1e-15) // 約 -150 dB，視為靜音
            return kMinusInfinityDb;
        return kLufsOffset + static_cast<float>(10.0 * std::log10(weightedMeanSquare));
    }

    static double lufsToPower(float lufs) noexcept { return std::pow(10.0, (lufs - kLufsOffset) / 10.0); }

    // ---- 標準介面 -------------------------------------------------------------------------
    void prepare(double sampleRate, int numChannels) {
        assert(sampleRate > 0.0 && numChannels > 0 && numChannels <= kMaxChannels);
        sampleRate_ = sampleRate;
        numChannels_ = std::min(numChannels, kMaxChannels);
        stepSamples_ = std::max(1, static_cast<int>(std::lround(kStepSeconds * sampleRate)));

        stage1_.prepare(sampleRate, 1, numChannels_);
        stage2_.prepare(sampleRate, 1, numChannels_);
        stage1_.setCoefficients(makeKWeightingStage1(sampleRate));
        stage2_.setCoefficients(makeKWeightingStage2(sampleRate));

        histogramCount_.assign(kHistogramBins, 0u);
        histogramPower_.assign(kHistogramBins, 0.0);
        reset();
    }

    void reset() noexcept {
        stage1_.reset();
        stage2_.reset();
        stepAccumulators_.fill(0.0);
        stepRing_.fill(0.0);
        stepSampleCount_ = 0;
        ringPos_ = 0;
        stepsSeen_ = 0;
        std::fill(histogramCount_.begin(), histogramCount_.end(), 0u);
        std::fill(histogramPower_.begin(), histogramPower_.end(), 0.0);
        momentary_.store(kMinusInfinityDb, std::memory_order_relaxed);
        shortTerm_.store(kMinusInfinityDb, std::memory_order_relaxed);
        integrated_.store(kMinusInfinityDb, std::memory_order_relaxed);
        maxMomentary_.store(kMinusInfinityDb, std::memory_order_relaxed);
        maxShortTerm_.store(kMinusInfinityDb, std::memory_order_relaxed);
        updateCount_.store(0u, std::memory_order_relaxed);
    }

    // 各 channel 的加權（預設全部 1.0）。5.1 的 Ls / Rs 應設 1.41，LFE 設 0。
    void setChannelWeight(int channel, float weight) noexcept {
        if (channel >= 0 && channel < kMaxChannels)
            channelWeights_[static_cast<size_t>(channel)] = weight;
    }
    float getChannelWeight(int channel) const noexcept {
        return (channel >= 0 && channel < kMaxChannels) ? channelWeights_[static_cast<size_t>(channel)] : 0.0f;
    }

    // audio thread：不 allocate、不鎖
    void process(const AudioBlock& block) noexcept {
        const int nch = std::min(numChannels_, block.getNumChannels());
        const int n = block.getNumSamples();
        for (int i = 0; i < n; ++i) {
            for (int ch = 0; ch < nch; ++ch) {
                // K-weighting：兩級 biquad 串接
                const float y = stage2_.processSample(ch, stage1_.processSample(ch, block.getSample(ch, i)));
                stepAccumulators_[static_cast<size_t>(ch)] += static_cast<double>(y) * y;
            }
            if (++stepSampleCount_ >= stepSamples_)
                finishStep();
        }
    }

    // ---- thread-safe getters -------------------------------------------------------------
    float getMomentaryLufs() const noexcept { return momentary_.load(std::memory_order_relaxed); }   // 400 ms
    float getShortTermLufs() const noexcept { return shortTerm_.load(std::memory_order_relaxed); }   // 3 s
    float getIntegratedLufs() const noexcept { return integrated_.load(std::memory_order_relaxed); } // gated
    // 最大值：只統計完整的視窗（max momentary 從第 400 ms、max short-term 從第 3 s 開始更新），之前回傳 kMinusInfinityDb
    float getMaxMomentaryLufs() const noexcept { return maxMomentary_.load(std::memory_order_relaxed); }
    float getMaxShortTermLufs() const noexcept { return maxShortTerm_.load(std::memory_order_relaxed); }

    // 每 100 ms（每個子區塊結束）+1。UI 或練習（例如 LRA）可以用它判斷 short-term 值是否更新了。
    uint32_t getUpdateCount() const noexcept { return updateCount_.load(std::memory_order_relaxed); }

    double getSampleRate() const noexcept { return sampleRate_; }
    int getNumChannels() const noexcept { return numChannels_; }
    int getStepSamples() const noexcept { return stepSamples_; }

private:
    static constexpr int kHistogramBins = 800;        // [-70, +10) LUFS
    static constexpr float kHistogramMinLufs = -70.0f;
    static constexpr float kHistogramStepLu = 0.1f;

    // 一個 100 ms 子區塊結束：把各 channel 的平方和加權相加後放進 ring，更新三種 loudness
    void finishStep() noexcept {
        double weighted = 0.0;
        for (int ch = 0; ch < numChannels_; ++ch) {
            weighted += static_cast<double>(channelWeights_[static_cast<size_t>(ch)]) *
                        stepAccumulators_[static_cast<size_t>(ch)];
            stepAccumulators_[static_cast<size_t>(ch)] = 0.0;
        }
        stepSampleCount_ = 0;
        stepRing_[static_cast<size_t>(ringPos_)] = weighted; // 這格存的是「平方總和」，還沒除以 sample 數
        ringPos_ = (ringPos_ + 1) % kShortTermSteps;
        if (stepsSeen_ < kShortTermSteps)
            ++stepsSeen_;

        // momentary：最近 4 個子區塊；short-term：最近 30 個（不足時用目前累積到的數量）
        const int momentarySteps = std::min(stepsSeen_, kBlockSteps);
        const int shortTermSteps = stepsSeen_;
        double sumM = 0.0, sumS = 0.0;
        for (int k = 0; k < shortTermSteps; ++k) {
            const int idx = (ringPos_ - 1 - k + kShortTermSteps) % kShortTermSteps;
            const double v = stepRing_[static_cast<size_t>(idx)];
            sumS += v;
            if (k < momentarySteps)
                sumM += v;
        }
        const double momentaryPower = sumM / (static_cast<double>(momentarySteps) * stepSamples_);
        const double shortTermPower = sumS / (static_cast<double>(shortTermSteps) * stepSamples_);
        const float momentaryLufs = powerToLufs(momentaryPower);
        const float shortTermLufs = powerToLufs(shortTermPower);
        momentary_.store(momentaryLufs, std::memory_order_relaxed);
        shortTerm_.store(shortTermLufs, std::memory_order_relaxed);
        // 最大值只在「視窗已經填滿」之後才更新：開頭不到 400 ms / 3 s 時只平均了 1~3 個（或不到 30 個）子區塊，
        // 一個剛好落在開頭的強 transient 會被高估（例如 100 ms 的 burst 只平均 100 ms 而不是 400 ms，高 6 dB），
        // 而且結果會隨內容在串流中的位置改變。momentary / short-term 的即時讀數則照常顯示，讓 UI 一開始就有反應。
        if (stepsSeen_ >= kBlockSteps && momentaryLufs > maxMomentary_.load(std::memory_order_relaxed))
            maxMomentary_.store(momentaryLufs, std::memory_order_relaxed);
        if (stepsSeen_ >= kShortTermSteps && shortTermLufs > maxShortTerm_.load(std::memory_order_relaxed))
            maxShortTerm_.store(shortTermLufs, std::memory_order_relaxed);

        // integrated：只有「完整的 400 ms block」才算 gating block
        if (stepsSeen_ >= kBlockSteps && momentaryLufs > kAbsoluteGateLufs) {
            int bin = static_cast<int>(std::floor((momentaryLufs - kHistogramMinLufs) / kHistogramStepLu));
            bin = std::clamp(bin, 0, kHistogramBins - 1);
            ++histogramCount_[static_cast<size_t>(bin)];
            histogramPower_[static_cast<size_t>(bin)] += momentaryPower;
            integrated_.store(computeIntegrated(), std::memory_order_relaxed);
        }
        updateCount_.fetch_add(1u, std::memory_order_relaxed);
    }

    float computeIntegrated() const noexcept {
        // 第一道：absolute gate 已經在放進直方圖時過濾了。先算通過 absolute gate 的平均
        double sumPower = 0.0;
        uint64_t count = 0;
        for (int b = 0; b < kHistogramBins; ++b) {
            sumPower += histogramPower_[static_cast<size_t>(b)];
            count += histogramCount_[static_cast<size_t>(b)];
        }
        if (count == 0)
            return kMinusInfinityDb;
        const float relativeGate = powerToLufs(sumPower / static_cast<double>(count)) + kRelativeGateLu;
        // 第二道：relative gate（以格中心值判斷）
        double gatedPower = 0.0;
        uint64_t gatedCount = 0;
        for (int b = 0; b < kHistogramBins; ++b) {
            const float binCentre = kHistogramMinLufs + (static_cast<float>(b) + 0.5f) * kHistogramStepLu;
            if (binCentre >= relativeGate) {
                gatedPower += histogramPower_[static_cast<size_t>(b)];
                gatedCount += histogramCount_[static_cast<size_t>(b)];
            }
        }
        if (gatedCount == 0)
            return kMinusInfinityDb;
        return powerToLufs(gatedPower / static_cast<double>(gatedCount));
    }

    double sampleRate_ = 0.0;
    int numChannels_ = 0;
    int stepSamples_ = 1;
    Biquad stage1_, stage2_;
    std::array<float, kMaxChannels> channelWeights_{{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f}};
    std::array<double, kMaxChannels> stepAccumulators_{};
    std::array<double, kShortTermSteps> stepRing_{};
    int stepSampleCount_ = 0;
    int ringPos_ = 0;
    int stepsSeen_ = 0;
    std::vector<uint32_t> histogramCount_;
    std::vector<double> histogramPower_;

    std::atomic<float> momentary_{kMinusInfinityDb};
    std::atomic<float> shortTerm_{kMinusInfinityDb};
    std::atomic<float> integrated_{kMinusInfinityDb};
    std::atomic<float> maxMomentary_{kMinusInfinityDb};
    std::atomic<float> maxShortTerm_{kMinusInfinityDb};
    std::atomic<uint32_t> updateCount_{0};
};

} // namespace dawlab::dsp
