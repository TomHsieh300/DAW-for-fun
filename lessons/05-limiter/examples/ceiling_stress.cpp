// Example 4: stress test of the ceiling guarantee -- extreme signals and random block sizes
//
// What it shows:
//   The limiter's core promise is "for any input, the output sample peak is <= the ceiling".
//   This program hammers it with all kinds of hostile signals (+40 dB noise, a 50 Hz square wave
//   of +/-10 (+20 dBFS), DC, +/-1000 impulses, a sample-by-sample alternating +/-10^4, ...)
//   combined with random block lengths and random parameters, then checks the output peak and the
//   safety clamp trigger count in every case.
//   It is also a scaled-down version of Exercise 6 (the fuzz test).
//
// How to run: l05_ceiling_stress (no arguments)
// Output files: none (pure numeric checks)
// What you should see: every row's "out peak" (linear value) is <= the ceiling and "clamp#" is 0; finally it
//         verifies that the output is sample-for-sample identical for block 512 / block 1 / random blocks,
//         and prints "All passed".
//
// Why look at clamp#: the Limiter ends with a safety clamp, so even if the algorithm is wrong (window length,
// delay off by one sample, ...) the output is forced back to the ceiling, and "out peak <= ceiling" on its own
// would always pass. Only clamp# == 0 proves the algorithm itself stayed under the ceiling, which is why this
// program can serve as a regression test.

#include <dawlab/Common.h>
#include <dawlab/dsp/Limiter.h>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;

struct Case {
    std::string name;
    dawlab::AudioBuffer signal;
};

dawlab::AudioBuffer fill(int numSamples, const std::function<float(int, int)>& f, int numChannels = 2) {
    dawlab::AudioBuffer b(numChannels, numSamples);
    for (int ch = 0; ch < numChannels; ++ch)
        for (int i = 0; i < numSamples; ++i)
            b.setSample(ch, i, f(ch, i));
    return b;
}

// Feed the limiter blocks of random length (1..maxBlock), simulating a host whose block size is not fixed
void processRandomBlocks(dawlab::AudioBuffer& buffer, dawlab::dsp::Limiter& limiter, dawlab::NoiseGenerator& rng,
                         int maxBlock) {
    dawlab::AudioBlock whole(buffer);
    int start = 0;
    while (start < buffer.getNumSamples()) {
        const int len = std::min(1 + static_cast<int>((rng.next() * 0.5f + 0.5f) * static_cast<float>(maxBlock - 1)),
                                 buffer.getNumSamples() - start);
        limiter.process(whole.getSubBlock(start, len));
        start += len;
    }
}

} // namespace

