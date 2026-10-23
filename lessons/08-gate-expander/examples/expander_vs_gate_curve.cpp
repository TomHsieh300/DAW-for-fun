// Example 2: transfer-curve comparison of the dynamics-processor family, plus measured expander / gate behavior
//
// What it shows:
//   - Uses a static gain computer to print the transfer curves of a compressor (4:1), limiter (inf:1),
//     expander (1:2), and gate (input -80..0 dB, one row every 5 dB), so you can see how each one
//     treats the region above versus below the threshold
//   - How the expander's soft knee (knee 0 / 6 / 12 dB) differs around the threshold
//   - Feeds a series of sine tone bursts at different levels to the Expander and the Gate, and measures the
//     settled output level of each segment to verify that they really land on the static curve
//     (the same verification technique the solutions use)
// How to run: l08_expander_vs_gate_curve (takes no arguments).
// Output files: dawlab_output/l08_bursts_input.wav, l08_bursts_expander.wav, l08_bursts_gate.wav.
// What to listen for: the bursts consist of five segments at -20 / -40 / -50 / -60 / -20 dBFS. In the expander
//            version the quiet segments get quieter but are still audible; in the gate version the segments
//            below -40 dB disappear entirely, and each segment has a 1 ms fade-in at its start and a
//            release fade-out at its end.

#include <dawlab/Common.h>
#include <dawlab/dsp/Compressor.h>
#include <dawlab/dsp/Expander.h>
#include <dawlab/dsp/Gate.h>

#include <cstdio>
#include <vector>

using namespace dawlab;
using dawlab::dsp::Compressor;
using dawlab::dsp::Expander;
using dawlab::dsp::Gate;

namespace {

constexpr int kBlockSize = 512;

// Static gate curve: unchanged above the threshold, attenuated by |range| below it (the real gate also has
// hysteresis and time behavior)
float gateOutputDb(float inputDb, float thresholdDb, float rangeDb) {
    return inputDb >= thresholdDb ? inputDb : inputDb + rangeDb;
}

template <typename Processor>
AudioBuffer runBlocks(const AudioBuffer& input, Processor& p) {
    AudioBuffer out = input;
    AudioBlock whole(out);
    const int n = out.getNumSamples();
    for (int start = 0; start < n; start += kBlockSize)
        p.process(whole.getSubBlock(start, std::min(kBlockSize, n - start)));
    return out;
}

// Measure the sine amplitude of the second half of a segment (after it has settled), in dB
double segmentLevelDb(const AudioBuffer& b, int segment, int segLen, double freq, double sampleRate) {
    const int start = segment * segLen + segLen / 2;
    const int n = segLen / 2;
    return gainToDb(goertzelAmplitude(b.getReadPointer(0) + start, n, freq, sampleRate));
}

} // namespace

