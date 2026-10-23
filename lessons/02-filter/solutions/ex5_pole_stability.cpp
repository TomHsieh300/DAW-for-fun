// Exercise 5 solution: compute the poles from biquad coefficients, judge stability, and verify it with an actual impulse.
//
// Denominator 1 + a1 z^-1 + a2 z^-2 = 0  <=>  z² + a1 z + a2 = 0
//     p = (-a1 ± sqrt(a1² - 4 a2)) / 2
// Stable <=> every pole lies inside the unit circle (|p| < 1).
// When the discriminant is < 0 the poles are a complex-conjugate pair: radius r = sqrt(a2), angle θ = acos(-a1 / (2r)),
//   - θ corresponds to the "resonant frequency" f = θ fs / (2π)
//   - r sets the decay rate: the envelope of the impulse response ∝ r^n, with time constant τ = -1 / ln(r) samples
// The stability condition of a second-order system can also be written as the "triangle condition": |a2| < 1 and |a1| < 1 + a2.
#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>

#include <complex>
#include <cstdio>
#include <limits>
#include <utility>

using namespace dawlab;
using dawlab::dsp::Biquad;
using dawlab::dsp::BiquadCoefficients;

constexpr double kSampleRate = 48000.0;

using Poles = std::pair<std::complex<double>, std::complex<double>>;

// Compute the poles ourselves with the quadratic formula (without calling BiquadCoefficients::poles(); we check against it at the end)
static Poles computePoles(double a1, double a2) {
    const std::complex<double> disc = std::sqrt(std::complex<double>(a1 * a1 - 4.0 * a2, 0.0));
    return {(-a1 + disc) * 0.5, (-a1 - disc) * 0.5};
}

static bool isStable(double a1, double a2) {
    const Poles p = computePoles(a1, a2);
    return std::abs(p.first) < 1.0 && std::abs(p.second) < 1.0;
}

// Feed an impulse into the filter for n samples and return "peak of the last 10% / peak of the first 10%":
// stable -> approaches 0; marginal -> about 1; unstable -> much greater than 1 (or inf)
static double tailToHeadRatio(const BiquadCoefficients& c, int n) {
    Biquad f;
    f.prepare(kSampleRate, 512, 1);
    f.setCoefficients(c);
    double head = 0.0, tail = 0.0;
    for (int i = 0; i < n; ++i) {
        const double y = f.processSample(0, i == 0 ? 1.0f : 0.0f);
        if (!std::isfinite(y))
            return std::numeric_limits<double>::infinity();
        if (i < n / 10)
            head = std::max(head, std::abs(y));
        if (i >= n - n / 10)
            tail = std::max(tail, std::abs(y));
    }
    return tail / head;
}

