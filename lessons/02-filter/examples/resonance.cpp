// l02_resonance: Q (resonance) sweep.
//
// What it shows:
//   - The gain of a second-order lowpass at the cutoff = Q (linear) = 20 log10(Q) dB: Q = 0.707 -> -3 dB,
//     Q = 1 -> 0 dB, Q = 4 -> +12 dB, Q = 16 -> +24 dB. Prints a theory vs. measurement table.
//   - The higher the Q, the sharper the peak and the narrower the bandwidth: prints the -3 dB
//     bandwidth (relative to the peak) against fc/Q (the definition for a bandpass).
//   - A synthesizer-style filter sweep: runs a pluck arpeggio through an LPF whose cutoff slowly
//     sweeps from 200 Hz to 8 kHz, updating the coefficients once per block (512 samples); writes
//     WAVs for Q = 0.707 and Q = 8.
//
// How to run: run with no arguments. It writes WAVs (to dawlab_output/):
//   l02_resonance_dry.wav, l02_resonance_q0707.wav, l02_resonance_q8.wav
// What you should hear: the q0707 version is a soft "opening up" of the timbre; in the q8 version,
//   as the cutoff passes each overtone it goes "wooow", bringing that overtone out (the classic
//   resonant sweep), and the whole thing is louder and harsher (watch the peak number).
#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>

#include <cstdio>
#include <vector>

using namespace dawlab;
using dawlab::dsp::Biquad;
using dawlab::dsp::BiquadCoefficients;

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

