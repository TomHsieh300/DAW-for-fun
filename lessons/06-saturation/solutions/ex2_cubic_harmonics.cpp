// Exercise 2 solution: derive by hand the harmonics that x^2 and x^3 produce from a sine, then verify
// them with Goertzel measurements.
//
// By hand (x = A sin(theta), theta = 2*pi*f0*t):
//   x^2 = A^2 sin^2(theta) = (A^2/2) - (A^2/2) cos(2*theta)
//        -> DC = A^2/2, 2nd-harmonic amplitude A^2/2, no fundamental
//   x^3 = A^3 sin^3(theta) = (3A^3/4) sin(theta) - (A^3/4) sin(3*theta)
//        -> fundamental amplitude 3A^3/4, 3rd-harmonic amplitude A^3/4 (H3/H1 = 1/3 -> -9.54 dB, independent of A!)
//   cubic soft clip y = x - x^3/3 (|x| <= 1)
//        -> fundamental A - A^3/4, 3rd harmonic A^3/12; H3/H1 = A^2/(12 - 3A^2)
//          A = 0.5: H1 = 0.46875, H3 = 0.0104167 -> -33.06 dB, THD 2.222 %
//          A = 1.0: H1 = 0.75, H3 = 0.0833 -> -19.08 dB, THD 11.11 %
//   y = x + k x^2 (the simplest asymmetric curve):
//        -> DC = kA^2/2, H1 = A, H2 = kA^2/2 -> H2/H1 = kA/2 (k = 0.5, A = 0.5 -> 0.125 -> -18.06 dB)
//
// Run: l06_ex2_solution

#include <dawlab/Common.h>
#include <dawlab/dsp/Saturation.h>

#include <cstdio>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr double kF0 = 100.0;

double meanOf(const float* x, int n) {
    double s = 0.0;
    for (int i = 0; i < n; ++i)
        s += x[i];
    return s / n;
}

// Feed a sine of amplitude A through f() and return the processed buffer
template <typename Fn>
dawlab::AudioBuffer shaped(float amplitude, Fn&& f) {
    dawlab::AudioBuffer b = dawlab::makeSine(kF0, kSampleRate, 1.0, amplitude, 1);
    float* d = b.getWritePointer(0);
    for (int i = 0; i < b.getNumSamples(); ++i)
        d[i] = f(d[i]);
    return b;
}

double amp(const dawlab::AudioBuffer& b, double freq) {
    return dawlab::goertzelAmplitude(b.getReadPointer(0), b.getNumSamples(), freq, kSampleRate);
}

} // namespace

int main() {
    // ---- x^2 -----------------------------------------------------------------------------
    {
        const float A = 0.8f;
        const auto b = shaped(A, [](float x) { return x * x; });
        const double dc = meanOf(b.getReadPointer(0), b.getNumSamples());
        std::printf("x^2, A = %.1f: DC %.4f (theory %.4f), H1 %.5f (theory 0), H2 %.4f (theory %.4f)\n", A, dc,
                    A * A / 2.0, amp(b, kF0), amp(b, 2 * kF0), A * A / 2.0);
        DAWLAB_CHECK_NEAR(dc, A * A / 2.0, 1e-3);
        DAWLAB_CHECK_NEAR(amp(b, 2 * kF0), A * A / 2.0, 2e-3);
        DAWLAB_CHECK(amp(b, kF0) < 1e-3);
        DAWLAB_CHECK(amp(b, 3 * kF0) < 1e-3);
    }
    // ---- x^3 -----------------------------------------------------------------------------
    for (float A : {0.5f, 1.0f}) {
        const auto b = shaped(A, [](float x) { return x * x * x; });
        const double h1 = amp(b, kF0), h3 = amp(b, 3 * kF0);
        std::printf("x^3, A = %.1f: H1 %.5f (theory %.5f), H3 %.5f (theory %.5f), H3/H1 = %.2f dB (theory -9.54)\n", A, h1,
                    0.75 * A * A * A, h3, 0.25 * A * A * A, dawlab::gainToDb(h3 / h1));
        DAWLAB_CHECK_NEAR(h1, 0.75 * A * A * A, 2e-3);
        DAWLAB_CHECK_NEAR(h3, 0.25 * A * A * A, 2e-3);
        DAWLAB_CHECK_NEAR(dawlab::gainToDb(h3 / h1), -9.542, 0.05);
        DAWLAB_CHECK(amp(b, 2 * kF0) < 1e-3); // odd function -> no even harmonics
        DAWLAB_CHECK(amp(b, 5 * kF0) < 1e-3); // a 3rd-degree polynomial produces at most the 3rd harmonic
    }
    // ---- cubic soft clip (via Saturation::shape) -------------------------------------------
    for (float A : {0.5f, 1.0f}) {
        const auto b = shaped(A, [](float x) {
            return dawlab::dsp::Saturation::shape(dawlab::dsp::SaturationCurve::Cubic, x);
        });
        const double theoryH1 = A - A * A * A / 4.0, theoryH3 = A * A * A / 12.0;
        const double thd = dawlab::thdPercent(b.getReadPointer(0), b.getNumSamples(), kF0, kSampleRate, 10);
        std::printf("cubic, A = %.1f: H3/H1 = %.2f dB (theory %.2f), THD %.3f %% (theory %.3f %%)\n", A,
                    dawlab::gainToDb(amp(b, 3 * kF0) / amp(b, kF0)), dawlab::gainToDb(theoryH3 / theoryH1), thd,
                    100.0 * theoryH3 / theoryH1);
        DAWLAB_CHECK_NEAR(amp(b, kF0), theoryH1, 2e-3);
        DAWLAB_CHECK_NEAR(dawlab::gainToDb(amp(b, 3 * kF0) / amp(b, kF0)), dawlab::gainToDb(theoryH3 / theoryH1), 0.05);
        DAWLAB_CHECK_NEAR(thd, 100.0 * theoryH3 / theoryH1, 0.02);
    }
    // ---- x + k x^2 -------------------------------------------------------------------------
    {
        const float A = 0.5f, k = 0.5f;
        const auto b = shaped(A, [k](float x) { return x + k * x * x; });
        const double ratioDb = dawlab::gainToDb(amp(b, 2 * kF0) / amp(b, kF0));
        std::printf("x + 0.5x^2, A = 0.5: H2/H1 = %.2f dB (theory %.2f), DC %.4f (theory %.4f)\n", ratioDb,
                    dawlab::gainToDb(k * A / 2.0), meanOf(b.getReadPointer(0), b.getNumSamples()), k * A * A / 2.0);
        DAWLAB_CHECK_NEAR(ratioDb, dawlab::gainToDb(k * A / 2.0), 0.05);
        DAWLAB_CHECK_NEAR(meanOf(b.getReadPointer(0), b.getNumSamples()), k * A * A / 2.0, 1e-3);
    }
    return dawlab::test::summary("l06_ex2");
}
