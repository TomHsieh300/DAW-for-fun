#pragma once
// Chorus：用 LFO 調變的短延遲（5–30 ms）加上原音，做出「好幾個人一起演奏、各自稍微走音」的厚度。
// 第 10 章。
//
// 訊號流程（每個 voice 一條）：
//
//   x ──┬──────────────────────────────────────────(1-mix)──┐
//       │                                                     ├─► y
//       └─► FractionalDelayLine ─ read(centre + depth·lfo_v) ─(mix/N)─┘   （N 個 voice 相加）
//
// - 延遲時間被 LFO 慢慢拉長 / 縮短 → 讀取速度不等於寫入速度 → 音高輕微上下漂（Doppler 效應）。
//   d(t) = d0 + D·sin(2π r t)  ⇒  音高比例 = 1 - d'(t) = 1 - 2π r D cos(2π r t)
// - 多個 voice 的 LFO 起始相位均分一圈（voice v 的相位 = v / N），彼此的走音方向不同。
// - stereo spread：右聲道讀同一個 voice 的 LFO 時，相位再加上 spread × 0.25 週期（最多相差 90°），
//   左右走音不同步 → 立體聲變寬（左右相關性下降）。
//
// 依賴：第 09 章 FractionalDelayLine（readCubic）、本章 Lfo。

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>
#include <dawlab/dsp/FractionalDelayLine.h>
#include <dawlab/dsp/Lfo.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace dawlab::dsp {

class Chorus {
public:
    static constexpr int kMaxVoices = 4;
    static constexpr float kMaxCentreDelayMs = 50.0f;
    static constexpr float kMaxDepthMs = 30.0f;

    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        (void)maxBlockSize; // 逐 sample 處理，不需要暫存 block
        sampleRate_ = sampleRate;
        numChannels_ = std::max(1, numChannels);

        // 唯一可以 allocate 的地方：最長延遲 = centre 上限 + depth 上限，再留幾個 sample 給插值
        const int maxDelay = msToSamples(kMaxCentreDelayMs + kMaxDepthMs, sampleRate) + 8;
        delay_.prepare(maxDelay, numChannels_);

        for (auto& lfo : lfos_) {
            lfo.prepare(sampleRate);
            lfo.setRateHz(rateHz_); // prepare 前呼叫過 setRateHz 也不會遺失
        }

