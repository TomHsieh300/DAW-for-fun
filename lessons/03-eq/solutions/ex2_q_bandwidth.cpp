// Exercise 2 (solution): Q <-> bandwidth (octaves) conversion, and using a Biquad to verify the gain at the "bandwidth edges"
//
// Formulas (derived in README section 2.4):
//   BW = (2 / ln 2) · asinh(1 / (2Q))           Q -> octaves
//   Q  = 2^(BW/2) / (2^BW - 1)                   octaves -> Q
//   Bandwidth edges: f_{1,2} = f0 · ( sqrt(1 + 1/(4Q²)) ∓ 1/(2Q) ), f1·f2 = f0², f2 - f1 = f0 / Q
// The bandwidth of an RBJ peak is defined at the "midpoint gain" (gainDb / 2); for bandpass / notch it is defined at -3 dB.

#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>

#include <cstdio>

using namespace dawlab;
using namespace dawlab::dsp;

namespace {

double qToBandwidthOctaves(double q) { return 2.0 / std::log(2.0) * std::asinh(1.0 / (2.0 * q)); }

double bandwidthOctavesToQ(double bw) {
    const double p = std::pow(2.0, bw);
    return std::sqrt(p) / (p - 1.0);
}

void bandEdges(double f0, double q, double& fLow, double& fHigh) {
    const double r = std::sqrt(1.0 + 1.0 / (4.0 * q * q));
    fLow = f0 * (r - 1.0 / (2.0 * q));
    fHigh = f0 * (r + 1.0 / (2.0 * q));
}

} // namespace