int main() {
    struct Case {
        const char* name;
        BiquadCoefficients c;
        bool expectStable;
    };
    const auto raw = [](double a1, double a2) {
        BiquadCoefficients c;
        c.b0 = 1.0;
        c.a1 = a1;
        c.a2 = a2;
        return c;
    };
    const Case cases[] = {
        {"LPF 1 kHz Q=0.707", BiquadCoefficients::makeLowpass(kSampleRate, 1000.0, 0.7071), true},
        {"LPF 1 kHz Q=8", BiquadCoefficients::makeLowpass(kSampleRate, 1000.0, 8.0), true},
        {"a1=-1.8  a2=0.81 (double real pole 0.9)", raw(-1.8, 0.81), true},
        {"a1=-1.9  a2=1.0  (|p| = 1, marginal)", raw(-1.9, 1.0), false},
        {"a1=-2.1  a2=1.1025 (p = 1.05, unstable)", raw(-2.1, 1.1025), false},
        {"a1=0.5   a2=-0.6  (real poles 0.564, -1.064)", raw(0.5, -0.6), false},
    };

    std::printf("%-44s %22s %22s %8s %10s %10s\n", "case", "pole 1", "pole 2", "|p|max", "stable?", "tail/head");
    for (const Case& cs : cases) {
        const Poles p = computePoles(cs.c.a1, cs.c.a2);
        const double rmax = std::max(std::abs(p.first), std::abs(p.second));
        const bool stable = isStable(cs.c.a1, cs.c.a2);
        const double ratio = tailToHeadRatio(cs.c, 20000);
        std::printf("%-44s %10.4f%+10.4fj %10.4f%+10.4fj %8.4f %10s %10.3g\n", cs.name, p.first.real(),
                    p.first.imag(), p.second.real(), p.second.imag(), rmax, stable ? "yes" : "NO", ratio);
        DAWLAB_CHECK(stable == cs.expectStable);
        DAWLAB_CHECK(stable == cs.c.isStable()); // agrees with the triangle condition in the header
        // Check against the header's poles() (the order may be reversed, so compare both pairings)
        const auto hp = cs.c.poles();
        const double d1 = std::abs(hp.first - p.first) + std::abs(hp.second - p.second);
        const double d2 = std::abs(hp.first - p.second) + std::abs(hp.second - p.first);
        DAWLAB_CHECK(std::min(d1, d2) < 1e-9);
        // Verify with an impulse: a stable one must decay, an unstable / marginal one does not
        if (cs.expectStable)
            DAWLAB_CHECK(ratio < 1e-3);
        else
            DAWLAB_CHECK(ratio > 0.5);
    }

    // The geometric meaning of conjugate poles: radius and angle -> decay time and resonant frequency (using the Q=8 LPF as the example)
    {
        const BiquadCoefficients c = BiquadCoefficients::makeLowpass(kSampleRate, 1000.0, 8.0);
        const double r = std::sqrt(c.a2);
        const double theta = std::acos(-c.a1 / (2.0 * r));
        const double fRes = theta * kSampleRate / kTwoPi;
        const double tau = -1.0 / std::log(r);
        std::printf("\nLPF 1 kHz Q=8: pole radius r = %.6f, angle θ = %.5f rad -> resonant frequency %.1f Hz, τ = %.0f samples (%.1f ms)\n",
                    r, theta, fRes, tau, samplesToMs(tau, kSampleRate));
        // The frequency corresponding to the angle is close to fc (for high Q the pole angle ~ 2π fc / fs)
        DAWLAB_CHECK_NEAR(fRes, 1000.0, 10.0);
        // Measure the envelope of the impulse response: n-th peak / 0-th peak ~ r^n -> look after 1000 samples
        Biquad f;
        f.prepare(kSampleRate, 512, 1);
        f.setCoefficients(c);
        double early = 0.0, late = 0.0;
        for (int i = 0; i < 2000; ++i) {
            const double y = std::abs(f.processSample(0, i == 0 ? 1.0f : 0.0f));
            if (i < 100)
                early = std::max(early, y);
            if (i >= 1000 && i < 1100)
                late = std::max(late, y);
        }
        const double expectedDecayDb = 20.0 * std::log10(std::pow(r, 1000.0));
        std::printf("Impulse response envelope: decays %.2f dB after 1000 samples (theory 20 log10(r^1000) = %.2f dB)\n",
                    gainToDb(late / early), expectedDecayDb);
        DAWLAB_CHECK_NEAR(gainToDb(late / early), expectedDecayDb, 1.0);
    }

    // Triangle condition: scan the (a1, a2) plane; it agrees exactly with the verdict from "computing the poles"
    int agree = 0, total = 0;
    for (int i = -50; i <= 50; ++i)
        for (int j = -30; j <= 30; ++j) {
            const double a1 = i * 0.05, a2 = j * 0.05;
            // Points that fall exactly on the triangle's boundary (|p| = 1) are affected by floating-point error in both methods, so skip them
            if (std::abs(std::abs(a2) - 1.0) < 1e-9 || std::abs(std::abs(a1) - (1.0 + a2)) < 1e-9)
                continue;
            const bool triangle = std::abs(a2) < 1.0 && std::abs(a1) < 1.0 + a2;
            ++total;
            if (triangle == isStable(a1, a2))
                ++agree;
        }
    std::printf("Triangle condition vs. computing the poles directly: %d / %d (a1, a2) points agree\n", agree, total);
    DAWLAB_CHECK(agree == total);

    return dawlab::test::summary("l02_ex5");
}
