// Example 2: pan law tables, pan sweeps, mono pan vs. stereo balance
//
// What it demonstrates:
//   (A) Prints, for the three pan laws (Linear / ConstantPower / -4.5 dB) at each pan position, the left and right gains,
//       the power sum gL^2+gR^2 (the energy of the two speakers added together) and the amplitude sum gL+gR (left and right summed to mono).
//   (B) Sweeps a mono plucked arpeggio from hard left to hard right (4 seconds), writes one stereo WAV per law,
//       and measures the RMS of the left and right channels at the center to verify the -6 / -3 / -4.5 dB center levels.
//   (C) Applies the Panner to a "genuinely stereo" signal (uncorrelated noise in left and right) to show that it is only a balance:
//       panning right makes the left channel quieter, but the content of the left channel is not moved to the right.
//   (D) Endpoint check: at pan = +/-1 the gain on the opposite side is exactly 0; setPan(NaN) is ignored (self-check).
//
// How to run:
//   cd <build dir> && bin/Debug/l01_pan_law.exe
//
// Output files (in <build dir>/dawlab_output/):
//   l01_pan_sweep_linear.wav / l01_pan_sweep_constpower.wav / l01_pan_sweep_minus4_5.wav
//
// What you should hear: the image moves from left to right. Listening to the linear version on headphones, the level seems to "dip" as it passes the center;
// the constant power version has a steadier level (when heard on speakers), and -4.5 dB lies between the two.

#include <dawlab/Common.h>
#include <dawlab/dsp/Panner.h>

#include <cmath>
#include <cstdio>

using namespace dawlab;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

const dsp::PanLaw kLaws[] = {dsp::PanLaw::Linear, dsp::PanLaw::ConstantPower, dsp::PanLaw::Minus4_5dB};
const char* kSweepFiles[] = {"l01_pan_sweep_linear.wav", "l01_pan_sweep_constpower.wav",
                             "l01_pan_sweep_minus4_5.wav"};

void printTable(dsp::PanLaw law) {
    std::printf("--- %s ---\n", dsp::panLawName(law));
    std::printf("  %6s %9s %9s %12s %12s\n", "pan", "gL (dB)", "gR (dB)", "gL^2+gR^2 dB", "gL+gR dB");
    for (int i = -4; i <= 4; ++i) {
        const float pan = 0.25f * static_cast<float>(i);
        float l = 0.0f, r = 0.0f;
        dsp::Panner::computeGains(pan, law, l, r);
        const float powerSum = l * l + r * r;
        std::printf("  %+6.2f %9.2f %9.2f %12.2f %12.2f\n", pan, gainToDb(l), gainToDb(r),
                    10.0f * std::log10(powerSum), gainToDb(l + r));
    }
}

} // namespace

