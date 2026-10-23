// Exercise 3 (solution): verify that the dB responses add when two peaks are cascaded
//
// A cascade = multiplying the transfer functions of two LTI systems: H(z) = H1(z)·H2(z)
//   |H| = |H1|·|H2|  ->  20 log10|H| = 20 log10|H1| + 20 log10|H2|   (dB addition)
// Multiplication is commutative, so the order of the cascade does not affect the "frequency response" (though it does affect numerical behavior and headroom; see README section 4).
// We verify it in two ways:
//   1. Formula: the magnitudeDb of H1·H2 equals the sum of the individual magnitudeDb values.
//   2. Measurement: actually feed a sine wave through two cascaded Biquads and measure it with measureGainDb.

#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>

#include <cstdio>
#include <vector>

using namespace dawlab;
using namespace dawlab::dsp;

namespace {

// Two Biquads in cascade: y = H2(H1(x))
class Cascade2 {
public:
    void prepare(double sr, int maxBlock, int nch) {
        a_.prepare(sr, maxBlock, nch);
        b_.prepare(sr, maxBlock, nch);
    }
    void reset() noexcept {
        a_.reset();
        b_.reset();
    }
    void setCoefficients(const BiquadCoefficients& first, const BiquadCoefficients& second) noexcept {
        a_.setCoefficients(first);
        b_.setCoefficients(second);
    }
    float processSample(int ch, float x) noexcept { return b_.processSample(ch, a_.processSample(ch, x)); }

private:
    Biquad a_, b_;
};

} // namespace

int main() {
    constexpr double sr = 48000.0;
    std::printf("=== Chapter 03 Exercise 3: cascade = dB addition ===\n\n");

    const BiquadCoefficients p1 = BiquadCoefficients::makePeak(sr, 500.0, 1.0, 6.0);   // +6 dB @ 500 Hz
    const BiquadCoefficients p2 = BiquadCoefficients::makePeak(sr, 2000.0, 2.0, -4.0); // -4 dB @ 2 kHz
    Cascade2 forward, backward;
    forward.prepare(sr, 512, 1);
    backward.prepare(sr, 512, 1);
    forward.setCoefficients(p1, p2);
    backward.setCoefficients(p2, p1); // reversed order

    std::printf("  freq(Hz)   H1(dB)   H2(dB)   H1+H2   |H1·H2|   measured p1->p2   measured p2->p1\n");
    for (double f : {100.0, 250.0, 500.0, 1000.0, 1414.0, 2000.0, 3000.0, 8000.0}) {
        const double d1 = p1.magnitudeDb(f, sr), d2 = p2.magnitudeDb(f, sr);
        const double product = gainToDb(std::abs(p1.response(f, sr) * p2.response(f, sr)));
        forward.reset();
        backward.reset();
        const double mf = measureGainDb([&](float x) { return forward.processSample(0, x); }, f, sr);
        const double mb = measureGainDb([&](float x) { return backward.processSample(0, x); }, f, sr);
        std::printf("  %7.0f   %+6.2f   %+6.2f   %+6.2f   %+6.2f     %+6.2f        %+6.2f\n", f, d1, d2, d1 + d2, product,
                    mf, mb);
        DAWLAB_CHECK_NEAR(product, d1 + d2, 1e-9); // complex multiplication <=> dB addition (a mathematical identity)
        DAWLAB_CHECK_NEAR(mf, d1 + d2, 0.05);      // measurement
        DAWLAB_CHECK_NEAR(mb, mf, 0.02);           // independent of order
    }

    // Stacking two +6 dB bands at the same frequency: the dB value at every frequency exactly doubles (center +12, edge +6),
    // so the position of the "midpoint gain" edge does not change -- on the dB axis the curve shape is simply stretched to twice the height
    const BiquadCoefficients q1 = BiquadCoefficients::makePeak(sr, 1000.0, std::sqrt(2.0), 6.0);
    const double centre = 2.0 * q1.magnitudeDb(1000.0, sr);
    const double edge = 2.0 * q1.magnitudeDb(1000.0 * std::sqrt(2.0), sr);
    std::printf("\n  Two +6 dB Q=sqrt(2) @ 1 kHz peaks in cascade: center %+.2f dB, %+.2f dB at 1414 Hz (a single one gives %+.2f)\n", centre, edge,
                0.5 * edge);
    DAWLAB_CHECK_NEAR(centre, 12.0, 0.01);
    DAWLAB_CHECK_NEAR(edge, 6.0, 0.1); // the dB at the edge also doubles: +3 -> +6, so it is still half "relative to the center" -> the midpoint edge does not change
    // The wrong intuition of adding linear amplitudes: 2x and 2x make 4x (+12 dB); it is not the "addition" 2+2 = 4 -- it is multiplication
    DAWLAB_CHECK_NEAR(dbToGain(6.0) * dbToGain(6.0), dbToGain(12.0), 1e-6);

    // A boost cascaded with a cut at the same f and Q = complete cancellation: for the RBJ peak, H_{-g}(z) = 1 / H_{+g}(z)
    // (the numerator and denominator of the analog prototype, s²+s·A/Q+1 and s²+s/(A·Q)+1, swap exactly, and this still holds after the bilinear transform)
    const BiquadCoefficients boost = BiquadCoefficients::makePeak(sr, 1000.0, 1.0, 6.0);
    const BiquadCoefficients cut = BiquadCoefficients::makePeak(sr, 1000.0, 1.0, -6.0);
    double worst = 0.0;
    for (double f = 20.0; f < 20000.0; f *= 1.1)
        worst = std::max(worst, std::abs(gainToDb(std::abs(boost.response(f, sr) * cut.response(f, sr)))));
    std::printf("  +6 dB and -6 dB (1 kHz, Q=1) in cascade: maximum deviation over 20 Hz-20 kHz %.2e dB\n", worst);
    DAWLAB_CHECK(worst < 1e-9);

    return dawlab::test::summary("l03_ex3");
}
