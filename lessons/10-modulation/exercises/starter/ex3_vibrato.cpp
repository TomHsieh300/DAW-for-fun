// 練習 3 起始碼：Vibrato —— 調變延遲，但不混原音
//
// 目標：寫一個 Vibrato class，用 FractionalDelayLine + Lfo 做出「音高上下漂、振幅不變」的效果。
// 和 chorus 的差別只有：輸出全部是延遲後的訊號（mix = 1），沒有乾訊號 → 沒有 comb 峰谷。
//
// 延遲 d(t) = centre + depth·lfo(t)
// 音高比例 = 1 − d'(t)，最大偏移 = ±2πr·depth（depth 以秒計）
//
// 這支程式可以編譯；執行時因為 TODO 還沒完成，self-check 會印出 FAIL（這是預期的）。
// 完成後應該看到：1 kHz 正弦、rate 5 Hz、depth 1 ms → 瞬時頻率 968.6 .. 1031.4 Hz，RMS 不變。

#include <dawlab/Common.h>
#include <dawlab/dsp/FractionalDelayLine.h>
#include <dawlab/dsp/Lfo.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

using dawlab::dsp::FractionalDelayLine;
using dawlab::dsp::Lfo;
using dawlab::dsp::LfoShape;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

class Vibrato {
public:
    static constexpr float kMaxCentreMs = 20.0f;
    static constexpr float kMaxDepthMs = 10.0f;

    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        (void)maxBlockSize;
        sampleRate_ = sampleRate;
        numChannels_ = std::max(1, numChannels);
        // TODO 1：delay_.prepare(...) —— 最長延遲 = kMaxCentreMs + kMaxDepthMs（換成 sample），多留幾個 sample 給插值
        lfo_.prepare(sampleRate);
        depthSm_.prepare(sampleRate, 20.0);
        reset();
    }

    void reset() noexcept {
        delay_.reset();
        lfo_.reset();
        depthSm_.setCurrentAndTarget(depthSm_.getTarget());
    }

    void setRateHz(float hz) noexcept { lfo_.setRateHz(std::clamp(hz, 0.0f, 20.0f)); }
    void setDepthMs(float ms) noexcept { depthSm_.setTarget(std::clamp(ms, 0.0f, kMaxDepthMs)); }
    void setCentreDelayMs(float ms) noexcept { centreMs_ = std::clamp(ms, 1.0f, kMaxCentreMs); }
    void setShape(LfoShape shape) noexcept { lfo_.setShape(shape); }

    float processSample(int channel, float x) noexcept {
        if (channel == 0)
            advanceModulation();
        // TODO 3：push 進 delay line，再用 readCubic 讀 currentDelaySamples_ 回傳（不要混 x！）
        return x;
    }

    void process(dawlab::AudioBlock block) noexcept {
        const int n = block.getNumSamples();
        const int nch = std::min(block.getNumChannels(), numChannels_);
        for (int i = 0; i < n; ++i) {
            advanceModulation();
            for (int ch = 0; ch < nch; ++ch) {
                float* d = block.getChannelPointer(ch);
                // TODO 3：同上
                (void)ch;
                (void)d;
            }
        }
    }

private:
    void advanceModulation() noexcept {
        const float depth = depthSm_.next();
        // TODO 2：ms = centreMs_ + depth · lfo_.getNextValue()，換成 sample 後 clamp 在 [2, maxDelay − 3]，存進 currentDelaySamples_
        (void)depth;
    }

    double sampleRate_ = 48000.0;
    int numChannels_ = 2;
    float centreMs_ = 5.0f;
    float currentDelaySamples_ = 240.0f;
    FractionalDelayLine delay_;
    Lfo lfo_;
    dawlab::dsp::detail::ModParamSmoother depthSm_{1.0f};
};

template <typename Effect>
void processInBlocks(Effect& fx, dawlab::AudioBuffer& buffer) {
    dawlab::AudioBlock whole(buffer);
    for (int start = 0; start < buffer.getNumSamples(); start += kBlockSize) {
        const int n = std::min(kBlockSize, buffer.getNumSamples() - start);
        fx.process(whole.getSubBlock(start, n));
    }
}

