#pragma once
// Flanger：很短的延遲（0.5–10 ms）+ LFO 掃動 + feedback，形成「會掃動的 comb filter」。第 10 章。
//
// 訊號流程：
//
//   x ──┬───────────────────────────────────────(1-mix)──┐
//       │                                                  ├─► y
//       └─►(+)─► FractionalDelayLine ─ read(d(t)) ──┬──(mix)─┘
//           ▲                                       │
//           └──────────── feedback ◄────────────────┘
//
// 沒有 feedback、mix = 0.5 時就是第 09 章的 FIR comb：
//   |H(f)| = |0.5 + 0.5 e^{-j2πf d}| = |cos(π f d)|
//   notch 在 f = (2k+1) / (2d)，peak 在 f = k / d（d 以秒計）。
// 延遲 d(t) 被 LFO 掃動 → 整排 notch 一起上下滑動，就是噴射機的「咻──」聲。
//
// 延遲時間 d(t) = delayMs + depthMs · (1 + lfo) / 2，從 delayMs 掃到 delayMs + depthMs。
//
// 依賴：第 09 章 FractionalDelayLine、本章 Lfo。

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>
#include <dawlab/dsp/FractionalDelayLine.h>
#include <dawlab/dsp/Lfo.h>

#include <algorithm>
#include <cmath>

namespace dawlab::dsp {

class Flanger {
public:
    static constexpr float kMinDelayMs = 0.1f;
    static constexpr float kMaxDelayMs = 20.0f;
    static constexpr float kMaxDepthMs = 20.0f;

    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        (void)maxBlockSize;
        sampleRate_ = sampleRate;
        numChannels_ = std::max(1, numChannels);
        const int maxDelay = msToSamples(kMaxDelayMs + kMaxDepthMs, sampleRate) + 8;
        delay_.prepare(maxDelay, numChannels_);
        lfo_.prepare(sampleRate);
        delaySm_.prepare(sampleRate, 20.0);
        depthSm_.prepare(sampleRate, 20.0);
        feedbackSm_.prepare(sampleRate, 20.0);
        mixSm_.prepare(sampleRate, 20.0);
        reset();
    }

    void reset() noexcept {
        delay_.reset();
        lfo_.reset();
        delaySm_.setCurrentAndTarget(delaySm_.getTarget());
        depthSm_.setCurrentAndTarget(depthSm_.getTarget());
        feedbackSm_.setCurrentAndTarget(feedbackSm_.getTarget());
        mixSm_.setCurrentAndTarget(mixSm_.getTarget());
    }

    // ---- 參數 ----------------------------------------------------------------
    void setRateHz(float hz) noexcept { lfo_.setRateHz(std::clamp(hz, 0.0f, 20.0f)); }
    void setDepthMs(float ms) noexcept { depthSm_.setTarget(std::clamp(ms, 0.0f, kMaxDepthMs)); }
    // 基礎延遲（掃動範圍的下限）
    void setDelayMs(float ms) noexcept { delaySm_.setTarget(std::clamp(ms, kMinDelayMs, kMaxDelayMs)); }
    // 正值：peak 變尖（f = k/d 的共振感）；負值：notch 被填平、peak 移到 (2k+1)/(2d)，聲音較「空心」
    void setFeedback(float fb) noexcept { feedbackSm_.setTarget(std::clamp(fb, -0.99f, 0.99f)); }
    void setMix(float mix) noexcept { mixSm_.setTarget(std::clamp(mix, 0.0f, 1.0f)); }
    void setLfoShape(LfoShape shape) noexcept { lfo_.setShape(shape); }
    // 把 LFO 停在某個相位（rate 設 0 之後使用），量測頻率響應時用
    void setLfoPhase(float phase01) noexcept { lfo_.setPhase(phase01); }

    float getRateHz() const noexcept { return lfo_.getRateHz(); }
    float getDepthMs() const noexcept { return depthSm_.getTarget(); }
    float getDelayMs() const noexcept { return delaySm_.getTarget(); }
    float getFeedback() const noexcept { return feedbackSm_.getTarget(); }
    float getMix() const noexcept { return mixSm_.getTarget(); }
    // 最近處理的 sample 實際使用的延遲（ms），給 UI 或量測用（thread-safe：relaxed atomic）
    float getCurrentDelayMs() const noexcept { return uiDelayMs_.load(); }

    // 理論 notch 頻率（mix = 0.5、feedback = 0）：f_k = (2k+1) / (2 d)
    static double notchFrequencyHz(int k, double delayMs) noexcept {
        return (2.0 * k + 1.0) / (2.0 * delayMs * 0.001);
    }
    // 理論 peak 頻率：f_k = k / d
    static double peakFrequencyHz(int k, double delayMs) noexcept { return k / (delayMs * 0.001); }

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
        const float baseMs = delaySm_.next();
        const float depthMs = depthSm_.next();
        feedback_ = feedbackSm_.next();
        mix_ = mixSm_.next();
        const float m = lfo_.getNextValue();                     // -1..1
        currentDelayMs_ = baseMs + depthMs * 0.5f * (1.0f + m);  // delayMs .. delayMs + depthMs
        uiDelayMs_.store(currentDelayMs_);
        const float maxRead = static_cast<float>(delay_.getMaxDelaySamples() - 3);
        currentDelaySamples_ = std::clamp(static_cast<float>(currentDelayMs_ * sampleRate_ * 0.001), 1.0f, maxRead);
    }

    float renderSample(int ch, float x) noexcept {
        // 有 feedback 時必須「先讀後寫」：寫入的值要包含讀出來的 delayed。
        // 讀取發生在 push 之前，所以 read(d - 1) 才是距離現在 d 個 sample 的值
        // （push 之後 read(0) = 剛寫入的 sample，push 之前 read(0) = 上一個 sample）。
        const float delayed = delay_.readCubic(ch, std::max(1.0f, currentDelaySamples_ - 1.0f));
        float w = x + feedback_ * delayed;
        if (std::abs(w) < 1e-20f) // 迴路中的極小值 flush 成 0，避免 denormal
            w = 0.0f;
        delay_.push(ch, w);
        return (1.0f - mix_) * x + mix_ * delayed;
    }

    double sampleRate_ = 48000.0;
    int numChannels_ = 2;
    FractionalDelayLine delay_;
    Lfo lfo_;

    // 預設值：delay 1 ms、depth 2 ms、feedback 0.5、mix 0.5
    detail::ModParamSmoother delaySm_{1.0f}, depthSm_{2.0f}, feedbackSm_{0.5f}, mixSm_{0.5f};
    float feedback_ = 0.5f, mix_ = 0.5f;
    float currentDelayMs_ = 1.0f;                       // audio thread 內部使用
    detail::RelaxedAtomicFloat uiDelayMs_{1.0f};        // 給 UI 讀
    float currentDelaySamples_ = 48.0f;
};

} // namespace dawlab::dsp
