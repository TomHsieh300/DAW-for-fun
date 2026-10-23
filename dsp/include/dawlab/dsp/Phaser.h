#pragma once
// Phaser：串接 N 個一階 allpass filter，讓不同頻率的相位偏移不同量，再和原音相加 →
// 相位差 180° 的頻率互相抵消成 notch。LFO 以指數刻度掃動 allpass 的中心頻率。第 10 章。
//
// 訊號流程：
//
//   x ──┬──────────────────────────────────────────────(1-mix)──┐
//       │                                                         ├─► y
//       └─►(+)─► AP1 ─► AP2 ─► … ─► APN ──┬────────────────(mix)─┘
//           ▲                              │
//           └──────── feedback ◄───────────┘
//
// 一階 allpass：H(z) = (a + z⁻¹) / (1 + a z⁻¹)，  a = (tan(π fc/fs) − 1) / (tan(π fc/fs) + 1)
//   |H| = 1（所有頻率都不改變振幅），相位 φ(ω) = −2·arctan( tan(ω/2) / tan(ωc/2) )
//   在 f = fc 時 φ = −90°，DC 是 0°，Nyquist 是 −180°。
// N 級串接的總相位 = N·φ(ω)，和原音相加後 |1 + e^{jNφ}| = 2|cos(Nφ/2)|，
//   Nφ = −(2k+1)π 時為 0 → notch 數 = N/2（N 為偶數）。
//
// 這裡用第 02 章的 Biquad 當每一級的容器：把 b2 = a2 = 0 就是一階 section
//   （b0 = a, b1 = 1, b2 = 0, a1 = a, a2 = 0）。
//
// 依賴：第 02 章 Biquad、本章 Lfo。

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>
#include <dawlab/dsp/Biquad.h>
#include <dawlab/dsp/Lfo.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace dawlab::dsp {

class Phaser {
public:
    static constexpr int kMinStages = 2;
    static constexpr int kMaxStages = 12;

    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        sampleRate_ = sampleRate;
        numChannels_ = std::max(1, numChannels);
        for (auto& s : stages_)
            s.prepare(sampleRate, maxBlockSize, numChannels_);
        lastOut_.assign(static_cast<size_t>(numChannels_), 0.0f);
        lfo_.prepare(sampleRate);
        depthSm_.prepare(sampleRate, 20.0);
        centreSm_.prepare(sampleRate, 20.0);
        feedbackSm_.prepare(sampleRate, 20.0);
        mixSm_.prepare(sampleRate, 20.0);
        reset();
    }

    void reset() noexcept {
        for (auto& s : stages_)
            s.reset();
        std::fill(lastOut_.begin(), lastOut_.end(), 0.0f);
        lfo_.reset();
        depthSm_.setCurrentAndTarget(depthSm_.getTarget());
        centreSm_.setCurrentAndTarget(centreSm_.getTarget());
        feedbackSm_.setCurrentAndTarget(feedbackSm_.getTarget());
        mixSm_.setCurrentAndTarget(mixSm_.getTarget());
    }

    // ---- 參數 ----------------------------------------------------------------
    void setRateHz(float hz) noexcept { lfo_.setRateHz(std::clamp(hz, 0.0f, 20.0f)); }
    // 0 = 不掃動（停在 centre）；1 = 掃動 ±sweepOctaves 個八度
    void setDepth(float depth) noexcept { depthSm_.setTarget(std::clamp(depth, 0.0f, 1.0f)); }
    void setCentreHz(float hz) noexcept { centreSm_.setTarget(std::clamp(hz, 20.0f, 20000.0f)); }
    void setFeedback(float fb) noexcept { feedbackSm_.setTarget(std::clamp(fb, -0.95f, 0.95f)); }
    // 2..12 的偶數；奇數會向下取偶
    void setStages(int stages) noexcept {
        stages = std::clamp(stages, kMinStages, kMaxStages);
        stages -= stages % 2;
        // 新啟用的 stage 可能留著很久以前的狀態，先清掉，避免切換時冒出舊的訊號
        for (int s = numStages_; s < stages; ++s)
            stages_[static_cast<size_t>(s)].reset();
        numStages_ = stages;
    }
    void setMix(float mix) noexcept { mixSm_.setTarget(std::clamp(mix, 0.0f, 1.0f)); }
    void setLfoShape(LfoShape shape) noexcept { lfo_.setShape(shape); }
    void setLfoPhase(float phase01) noexcept { lfo_.setPhase(phase01); }
    // depth = 1 時掃動 ±sweepOctaves（預設 ±2 個八度 = 總共 4 個八度）
    void setSweepOctaves(float octaves) noexcept { sweepOctaves_ = std::clamp(octaves, 0.0f, 6.0f); }

    float getRateHz() const noexcept { return lfo_.getRateHz(); }
    float getDepth() const noexcept { return depthSm_.getTarget(); }
    float getCentreHz() const noexcept { return centreSm_.getTarget(); }
    float getFeedback() const noexcept { return feedbackSm_.getTarget(); }
    int getStages() const noexcept { return numStages_; }
    float getMix() const noexcept { return mixSm_.getTarget(); }
    // 最近處理的 sample 的 allpass 中心頻率（Hz），給 UI / 量測（thread-safe：relaxed atomic）
    float getCurrentCentreHz() const noexcept { return uiCentreHz_.load(); }

    // ---- 公式（static，給範例 / 練習驗證用） -----------------------------------
    // 一階 allpass 係數：a = (tan(π fc/fs) − 1) / (tan(π fc/fs) + 1)
    static double allpassCoefficient(double fcHz, double sampleRate) noexcept {
        const double t = std::tan(kPi * std::clamp(fcHz, 1.0, 0.49 * sampleRate) / sampleRate);
        return (t - 1.0) / (t + 1.0);
    }
    // 一階 allpass 在頻率 f 的相位（弧度，0 .. −π）：φ = −2 arctan( tan(πf/fs) / tan(πfc/fs) )
    static double allpassPhase(double fHz, double fcHz, double sampleRate) noexcept {
        return -2.0 * std::atan(std::tan(kPi * fHz / sampleRate) / std::tan(kPi * fcHz / sampleRate));
    }
    // N 級、全部中心頻率為 fc 時，第 k 個 notch（k = 0 .. N/2−1）的頻率：
    //   N·φ(f) = −(2k+1)π  ⇒  tan(πf/fs) = tan(πfc/fs) · tan((2k+1)π / (2N))
    static double notchFrequencyHz(int k, int stages, double fcHz, double sampleRate) noexcept {
        const double t = std::tan(kPi * fcHz / sampleRate) * std::tan((2.0 * k + 1.0) * kPi / (2.0 * stages));
        return std::atan(t) * sampleRate / kPi;
    }
    // 把 fc 包成「一階 allpass 的 Biquad 係數」
    static BiquadCoefficients makeFirstOrderAllpass(double fcHz, double sampleRate) noexcept {
        const double a = allpassCoefficient(fcHz, sampleRate);
        BiquadCoefficients c;
        c.b0 = a;
        c.b1 = 1.0;
        c.b2 = 0.0;
        c.a1 = a;
        c.a2 = 0.0;
        return c;
    }

    // ---- 處理 ----------------------------------------------------------------
    float processSample(int channel, float x) noexcept {
        if (channel == 0)
            advanceModulation();
        return renderSample(channel, x);
    }

    void process(AudioBlock block) noexcept {
        const int n = block.getNumSamples();
        const int nch = std::min(block.getNumChannels(), numChannels_);
        for (int i = 0; i < n; ++i) {
            advanceModulation();
            for (int ch = 0; ch < nch; ++ch) {
                float* d = block.getChannelPointer(ch);
                d[i] = renderSample(ch, d[i]);
            }
        }
    }

