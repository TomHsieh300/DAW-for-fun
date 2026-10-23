// Exercise 6 solution: fuzz test -- random signals + random parameters + random block sizes, verifying the ceiling guarantee
//
// A "proof" tells us the algorithm cannot exceed the ceiling mathematically, but the code may still have off-by-one errors,
// floating-point rounding, circular-buffer wrap-around, block-boundary problems and so on. The spirit of a fuzz test is to
// hammer the code with a large amount of random (but reproducible) input; any single violation of the promise counts as a bug.
//
// We check three things:
//   (a) the output sample peak is <= the ceiling (linear value, exact comparison, no slack)
//   (b) the output contains no NaN / Inf
//   (c) getSafetyClipCount() == 0: the algorithm itself stayed under the ceiling, rather than being rescued by the final clamp
//
// Run: l05_ex6_solution (about 400 random cases; finishes within a few seconds in a Debug build)
// Output: no WAV (pure numeric checks)

#include <dawlab/Common.h>
#include <dawlab/dsp/Limiter.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <cstdio>
#include <vector>

namespace {

class Random {
public:
    explicit Random(uint32_t seed) : gen_(seed) {}
    float uniform(float lo, float hi) { return lo + (hi - lo) * (gen_.next() * 0.5f + 0.5f); } // [lo, hi)
    int integer(int lo, int hi) { return lo + static_cast<int>(uniform(0.0f, static_cast<float>(hi - lo + 1))); } // [lo, hi]
    float next() { return gen_.next(); }

private:
    dawlab::NoiseGenerator gen_;
};

enum class SignalKind { Noise, Sine, Bursts, Impulses, Dc, Square, Mixed, Count };

const char* kindName(SignalKind k) {
    switch (k) {
    case SignalKind::Noise: return "noise";
    case SignalKind::Sine: return "sine";
    case SignalKind::Bursts: return "bursts";
    case SignalKind::Impulses: return "impulses";
    case SignalKind::Dc: return "dc";
    case SignalKind::Square: return "square";
    case SignalKind::Mixed: return "mixed";
    default: return "?";
    }
}

// Generate a random signal: the kind, length and amplitude (up to +60 dB) are all random
dawlab::AudioBuffer makeRandomSignal(Random& rng, double sampleRate, int numChannels, SignalKind kind) {
    const int n = rng.integer(1, dawlab::secondsToSamples(0.3, sampleRate));
    const float amp = dawlab::dbToGain(rng.uniform(-40.0f, 60.0f));
    const double freq = static_cast<double>(rng.uniform(20.0f, static_cast<float>(sampleRate * 0.45)));
    dawlab::AudioBuffer b(numChannels, n);
    for (int ch = 0; ch < numChannels; ++ch) {
        float* d = b.getWritePointer(ch);
        const int burstLen = std::max(1, rng.integer(1, 2000));
        const int impulseEvery = rng.integer(1, 500);
        for (int i = 0; i < n; ++i) {
            const double phase = dawlab::kTwoPi * freq * i / sampleRate;
            float v = 0.0f;
            switch (kind) {
            case SignalKind::Noise: v = amp * rng.next(); break;
            case SignalKind::Sine: v = amp * static_cast<float>(std::sin(phase)); break;
            case SignalKind::Bursts: v = ((i / burstLen) % 2 == 0) ? amp * static_cast<float>(std::sin(phase)) : 0.0f; break;
            case SignalKind::Impulses: v = (i % impulseEvery == 0) ? ((i / impulseEvery) % 2 ? -amp : amp) : 0.0f; break;
            case SignalKind::Dc: v = amp; break;
            case SignalKind::Square: v = std::sin(phase) >= 0.0 ? amp : -amp; break;
            case SignalKind::Mixed:
                v = amp * (0.5f * static_cast<float>(std::sin(phase)) + 0.5f * rng.next()) +
                    ((i % impulseEvery == 0) ? 10.0f * amp : 0.0f);
                break;
            default: break;
            }
            d[i] = v;
        }
    }
    return b;
}

// Feed the limiter blocks of random length (1..maxBlock)
void processRandomBlocks(dawlab::AudioBuffer& buffer, dawlab::dsp::Limiter& limiter, Random& rng, int maxBlock) {
    dawlab::AudioBlock whole(buffer);
    int start = 0;
    while (start < buffer.getNumSamples()) {
        const int len = std::min(rng.integer(1, maxBlock), buffer.getNumSamples() - start);
        limiter.process(whole.getSubBlock(start, len));
        start += len;
    }
}

struct Stats {
    int cases = 0;
    int failures = 0;
    float worstMarginDb = 1.0e9f; // the minimum of (ceiling - output peak), in dB; the closer to 0, the more the limiter really reached the ceiling
};

void report(Stats& s, const char* label, bool pass, float ceilingDb, float outPeakDb) {
    ++s.cases;
    s.worstMarginDb = std::min(s.worstMarginDb, ceilingDb - outPeakDb);
    if (!pass) {
        ++s.failures;
        std::printf("  [FAIL] %s: ceiling %.3f dB, output peak %.3f dB\n", label, ceilingDb, outPeakDb);
    }
}

} // namespace

