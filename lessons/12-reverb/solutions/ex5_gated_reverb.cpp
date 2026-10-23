// Exercise 5 solution: gated reverb (the signature drum sound of the 1980s)
//
//   snare --+------------------------------------> dry
//           |
//           +-> Reverb (RT60 3 s, mix 1) --> Gate (sidechain = dry snare) --> wet
//
// Principle: a very long reverb is "chopped off" by a gate. The gate's detection signal is the dry drum
// (the sidechain), so every hit opens the gate (fast attack), it holds for a fixed time (e.g. 100 ms), and
// then it closes quickly with a short release: the tail does not decay naturally but vanishes abruptly,
// which sounds big and tight. Using the reverb's own output as the detection signal also works, but then
// the hold time gets tangled up with the reverb's level and is hard to control.
//
// Output: dawlab_output/l12_ex5_dry.wav, l12_ex5_reverb.wav (no gate), l12_ex5_gated.wav

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
constexpr double kHitInterval = 0.6; // one snare hit every 0.6 s

// Synthesized snare: 185 Hz body + noise rattle, the same recipe as the drum loop in Signals.h
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
    std::printf("=== Exercise 5: gated reverb ===\n");

    AudioBuffer dry = makeSnareHits(2.4, 2);
    const int n = dry.getNumSamples();

    // Long reverb (RT60 3 s), pure wet
    Reverb reverb;
    reverb.prepare(kSampleRate, kBlockSize, 2);
    reverb.setMix(1.0f);
    reverb.setDecaySeconds(3.0f);
    reverb.setDampingHz(8000.0f);
    reverb.setSize(0.7f);
    reverb.setPreDelayMs(0.0f);
    reverb.reset(); // make mix = 1 take effect immediately (otherwise the first ~0.1 s would glide in from the default mix of 0.3 and leak the dry signal)

    AudioBuffer wet = dry;
    for (int start = 0; start < n; start += kBlockSize)
        reverb.process(wet.toBlock().getSubBlock(start, std::min(kBlockSize, n - start)));
    AudioBuffer ungated = wet; // keep an ungated copy for comparison

    // Gate: sidechain = dry snare; after the 100 ms hold it closes within 30 ms
    Gate gate;
    gate.prepare(kSampleRate, kBlockSize, 2);
    gate.setThresholdDb(-30.0f);
    gate.setHysteresisDb(6.0f);
    gate.setRangeDb(-80.0f);
    gate.setAttackMs(0.5f);
    gate.setHoldMs(100.0f);
    gate.setReleaseMs(30.0f);

    for (int start = 0; start < n; start += kBlockSize) {
        const int len = std::min(kBlockSize, n - start);
        AudioBlock main = wet.toBlock().getSubBlock(start, len);
        const AudioBlock key = dry.toBlock().getSubBlock(start, len);
        gate.process(main, key);
    }

    // ---- Self-check: look at the wet energy after the first hit (t = 0) -----------------
    // While the gate is open (0.02 .. 0.10 s): gated and ungated are nearly identical
    const float openGated = rmsDbWindow(wet, 0.02, 0.10);
    const float openUngated = rmsDbWindow(ungated, 0.02, 0.10);
    // After the gate has closed (0.40 .. 0.58 s; the next hit is at 0.6 s): gated should be almost silent, while the 3 s tail of ungated is still loud
    const float closedGated = rmsDbWindow(wet, 0.40, 0.58);
    const float closedUngated = rmsDbWindow(ungated, 0.40, 0.58);
    std::printf("%-22s %12s %12s\n", "window", "ungated(dB)", "gated(dB)");
    std::printf("%-22s %12.1f %12.1f\n", "open   0.02..0.10 s", openUngated, openGated);
    std::printf("%-22s %12.1f %12.1f\n", "closed 0.40..0.58 s", closedUngated, closedGated);
    std::printf("Final gate state: %s, gain reduction %.1f dB\n", gate.isOpen() ? "open" : "closed", gate.getGainReductionDb());

    DAWLAB_CHECK_NEAR(openGated, openUngated, 1.0);     // no effect while the gate is open
    DAWLAB_CHECK(closedUngated > openUngated - 15.0f);  // no gate: the 3 s tail has dropped only a few dB by 0.4 s
    DAWLAB_CHECK(closedGated < openGated - 50.0f);      // with the gate: the tail is chopped off
    DAWLAB_CHECK(closedGated < closedUngated - 40.0f);
    DAWLAB_CHECK(!hasNanOrInf(wet));

    // The gate must open again for the second hit (t = 0.6 s): gated energy at 0.62..0.70 s is close to ungated
    DAWLAB_CHECK_NEAR(rmsDbWindow(wet, 0.62, 0.70), rmsDbWindow(ungated, 0.62, 0.70), 1.5);

    // For listening: dry + gated wet
    AudioBuffer mix = dry;
    for (int ch = 0; ch < 2; ++ch)
        mix.addFrom(ch, 0, wet, ch, 0, n, 0.7f);
    writeWav(outputPath("l12_ex5_dry.wav"), dry, kSampleRate);
    writeWav(outputPath("l12_ex5_reverb.wav"), ungated, kSampleRate);
    writeWav(outputPath("l12_ex5_gated.wav"), mix, kSampleRate);
    std::printf("Output: dawlab_output/l12_ex5_dry.wav, l12_ex5_reverb.wav, l12_ex5_gated.wav\n");

    return dawlab::test::summary("l12_ex5");
}