private:
    void advanceModulation() noexcept {
        const float depth = depthSm_.next();
        const float centre = centreSm_.next();
        feedback_ = feedbackSm_.next();
        mix_ = mixSm_.next();

        // 指數刻度：fc = centre · 2^(depth · octaves · lfo)，耳朵聽音高是對數的
        const float m = lfo_.getNextValue();
        const float octaves = depth * sweepOctaves_ * m;
        currentHz_ = std::clamp(centre * std::exp2(octaves), 20.0f, static_cast<float>(0.45 * sampleRate_));
        uiCentreHz_.store(currentHz_);

        const BiquadCoefficients c = makeFirstOrderAllpass(currentHz_, sampleRate_);
        for (int s = 0; s < numStages_; ++s)
            stages_[static_cast<size_t>(s)].setCoefficients(c);
    }

    float renderSample(int ch, float x) noexcept {
        float w = x + feedback_ * lastOut_[static_cast<size_t>(ch)];
        for (int s = 0; s < numStages_; ++s)
            w = stages_[static_cast<size_t>(s)].processSample(ch, w);
        if (std::abs(w) < 1e-20f)
            w = 0.0f;
        lastOut_[static_cast<size_t>(ch)] = w;
        return (1.0f - mix_) * x + mix_ * w;
    }

    double sampleRate_ = 48000.0;
    int numChannels_ = 2;
    int numStages_ = 4;
    float sweepOctaves_ = 2.0f;

    std::array<Biquad, kMaxStages> stages_;
    std::vector<float> lastOut_;
    Lfo lfo_;

    // 預設值：depth 1、centre 800 Hz、feedback 0.3、mix 0.5
    detail::ModParamSmoother depthSm_{1.0f}, centreSm_{800.0f}, feedbackSm_{0.3f}, mixSm_{0.5f};
    float feedback_ = 0.3f, mix_ = 0.5f;
    float currentHz_ = 800.0f;                   // audio thread 內部使用
    detail::RelaxedAtomicFloat uiCentreHz_{800.0f}; // 給 UI 讀
};

} // namespace dawlab::dsp
