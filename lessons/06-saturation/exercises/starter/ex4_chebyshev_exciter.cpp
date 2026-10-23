// Exercise 4 (**): Chebyshev polynomial harmonic exciter
//
// Goal: write an exciter y = x + sum_{n=2..5} g_n T_n(x) so that, for a sine of amplitude 1, the n-th
//       harmonic comes out at exactly g_n relative to the fundamental.
//
// Hints:
//   - T_n(cos(theta)) = cos(n*theta)
//   - Recurrence: T_0 = 1, T_1 = x, T_{n+1}(x) = 2x T_n(x) - T_{n-1}(x)
//   - T_n grows explosively for |x| > 1, so in processSample() clamp x to [-1, 1] before evaluating the polynomial
//   - Think about it: with an input amplitude of 0.5, what are the H2 amplitude and the DC? (part 3 of the self-check)
//
// Until all TODOs are done, running this prints FAIL (that is expected).
// Run: l06_ex4_starter

#include <dawlab/Common.h>

#include <algorithm>
#include <array>
#include <cstdio>

namespace {

// TODO 1: implement T_n(x) with the recurrence
float chebyshev(int n, float x) noexcept {
    (void)n;
    (void)x;
    return 0.0f;
}

class ChebyshevExciter {
public:
    static constexpr int kMaxHarmonic = 5;

    void setHarmonicGain(int harmonic, float gain) noexcept {
        if (harmonic >= 2 && harmonic <= kMaxHarmonic)
            gains_[static_cast<size_t>(harmonic)] = gain;
    }

    // TODO 2: y = x + sum g_n T_n(clamp(x, -1, 1))
    float processSample(float x) const noexcept { return x; }

private:
    std::array<float, kMaxHarmonic + 1> gains_{};
};

constexpr double kSampleRate = 48000.0;
constexpr double kF0 = 1000.0;

} // namespace

int main() {
    // 1. The identity
    for (int n = 0; n <= 5; ++n) {
        double worst = 0.0;
        for (double th = 0.0; th < dawlab::kTwoPi; th += 0.01)
            worst = std::max(worst, std::abs(chebyshev(n, static_cast<float>(std::cos(th))) - std::cos(n * th)));
        DAWLAB_CHECK(worst < 1e-5);
    }

    // 2. Amplitude 1: H2 = -20 dB, H3 = -26.02 dB, H5 = -33.98 dB, no H4 / H6
    ChebyshevExciter ex;
    const float g2 = 0.1f, g3 = 0.05f, g5 = 0.02f;
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
        std::printf("Amplitude 1.0: H2 %.2f dB, H3 %.2f, H4 %.2f, H5 %.2f, H6 %.2f\n", lv[1], lv[2], lv[3], lv[4], lv[5]);
        DAWLAB_CHECK_NEAR(lv[1], dawlab::gainToDb(g2), 0.1);
        DAWLAB_CHECK_NEAR(lv[2], dawlab::gainToDb(g3), 0.1);
        DAWLAB_CHECK_NEAR(lv[4], dawlab::gainToDb(g5), 0.1);
        DAWLAB_CHECK(lv[3] < -80.0);
        DAWLAB_CHECK(lv[5] < -80.0);
    }
    // 3. Amplitude 0.5: DC and H2 amplitude (TODO 3: replace the two 0.0 values below with your hand-derived theory)
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
        const double expectedDc = 0.0; // TODO 3a
        const double expectedH2 = 0.0; // TODO 3b
        std::printf("Amplitude 0.5: DC %.4f (your theory %.4f), H2 %.4f (your theory %.4f)\n", mean, expectedDc, h2, expectedH2);
        DAWLAB_CHECK_NEAR(mean, expectedDc, 1e-3);
        DAWLAB_CHECK_NEAR(h2, expectedH2, 1e-3);
        (void)A;
    }
    return dawlab::test::summary("l06_ex4_starter");
}
