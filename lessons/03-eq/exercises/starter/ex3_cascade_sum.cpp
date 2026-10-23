// Exercise 3 (starter code): verify that the dB responses add when two peaks are cascaded
//
// Goals:
//   1. Complete Cascade2 (two Biquads in cascade: y = H2(H1(x))).
//   2. At several frequencies, verify that the dB of |H1·H2| = the dB of |H1| + the dB of |H2|, and that the measured value (measureGainDb) agrees too.
//   3. Reverse the order of the cascade and confirm that the frequency response does not change.
// When run, the self-check prints FAIL until you complete the TODOs.

#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>

#include <cstdio>

using namespace dawlab;
using namespace dawlab::dsp;

namespace {

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
    float processSample(int ch, float x) noexcept {
        (void)ch;
        return x; // TODO: pass through a_ first, then b_
    }

private:
    Biquad a_, b_;
};

} // namespace

int main() {
    constexpr double sr = 48000.0;
    std::printf("=== Chapter 03 Exercise 3: cascade = dB addition ===\n\n");

    const BiquadCoefficients p1 = BiquadCoefficients::makePeak(sr, 500.0, 1.0, 6.0);
    const BiquadCoefficients p2 = BiquadCoefficients::makePeak(sr, 2000.0, 2.0, -4.0);
    Cascade2 forward, backward;
    forward.prepare(sr, 512, 1);
    backward.prepare(sr, 512, 1);
    forward.setCoefficients(p1, p2);
    backward.setCoefficients(p2, p1);

    std::printf("  freq(Hz)   H1(dB)   H2(dB)   H1+H2   |H1·H2|   measured p1->p2   measured p2->p1\n");
    for (double f : {100.0, 250.0, 500.0, 1000.0, 1414.0, 2000.0, 3000.0, 8000.0}) {
        const double d1 = p1.magnitudeDb(f, sr), d2 = p2.magnitudeDb(f, sr);
        // TODO: compute the dB of |H1·H2| from p1.response(f, sr) * p2.response(f, sr)
        const double product = 0.0;
        forward.reset();
        backward.reset();
        const double mf = measureGainDb([&](float x) { return forward.processSample(0, x); }, f, sr);
        const double mb = measureGainDb([&](float x) { return backward.processSample(0, x); }, f, sr);
        std::printf("  %7.0f   %+6.2f   %+6.2f   %+6.2f   %+6.2f     %+6.2f        %+6.2f\n", f, d1, d2, d1 + d2, product,
                    mf, mb);
        DAWLAB_CHECK_NEAR(product, d1 + d2, 1e-9);
        DAWLAB_CHECK_NEAR(mf, d1 + d2, 0.05);
        DAWLAB_CHECK_NEAR(mb, mf, 0.02);
    }

    // TODO (bonus): cascade two +6 dB Q=√2 @ 1 kHz peaks; the center should be +12 dB, and 1414 Hz should be +6 dB
    return dawlab::test::summary("l03_ex3_starter");
}