int main() {
    std::printf("=== Chapter 01 Example 2: pan law ===\n\n");

    // ---- (A) Tables ------------------------------------------------------------
    std::printf("(A) Left and right gains for the three pan laws (theta = (pan+1)/2)\n");
    for (dsp::PanLaw law : kLaws)
        printTable(law);
    std::printf("\n");

    // ---- (B) Pan sweep ---------------------------------------------------------
    std::printf("(B) Mono plucked arpeggio swept from left to right (4 s), RMS of the left and right channels measured at the center:\n");
    const AudioBuffer mono = makePluckSequence(kSampleRate, 4.0, 0.4f, 1);
    const int total = mono.getNumSamples();
    for (int lawIndex = 0; lawIndex < 3; ++lawIndex) {
        dsp::Panner panner;
        panner.setPanLaw(kLaws[lawIndex]);
        panner.setRampSeconds(0.0); // The sweep itself is already continuous; 0 here keeps the measurement position accurate
        panner.prepare(kSampleRate, kBlockSize, 2);
        panner.setPan(-1.0f);

        AudioBuffer stereo(2, total);
        AudioBlock out(stereo);
        for (int start = 0; start < total; start += kBlockSize) {
            const int len = std::min(kBlockSize, total - start);
            const float pan = -1.0f + 2.0f * static_cast<float>(start) / static_cast<float>(total);
            panner.setPan(pan);
            panner.processMonoToStereo(mono.getReadPointer(0) + start, out.getSubBlock(start, len));
        }
        writeWav(outputPath(kSweepFiles[lawIndex]), stereo, kSampleRate);

        // Measure the RMS over 0.1 s around the center (2.0 s) and compare it with the original mono signal
        const int c = total / 2;
        const int n = secondsToSamples(0.1, kSampleRate);
        const float rmsMono = rms(mono.getReadPointer(0) + c, n);
        const float rmsL = rms(stereo.getReadPointer(0) + c, n);
        const float rmsR = rms(stereo.getReadPointer(1) + c, n);
        std::printf("  %-14s center: L %.2f dB, R %.2f dB (relative to the original mono signal) -> %s\n", dsp::panLawName(kLaws[lawIndex]),
                    gainToDb(rmsL / rmsMono), gainToDb(rmsR / rmsMono), kSweepFiles[lawIndex]);
    }
    std::printf("\n");

    // ---- (C) A genuinely stereo signal: the Panner is really a balance ---------
    std::printf("(C) Stereo (uncorrelated white noise in left and right) through the Panner (ConstantPower, pan = +0.5):\n");
    AudioBuffer noise = makeWhiteNoise(kSampleRate, 1.0, 0.25f, 2);
    const float inL = rms(noise, 0), inR = rms(noise, 1);
    dsp::Panner balance;
    balance.setRampSeconds(0.0);
    balance.prepare(kSampleRate, kBlockSize, 2);
    balance.setPan(0.5f);
    AudioBlock nb(noise);
    for (int start = 0; start < noise.getNumSamples(); start += kBlockSize)
        balance.process(nb.getSubBlock(start, std::min(kBlockSize, noise.getNumSamples() - start)));
    float gL = 0.0f, gR = 0.0f;
    dsp::Panner::computeGains(0.5f, dsp::PanLaw::ConstantPower, gL, gR);
    std::printf("  L: %.2f dB -> %.2f dB (change %.2f dB, = gL %.2f dB)\n", gainToDb(inL), gainToDb(rms(noise, 0)),
                gainToDb(rms(noise, 0) / inL), gainToDb(gL));
    std::printf("  R: %.2f dB -> %.2f dB (change %.2f dB, = gR %.2f dB)\n", gainToDb(inR), gainToDb(rms(noise, 1)),
                gainToDb(rms(noise, 1) / inR), gainToDb(gR));
    std::printf("  The left channel simply got %.2f dB quieter and its content was not moved to the right: this is a balance, not a mono pan.\n",
                -gainToDb(rms(noise, 0) / inL));

    // ---- (D) Endpoints and invalid input: at hard left / hard right the opposite side must be exactly 0, and NaN must never flow into the audio ----
    std::printf("\n(D) Endpoint check: gain on the opposite side at pan = +/-1\n");
    bool endpointsExact = true;
    for (dsp::PanLaw law : {dsp::PanLaw::Linear, dsp::PanLaw::ConstantPower, dsp::PanLaw::Minus4_5dB}) {
        float l1 = 0.0f, r1 = 0.0f, l2 = 0.0f, r2 = 0.0f;
        dsp::Panner::computeGains(+1.0f, law, l1, r1); // hard right: the left channel must be == 0
        dsp::Panner::computeGains(-1.0f, law, l2, r2); // hard left: the right channel must be == 0
        std::printf("  %-14s hard right gL = %.9g, hard left gR = %.9g\n", dsp::panLawName(law), l1, r2);
        endpointsExact = endpointsExact && l1 == 0.0f && r2 == 0.0f && r1 > 0.999f && l2 > 0.999f;
    }
    dsp::Panner bad;
    bad.prepare(kSampleRate, kBlockSize, 2);
    bad.setPan(0.25f);
    bad.setPan(std::nanf("")); // invalid: should be ignored, keeping 0.25
    float nanL = 0.0f, nanR = 0.0f;
    dsp::Panner::computeGains(std::nanf(""), dsp::PanLaw::ConstantPower, nanL, nanR); // treated as center
    std::printf("  After setPan(NaN) pan = %.2f; computeGains(NaN) = %.4f / %.4f (treated as center)\n", bad.getPan(), nanL, nanR);
    DAWLAB_CHECK(endpointsExact);
    DAWLAB_CHECK(bad.getPan() == 0.25f);
    DAWLAB_CHECK(std::isfinite(nanL) && std::isfinite(nanR));

    std::printf("\nOutput: dawlab_output/%s, %s, %s\n", kSweepFiles[0], kSweepFiles[1], kSweepFiles[2]);
    return dawlab::test::summary("l01_pan_law");
}
