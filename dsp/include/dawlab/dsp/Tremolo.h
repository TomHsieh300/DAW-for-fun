#pragma once
// Tremolo：振幅調變（amplitude modulation）。gain 隨 LFO 在 1 和 (1 − depth) 之間擺動。第 10 章。
//
//   y = x · g(t)，  g(t) = 1 − depth · (1 − lfo(t)) / 2        （lfo = +1 → g = 1；lfo = −1 → g = 1 − depth）
//
// 用 sine LFO 時 g(t) = (1 − depth/2) + (depth/2)·sin(2π r t)，所以一個頻率 f 的正弦輸入會變成
// f（振幅 ×(1 − depth/2)）加上 f ± r 兩個 sideband（振幅各 ×depth/4）—— 這就是 AM 的頻譜。
//
// autopan：右聲道用 −lfo，左大聲時右小聲，聲像左右來回。
// 兩聲道 gain 相加 = 2 − depth 為常數（linear pan law），聽感上總音量大致不變。
//
// 依賴：本章 Lfo。

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>
#include <dawlab/dsp/Lfo.h>

#include <algorithm>
#include <cmath>

namespace dawlab::dsp {

class Tremolo {
public:
    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        (void)maxBlockSize;
        numChannels_ = std::max(1, numChannels);
        lfo_.prepare(sampleRate);
        depthSm_.prepare(sampleRate, 20.0);
        reset();
    }

    void reset() noexcept {
        lfo_.reset();
        depthSm_.setCurrentAndTarget(depthSm_.getTarget());
        gain_[0] = gain_[1] = 1.0f;
        publishGains();
    }

    // ---- 參數 ----------------------------------------------------------------
    void setRateHz(float hz) noexcept { lfo_.setRateHz(std::clamp(hz, 0.0f, 50.0f)); }
    void setDepth(float depth) noexcept { depthSm_.setTarget(std::clamp(depth, 0.0f, 1.0f)); }
    void setShape(LfoShape shape) noexcept { lfo_.setShape(shape); }
    void setAutoPan(bool autoPan) noexcept { autoPan_ = autoPan; }
    void setLfoPhase(float phase01) noexcept { lfo_.setPhase(phase01); }
    // Square / Saw / S&H 的邊緣平滑（ms），0 = 關閉（會 click，示範用）
    void setSmoothingMs(float ms) noexcept { lfo_.setSmoothingMs(ms); }

    float getRateHz() const noexcept { return lfo_.getRateHz(); }
    float getDepth() const noexcept { return depthSm_.getTarget(); }
    LfoShape getShape() const noexcept { return lfo_.getShape(); }
    bool getAutoPan() const noexcept { return autoPan_; }
    // 最近處理的 sample 套用在某聲道的 gain（給 UI / 量測；thread-safe）。
    // audio thread 每個 sample 用普通 float 計算，再 relaxed store 到 atomic，UI thread 讀 atomic。
    // channel 1 只有在 autopan 時才和 channel 0 不同。
    float getCurrentGain(int channel) const noexcept { return uiGain_[channel == 1 ? 1 : 0].load(); }

    // ---- 處理 ----------------------------------------------------------------
    float processSample(int channel, float x) noexcept {
        if (channel == 0)
            advanceModulation();
        return x * gain_[(autoPan_ && channel == 1) ? 1 : 0];
    }

    void process(AudioBlock block) noexcept {
        const int n = block.getNumSamples();
        const int nch = std::min(block.getNumChannels(), numChannels_);
        for (int i = 0; i < n; ++i) {
            advanceModulation();
            for (int ch = 0; ch < nch; ++ch) {
                float* d = block.getChannelPointer(ch);
                d[i] *= gain_[(autoPan_ && ch == 1) ? 1 : 0];
            }
        }
    }

private:
    void advanceModulation() noexcept {
        const float depth = depthSm_.next();
        const float m = lfo_.getNextValue();
        gain_[0] = 1.0f - depth * 0.5f * (1.0f - m); // lfo = +1 → 1，lfo = −1 → 1 − depth
        gain_[1] = 1.0f - depth * 0.5f * (1.0f + m); // 反相（autopan 的右聲道）
        publishGains();
    }

    void publishGains() noexcept {
        uiGain_[0].store(gain_[0]);
        uiGain_[1].store(autoPan_ ? gain_[1] : gain_[0]); // 實際乘在右聲道的值
    }

    int numChannels_ = 2;
    bool autoPan_ = false;
    Lfo lfo_;
    detail::ModParamSmoother depthSm_{0.5f}; // 預設 depth 0.5
    float gain_[2] = {1.0f, 1.0f};                           // audio thread 內部使用
    detail::RelaxedAtomicFloat uiGain_[2] = {detail::RelaxedAtomicFloat{1.0f}, detail::RelaxedAtomicFloat{1.0f}}; // 給 UI 讀
};

} // namespace dawlab::dsp