int main() {
    std::printf("=== Chapter 02: Q / resonance sweep (LPF fc = 1 kHz) ===\n\n");

    // ---- 1. Gain at the cutoff vs. Q ---------------------------------------------------
    std::printf("[1] Gain at the cutoff\n");
    std::printf("    %6s  %12s  %12s  %12s  %14s\n", "Q", "20log10(Q)", "theory dB", "measured dB", "peak freq (Hz)");
    Biquad filter;
    filter.prepare(kSampleRate, kBlockSize, 1);
    for (double q : {0.5, 0.7071, 1.0, 2.0, 4.0, 8.0, 16.0}) {
        const BiquadCoefficients c = BiquadCoefficients::makeLowpass(kSampleRate, 1000.0, q);
        filter.setCoefficients(c);
        filter.reset();
        const double theory = c.magnitudeDb(1000.0, kSampleRate);
        const double meas = measureGainDb([&](float x) { return filter.processSample(0, x); }, 1000.0, kSampleRate,
                                          0.5, 0.5);
        // The true peak frequency: a peak exists only when Q > 1/√2, and sits slightly below fc (analog: fc·sqrt(1 - 1/(2Q²)))
        double peakF = 0.0, peakDb = -1e9;
        for (double f = 20.0; f < 20000.0; f *= 1.002) {
            const double m = c.magnitudeDb(f, kSampleRate);
            if (m > peakDb) {
                peakDb = m;
                peakF = f;
            }
        }
        // A peak at the start of the sweep (20 Hz) = the curve falls monotonically, with no resonance peak
        const bool hasPeak = peakF > 25.0;
        char peakText[32];
        if (hasPeak)
            std::snprintf(peakText, sizeof(peakText), "%.0f", peakF);
        else
            std::snprintf(peakText, sizeof(peakText), "(no peak)");
        std::printf("    %6.3f  %12.2f  %12.2f  %12.2f  %14s\n", q, 20.0 * std::log10(q), theory, meas, peakText);
        DAWLAB_CHECK_NEAR(theory, 20.0 * std::log10(q), 0.01);
        DAWLAB_CHECK_NEAR(meas, theory, 0.1);
        // Peak frequency of an analog second-order lowpass: fc · sqrt(1 - 1/(2Q²)) (a peak exists only for Q > 1/√2)
        if (q > 0.71)
            DAWLAB_CHECK_NEAR(peakF, 1000.0 * std::sqrt(1.0 - 1.0 / (2.0 * q * q)), 6.0);
        else
            DAWLAB_CHECK(!hasPeak);
    }
    std::printf("    -> There is no peak for Q <= 0.707 (maximally flat); a resonance peak appears once Q > 0.707, slightly below fc.\n\n");

    // ---- 2. Bandpass bandwidth = fc / Q ------------------------------------------------
    std::printf("[2] -3 dB bandwidth of the bandpass (0 dB peak) vs. fc/Q\n");
    std::printf("    %6s  %10s  %10s  %12s  %12s\n", "Q", "f_low", "f_high", "BW measured", "fc/Q");
    for (double q : {0.5, 1.0, 2.0, 4.0, 8.0}) {
        const BiquadCoefficients c = BiquadCoefficients::makeBandpass(kSampleRate, 1000.0, q);
        // Search outward from the center for the -3.01 dB points
        double fLow = 1000.0, fHigh = 1000.0;
        while (fLow > 1.0 && c.magnitudeDb(fLow, kSampleRate) > -3.0103)
            fLow /= 1.0005;
        while (fHigh < kSampleRate * 0.499 && c.magnitudeDb(fHigh, kSampleRate) > -3.0103)
            fHigh *= 1.0005;
        const double bw = fHigh - fLow;
        std::printf("    %6.2f  %10.1f  %10.1f  %12.1f  %12.1f\n", q, fLow, fHigh, bw, 1000.0 / q);
        // RBJ's bandpass definition: BW = fc / Q (bilinear prewarping guarantees it holds near fc, and more accurately for high Q)
        DAWLAB_CHECK_NEAR(bw, 1000.0 / q, 1000.0 / q * 0.03);
        // Geometric symmetry: f_low · f_high ~ fc²
        DAWLAB_CHECK_NEAR(std::sqrt(fLow * fHigh), 1000.0, 5.0);
    }
    std::printf("    -> The definition of Q: Q = fc / BW. The larger the Q, the narrower the passed band.\n\n");

    // ---- 3. Synthesizer-style resonant sweep -------------------------------------------
    std::printf("[3] Resonant filter sweep (cutoff 200 Hz -> 8 kHz, coefficients updated every block)\n");
    AudioBuffer dry = makePluckSequence(kSampleRate, 6.0, 0.4f, 2);
    writeWav(outputPath("l02_resonance_dry.wav"), dry, kSampleRate);
    std::printf("    dry   peak %.2f dBFS\n", gainToDb(peakAbs(dry)));

    for (double q : {0.7071, 8.0}) {
        AudioBuffer out = dry;
        Biquad sweepFilter;
        sweepFilter.prepare(kSampleRate, kBlockSize, out.getNumChannels());
        AudioBlock whole(out);
        const int total = out.getNumSamples();
        for (int start = 0; start < total; start += kBlockSize) {
            const int len = std::min(kBlockSize, total - start);
            // The cutoff sweeps linearly on a logarithmic frequency axis: 200 · (8000/200)^t
            const double t = static_cast<double>(start) / total;
            const double cutoff = 200.0 * std::pow(40.0, t);
            sweepFilter.setCoefficients(BiquadCoefficients::makeLowpass(kSampleRate, cutoff, q));
            sweepFilter.process(whole.getSubBlock(start, len));
        }
        const char* file = q < 1.0 ? "l02_resonance_q0707.wav" : "l02_resonance_q8.wav";
        std::printf("    Q=%-5g peak %.2f dBFS  RMS %.2f dB  -> %s\n", q, gainToDb(peakAbs(out)), gainToDb(rms(out, 0)),
                    file);
        DAWLAB_CHECK(!hasNanOrInf(out));
        writeWav(outputPath(file), out, kSampleRate, 32); // 32-bit float: Q=8 may exceed 0 dBFS, so keep the overs
    }
    std::printf("    (The Q=8 version is written as 32-bit float and may exceed 0 dBFS; turn the volume down before playing it)\n\n");

    return dawlab::test::summary("l02_resonance");
}
