// Exercise 4 solution: Chebyshev polynomial harmonic exciter - "dial in" exactly the harmonics you want.
//
// Key identity: T_n(cos(theta)) = cos(n*theta).
// So a sine of amplitude 1 fed into T_n comes out as a "pure" n-th harmonic with nothing else.
// Recurrence: T_0 = 1, T_1 = x, T_{n+1}(x) = 2x T_n(x) - T_{n-1}(x)
//   T_2 = 2x^2 - 1, T_3 = 4x^3 - 3x, T_4 = 8x^4 - 8x^2 + 1, T_5 = 16x^5 - 20x^3 + 5x
//
// The exciter: y = x + sum_{n=2..5} g_n T_n(x)
//   - With input amplitude 1: H1 = 1, H_n = |g_n| -> to get H2 = -20 dB, set g_2 = 0.1.
//   - With input amplitude A < 1 it is no longer exact: T_2(A cos(theta)) = A^2 cos(2*theta) + (A^2 - 1)
//       -> the H2 amplitude becomes g_2 A^2, and there is an extra DC of g_2 (A^2 - 1) (which is why a DC
//          blocker should follow in practice).
//     In general the n-th harmonic of T_n(A cos(theta)) has amplitude A^n, and components at orders
//     n-2, n-4, ... leak out as well.
//   - The polynomial degree N sets the highest harmonic = N -> you know exactly how much oversampling
//     is needed to avoid aliasing (a factor of N is enough).
//
// Run: l06_ex4_solution

#include <dawlab/Common.h>

#include <algorithm>
#include <array>
#include <cstdio>

namespace {

// T_n(x): computed by recurrence, no lookup table
float chebyshev(int n, float x) noexcept {
    if (n == 0)
        return 1.0f;
    float tPrev = 1.0f, t = x;
    for (int k = 1; k < n; ++k) {
        const float next = 2.0f * x * t - tPrev;
        tPrev = t;
        t = next;
    }
    return t;
}

class ChebyshevExciter {
public:
    static constexpr int kMaxHarmonic = 5;

    // harmonic = 2..5; gain = amplitude of that harmonic relative to the fundamental (linear) for an input amplitude of 1
    void setHarmonicGain(int harmonic, float gain) noexcept {
        if (harmonic >= 2 && harmonic <= kMaxHarmonic)
            gains_[static_cast<size_t>(harmonic)] = gain;
    }

    float processSample(float x) const noexcept {
        // Limit x to [-1, 1]: Chebyshev polynomials grow explosively for |x| > 1 (T_5(2) = 362)
        const float xc = std::clamp(x, -1.0f, 1.0f);
        float y = x;
        // Run the same recurrence upward and add each order on the way (cheaper than calling chebyshev(n, x) four times)
        float tPrev = 1.0f, t = xc;
        for (int n = 2; n <= kMaxHarmonic; ++n) {
            const float next = 2.0f * xc * t - tPrev;
            tPrev = t;
            t = next;
            y += gains_[static_cast<size_t>(n)] * t;
        }
        return y;
    }

private:
    std::array<float, kMaxHarmonic + 1> gains_{};
};

constexpr double kSampleRate = 48000.0;
constexpr double kF0 = 1000.0;

} // namespace

int main() {
    // ---- 1. The identity for T_n -------------------------------------------------------------
    for (int n = 0; n <= 5; ++n) {
        double worst = 0.0;
        for (double th = 0.0; th < dawlab::kTwoPi; th += 0.01)
            worst = std::max(worst, std::abs(chebyshev(n, static_cast<float>(std::cos(th))) - std::cos(n * th)));
        DAWLAB_CHECK(worst < 1e-5);
    }
    DAWLAB_CHECK_NEAR(chebyshev(3, 0.5f), 4 * 0.125 - 1.5, 1e-6);   // 4x^3 - 3x
    DAWLAB_CHECK_NEAR(chebyshev(4, 0.5f), 8 * 0.0625 - 2 + 1, 1e-6); // 8x^4 - 8x^2 + 1

    // ---- 2. Amplitude 1: the harmonics equal the configured values exactly ------------------
    ChebyshevExciter ex;
    const float g2 = 0.1f, g3 = 0.05f, g5 = 0.02f; // -20 dB, -26.02 dB, -33.98 dB; no H4
    ex.setHarmonicGain(2, g2);
    ex.setHarmonicGain(3, g3);
    ex.setHarmonicGain(5, g5);

    auto run = [&](float amplitude) {
        dawlab::AudioBuffer b = dawlab::makeSine(kF0, kSampleRate, 1.0, amplitude, 1);
        float* d = b.getWritePointer(0);
        for (int i = 0; i < b.getNumSamples(); ++i)
            d[i] = ex.processSample(d[i]);
        return b;
    };
    {
        const auto b = run(1.0f);
        const auto lv = dawlab::harmonicLevelsDb(b.getReadPointer(0), b.getNumSamples(), kF0, kSampleRate, 6);
        std::printf("Amplitude 1.0: H2 %.2f dB (target %.2f), H3 %.2f (%.2f), H4 %.2f (none), H5 %.2f (%.2f), H6 %.2f (none)\n",
                    lv[1], dawlab::gainToDb(g2), lv[2], dawlab::gainToDb(g3), lv[3], lv[4], dawlab::gainToDb(g5), lv[5]);
        DAWLAB_CHECK_NEAR(lv[1], dawlab::gainToDb(g2), 0.1);
        DAWLAB_CHECK_NEAR(lv[2], dawlab::gainToDb(g3), 0.1);
        DAWLAB_CHECK_NEAR(lv[4], dawlab::gainToDb(g5), 0.1);
        DAWLAB_CHECK(lv[3] < -80.0); // H4 not set -> absent
        DAWLAB_CHECK(lv[5] < -80.0); // 5th-degree polynomial -> nothing at 6th or above
    }
    // ---- 3. Amplitude 0.5: no longer exact, and a DC appears ------------------------------
    {
        const float A = 0.5f;
        const auto b = run(A);
        const float* d = b.getReadPointer(0);
        const int n = b.getNumSamples();
        double mean = 0.0;
        for (int i = 0; i < n; ++i)
            mean += d[i];
        mean /= n;
        const double h2 = dawlab::goertzelAmplitude(d, n, 2 * kF0, kSampleRate);
        // DC comes only from even orders: T_2 -> g2 (A^2 - 1); T_4 is not used
        std::printf("Amplitude 0.5: DC %.4f (theory g2(A^2-1) = %.4f), H2 amplitude %.4f (theory g2*A^2 = %.4f)\n", mean,
                    g2 * (A * A - 1.0f), h2, g2 * A * A);
        DAWLAB_CHECK_NEAR(mean, g2 * (A * A - 1.0f), 1e-3);
        DAWLAB_CHECK_NEAR(h2, g2 * A * A, 1e-3);
    }
    return dawlab::test::summary("l06_ex4");
}
