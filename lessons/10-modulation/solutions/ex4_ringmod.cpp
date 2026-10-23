// 練習 4 解答：Ring modulator —— 乘上載波，只剩和頻與差頻
//
//   y(t) = x(t) · sin(2π fc t)
//   若 x = A·sin(2π f t)：sin(a)·sin(b) = ½[cos(a−b) − cos(a+b)]
//   → 輸出只有 f − fc 與 f + fc 兩個頻率（振幅各 A/2），原本的 f 與載波 fc 都不見了。
//
// 和 tremolo（AM）的差別：AM 的 gain 有 DC 偏移（永遠 ≥ 0），所以載波保留；
// ring mod 的「gain」在 −1..+1 之間穿過 0，載波就被抵消了。
// 載波頻率低（< 20 Hz）時聽起來像很深的 tremolo（每個週期有兩次「無聲」）；
// 載波進入可聽頻率範圍後，和差頻和原音不成整數倍 → 金屬 / 鐘聲的「不和諧」音色（Dalek 聲）。
//
// self-check：
//   1. 1000 Hz 正弦（A = 0.5）× 150 Hz 載波：850 與 1150 Hz 振幅 ≈ 0.25；1000 與 150 Hz 低於 −60 dB
//   2. 載波 0 Hz（相位固定在 sin = 1）→ 輸出 = 輸入（ring mod 退化成 gain 1）
//   3. mix 參數：mix 0.5 時 1000 Hz 保留一半（0.25）、sideband 各 0.125
// 輸出：dawlab_output/l10_ex4_ringmod_30hz.wav（撥弦琶音 × 30 Hz）、l10_ex4_ringmod_500hz.wav（× 500 Hz，金屬聲）

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
        carrier_.prepare(sampleRate); // 載波就是一個「跑在可聽頻率範圍」的 sine LFO
        carrier_.setShape(LfoShape::Sine);
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
        return (1.0f - mix_) * x + mix_ * x * carrierValue_; // y = x · carrier（再和乾訊號混合）
    }

    void process(dawlab::AudioBlock block) noexcept {
        const int n = block.getNumSamples();
        const int nch = std::min(block.getNumChannels(), numChannels_);
        for (int i = 0; i < n; ++i) {
            advance();
            for (int ch = 0; ch < nch; ++ch) {
                float* d = block.getChannelPointer(ch);
                d[i] = (1.0f - mix_) * d[i] + mix_ * d[i] * carrierValue_;
            }
        }
    }

private:
    void advance() noexcept {
        mix_ = mixSm_.next();
        carrierValue_ = carrier_.getNextValue();
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
    std::printf("=== 第 10 章 練習 4：Ring modulator ===\n\n");
    const int skip = dawlab::secondsToSamples(0.5, kSampleRate);

    // ---- 1. 和差頻 ----------------------------------------------------------
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
        std::printf("%.0f Hz（A = %.2f）× %.0f Hz 載波：\n", f, A, fc);
        std::printf("   freq(Hz)   振幅     理論\n");
        const double sideband = amp(tone, f - fc, skip), sideband2 = amp(tone, f + fc, skip);
        const double carrierLeft = amp(tone, f, skip), lfoLeft = amp(tone, fc, skip);
        std::printf("   %8.0f   %.4f   %.4f（差頻 A/2）\n", f - fc, sideband, A / 2.0);
        std::printf("   %8.0f   %.4f   %.4f（和頻 A/2）\n", f + fc, sideband2, A / 2.0);
        std::printf("   %8.0f   %.4f   0（原音消失，%.1f dB）\n", f, carrierLeft, dawlab::gainToDb(carrierLeft / A));
        std::printf("   %8.0f   %.4f   0（載波不會出現，%.1f dB）\n\n", fc, lfoLeft, dawlab::gainToDb(lfoLeft / A));
        DAWLAB_CHECK_NEAR(sideband, A / 2.0, 0.005);
        DAWLAB_CHECK_NEAR(sideband2, A / 2.0, 0.005);
        DAWLAB_CHECK(dawlab::gainToDb(carrierLeft / A) < -60.0);
        DAWLAB_CHECK(dawlab::gainToDb(lfoLeft / A) < -60.0);
    }

    // ---- 2. 載波 0 Hz、相位 0.25（sin = 1）→ 輸出 = 輸入 ---------------------------
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

    // ---- 3. mix 0.5 --------------------------------------------------------
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

    // ---- 聽 -----------------------------------------------------------------
    {
        for (float fc : {30.0f, 500.0f}) {
            dawlab::AudioBuffer pluck = dawlab::makePluckSequence(kSampleRate, 4.0, 0.4f, 2);
            RingMod rm;
            rm.prepare(kSampleRate, kBlockSize, 2);
            rm.setCarrierHz(fc);
            rm.setMix(1.0f);
            rm.reset();
            processInBlocks(rm, pluck);
            const std::string name = fc < 100.0f ? "l10_ex4_ringmod_30hz.wav" : "l10_ex4_ringmod_500hz.wav";
            dawlab::writeWav(dawlab::outputPath(name), pluck, kSampleRate);
            std::printf("輸出 %s（載波 %.0f Hz）：peak %.3f\n", name.c_str(), fc, dawlab::peakAbs(pluck));
            DAWLAB_CHECK(!dawlab::hasNanOrInf(pluck));
        }
        std::printf("  -> 30 Hz 像極深的 tremolo（每秒 60 次無聲）；500 Hz 的和差頻與原音不成整數倍 → 金屬鐘聲。\n");
    }

    return dawlab::test::summary("l10_ex4");
}