// 用相鄰正向零交叉的間距估計瞬時頻率：回傳最小、最大、平均
void instantaneousFrequency(const float* x, int n, int skip, double& fMin, double& fMax, double& fMean) {
    fMin = 1.0e9;
    fMax = 0.0;
    double lastCross = -1.0, sum = 0.0;
    int count = 0;
    for (int i = std::max(1, skip); i < n; ++i) {
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) {
            const double cross = (i - 1) + double(-x[i - 1]) / (double(x[i]) - x[i - 1]);
            if (lastCross >= 0.0) {
                const double f = kSampleRate / (cross - lastCross);
                fMin = std::min(fMin, f);
                fMax = std::max(fMax, f);
                sum += f;
                ++count;
            }
            lastCross = cross;
        }
    }
    fMean = count > 0 ? sum / count : 0.0;
}

} // namespace

int main() {
    std::printf("=== 第 10 章 練習 3：Vibrato（starter）===\n\n");

    const float rate = 5.0f, depthMs = 1.0f;
    const double dev = dawlab::kTwoPi * rate * depthMs * 0.001;

    {
        dawlab::AudioBuffer tone = dawlab::makeSine(1000.0, kSampleRate, 3.0, 0.5f, 1);
        const int skip = dawlab::secondsToSamples(0.5, kSampleRate);
        const float inRms = dawlab::rms(tone.getReadPointer(0) + skip, tone.getNumSamples() - skip);

        Vibrato v;
        v.prepare(kSampleRate, kBlockSize, 1);
        v.setRateHz(rate);
        v.setDepthMs(depthMs);
        v.setCentreDelayMs(5.0f);
        v.reset();
        processInBlocks(v, tone);

        double fMin, fMax, fMean;
        instantaneousFrequency(tone.getReadPointer(0), tone.getNumSamples(), skip, fMin, fMax, fMean);
        const float outRms = dawlab::rms(tone.getReadPointer(0) + skip, tone.getNumSamples() - skip);
        std::printf("1 kHz 正弦、rate %.1f Hz、depth %.1f ms：\n", rate, depthMs);
        std::printf("  理論：%.1f .. %.1f Hz    量測：%.1f .. %.1f Hz，平均 %.2f Hz；RMS %+.2f dB\n",
                    1000.0 * (1.0 - dev), 1000.0 * (1.0 + dev), fMin, fMax, fMean, dawlab::gainToDb(outRms / inRms));
        DAWLAB_CHECK_NEAR(fMax, 1000.0 * (1.0 + dev), 2.0);
        DAWLAB_CHECK_NEAR(fMin, 1000.0 * (1.0 - dev), 2.0);
        DAWLAB_CHECK_NEAR(fMean, 1000.0, 1.0);
        DAWLAB_CHECK_NEAR(dawlab::gainToDb(outRms / inRms), 0.0, 0.3);
    }

    // depth 0：只是固定延遲，瞬時頻率應該固定在 1000 Hz
    {
        dawlab::AudioBuffer tone = dawlab::makeSine(1000.0, kSampleRate, 2.0, 0.5f, 1);
        Vibrato v;
        v.prepare(kSampleRate, kBlockSize, 1);
        v.setRateHz(rate);
        v.setDepthMs(0.0f);
        v.reset();
        processInBlocks(v, tone);
        double fMin, fMax, fMean;
        instantaneousFrequency(tone.getReadPointer(0), tone.getNumSamples(),
                               dawlab::secondsToSamples(0.5, kSampleRate), fMin, fMax, fMean);
        std::printf("depth 0：瞬時頻率 %.2f .. %.2f Hz（應該固定在 1000）\n\n", fMin, fMax);
        DAWLAB_CHECK_NEAR(fMin, 1000.0, 0.5);
        DAWLAB_CHECK_NEAR(fMax, 1000.0, 0.5);
    }

    {
        dawlab::AudioBuffer pluck = dawlab::makePluckSequence(kSampleRate, 4.0, 0.4f, 2);
        Vibrato v;
        v.prepare(kSampleRate, kBlockSize, 2);
        v.setRateHz(5.5f);
        v.setDepthMs(0.6f);
        v.reset();
        processInBlocks(v, pluck);
        dawlab::writeWav(dawlab::outputPath("l10_ex3_vibrato.wav"), pluck, kSampleRate);
        DAWLAB_CHECK(!dawlab::hasNanOrInf(pluck));
    }

    return dawlab::test::summary("l10_ex3");
}
