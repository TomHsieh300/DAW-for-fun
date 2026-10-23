// Exercise 6 starter: fuzz test -- random signals + random parameters + random block sizes, verifying the ceiling guarantee
//
// Goal: complete makeRandomSignal() and the fuzz loop in main(), run the Limiter through a few hundred random tests, and check:
//   (a) the output sample peak is <= the ceiling (linear value, exact comparison)
//   (b) the output contains no NaN / Inf
//   (c) limiter.getSafetyClipCount() == 0 (the algorithm itself stayed under the ceiling, not just the final clamp)
//
// Run: l05_ex6_starter -> it prints FAIL until you finish (expected).

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

// TODO 1: generate a random signal -- length 1 sample to 0.3 s, amplitude -40..+60 dB, frequency 20 Hz..0.45 fs,
//         producing noise / sine / on-off bursts / alternating-sign impulses / DC / square wave / a mix, depending on kind.
dawlab::AudioBuffer makeRandomSignal(Random& rng, double sampleRate, int numChannels, SignalKind kind) {
    (void)kind;
    const int n = rng.integer(1, dawlab::secondsToSamples(0.3, sampleRate));
    dawlab::AudioBuffer b(numChannels, n);
    return b; // all zeros for now: the limiter of course never exceeds the ceiling, but nothing is being tested either
}

// Feed the limiter blocks of random length (1..maxBlock) (already done)
void processRandomBlocks(dawlab::AudioBuffer& buffer, dawlab::dsp::Limiter& limiter, Random& rng, int maxBlock) {
    dawlab::AudioBlock whole(buffer);
    int start = 0;
    while (start < buffer.getNumSamples()) {
        const int len = std::min(rng.integer(1, maxBlock), buffer.getNumSamples() - start);
        limiter.process(whole.getSubBlock(start, len));
        start += len;
    }
}

} // namespace

int main() {
    Random rng(20240521);
    int cases = 0, failures = 0;
    float worstMarginDb = 1.0e9f; // the minimum of (ceiling - output peak), in dB

    const int kCases = 400;
    for (int c = 0; c < kCases; ++c) {
        // TODO 2: random parameters -- sample rate (44.1k / 48k / 96k), channel count 1..2,
        //         maxLookaheadMs 0..30, lookaheadMs (deliberately allowed to go out of range), ceiling -30..0 dB,
        //         release 0..1000 ms, input gain -10..+40 dB, maxBlock 1..1024, a random SignalKind
        const double sampleRate = 48000.0;
        const int numChannels = 2;
        const float ceilingDb = -1.0f;
        const float lookaheadMs = 5.0f;
        const float maxLookaheadMs = 20.0f;
        const auto kind = SignalKind::Noise;

        dawlab::dsp::Limiter limiter;
        limiter.setMaxLookaheadMs(maxLookaheadMs);
        limiter.prepare(sampleRate, 1024, numChannels);
        limiter.setCeilingDb(ceilingDb);
        limiter.setLookaheadMs(lookaheadMs);
        // TODO 3: setReleaseMs, setInputGainDb; in half of the cases call reset() so the input gain is in place immediately

        dawlab::AudioBuffer signal = makeRandomSignal(rng, sampleRate, numChannels, kind);
        processRandomBlocks(signal, limiter, rng, 1024);

        // TODO 4: check (a)(b)(c) and print the case's parameters on failure; update cases / failures / worstMarginDb
        //         and use DAWLAB_CHECK to verify getLatencySamples() == clamp(msToSamples(lookaheadMs), 0, msToSamples(maxLookaheadMs))
        ++cases;
    }
    std::printf("Fixed parameters: %d cases, %d failures; minimum margin = %.4f dB\n", cases, failures, worstMarginDb);
    DAWLAB_CHECK(failures == 0);
    DAWLAB_CHECK(worstMarginDb < 0.01f); // at least one case must actually reach the ceiling, otherwise the test is not testing anything

    // TODO 5 (advanced):
    //   - randomly change setReleaseMs / setInputGainDb between blocks; the guarantee should still hold
    //   - lower the ceiling during playback: the output is still <= the new ceiling, but getSafetyClipCount() will be > 0 (why?)
    //   - put NaN / Inf into the input: the output must still be finite and <= the ceiling
    //   - call setInputGainDb(the same value) once per block (simulating a plugin host) and confirm the output reaches the target gain after about 20 ms

    return dawlab::test::summary("l05_ex6");
}