        centreSm_.prepare(sampleRate, 20.0);
        depthSm_.prepare(sampleRate, 20.0);
        mixSm_.prepare(sampleRate, 20.0);
        spreadSm_.prepare(sampleRate, 20.0);
        reset();
    }

    void reset() noexcept {
        delay_.reset();
        spreadVoicePhases(0.0f); // 回到初始狀態：voice v 的起始相位 = v / N
        for (auto& lfo : lfos_)
            lfo.reset();
        centreSm_.setCurrentAndTarget(centreSm_.getTarget());
        depthSm_.setCurrentAndTarget(depthSm_.getTarget());
        mixSm_.setCurrentAndTarget(mixSm_.getTarget());
        spreadSm_.setCurrentAndTarget(spreadSm_.getTarget());
    }

    // ---- 參數 ----------------------------------------------------------------
    void setRateHz(float hz) noexcept {
        rateHz_ = std::clamp(hz, 0.0f, 20.0f);
        for (auto& lfo : lfos_)
            lfo.setRateHz(rateHz_);
    }
    // 延遲偏離 centre 的最大量（±depth）。
    void setDepthMs(float ms) noexcept { depthSm_.setTarget(std::clamp(ms, 0.0f, kMaxDepthMs)); }
    void setCentreDelayMs(float ms) noexcept { centreSm_.setTarget(std::clamp(ms, 1.0f, kMaxCentreDelayMs)); }
    // plugin / DAW 常常每個 block 都把所有參數推一次，所以「值沒變」時必須什麼都不做——
    // 否則每個 block 都把 LFO 拉回起始相位，延遲週期性地跳回去，就變成嚴重的走音 glitch。
    // 值真的改變時：voice 0 保持目前相位繼續走，只把其他 voice 重新均分在它後面（v / N 週期）。
    // 其他 voice 的延遲仍會跳一下，所以這是「設定時」的操作（UI 上做成離散選項）。
    void setVoices(int voices) noexcept {
        voices = std::clamp(voices, 1, kMaxVoices);
        if (voices == voices_)
            return;
        voices_ = voices;
        spreadVoicePhases(lfos_[0].getPhase());
    }
    void setMix(float mix) noexcept { mixSm_.setTarget(std::clamp(mix, 0.0f, 1.0f)); }
    // 0 = 左右完全相同；1 = 右聲道的 LFO 和左聲道相差 90°（相位 +0.25 週期）
    void setStereoSpread(float spread) noexcept { spreadSm_.setTarget(std::clamp(spread, 0.0f, 1.0f)); }
    // 示範用：關掉 cubic 改用 linear 插值，聽 / 量高頻損失
    void setUseCubicInterpolation(bool cubic) noexcept { useCubic_ = cubic; }

    float getRateHz() const noexcept { return rateHz_; }
    float getDepthMs() const noexcept { return depthSm_.getTarget(); }
    float getCentreDelayMs() const noexcept { return centreSm_.getTarget(); }
    int getVoices() const noexcept { return voices_; }
    float getMix() const noexcept { return mixSm_.getTarget(); }
    float getStereoSpread() const noexcept { return spreadSm_.getTarget(); }

    // ---- 處理 ----------------------------------------------------------------
    // 逐 sample：呼叫順序必須是每個時間點先 channel 0、再 channel 1……
    // （LFO 與參數平滑只在 channel 0 前進一步，其他 channel 共用同一組值）
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
    // voice v 的相位 = base + v / N（Lfo::setPhase 會 wrap 回 0..1）
    void spreadVoicePhases(float base) noexcept {
        for (int v = 0; v < kMaxVoices; ++v)
            lfos_[static_cast<size_t>(v)].setPhase(base + static_cast<float>(v) / static_cast<float>(voices_));
    }

    // 每個時間點做一次：平滑參數、抓各 voice 的相位並讓 LFO 前進
    void advanceModulation() noexcept {
        centreMs_ = centreSm_.next();
        depthMs_ = depthSm_.next();
        mix_ = mixSm_.next();
        spread_ = spreadSm_.next();
        for (int v = 0; v < voices_; ++v) {
            auto& lfo = lfos_[static_cast<size_t>(v)];
            voicePhase_[v] = lfo.getPhase();
            lfo.getNextValue();
        }
    }

    float renderSample(int ch, float x) noexcept {
        delay_.push(ch, x); // 先寫入，讀 d 個 sample 前就是精確的 d（d = 0 會讀到剛寫入的 x）

        const float phaseOffset = 0.25f * spread_ * static_cast<float>(ch); // 右聲道最多相差 90°
        const float msToSamplesF = static_cast<float>(sampleRate_ * 0.001);
        const float maxRead = static_cast<float>(delay_.getMaxDelaySamples() - 3);

        float wet = 0.0f;
        for (int v = 0; v < voices_; ++v) {
            const float m = Lfo::shapeValue(LfoShape::Sine, voicePhase_[v] + phaseOffset);
            const float delayMs = centreMs_ + depthMs_ * m;
            const float delaySamples = std::clamp(delayMs * msToSamplesF, 2.0f, maxRead);
            wet += useCubic_ ? delay_.readCubic(ch, delaySamples) : delay_.readLinear(ch, delaySamples);
        }
        wet /= static_cast<float>(voices_);
        return (1.0f - mix_) * x + mix_ * wet;
    }

    double sampleRate_ = 48000.0;
    int numChannels_ = 2;
    int voices_ = 2;
    float rateHz_ = 0.8f;
    bool useCubic_ = true;

    FractionalDelayLine delay_;
    std::array<Lfo, kMaxVoices> lfos_{}; // 固定大小：prepare 之前呼叫 setter 也安全
    float voicePhase_[kMaxVoices] = {};

    // 預設值：centre 15 ms、depth 3 ms、mix 0.5、spread 1
    detail::ModParamSmoother centreSm_{15.0f}, depthSm_{3.0f}, mixSm_{0.5f}, spreadSm_{1.0f};
    float centreMs_ = 15.0f, depthMs_ = 3.0f, mix_ = 0.5f, spread_ = 1.0f;
};

} // namespace dawlab::dsp
