// Exercise 5 solution: a soft clipper, compared with the limiter
//
// Two ways to "keep the signal from exceeding the upper limit":
//   clipper  applies a waveshaping curve directly to each sample (stateless, zero delay), and whatever exceeds the limit is "bent";
//            the price is that the waveform changes -> harmonics (distortion).
//   limiter  changes the "gain" without changing the "waveform": all samples over a stretch of time are multiplied by the same gain,
//            so a steady-state sine goes in and a sine comes out; the price is the need for lookahead (delay) and the pumping of the release.
//
// The soft clipper in this exercise (threshold t in (0, 1)):
//   |x| <= t:     y = x                                   (linear region, completely untouched)
//   |x| > t:      y = sign(x) * [ t + (1 - t) * tanh((|x| - t) / (1 - t)) ]
//   The slope of tanh at 0 is 1, so the curve is continuous at t with continuous slope (C1), with no high-order harmonics from a "corner";
//   tanh <= 1 so |y| <= t + (1 - t) = 1, and it never exceeds 0 dBFS (in float, a very large input saturates to exactly 1.0).
//
// Run: l05_ex5_solution
// Output: dawlab_output/l05_ex5_sine_clipped.wav, l05_ex5_sine_limited.wav,
//         l05_ex5_drums_clipped.wav, l05_ex5_drums_limited.wav

#include <dawlab/Common.h>
#include <dawlab/dsp/Limiter.h>

#include <cmath>
#include <cstdio>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