int main() {
    const int n = dawlab::secondsToSamples(1.0, kSampleRate);
    dawlab::NoiseGenerator rng(2024);

    std::vector<Case> cases;
    cases.push_back({"white noise +40 dB", dawlab::makeWhiteNoise(kSampleRate, 1.0, 100.0f)});
    cases.push_back({"50 Hz square x10", fill(n, [&](int, int i) {
                         return std::sin(dawlab::kTwoPi * 50.0 * i / kSampleRate) >= 0.0 ? 10.0f : -10.0f;
                     })});
    cases.push_back({"DC +5", fill(n, [](int, int) { return 5.0f; })});
    cases.push_back({"impulse train +/-1000", fill(n, [](int, int i) {
                         return (i % 1000 == 0) ? ((i / 1000) % 2 ? -1000.0f : 1000.0f) : 0.0f;
                     })});
    cases.push_back({"alternating +/-1e4", fill(n, [](int, int i) { return (i & 1) ? -1.0e4f : 1.0e4f; })});
    cases.push_back({"sine 1 kHz +60 dB", dawlab::makeSine(1000.0, kSampleRate, 1.0, 1000.0f)});
    cases.push_back({"drums +30 dB", [&] {
                         auto d = dawlab::makeDrumLoop(kSampleRate, 1);
                         d.applyGain(dawlab::dbToGain(30.0f));
                         return d;
                     }()});
    cases.push_back({"L loud / R silent", fill(n, [&](int ch, int) { return ch == 0 ? 3.0f * rng.next() : 0.0f; })});
    cases.push_back({"silence", fill(n, [](int, int) { return 0.0f; })});

    std::printf("%-22s %10s %10s %10s %10s %8s %10s %6s\n", "case", "ceiling", "lookahead", "release", "in peak",
                "out peak", "clamp#", "ok?");
    bool ok = true;
    int caseIndex = 0;
    for (auto& c : cases) {
        // The parameters are random too: ceiling -20..0 dB, lookahead 0..20 ms, release 1..500 ms
        const float ceilingDb = -20.0f * (rng.next() * 0.5f + 0.5f);
        const float lookaheadMs = 20.0f * (rng.next() * 0.5f + 0.5f);
        const float releaseMs = 1.0f + 499.0f * (rng.next() * 0.5f + 0.5f);

        dawlab::dsp::Limiter limiter;
        limiter.prepare(kSampleRate, 2048, c.signal.getNumChannels());
        limiter.setCeilingDb(ceilingDb);
        limiter.setLookaheadMs(lookaheadMs);
        limiter.setReleaseMs(releaseMs);
        limiter.setInputGainDb(caseIndex % 2 == 0 ? 0.0f : 12.0f);

        dawlab::AudioBuffer out = c.signal;
        processRandomBlocks(out, limiter, rng, 2048);

        const float ceiling = dawlab::dbToGain(ceilingDb);
        const float outPeak = dawlab::peakAbs(out);
        // the safety clamp pulls any wrong output back to the ceiling, so we must also check that it never fired
        const bool pass = outPeak <= ceiling && !dawlab::hasNanOrInf(out) && limiter.getSafetyClipCount() == 0;
        ok = ok && pass;
        std::printf("%-22s %7.2f dB %7.2f ms %7.0f ms %10.3g %10.6f %10llu %6s\n", c.name.c_str(), ceilingDb,
                    lookaheadMs, releaseMs, dawlab::peakAbs(c.signal), outPeak,
                    static_cast<unsigned long long>(limiter.getSafetyClipCount()), pass ? "yes" : "NO");
        ++caseIndex;
    }

    // ---- Block-size independence: the same signal processed with block 512, block 1 and random blocks must give sample-for-sample identical output ----
    {
        auto run = [&](int mode) {
            dawlab::AudioBuffer d = dawlab::makeDrumLoop(kSampleRate, 1);
            dawlab::dsp::Limiter limiter;
            limiter.prepare(kSampleRate, 2048, d.getNumChannels());
            limiter.setCeilingDb(-1.0f);
            limiter.setLookaheadMs(5.0f);
            limiter.setReleaseMs(80.0f);
            limiter.setInputGainDb(18.0f); // no reset: let the input gain ramp cross block boundaries as well
            dawlab::AudioBlock whole(d);
            if (mode == 2) {
                dawlab::NoiseGenerator blockRng(7);
                processRandomBlocks(d, limiter, blockRng, 2048);
            } else {
                const int bs = (mode == 0) ? 512 : 1;
                for (int start = 0; start < d.getNumSamples(); start += bs)
                    limiter.process(whole.getSubBlock(start, std::min(bs, d.getNumSamples() - start)));
            }
            ok = ok && limiter.getSafetyClipCount() == 0;
            return d;
        };
        const dawlab::AudioBuffer a = run(0), b = run(1), c = run(2);
        float maxDiff = 0.0f;
        for (int ch = 0; ch < a.getNumChannels(); ++ch)
            for (int i = 0; i < a.getNumSamples(); ++i)
                maxDiff = std::max({maxDiff, std::abs(a.getSample(ch, i) - b.getSample(ch, i)),
                                    std::abs(a.getSample(ch, i) - c.getSample(ch, i))});
        std::printf("\nBlock-size independence: max difference between block 512 / block 1 / random block outputs = %g (should be 0)\n", maxDiff);
        ok = ok && maxDiff == 0.0f;
    }

    std::printf("\n%s\n", ok ? "All passed: every case's output peak is <= the ceiling, the safety clamp never fired, and the output is independent of block size."
                              : "[FAIL] A case exceeded the ceiling, the safety clamp fired, or the output changed with block size!");
    return ok ? 0 : 1;
}
