// Example 1: Harmonic analysis of four transfer curves - which curve produces which harmonics,
// how much THD, and whether there is a DC offset.
//
// What it shows:
//   1. Prints a numeric table (x -> y) of the four transfer curves (Tanh / Cubic / Arctan / Asymmetric).
//   2. Feeds a 100 Hz sine (amplitude A = 0.25 ... 4) straight into Saturation::shape(), then uses
//      harmonicLevelsDb() to measure each harmonic in dB relative to the fundamental, thdPercent() for
//      THD, and the mean for the DC offset.
//      Observe: symmetric curves produce only odd harmonics; Asymmetric adds even harmonics and DC;
//      the more drive, the more harmonics.
//      It also compares the DC of Asymmetric with the small-signal approximation 2A^3/(3*pi)
//      (README section 2.4).
//   3. Compares the measured Cubic (y = x - x^3/3) values with the hand-derived results of README
//      section 2.2:
//        A = 0.5 -> H3 = -33.06 dB, THD = 2.22%;  A = 1 -> H3 = -19.08 dB, THD = 11.11%.
//   4. Writes WAVs of a 220 Hz sine whose drive sweeps from 0 dB to +24 dB (through Saturation with
//      4x oversampling), so you can hear the harmonics grow in bit by bit.
//
//   Why 100 Hz? The 240th harmonic of 100 Hz is the first to reach Nyquist (24 kHz). The high
//   harmonics of these smooth curves have long since decayed to nothing by then, so no oversampling is
//   needed to measure the "real" harmonics (no aliasing contamination).
//   The second example, l06_aliasing_demo, deliberately uses 7 kHz to make aliasing happen.
//
// How to run: l06_harmonic_analysis (no arguments)
//
// Output files (in dawlab_output/):
//   l06_drive_sweep_tanh.wav / l06_drive_sweep_cubic.wav / l06_drive_sweep_arctan.wav /
//   l06_drive_sweep_asymmetric.wav     220 Hz, 4 seconds, drive 0 -> +24 dB
//
// What you should hear:
//   A clean sine at first, then increasingly "bright" and "squashed". Tanh and Cubic sound hollow and
//   square-wave-like (odd harmonics); Asymmetric sounds thicker, with an octave-up flavor (even
//   harmonics); at very high drive Cubic has the roughness of a hard clip.

#include <dawlab/Common.h>
#include <dawlab/dsp/Saturation.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr double kAnalysisHz = 100.0;

using dawlab::dsp::Saturation;
using dawlab::dsp::SaturationCurve;

const SaturationCurve kCurves[] = {SaturationCurve::Tanh, SaturationCurve::Cubic, SaturationCurve::Arctan,
                                   SaturationCurve::Asymmetric};

double meanOf(const float* x, int n) {
    double s = 0.0;
    for (int i = 0; i < n; ++i)
        s += x[i];
    return n > 0 ? s / n : 0.0;
}

// Feed a sine of amplitude A through the curve; returns (H1 amplitude, per-harmonic dB, THD %, DC).
struct Measurement {
    double h1 = 0.0;
    std::vector<double> levelsDb;
    double thd = 0.0;
    double dc = 0.0;
};

Measurement analyse(SaturationCurve curve, float amplitude) {
    dawlab::AudioBuffer b = dawlab::makeSine(kAnalysisHz, kSampleRate, 1.0, amplitude, 1);
    float* d = b.getWritePointer(0);
    const int n = b.getNumSamples();
    for (int i = 0; i < n; ++i)
        d[i] = Saturation::shape(curve, d[i]);
    Measurement m;
    m.h1 = dawlab::goertzelAmplitude(d, n, kAnalysisHz, kSampleRate);
    m.levelsDb = dawlab::harmonicLevelsDb(d, n, kAnalysisHz, kSampleRate, 8);
    m.thd = dawlab::thdPercent(d, n, kAnalysisHz, kSampleRate, 20);
    m.dc = meanOf(d, n);
    return m;
}

} // namespace

