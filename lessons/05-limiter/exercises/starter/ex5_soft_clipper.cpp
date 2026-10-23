// 練習 5 起始碼：soft clipper，並與 limiter 比較
//
// 目標：完成 softClip()：
//   |x| ≤ t：     y = x
//   |x| > t：     y = sign(x) · [ t + (1 - t) · tanh((|x| - t) / (1 - t)) ]
// 要求：線性區完全不動、在 t 處連續且斜率連續、|y| ≤ 1、奇對稱、單調遞增。
//
// 執行：l05_ex5_starter → 完成前會印出 FAIL（預期）。

#include <dawlab/Common.h>
#include <dawlab/dsp/Limiter.h>

#include <cmath>
#include <cstdio>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

float softClip(float x, float threshold) noexcept {
    const float t = std::clamp(threshold, 1.0e-3f, 0.999f);
    // TODO：依上面的公式實作（提示：std::tanh、std::copysign、std::abs）
    (void)t;
    return x;
}

void clipBuffer(dawlab::AudioBuffer& b, float threshold) {
    for (int ch = 0; ch < b.getNumChannels(); ++ch) {
        float* d = b.getWritePointer(ch);
        for (int i = 0; i < b.getNumSamples(); ++i)
            d[i] = softClip(d[i], threshold);
    }
}

template <typename Processor>
void processInBlocks(dawlab::AudioBuffer& buffer, Processor& processor) {
    dawlab::AudioBlock whole(buffer);
    for (int start = 0; start < buffer.getNumSamples(); start += kBlockSize)
        processor.process(whole.getSubBlock(start, std::min(kBlockSize, buffer.getNumSamples() - start)));
}

float peakDb(const dawlab::AudioBuffer& b) { return dawlab::gainToDb(dawlab::peakAbs(b)); }
float rmsDb(const dawlab::AudioBuffer& b) { return dawlab::gainToDb(dawlab::rms(b, 0)); }

} // namespace

