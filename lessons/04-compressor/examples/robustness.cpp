// Example 5: bad input and stereo-link toggling — two "invisible" traps for a compressor on the audio thread.
//
// What it demonstrates:
//   (A) Bad input: drop a +Inf, NaN or huge value (2e19, which overflows when squared) into the middle of a 1 kHz
//       sine, and compare the output peak and GR one second later against a clean input.
//       A recursive attack / release smoother that swallows a single NaN gets stuck permanently:
//       in Peak mode the output is silenced forever, in RMS mode the compressor stops compressing forever,
//       and the GR meter shows nothing unusual.
//       AttackReleaseFilter / EnvelopeFollower / Compressor in this chapter all block non-finite values,
//       so here you should see the compressor return to the same state as with the clean input shortly after
//       the bad sample has passed.
//   (B) Stereo link toggling: start unlinked with a loud right channel (GR about 15 dB) and a quiet left channel
//       (GR 0), then switch to linked, then back to unlinked. Measure the gain sample by sample and confirm there
//       is no gain jump (click) at the switch: after switching to linked, L must catch up with R's GR at the
//       attack speed instead of being pulled down 15 dB instantly.
// How to run: just run l04_robustness (no arguments).
// Output files: dawlab_output/l04_link_toggle.wav (the output of (B); the switch points are at 1.0 s, 1.1 s, 2.1 s).
// What you should hear: l04_link_toggle.wav is a DC test signal, so there is nothing to hear; the point is the
//             numbers. Open it in a waveform editor: around the switch points the waveform height should change
//             smoothly, with no sudden steps.
// At the end this example verifies itself with DAWLAB_CHECK and returns 0 only if everything passes.

#include <dawlab/Common.h>
#include <dawlab/dsp/Compressor.h>

#include <cmath>
#include <cstdio>
#include <limits>

using namespace dawlab;
using dawlab::dsp::Compressor;
using dawlab::dsp::DetectorMode;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

void processBlocks(AudioBuffer& buffer, Compressor& comp) {
    AudioBlock whole(buffer);
    for (int start = 0; start < buffer.getNumSamples(); start += kBlockSize)
        comp.process(whole.getSubBlock(start, std::min(kBlockSize, buffer.getNumSamples() - start)));
}

// Peak (dBFS) of the last 100 ms
float tailPeakDb(const AudioBuffer& b, int ch) {
    const int n = static_cast<int>(0.1 * kSampleRate);
    return gainToDb(peakAbs(b.getReadPointer(ch) + b.getNumSamples() - n, n));
}

// (A): returns {tail peak dB, GR of the last block}
struct Result {
    float tailDb;
    float grDb;
};

Result runWithBadSample(DetectorMode mode, bool inject, float badValue) {
    Compressor comp; // Defaults: T -20, 4:1, knee 6, attack 10 ms, release 100 ms
    comp.prepare(kSampleRate, kBlockSize, 1);
    comp.setDetectorMode(mode);
    AudioBuffer x = makeSine(1000.0, kSampleRate, 1.0, 0.5f, 1); // -6 dBFS, 1 second
    if (inject)
        x.setSample(0, 100, badValue);
    processBlocks(x, comp);
    return {tailPeakDb(x, 0), comp.getGainReductionDb()};
}

} // namespace