float softClip(float x, float threshold) noexcept {
    const float t = std::clamp(threshold, 1.0e-3f, 0.999f);
    const float a = std::abs(x);
    if (a <= t)
        return x;
    const float over = (a - t) / (1.0f - t);
    const float y = t + (1.0f - t) * std::tanh(over);
    return std::copysign(y, x);
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
    // ---- (1) Properties of the curve itself ----
    {
        const float t = 0.5f;
        DAWLAB_CHECK(softClip(0.3f, t) == 0.3f);   // the linear region is untouched
        DAWLAB_CHECK(softClip(-0.5f, t) == -0.5f); // the boundary is untouched too
        DAWLAB_CHECK(std::abs(softClip(100.0f, t)) <= 1.0f); // never exceeds 1 (in float, tanh saturates to exactly 1.0)
        DAWLAB_CHECK(std::abs(softClip(1.0e9f, t)) <= 1.0f);
        DAWLAB_CHECK_NEAR(softClip(1.0e9f, t), 1.0, 1.0e-5); // but it approaches 1
        DAWLAB_CHECK(softClip(-2.0f, t) == -softClip(2.0f, t)); // odd symmetry
        // Monotonically increasing + continuous slope at t (numerical derivative ~ 1)
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
        std::printf("softClip(x, 0.5): x=0.6 -> %.4f, x=1.0 -> %.4f, x=2.0 -> %.4f, x=10 -> %.4f\n", softClip(0.6f, t),
                    softClip(1.0f, t), softClip(2.0f, t), softClip(10.0f, t));
    }

    // ---- (2) Sine +6 dB, over 0 dBFS: the clipper generates harmonics, the limiter does not ----
    {
        const double freq = 1000.0;
        dawlab::AudioBuffer sine = dawlab::makeSine(freq, kSampleRate, 1.0, dawlab::dbToGain(6.0f), 1); // peak 2.0

        dawlab::AudioBuffer clipped = sine;
        clipBuffer(clipped, 0.5f);

        dawlab::AudioBuffer limited = sine;
        dawlab::dsp::Limiter limiter;
        limiter.prepare(kSampleRate, kBlockSize, 1);
        limiter.setCeilingDb(-0.5f); // about the same as the clipper's output peak
        limiter.setLookaheadMs(5.0f);
        limiter.setReleaseMs(100.0f);
        processInBlocks(limited, limiter);

        // Skip the first 0.2 s (the limiter's attack / settling period) and measure THD over the next 0.5 s
        const int skip = dawlab::secondsToSamples(0.2, kSampleRate);
        const int n = dawlab::secondsToSamples(0.5, kSampleRate);
        const double thdClip = dawlab::thdPercent(clipped.getReadPointer(0) + skip, n, freq, kSampleRate);
        const double thdLim = dawlab::thdPercent(limited.getReadPointer(0) + skip, n, freq, kSampleRate);
        const auto harmonics = dawlab::harmonicLevelsDb(clipped.getReadPointer(0) + skip, n, freq, kSampleRate, 5);

        std::printf("\n1 kHz sine +6 dBFS (peak 2.0):\n");
        std::printf("  soft clip (t=0.5): peak %6.2f dBFS, THD %6.2f %%, harmonics H2 %.1f / H3 %.1f / H4 %.1f / H5 %.1f dB\n",
                    peakDb(clipped), thdClip, harmonics[1], harmonics[2], harmonics[3], harmonics[4]);
        std::printf("  limiter (-0.5 dB): peak %6.2f dBFS, THD %6.4f %%\n", peakDb(limited), thdLim);
        DAWLAB_CHECK(dawlab::peakAbs(clipped) <= 1.0f);
        DAWLAB_CHECK(dawlab::peakAbs(limited) <= dawlab::dbToGain(-0.5f));
        DAWLAB_CHECK(thdClip > 5.0);          // clearly distorted
        DAWLAB_CHECK(thdLim < 0.1);           // gain is constant in steady state -> the waveform is unchanged
        DAWLAB_CHECK(harmonics[1] < -40.0);   // odd-symmetric curve: the even harmonic (H2) is almost 0
        DAWLAB_CHECK(harmonics[2] > -30.0);   // the odd harmonic (H3) is prominent
        dawlab::writeWav(dawlab::outputPath("l05_ex5_sine_clipped.wav"), clipped, kSampleRate);
        dawlab::writeWav(dawlab::outputPath("l05_ex5_sine_limited.wav"), limited, kSampleRate);
    }

    // ---- (3) Drum loop +12 dB: both can hold the peak down, but they differ in sound and RMS ----
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

        std::printf("\ndrum loop +12 dB (input peak %.2f dBFS):\n", peakDb(hot));
        std::printf("  %-20s peak %6.2f dBFS  RMS %6.2f dBFS  crest %5.2f dB\n", "dry", peakDb(dry), rmsDb(dry),
                    peakDb(dry) - rmsDb(dry));
        std::printf("  %-20s peak %6.2f dBFS  RMS %6.2f dBFS  crest %5.2f dB\n", "soft clip (t=0.6)", peakDb(clipped),
                    rmsDb(clipped), peakDb(clipped) - rmsDb(clipped));
        std::printf("  %-20s peak %6.2f dBFS  RMS %6.2f dBFS  crest %5.2f dB\n", "limiter (-0.1 dB)", peakDb(limited),
                    rmsDb(limited), peakDb(limited) - rmsDb(limited));
        DAWLAB_CHECK(dawlab::peakAbs(clipped) <= 1.0f);
        DAWLAB_CHECK(dawlab::peakAbs(limited) <= dawlab::dbToGain(-0.1f));
        DAWLAB_CHECK(rmsDb(clipped) > rmsDb(dry) + 6.0f); // both are clearly louder
        DAWLAB_CHECK(rmsDb(limited) > rmsDb(dry) + 6.0f);
        dawlab::writeWav(dawlab::outputPath("l05_ex5_drums_clipped.wav"), clipped, kSampleRate);
        dawlab::writeWav(dawlab::outputPath("l05_ex5_drums_limited.wav"), limited, kSampleRate);
        std::printf("  Listen: the clipped kick / snare gain gritty harmonics; the limited version is cleaner but its transients are dulled and it pumps slightly\n");
    }

    return dawlab::test::summary("l05_ex5");
}