int main() {
    constexpr double sr = 48000.0;
    std::printf("=== Chapter 03 Exercise 2: Q <-> bandwidth ===\n\n");

    std::printf("  Q -> bandwidth(octave)\n");
    for (double q : {0.5, 0.707, 1.0, 1.4142, 2.0, 4.3185, 10.0})
        std::printf("    Q = %6.3f  ->  BW = %.3f oct\n", q, qToBandwidthOctaves(q));
    std::printf("  bandwidth(octave) -> Q\n");
    for (double bw : {3.0, 2.0, 1.0, 0.5, 1.0 / 3.0, 1.0 / 6.0})
        std::printf("    BW = %.3f oct  ->  Q = %.4f\n", bw, bandwidthOctavesToQ(bw));

    // ---- Self-check of the formulas ----
    DAWLAB_CHECK_NEAR(bandwidthOctavesToQ(1.0), std::sqrt(2.0), 1e-9); // 1 octave is exactly Q = √2
    DAWLAB_CHECK_NEAR(bandwidthOctavesToQ(2.0), 2.0 / 3.0, 1e-9);       // 2 octaves: 2/(4-1)
    DAWLAB_CHECK_NEAR(bandwidthOctavesToQ(1.0 / 3.0), 4.3185, 5e-4);   // 1/3 octave (common in graphic EQs)
    DAWLAB_CHECK_NEAR(qToBandwidthOctaves(std::sqrt(2.0)), 1.0, 1e-9);
    DAWLAB_CHECK_NEAR(qToBandwidthOctaves(0.707), 1.9002, 5e-4);
    for (double q : {0.3, 0.707, 1.0, 2.5, 12.0}) // converting back and forth must return to the starting point
        DAWLAB_CHECK_NEAR(bandwidthOctavesToQ(qToBandwidthOctaves(q)), q, 1e-9);

    // ---- Bandwidth edges ----
    double f1 = 0.0, f2 = 0.0;
    bandEdges(1000.0, std::sqrt(2.0), f1, f2);
    std::printf("\n  f0 = 1000 Hz, Q = sqrt(2) (1 octave): edges = %.1f Hz / %.1f Hz\n", f1, f2);
    DAWLAB_CHECK_NEAR(f1, 1000.0 / std::sqrt(2.0), 0.01); // 707.1
    DAWLAB_CHECK_NEAR(f2, 1000.0 * std::sqrt(2.0), 0.01); // 1414.2
    DAWLAB_CHECK_NEAR(f1 * f2, 1000.0 * 1000.0, 1e-6);    // geometric symmetry: f1·f2 = f0²
    DAWLAB_CHECK_NEAR(f2 - f1, 1000.0 / std::sqrt(2.0), 1e-6); // f2 − f1 = f0 / Q

    // ---- Verify with a real Biquad: at the edges the peak is at the "midpoint gain" and the bandpass is at -3 dB ----
    const double gainDb = 12.0;
    const BiquadCoefficients peak = BiquadCoefficients::makePeak(sr, 1000.0, std::sqrt(2.0), gainDb);
    const BiquadCoefficients bp = BiquadCoefficients::makeBandpass(sr, 1000.0, std::sqrt(2.0));
    std::printf("  peak +%.0f dB: |H| @ %.1f = %+.3f dB, @ 1000 = %+.3f dB, @ %.1f = %+.3f dB\n", gainDb, f1,
                peak.magnitudeDb(f1, sr), peak.magnitudeDb(1000.0, sr), f2, peak.magnitudeDb(f2, sr));
    std::printf("  bandpass     : |H| @ %.1f = %+.3f dB, @ 1000 = %+.3f dB, @ %.1f = %+.3f dB\n", f1,
                bp.magnitudeDb(f1, sr), bp.magnitudeDb(1000.0, sr), f2, bp.magnitudeDb(f2, sr));
    DAWLAB_CHECK_NEAR(peak.magnitudeDb(1000.0, sr), gainDb, 0.01);
    DAWLAB_CHECK_NEAR(peak.magnitudeDb(f1, sr), gainDb * 0.5, 0.1); // midpoint gain (the bilinear error at 1 kHz is < 0.05 dB)
    DAWLAB_CHECK_NEAR(peak.magnitudeDb(f2, sr), gainDb * 0.5, 0.1);
    DAWLAB_CHECK_NEAR(bp.magnitudeDb(f1, sr), -3.01, 0.1);
    DAWLAB_CHECK_NEAR(bp.magnitudeDb(f2, sr), -3.01, 0.1);

    // Actually feed a sine wave through the filter and measure it (not just evaluate the formula)
    Biquad filter;
    filter.prepare(sr, 512, 1);
    filter.setCoefficients(peak);
    const double measured = measureGainDb([&](float x) { return filter.processSample(0, x); }, f2, sr);
    std::printf("  measured peak @ %.1f Hz = %+.3f dB\n", f2, measured);
    DAWLAB_CHECK_NEAR(measured, gainDb * 0.5, 0.1);

    // ---- Cross-check for the hand calculations of Exercise 1 (calculation problems) ----
    std::printf("\n  Exercise 1 cross-check:\n");
    std::printf("    A(+6 dB) = %.4f, A(-12 dB) = %.4f\n", std::pow(10.0, 6.0 / 40.0), std::pow(10.0, -12.0 / 40.0));
    DAWLAB_CHECK_NEAR(std::pow(10.0, 6.0 / 40.0), 1.4125, 1e-4);
    DAWLAB_CHECK_NEAR(std::pow(10.0, -12.0 / 40.0), 0.5012, 1e-4);
    std::printf("    BW = 2/3 oct -> Q = %.4f; Q = 2 -> BW = %.4f oct\n", bandwidthOctavesToQ(2.0 / 3.0),
                qToBandwidthOctaves(2.0));
    DAWLAB_CHECK_NEAR(bandwidthOctavesToQ(2.0 / 3.0), 2.1449, 1e-4);
    DAWLAB_CHECK_NEAR(qToBandwidthOctaves(2.0), 0.7140, 1e-4);
    // fs = 48 kHz, f0 = 12 kHz (ω0 = π/2: cos = 0, sin = 1), Q = 1, a +6 dB peak; the hand-calculated normalized coefficients
    const BiquadCoefficients hand = BiquadCoefficients::makePeak(sr, 12000.0, 1.0, 6.0);
    // cos(π/2) is 6e-17 rather than 0 in double, which would print as "-0.00000"; zero out such rounding errors for display
    auto tidy = [](double v) { return std::abs(v) < 1e-12 ? 0.0 : v; };
    std::printf("    peak 12 kHz: b0 = %.5f b1 = %.5f b2 = %.5f a1 = %.5f a2 = %.5f\n", hand.b0, tidy(hand.b1), hand.b2,
                tidy(hand.a1), hand.a2);
    DAWLAB_CHECK_NEAR(hand.b0, 1.26019, 1e-4);
    DAWLAB_CHECK_NEAR(hand.b1, 0.0, 1e-9);
    DAWLAB_CHECK_NEAR(hand.b2, 0.21694, 1e-4);
    DAWLAB_CHECK_NEAR(hand.a1, 0.0, 1e-9);
    DAWLAB_CHECK_NEAR(hand.a2, 0.47713, 1e-4);
    DAWLAB_CHECK_NEAR(hand.magnitudeDb(12000.0, sr), 6.0, 1e-6); // center = A² = +6 dB
    DAWLAB_CHECK_NEAR(hand.magnitudeDb(0.0, sr), 0.0, 1e-9);     // DC = 0 dB

    return dawlab::test::summary("l03_ex2");
}