int main() {
    std::printf("Chapter 04, example 5: bad input and stereo-link toggling\n");

    // ---- (A) Bad input ----------------------------------------------------------
    std::printf("\n(A) 1 kHz -6 dBFS sine, sample 100 replaced by a bad value; does it recover after 1 second?\n");
    std::printf("  Mode | Bad    | Tail peak (dBFS) | Clean input's tail | GR (dB) | Clean input's GR\n");
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const struct {
        const char* name;
        float value;
    } bads[] = {{"+Inf", inf}, {"NaN", nan}, {"2e19", 2.0e19f}};

    for (DetectorMode mode : {DetectorMode::Peak, DetectorMode::Rms}) {
        const Result clean = runWithBadSample(mode, false, 0.0f);
        for (const auto& bad : bads) {
            const Result r = runWithBadSample(mode, true, bad.value);
            std::printf("  %-4s | %-6s | %16.2f | %18.2f | %7.2f | %16.2f\n", mode == DetectorMode::Peak ? "Peak" : "RMS",
                        bad.name, r.tailDb, clean.tailDb, r.grDb, clean.grDb);
            // After 1 second it should be almost identical to the clean input (the bad sample only affects a short stretch around it)
            DAWLAB_CHECK(std::isfinite(r.tailDb) && std::isfinite(r.grDb));
            DAWLAB_CHECK_NEAR(r.tailDb, clean.tailDb, 0.05);
            DAWLAB_CHECK_NEAR(r.grDb, clean.grDb, 0.05);
        }
    }

    // ---- (B) Stereo link toggling -----------------------------------------------
    // Use DC as the test signal: every input sample is identical, so gain = output / input can be measured exactly, sample by sample.
    std::printf("\n(B) Stereo link toggling: hard knee, T -20, 4:1, attack 10 ms, release 100 ms\n");
    std::printf("    0.00–1.00 s unlinked, L = -40 dBFS, R = 0 dBFS (R's GR is about 15 dB)\n");
    std::printf("    1.00–1.10 s switch to linked, input unchanged (L should follow R's GR at the attack speed)\n");
    std::printf("    1.10–2.10 s switch back to unlinked, L and R both become -40 dBFS (each releases on its own)\n");
    std::printf("    2.10–3.10 s switch to linked again, input unchanged\n");

    Compressor comp;
    comp.prepare(kSampleRate, kBlockSize, 2);
    comp.setKneeDb(0.0f);
    comp.setStereoLink(false);

    const int seg1 = static_cast<int>(1.0 * kSampleRate);
    const int seg2 = static_cast<int>(0.1 * kSampleRate);
    const int seg3 = static_cast<int>(1.0 * kSampleRate);
    const int seg4 = static_cast<int>(1.0 * kSampleRate);
    const int total = seg1 + seg2 + seg3 + seg4;
    const float quiet = dbToGain(-40.0f);

    AudioBuffer input(2, total);
    for (int i = 0; i < total; ++i) {
        input.setSample(0, i, quiet);
        input.setSample(1, i, i < seg1 + seg2 ? 1.0f : quiet);
    }
    AudioBuffer output = input;
    AudioBlock whole(output);

    // Process each segment with its own 512-sample blocks; toggle the link between segments (on block boundaries)
    const int segStart[] = {0, seg1, seg1 + seg2, seg1 + seg2 + seg3};
    const int segLen[] = {seg1, seg2, seg3, seg4};
    const bool segLinked[] = {false, true, false, true};
    for (int s = 0; s < 4; ++s) {
        comp.setStereoLink(segLinked[s]);
        for (int off = 0; off < segLen[s]; off += kBlockSize)
            comp.process(whole.getSubBlock(segStart[s] + off, std::min(kBlockSize, segLen[s] - off)));
    }

    // Per-sample gain (dB) and the largest difference between adjacent samples
    float maxStepDb[2] = {0.0f, 0.0f};
    for (int ch = 0; ch < 2; ++ch) {
        float prevDb = 0.0f;
        for (int i = 0; i < total; ++i) {
            const float gDb = gainToDb(output.getSample(ch, i) / input.getSample(ch, i));
            if (i > 0)
                maxStepDb[ch] = std::max(maxStepDb[ch], std::abs(gDb - prevDb));
            prevDb = gDb;
        }
    }
    auto gainAtDb = [&](int ch, int i) { return gainToDb(output.getSample(ch, i) / input.getSample(ch, i)); };
    for (int s = 1; s < 4; ++s) {
        const int i = segStart[s];
        std::printf("  Switch to %-8s @ %.2f s: R gain %7.2f dB -> %7.2f dB; L gain %7.2f dB -> %7.2f dB\n",
                    segLinked[s] ? "linked" : "unlinked", i / kSampleRate, gainAtDb(1, i - 1), gainAtDb(1, i),
                    gainAtDb(0, i - 1), gainAtDb(0, i));
    }
    std::printf("  Largest gain change between adjacent samples over the whole run: L %.3f dB, R %.3f dB "
                "(at most about 0.03 dB per sample during attack)\n",
                maxStepDb[0], maxStepDb[1]);
    // After 0.1 s of linking (10 attack time constants), L's GR should have caught up with R's (they share one gain)
    const int endLinked = seg1 + seg2 - 1;
    std::printf("  End of the linked segment: L gain %.2f dB, R gain %.2f dB (should be almost identical)\n",
                gainAtDb(0, endLinked), gainAtDb(1, endLinked));
    DAWLAB_CHECK_NEAR(gainAtDb(0, endLinked), gainAtDb(1, endLinked), 0.01);
    DAWLAB_CHECK(maxStepDb[0] < 0.1f);
    DAWLAB_CHECK(maxStepDb[1] < 0.1f);

    writeWav(outputPath("l04_link_toggle.wav"), output, kSampleRate);
    std::printf("\n  Wrote dawlab_output/l04_link_toggle.wav\n");
    return dawlab::test::summary("l04_robustness");
}