int main() {
    std::printf("Chapter 08 Example 2: transfer-curve comparison and measured expander / gate behavior\n");

    // ---- 1. Static curves of the four processors ---------------------------------------------
    std::printf("\n[1] Transfer curves (output dB). compressor 4:1 T=-20; limiter inf:1 T=-10; "
                "expander 1:2 T=-40 range -40; gate T=-40 range -80\n");
    std::printf("    input    compressor   limiter   expander      gate\n");
    for (int in = -80; in <= 0; in += 5) {
        const float x = static_cast<float>(in);
        const float comp = Compressor::computeOutputDb(x, -20.0f, 4.0f, 0.0f);
        const float lim = Compressor::computeOutputDb(x, -10.0f, 1.0e9f, 0.0f);
        const float exp = std::max(x - 40.0f, Expander::computeOutputDb(x, -40.0f, 2.0f, 0.0f));
        const float gate = gateOutputDb(x, -40.0f, -80.0f);
        std::printf("    %5d  %10.1f  %9.1f  %9.1f  %9.1f\n", in, comp, lim, exp, gate);
    }

    // ---- 2. expander soft knee ---------------------------------------------
    std::printf("\n[2] Expander 1:2, T = -40 dB soft knee: knee width 0 / 6 / 12 dB (output dB)\n");
    std::printf("    input   knee 0   knee 6   knee 12\n");
    for (int in = -50; in <= -30; in += 2) {
        const float x = static_cast<float>(in);
        std::printf("    %5d  %7.2f  %7.2f  %7.2f\n", in, Expander::computeOutputDb(x, -40.0f, 2.0f, 0.0f),
                    Expander::computeOutputDb(x, -40.0f, 2.0f, 6.0f), Expander::computeOutputDb(x, -40.0f, 2.0f, 12.0f));
    }
    std::printf("    At the knee boundary (T +/- W/2) both the value and the slope of the curve are continuous: knee 12 at -46 dB = %.2f (hard knee: %.2f)\n",
                Expander::computeOutputDb(-46.0f, -40.0f, 2.0f, 12.0f), Expander::computeOutputDb(-46.0f, -40.0f, 2.0f, 0.0f));

    // ---- 3. Measurement: tone bursts ------------------------------------------------
    const double sampleRate = 48000.0;
    const double freq = 1000.0;
    const std::vector<float> levels = {-20.0f, -40.0f, -50.0f, -60.0f, -20.0f};
    const double segSec = 0.5;
    const AudioBuffer input = makeToneBursts(freq, sampleRate, levels, segSec);
    const int segLen = secondsToSamples(segSec, sampleRate);

    Expander expander;
    expander.prepare(sampleRate, kBlockSize, 2);
    expander.setThresholdDb(-40.0f);
    expander.setRatio(2.0f);
    expander.setKneeDb(0.0f);
    expander.setRangeDb(-40.0f);
    expander.setAttackMs(5.0f);
    expander.setReleaseMs(50.0f);

    Gate gate;
    gate.prepare(sampleRate, kBlockSize, 2);
    gate.setThresholdDb(-40.0f);
    gate.setHysteresisDb(4.0f);
    gate.setRangeDb(-80.0f);
    gate.setAttackMs(1.0f);
    gate.setHoldMs(10.0f);
    gate.setReleaseMs(50.0f);

    const AudioBuffer outExp = runBlocks(input, expander);
    const AudioBuffer outGate = runBlocks(input, gate);
    writeWav(outputPath("l08_bursts_input.wav"), input, sampleRate);
    writeWav(outputPath("l08_bursts_expander.wav"), outExp, sampleRate);
    writeWav(outputPath("l08_bursts_gate.wav"), outGate, sampleRate);

    std::printf("\n[3] Measured tone bursts (1 kHz amplitude of the second half of each segment, dB). "
                "The peak envelope of a sine wave is 3 dB above its RMS; the static curves are in terms of peak\n");
    std::printf("    seg   input   expander meas. / theory    gate meas. / theory\n");
    for (size_t s = 0; s < levels.size(); ++s) {
        const double in = segmentLevelDb(input, static_cast<int>(s), segLen, freq, sampleRate);
        const double e = segmentLevelDb(outExp, static_cast<int>(s), segLen, freq, sampleRate);
        const double g = segmentLevelDb(outGate, static_cast<int>(s), segLen, freq, sampleRate);
        const float expTheory = std::max(levels[s] - 40.0f, Expander::computeOutputDb(levels[s], -40.0f, 2.0f, 0.0f));
        const float gateTheory = gateOutputDb(levels[s], -40.0f, -80.0f);
        std::printf("    %4zu  %6.1f  %8.1f / %6.1f         %8.1f / %6.1f\n", s + 1, in, e, expTheory, g,
                    gateTheory);
    }
    std::printf("    (The measured gate values are clamped by the -100 dB floor of gainToDb(); the theoretical -130 / -140 dB is far below audibility, and anything \"well below -60 dB\" counts as closed)\n");
    std::printf("\nOutput: dawlab_output/l08_bursts_input.wav, l08_bursts_expander.wav, l08_bursts_gate.wav\n");
    return 0;
}