int main() {
    // ---- (1) 曲線本身的性質 ----
    {
        const float t = 0.5f;
        DAWLAB_CHECK(softClip(0.3f, t) == 0.3f);
        DAWLAB_CHECK(softClip(-0.5f, t) == -0.5f);
        DAWLAB_CHECK(std::abs(softClip(100.0f, t)) <= 1.0f);
        DAWLAB_CHECK(std::abs(softClip(1.0e9f, t)) <= 1.0f);
        DAWLAB_CHECK_NEAR(softClip(1.0e9f, t), 1.0, 1.0e-5);
        DAWLAB_CHECK(softClip(-2.0f, t) == -softClip(2.0f, t));
        bool monotonic = true;
        float prev = softClip(-3.0f, t);
        for (float x = -3.0f + 0.001f; x <= 3.0f; x += 0.001f) {
            const float y = softClip(x, t);
            monotonic = monotonic && (y >= prev);
            prev = y;
        }
        DAWLAB_CHECK(monotonic);
        const float h = 1.0e-3f;
        const float slopeBelow = (softClip(t, t) - softClip(t - h, t)) / h;
        const float slopeAbove = (softClip(t + h, t) - softClip(t, t)) / h;
        DAWLAB_CHECK_NEAR(slopeBelow, 1.0, 1.0e-3);
        DAWLAB_CHECK_NEAR(slopeAbove, 1.0, 1.0e-2);
        std::printf("softClip(x, 0.5)：x=0.6 → %.4f，x=1.0 → %.4f，x=2.0 → %.4f，x=10 → %.4f\n", softClip(0.6f, t),
                    softClip(1.0f, t), softClip(2.0f, t), softClip(10.0f, t));
    }

    // ---- (2) 正弦波 +6 dB：clipper 產生諧波、limiter 不會 ----
    {
        const double freq = 1000.0;
        dawlab::AudioBuffer sine = dawlab::makeSine(freq, kSampleRate, 1.0, dawlab::dbToGain(6.0f), 1);

        dawlab::AudioBuffer clipped = sine;
        clipBuffer(clipped, 0.5f);

        dawlab::AudioBuffer limited = sine;
        dawlab::dsp::Limiter limiter;
        limiter.prepare(kSampleRate, kBlockSize, 1);
        limiter.setCeilingDb(-0.5f);
        limiter.setLookaheadMs(5.0f);
        limiter.setReleaseMs(100.0f);
        processInBlocks(limited, limiter);

        const int skip = dawlab::secondsToSamples(0.2, kSampleRate);
        const int n = dawlab::secondsToSamples(0.5, kSampleRate);
        const double thdClip = dawlab::thdPercent(clipped.getReadPointer(0) + skip, n, freq, kSampleRate);
        const double thdLim = dawlab::thdPercent(limited.getReadPointer(0) + skip, n, freq, kSampleRate);
        const auto harmonics = dawlab::harmonicLevelsDb(clipped.getReadPointer(0) + skip, n, freq, kSampleRate, 5);

        std::printf("\n1 kHz sine +6 dBFS：\n");
        std::printf("  soft clip (t=0.5)：peak %6.2f dBFS，THD %6.2f %%，諧波 H2 %.1f / H3 %.1f / H4 %.1f / H5 %.1f dB\n",
                    peakDb(clipped), thdClip, harmonics[1], harmonics[2], harmonics[3], harmonics[4]);
        std::printf("  limiter (-0.5 dB)：peak %6.2f dBFS，THD %6.4f %%\n", peakDb(limited), thdLim);
        DAWLAB_CHECK(dawlab::peakAbs(clipped) <= 1.0f);
        DAWLAB_CHECK(dawlab::peakAbs(limited) <= dawlab::dbToGain(-0.5f));
        DAWLAB_CHECK(thdClip > 5.0);
        DAWLAB_CHECK(thdLim < 0.1);
        DAWLAB_CHECK(harmonics[1] < -40.0);
        DAWLAB_CHECK(harmonics[2] > -30.0);
        dawlab::writeWav(dawlab::outputPath("l05_ex5_sine_clipped.wav"), clipped, kSampleRate);
        dawlab::writeWav(dawlab::outputPath("l05_ex5_sine_limited.wav"), limited, kSampleRate);
    }

    // ---- (3) drum loop +12 dB ----
    {
        dawlab::AudioBuffer dry = dawlab::makeDrumLoop(kSampleRate, 2);
        dawlab::AudioBuffer hot = dry;
        hot.applyGain(dawlab::dbToGain(12.0f));

        dawlab::AudioBuffer clipped = hot;
        clipBuffer(clipped, 0.6f);

        dawlab::AudioBuffer limited = dry;
        dawlab::dsp::Limiter limiter;
        limiter.prepare(kSampleRate, kBlockSize, 2);
        limiter.setCeilingDb(-0.1f);
        limiter.setInputGainDb(12.0f);
        limiter.setLookaheadMs(5.0f);
        limiter.setReleaseMs(80.0f);
        limiter.reset();
        processInBlocks(limited, limiter);

        std::printf("\ndrum loop +12 dB（輸入 peak %.2f dBFS）：\n", peakDb(hot));
        std::printf("  %-20s peak %6.2f dBFS  RMS %6.2f dBFS\n", "dry", peakDb(dry), rmsDb(dry));
        std::printf("  %-20s peak %6.2f dBFS  RMS %6.2f dBFS\n", "soft clip (t=0.6)", peakDb(clipped), rmsDb(clipped));
        std::printf("  %-20s peak %6.2f dBFS  RMS %6.2f dBFS\n", "limiter (-0.1 dB)", peakDb(limited), rmsDb(limited));
        DAWLAB_CHECK(dawlab::peakAbs(clipped) <= 1.0f);
        DAWLAB_CHECK(dawlab::peakAbs(limited) <= dawlab::dbToGain(-0.1f));
        DAWLAB_CHECK(rmsDb(clipped) > rmsDb(dry) + 6.0f);
        DAWLAB_CHECK(rmsDb(limited) > rmsDb(dry) + 6.0f);
        dawlab::writeWav(dawlab::outputPath("l05_ex5_drums_clipped.wav"), clipped, kSampleRate);
        dawlab::writeWav(dawlab::outputPath("l05_ex5_drums_limited.wav"), limited, kSampleRate);
    }

    return dawlab::test::summary("l05_ex5");
}
