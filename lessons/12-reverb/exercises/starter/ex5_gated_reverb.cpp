// Exercise 5 starter: gated reverb
//
//   snare --+------------------------------------> dry
//           |
//           +-> Reverb (RT60 3 s, mix 1) --> Gate (sidechain = dry snare) --> wet
//
// Goals:
//   1. Configure the Gate: threshold -30 dB, hysteresis 6 dB, range -80 dB, attack 0.5 ms, hold 100 ms, release 30 ms.
//   2. Call gate.process(main, sidechain) block by block: main = the reverb's wet signal, sidechain = the dry snare.
//   3. Self-check: while the gate is open, gated ~ ungated; after the gate closes (0.40 .. 0.58 s), gated is
//      more than 40 dB below ungated.
//
// At run time the self-checks print FAIL; after you complete the TODOs they should all PASS.

#include <dawlab/Common.h>
#include <dawlab/dsp/Gate.h>
#include <dawlab/dsp/Reverb.h>

#include <cmath>
#include <cstdio>

using namespace dawlab;
using namespace dawlab::dsp;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr double kHitInterval = 0.6;

AudioBuffer makeSnareHits(double seconds, int numChannels) {
    AudioBuffer b(numChannels, secondsToSamples(seconds, kSampleRate));
    NoiseGenerator noise(4321);
    const int len = secondsToSamples(0.25, kSampleRate);
    for (double t = 0.0; t < seconds; t += kHitInterval) {
        const int start = secondsToSamples(t, kSampleRate);
        for (int i = 0; i < len && start + i < b.getNumSamples(); ++i) {
            const double s = i / kSampleRate;
            const double body = std::sin(kTwoPi * 185.0 * s) * std::exp(-s * 25.0);
            const double rattle = noise.next() * std::exp(-s * 18.0);
            const float v = static_cast<float>(0.8 * (0.35 * body + 0.45 * rattle));
            for (int ch = 0; ch < numChannels; ++ch)
                b.addSample(ch, start + i, v);
        }
    }
    return b;
}

float rmsDbWindow(const AudioBuffer& b, double t0, double t1) {
    const int s = secondsToSamples(t0, kSampleRate);
    const int n = secondsToSamples(t1 - t0, kSampleRate);
    return gainToDb(rms(b.getReadPointer(0) + s, n));
}

} // namespace

int main() {
    std::printf("=== Exercise 5: gated reverb (starter) ===\n");

    AudioBuffer dry = makeSnareHits(2.4, 2);
    const int n = dry.getNumSamples();

    Reverb reverb;
    reverb.prepare(kSampleRate, kBlockSize, 2);
    reverb.setMix(1.0f);
    reverb.setDecaySeconds(3.0f);
    reverb.setDampingHz(8000.0f);
    reverb.setSize(0.7f);
    reverb.reset(); // make mix = 1 take effect immediately (otherwise the first ~0.1 s would glide in from the default mix of 0.3)

    AudioBuffer wet = dry;
    for (int start = 0; start < n; start += kBlockSize)
        reverb.process(wet.toBlock().getSubBlock(start, std::min(kBlockSize, n - start)));
    AudioBuffer ungated = wet;

    Gate gate;
    gate.prepare(kSampleRate, kBlockSize, 2);
    // TODO: set the gate parameters (threshold / hysteresis / range / attack / hold / release)

    for (int start = 0; start < n; start += kBlockSize) {
        const int len = std::min(kBlockSize, n - start);
        // TODO: gate.process(a sub-block of wet, the matching sub-block of dry as the sidechain)
        (void)len;
    }

    const float openGated = rmsDbWindow(wet, 0.02, 0.10);
    const float openUngated = rmsDbWindow(ungated, 0.02, 0.10);
    const float closedGated = rmsDbWindow(wet, 0.40, 0.58);
    const float closedUngated = rmsDbWindow(ungated, 0.40, 0.58);
    std::printf("open   0.02..0.10 s : ungated %.1f dB, gated %.1f dB\n", openUngated, openGated);
    std::printf("closed 0.40..0.58 s : ungated %.1f dB, gated %.1f dB\n", closedUngated, closedGated);

    DAWLAB_CHECK_NEAR(openGated, openUngated, 1.0);
    DAWLAB_CHECK(closedGated < openGated - 50.0f);
    DAWLAB_CHECK(closedGated < closedUngated - 40.0f);
    DAWLAB_CHECK_NEAR(rmsDbWindow(wet, 0.62, 0.70), rmsDbWindow(ungated, 0.62, 0.70), 1.5);

    AudioBuffer mix = dry;
    for (int ch = 0; ch < 2; ++ch)
        mix.addFrom(ch, 0, wet, ch, 0, n, 0.7f);
    writeWav(outputPath("l12_ex5_gated.wav"), mix, kSampleRate);

    return dawlab::test::summary("l12_ex5_starter");
}
