// 練習 4 起始碼：Ring modulator —— 乘上載波，只剩和頻與差頻
//
//   y(t) = x(t) · sin(2π fc t)
//   sin(a)·sin(b) = ½[cos(a−b) − cos(a+b)] → 輸出只有 f − fc 與 f + fc，原本的 f 與載波 fc 都消失。
//
// 目標：完成 RingMod class（用 Lfo 當載波），並通過 self-check：
//   1. 1000 Hz 正弦（A = 0.5）× 150 Hz：850 / 1150 Hz 振幅 ≈ 0.25，1000 / 150 Hz 低於 −60 dB
//   2. 載波 0 Hz、相位 0.25（sin = 1）→ 輸出 = 輸入
//   3. mix 0.5：1000 Hz 剩 0.25、850 Hz 為 0.125
// 這支程式可以編譯；執行時因為 TODO 還沒完成，self-check 會 FAIL（預期）。

#include <dawlab/Common.h>
#include <dawlab/dsp/Lfo.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

using dawlab::dsp::Lfo;
using dawlab::dsp::LfoShape;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

class RingMod {
public:
    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        (void)maxBlockSize;
        numChannels_ = std::max(1, numChannels);
        carrier_.prepare(sampleRate);
        carrier_.setShape(LfoShape::Sine); // 載波 = 跑在可聽頻率範圍的 sine LFO
        mixSm_.prepare(sampleRate, 20.0);
        reset();
    }

    void reset() noexcept {
        carrier_.reset();
        mixSm_.setCurrentAndTarget(mixSm_.getTarget());
        carrierValue_ = 0.0f;
    }

    void setCarrierHz(float hz) noexcept { carrier_.setRateHz(std::clamp(hz, 0.0f, 20000.0f)); }
    void setCarrierPhase(float phase01) noexcept { carrier_.setPhase(phase01); }
    void setMix(float mix) noexcept { mixSm_.setTarget(std::clamp(mix, 0.0f, 1.0f)); }

    float processSample(int channel, float x) noexcept {
        if (channel == 0)
            advance();
        // TODO 2：回傳 (1 − mix)·x + mix·x·carrierValue_
        return x;
    }

    void process(dawlab::AudioBlock block) noexcept {
        const int n = block.getNumSamples();
        const int nch = std::min(block.getNumChannels(), numChannels_);
        for (int i = 0; i < n; ++i) {
            advance();
            for (int ch = 0; ch < nch; ++ch) {
                float* d = block.getChannelPointer(ch);
                // TODO 2：同上，就地改寫 d[i]
                (void)d;
            }
        }
    }

private:
    void advance() noexcept {
        mix_ = mixSm_.next();
        // TODO 1：從 carrier_ 取下一個值存進 carrierValue_
    }

    int numChannels_ = 2;
    Lfo carrier_;
    float carrierValue_ = 0.0f;
    dawlab::dsp::detail::ModParamSmoother mixSm_{1.0f};
    float mix_ = 1.0f;
};

template <typename Effect>
void processInBlocks(Effect& fx, dawlab::AudioBuffer& buffer) {
    dawlab::AudioBlock whole(buffer);
    for (int start = 0; start < buffer.getNumSamples(); start += kBlockSize) {
        const int n = std::min(kBlockSize, buffer.getNumSamples() - start);
        fx.process(whole.getSubBlock(start, n));
    }
}

double amp(const dawlab::AudioBuffer& b, double freq, int skip) {
    return dawlab::goertzelAmplitude(b.getReadPointer(0) + skip, b.getNumSamples() - skip, freq, kSampleRate);
}

} // namespace

int main() {
    std::printf("=== 第 10 章 練習 4：Ring modulator（starter）===\n\n");
    const int skip = dawlab::secondsToSamples(0.5, kSampleRate);

    {
        const double f = 1000.0, fc = 150.0;
        const float A = 0.5f;
        dawlab::AudioBuffer tone = dawlab::makeSine(f, kSampleRate, 3.0, A, 1);
        RingMod rm;
        rm.prepare(kSampleRate, kBlockSize, 1);
        rm.setCarrierHz(static_cast<float>(fc));
        rm.setMix(1.0f);
        rm.reset();
        processInBlocks(rm, tone);
        const double sideband = amp(tone, f - fc, skip), sideband2 = amp(tone, f + fc, skip);
        const double carrierLeft = amp(tone, f, skip), lfoLeft = amp(tone, fc, skip);
        std::printf("%.0f × %.0f Hz：%.0f Hz %.4f、%.0f Hz %.4f（理論 %.4f）；%.0f Hz %.1f dB、%.0f Hz %.1f dB（應 < −60）\n\n",
                    f, fc, f - fc, sideband, f + fc, sideband2, A / 2.0, f, dawlab::gainToDb(carrierLeft / A), fc,
                    dawlab::gainToDb(lfoLeft / A));
        DAWLAB_CHECK_NEAR(sideband, A / 2.0, 0.005);
        DAWLAB_CHECK_NEAR(sideband2, A / 2.0, 0.005);
        DAWLAB_CHECK(dawlab::gainToDb(carrierLeft / A) < -60.0);
        DAWLAB_CHECK(dawlab::gainToDb(lfoLeft / A) < -60.0);
    }

    {
        dawlab::AudioBuffer tone = dawlab::makeSine(1000.0, kSampleRate, 1.0, 0.5f, 1);
        const dawlab::AudioBuffer ref = tone;
        RingMod rm;
        rm.prepare(kSampleRate, kBlockSize, 1);
        rm.setCarrierHz(0.0f);
        rm.setCarrierPhase(0.25f);
        rm.reset();
        processInBlocks(rm, tone);
        float maxDiff = 0.0f;
        for (int i = 0; i < tone.getNumSamples(); ++i)
            maxDiff = std::max(maxDiff, std::abs(tone.getSample(0, i) - ref.getSample(0, i)));
        std::printf("載波 0 Hz、相位 0.25：輸出與輸入最大差 %.2e（應為 0）\n\n", maxDiff);
        DAWLAB_CHECK(maxDiff < 1.0e-5f);
    }

    {
        dawlab::AudioBuffer tone = dawlab::makeSine(1000.0, kSampleRate, 3.0, 0.5f, 1);
        RingMod rm;
        rm.prepare(kSampleRate, kBlockSize, 1);
        rm.setCarrierHz(150.0f);
        rm.setMix(0.5f);
        rm.reset();
        processInBlocks(rm, tone);
        const double dry = amp(tone, 1000.0, skip), sb = amp(tone, 850.0, skip);
        std::printf("mix 0.5：1000 Hz 振幅 %.4f（理論 0.25）、850 Hz 振幅 %.4f（理論 0.125）\n\n", dry, sb);
        DAWLAB_CHECK_NEAR(dry, 0.25, 0.005);
        DAWLAB_CHECK_NEAR(sb, 0.125, 0.005);
    }

    {
        dawlab::AudioBuffer pluck = dawlab::makePluckSequence(kSampleRate, 4.0, 0.4f, 2);
        RingMod rm;
        rm.prepare(kSampleRate, kBlockSize, 2);
        rm.setCarrierHz(500.0f);
        rm.reset();
        processInBlocks(rm, pluck);
        dawlab::writeWav(dawlab::outputPath("l10_ex4_ringmod_500hz.wav"), pluck, kSampleRate);
        DAWLAB_CHECK(!dawlab::hasNanOrInf(pluck));
    }

    return dawlab::test::summary("l10_ex4");
}