int main() {
    Random rng(20240521);
    Stats stats;

    // ---- (1) Main fuzz: each case's parameters are fixed before processing ----
    const int kCases = 400;
    for (int c = 0; c < kCases; ++c) {
        const double sampleRate = (c % 3 == 0) ? 44100.0 : (c % 3 == 1) ? 48000.0 : 96000.0;
        const int numChannels = rng.integer(1, 2);
        const float maxLookaheadMs = rng.uniform(0.0f, 30.0f);
        const float lookaheadMs = rng.uniform(-5.0f, maxLookaheadMs + 5.0f); // deliberately out of range, to exercise the clamp
        const float ceilingDb = rng.uniform(-30.0f, 0.0f);
        const float releaseMs = rng.uniform(0.0f, 1000.0f);
        const float inputGainDb = rng.uniform(-10.0f, 40.0f);
        const int maxBlock = rng.integer(1, 1024);
        const auto kind = static_cast<SignalKind>(rng.integer(0, static_cast<int>(SignalKind::Count) - 1));

        dawlab::dsp::Limiter limiter;
        limiter.setMaxLookaheadMs(maxLookaheadMs);
        limiter.prepare(sampleRate, 1024, numChannels);
        limiter.setCeilingDb(ceilingDb);
        limiter.setLookaheadMs(lookaheadMs);
        limiter.setReleaseMs(releaseMs);
        limiter.setInputGainDb(inputGainDb);
        if (c % 2 == 0)
            limiter.reset(); // in half of the cases the input gain is placed immediately, in the other half it ramps

        dawlab::AudioBuffer signal = makeRandomSignal(rng, sampleRate, numChannels, kind);
        processRandomBlocks(signal, limiter, rng, maxBlock);

        const float ceiling = dawlab::dbToGain(ceilingDb);
        const float outPeak = dawlab::peakAbs(signal);
        const bool pass = outPeak <= ceiling && !dawlab::hasNanOrInf(signal) && limiter.getSafetyClipCount() == 0;
        char label[160];
        std::snprintf(label, sizeof(label), "case %3d %-8s sr=%.0f ch=%d la=%.2f/%.2f ms rel=%.0f gain=%+.1f blk=%d", c,
                      kindName(kind), sampleRate, numChannels, lookaheadMs, maxLookaheadMs, releaseMs, inputGainDb, maxBlock);
        report(stats, label, pass, ceilingDb, dawlab::gainToDb(outPeak));

        // The lookahead clamp and the reported latency must agree
        const int expected = std::clamp(dawlab::msToSamples(lookaheadMs, sampleRate), 0,
                                        dawlab::msToSamples(maxLookaheadMs, sampleRate));
        DAWLAB_CHECK(limiter.getLatencySamples() == expected);
    }
    std::printf("(1) Fixed parameters: %d cases, %d failures; minimum margin (ceiling - output peak) = %.4f dB\n", stats.cases, stats.failures,
                stats.worstMarginDb);
    DAWLAB_CHECK(stats.failures == 0);
    DAWLAB_CHECK(stats.worstMarginDb < 0.01f); // at least one case actually reached the ceiling (otherwise the test is not testing anything)

    // ---- (2) Changing release / input gain during playback: the guarantee still holds (these two parameters do not affect the derivation) ----
    {
        Stats s2;
        for (int c = 0; c < 50; ++c) {
            const double sampleRate = 48000.0;
            const float ceilingDb = rng.uniform(-20.0f, 0.0f);
            dawlab::dsp::Limiter limiter;
            limiter.prepare(sampleRate, 512, 2);
            limiter.setCeilingDb(ceilingDb);
            limiter.setLookaheadMs(rng.uniform(0.0f, 20.0f));

            dawlab::AudioBuffer signal = makeRandomSignal(rng, sampleRate, 2, SignalKind::Mixed);
            dawlab::AudioBlock whole(signal);
            int start = 0;
            while (start < signal.getNumSamples()) {
                limiter.setReleaseMs(rng.uniform(0.0f, 500.0f));
                limiter.setInputGainDb(rng.uniform(-20.0f, 30.0f));
                const int len = std::min(rng.integer(1, 512), signal.getNumSamples() - start);
                limiter.process(whole.getSubBlock(start, len));
                start += len;
            }
            const bool pass = dawlab::peakAbs(signal) <= dawlab::dbToGain(ceilingDb) && !dawlab::hasNanOrInf(signal) &&
                              limiter.getSafetyClipCount() == 0;
            report(s2, "release/gain varying per block", pass, ceilingDb, dawlab::gainToDb(dawlab::peakAbs(signal)));
        }
        std::printf("(2) Changing release / input gain during playback: %d cases, %d failures\n", s2.cases, s2.failures);
        DAWLAB_CHECK(s2.failures == 0);
    }

    // ---- (3) Lowering the ceiling during playback: samples in the pipeline whose gain has already been computed will exceed the new ceiling,
    //          so the safety clamp takes over (clip count > 0) and the output is still <= the current ceiling ----
    {
        const double sampleRate = 48000.0;
        dawlab::dsp::Limiter limiter;
        limiter.prepare(sampleRate, 512, 2);
        limiter.setCeilingDb(-1.0f);
        limiter.setLookaheadMs(10.0f);
        dawlab::AudioBuffer signal = dawlab::makeWhiteNoise(sampleRate, 0.5, 4.0f);
        dawlab::AudioBlock whole(signal);
        const int half = signal.getNumSamples() / 2;
        limiter.process(whole.getSubBlock(0, half));
        limiter.setCeilingDb(-12.0f);
        limiter.process(whole.getSubBlock(half, signal.getNumSamples() - half));
        const float secondHalfPeak = dawlab::peakAbs(signal.getReadPointer(0) + half, signal.getNumSamples() - half);
        std::printf("(3) Switching ceiling -1 -> -12 dB midway: second-half peak %.2f dBFS, safety clamp fired %llu times (expected > 0)\n",
                    dawlab::gainToDb(secondHalfPeak), static_cast<unsigned long long>(limiter.getSafetyClipCount()));
        DAWLAB_CHECK(secondHalfPeak <= dawlab::dbToGain(-12.0f));
        DAWLAB_CHECK(limiter.getSafetyClipCount() > 0);
        DAWLAB_CHECK(!dawlab::hasNanOrInf(signal));
    }

    // ---- (4) Hostile input: NaN / Inf mixed in; the output must still be finite and <= the ceiling ----
    {
        const double sampleRate = 48000.0;
        dawlab::dsp::Limiter limiter;
        limiter.prepare(sampleRate, 512, 2);
        limiter.setCeilingDb(-3.0f);
        limiter.setLookaheadMs(5.0f);
        dawlab::AudioBuffer signal = dawlab::makeWhiteNoise(sampleRate, 0.2, 0.5f);
        signal.setSample(0, 1000, std::numeric_limits<float>::quiet_NaN());
        signal.setSample(1, 2000, std::numeric_limits<float>::infinity());
        signal.setSample(0, 3000, -std::numeric_limits<float>::infinity());
        processRandomBlocks(signal, limiter, rng, 512);
        std::printf("(4) NaN / Inf injection: output peak %.2f dBFS, contains NaN/Inf? %s\n", dawlab::gainToDb(dawlab::peakAbs(signal)),
                    dawlab::hasNanOrInf(signal) ? "yes" : "no");
        DAWLAB_CHECK(!dawlab::hasNanOrInf(signal));
        DAWLAB_CHECK(dawlab::peakAbs(signal) <= dawlab::dbToGain(-3.0f));
    }

    // ---- (5) The host re-sets the same input gain every block (the Chapter 13 plugin does exactly this):
    //          the ramp must still finish in about 20 ms and must not get slower and slower because of the "re-setting" ----
    {
        const double sampleRate = 48000.0;
        dawlab::dsp::Limiter limiter;
        limiter.prepare(sampleRate, 64, 1);
        limiter.setCeilingDb(0.0f);
        limiter.setLookaheadMs(0.0f);
        dawlab::AudioBuffer signal(1, dawlab::secondsToSamples(0.05, sampleRate));
        for (int i = 0; i < signal.getNumSamples(); ++i)
            signal.setSample(0, i, 0.1f); // DC 0.1; after +6 dB that is about 0.2, far below the ceiling
        dawlab::AudioBlock whole(signal);
        for (int start = 0; start < signal.getNumSamples(); start += 64) {
            limiter.setInputGainDb(6.0f); // set once per block
            limiter.process(whole.getSubBlock(start, std::min(64, signal.getNumSamples() - start)));
        }
        const int after = dawlab::secondsToSamples(0.025, sampleRate); // 20 ms ramp + margin
        const float expected = 0.1f * dawlab::dbToGain(6.0f);
        std::printf("(5) Re-setting input gain +6 dB every block: output after 25 ms is %.6f (target %.6f)\n", signal.getSample(0, after),
                    expected);
        DAWLAB_CHECK_NEAR(signal.getSample(0, after), expected, 1.0e-6);
    }

    return dawlab::test::summary("l05_ex6");
}