int main() {
    // ---- 1. Transfer-curve table -------------------------------------------------------------
    std::printf("Transfer curves (y = shape(curve, x)):\n");
    std::printf("%8s %10s %10s %10s %12s\n", "x", "Tanh", "Cubic", "Arctan", "Asymmetric");
    for (double x = -3.0; x <= 3.0001; x += 0.5) {
        std::printf("%8.2f", x);
        for (SaturationCurve c : kCurves)
            std::printf(" %10.4f", Saturation::shape(c, static_cast<float>(x)));
        std::printf("\n");
    }
    std::printf("(Cubic is fixed at +/-2/3 for |x| >= 1; the negative half of Asymmetric saturates early, at -0.5)\n\n");

    // ---- 2. Harmonic analysis table ----------------------------------------------------------
    const float amplitudes[] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f};
    for (SaturationCurve c : kCurves) {
        std::printf("=== %s ===  %.0f Hz sine input, amplitude A (= peak before the curve)\n", dawlab::dsp::saturationCurveName(c),
                    kAnalysisHz);
        std::printf("%6s %8s %8s %8s %8s %8s %8s %8s %8s %8s %9s\n", "A", "H1 amp", "H2", "H3", "H4", "H5", "H6", "H7",
                    "H8", "THD%", "DC");
        for (float a : amplitudes) {
            const Measurement m = analyse(c, a);
            std::printf("%6.2f %8.4f", a, m.h1);
            for (size_t h = 1; h < 8; ++h) {
                const double db = h < m.levelsDb.size() ? m.levelsDb[h] : dawlab::kMinusInfinityDb;
                if (db <= -99.0)
                    std::printf(" %8s", "<-99");
                else
                    std::printf(" %8.1f", db);
            }
            std::printf(" %8.2f %+9.4f\n", m.thd, m.dc);
        }
        std::printf("\n");
    }
    std::printf("(All dB values are relative to the fundamental H1; the even harmonics and DC of Tanh / Cubic /\n"
                "  Arctan should be 0 (printed as <-99); H2, H4 and DC of Asymmetric are not 0 -- that is the\n"
                "  tube-like even-harmonic content.)\n\n");

    // The even part of Asymmetric is g(x) = (f(x) + f(-x))/2 = t^3 / (2(1 + t^2)), t = tanh|x| (README 2.4).
    // g >= 0, so the DC is always positive; for small signals g ~ |x|^3/2, and the mean of |sin(theta)|^3
    // is 4/(3*pi), giving DC ~ 2A^3/(3*pi).
    std::printf("DC of Asymmetric: small-signal approximation 2A^3/(3*pi) vs measured\n");
    std::printf("%6s %12s %12s\n", "A", "approx", "measured");
    for (float a : {0.1f, 0.25f, 0.5f}) {
        const double approx = 2.0 * a * a * a / (3.0 * dawlab::kPi);
        std::printf("%6.2f %12.5f %12.5f\n", a, approx, analyse(SaturationCurve::Asymmetric, a).dc);
    }
    std::printf("(The smaller A is, the more accurate; for larger A the higher-order terms of tanh make the DC smaller than the approximation)\n\n");

    // ---- 3. Cubic: theory vs measurement -----------------------------------------------------
    std::printf("Cubic (y = x - x^3/3), hand derivation vs measurement:\n");
    std::printf("  x = A sin(theta) -> y = (A - A^3/4) sin(theta) + (A^3/12) sin(3*theta)\n");
    std::printf("%6s %12s %12s %12s %12s\n", "A", "theory H3 dB", "meas. H3 dB", "theory THD%", "meas. THD%");
    double worst = 0.0;
    for (float a : {0.25f, 0.5f, 1.0f}) {
        const double A = a;
        const double h1 = A - A * A * A / 4.0;
        const double h3 = A * A * A / 12.0;
        const double theoryDb = dawlab::gainToDb(h3 / h1);
        const double theoryThd = 100.0 * h3 / h1;
        const Measurement m = analyse(SaturationCurve::Cubic, a);
        std::printf("%6.2f %12.2f %12.2f %12.2f %12.2f\n", A, theoryDb, m.levelsDb[2], theoryThd, m.thd);
        worst = std::max(worst, std::abs(theoryDb - m.levelsDb[2]));
    }
    std::printf("(For |x| <= 1 Cubic has only a 3rd harmonic, so THD is just H3/H1; the 5th, 7th, ... appear only once A > 1 reaches the clipped region)\n\n");

    // ---- 4. Drive-sweep WAVs -----------------------------------------------------------------
    const double seconds = 4.0;
    for (SaturationCurve c : kCurves) {
        dawlab::AudioBuffer buffer = dawlab::makeSine(220.0, kSampleRate, seconds, 0.5f, 2);
        Saturation sat;
        sat.setOversamplingFactorLog2(2); // 4x: 220 Hz produces many high harmonics; avoid aliasing
        sat.prepare(kSampleRate, kBlockSize, 2);
        sat.setCurve(c);
        sat.setMix(1.0f);
        sat.setOutputGainDb(-3.0f);
        dawlab::AudioBlock whole(buffer);
        for (int start = 0; start < buffer.getNumSamples(); start += kBlockSize) {
            const int len = std::min(kBlockSize, buffer.getNumSamples() - start);
            // Amplitude before the curve is 0.5 x drive; drive sweeps linearly from 0 dB to +24 dB (peak 0.5 -> 7.9)
            sat.setDriveDb(static_cast<float>(24.0 * start / buffer.getNumSamples()));
            sat.process(whole.getSubBlock(start, len));
        }
        std::string name = dawlab::dsp::saturationCurveName(c);
        for (char& ch : name)
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        dawlab::writeWav(dawlab::outputPath("l06_drive_sweep_" + name + ".wav"), buffer, kSampleRate);
        std::printf("Wrote dawlab_output/l06_drive_sweep_%s.wav (peak %.3f)\n", name.c_str(), dawlab::peakAbs(buffer));
    }

    std::printf("\nLargest Cubic theory-vs-measurement difference: %.3f dB%s\n", worst, worst < 0.1 ? " (match)" : " ([FAIL] mismatch!)");
    return worst < 0.1 ? 0 : 1;
}
