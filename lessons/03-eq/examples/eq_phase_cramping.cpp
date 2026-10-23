// Example 4: phase, group delay, Nyquist cramping, and what happens when you "interpolate the coefficients directly"
//
// What it demonstrates:
//   A. A +6 dB@1 kHz Q=1 peak: prints magnitude / phase / group delay to show the minimum-phase behavior of an IIR EQ
//      (around a boost the phase is first positive and then negative, and the group delay is largest at the center).
//   B. Nyquist cramping: a +6 dB@10 kHz Q=1 peak at 44.1 kHz, comparing the analog prototype H(s) with the bilinear digital version H(z).
//      The digital version is forced back to 0 dB at Nyquist (22.05 kHz), and the bell becomes asymmetric.
//   C. Coefficient interpolation: linearly interpolate the coefficients of a "100 Hz peak" and a "10 kHz peak" to the halfway point; the result is not a 1 kHz peak,
//      and both its gain and its Q have changed too -- which is why we smooth the "parameters" rather than the "coefficients".
//   D. When coefficient interpolation becomes unstable:
//      D1. Multiply a 24 dB/oct high cut (4th order) out into a single 4th-order polynomial and then interpolate -> the pole at the midpoint leaves the unit circle.
//      D2. Alternate every sample between two sets of coefficients that are "each stable on their own" -> the time-varying filter diverges anyway.
// How to run: l03_eq_phase_cramping (no arguments)
// Output: console text only.

#include <dawlab/Common.h>
#include <dawlab/dsp/ParametricEq.h>

#include <array>
#include <complex>
#include <cstdio>

using namespace dawlab;
using namespace dawlab::dsp;

namespace {

// Analog prototype of the RBJ peak: H(s) = (s² + s·A/Q + 1) / (s² + s/(A·Q) + 1), s = jΩ, Ω = f / f0
double analogPeakDb(double f, double f0, double q, double gainDb) {
    const double A = std::pow(10.0, gainDb / 40.0);
    const std::complex<double> s(0.0, f / f0);
    const std::complex<double> h = (s * s + s * (A / q) + 1.0) / (s * s + s / (A * q) + 1.0);
    return gainToDb(std::abs(h));
}

// ---- Small helpers for section D ----

// 4th-order IIR: b[0..4], a[0..4] (a[0] = 1), i.e. the polynomials of two biquads multiplied out
struct Poly4 {
    std::array<double, 5> b{}, a{};
};

Poly4 cascade(const BiquadCoefficients& x, const BiquadCoefficients& y) {
    const double xb[3] = {x.b0, x.b1, x.b2}, xa[3] = {1.0, x.a1, x.a2};
    const double yb[3] = {y.b0, y.b1, y.b2}, ya[3] = {1.0, y.a1, y.a2};
    Poly4 p;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            p.b[static_cast<size_t>(i + j)] += xb[i] * yb[j];
            p.a[static_cast<size_t>(i + j)] += xa[i] * ya[j];
        }
    return p;
}

// The largest absolute value of the roots (poles) of 1 + a1 z^-1 + … + a4 z^-4 = 0.
// Find all 4 roots simultaneously with the Durand-Kerner iteration: z_i ← z_i − p(z_i) / Π_{j≠i}(z_i − z_j)
double maxPoleMagnitude(const std::array<double, 5>& a) {
    using cd = std::complex<double>;
    std::array<cd, 4> r;
    for (size_t i = 0; i < 4; ++i)
        r[i] = std::pow(cd(0.4, 0.9), static_cast<double>(i)); // The conventional starting points (off the real axis and distinct from each other)
    for (int iter = 0; iter < 500; ++iter)
        for (size_t i = 0; i < 4; ++i) {
            cd p = 1.0; // z^4 + a1 z^3 + a2 z^2 + a3 z + a4 (Horner)
            for (size_t k = 1; k < 5; ++k)
                p = p * r[i] + a[k];
            cd den = 1.0;
            for (size_t j = 0; j < 4; ++j)
                if (j != i)
                    den *= r[i] - r[j];
            r[i] -= p / den;
        }
    double m = 0.0;
    for (const cd& z : r)
        m = std::max(m, std::abs(z));
    return m;
}

// Run the impulse response of a 4th-order IIR in Direct Form I and return h[n]
double impulseResponseAt(const Poly4& p, int n) {
    std::array<double, 5> x{}, y{};
    double h = 0.0;
    for (int i = 0; i <= n; ++i) {
        for (size_t k = 4; k > 0; --k) {
            x[k] = x[k - 1];
            y[k] = y[k - 1];
        }
        x[0] = (i == 0) ? 1.0 : 0.0;
        h = 0.0;
        for (size_t k = 0; k < 5; ++k)
            h += p.b[k] * x[k];
        for (size_t k = 1; k < 5; ++k)
            h -= p.a[k] * y[k];
        y[0] = h;
    }
    return h;
}

BiquadCoefficients lerpCoefficients(const BiquadCoefficients& x, const BiquadCoefficients& y, double t) {
    BiquadCoefficients c;
    c.b0 = x.b0 + t * (y.b0 - x.b0);
    c.b1 = x.b1 + t * (y.b1 - x.b1);
    c.b2 = x.b2 + t * (y.b2 - x.b2);
    c.a1 = x.a1 + t * (y.a1 - x.a1);
    c.a2 = x.a2 + t * (y.a2 - x.a2);
    return c;
}

} // namespace

int main() {
    std::printf("=== Chapter 03 Example 4: phase / group delay / Nyquist cramping / coefficient interpolation ===\n");

    // ---- A. Minimum phase ----
    {
        constexpr double sr = 48000.0;
        ParametricEq eq;
        eq.prepare(sr, 512, 1);
        eq.setBand(0, {EqBandType::Peak, 1000.0f, 1.0f, 6.0f, true});
        eq.reset();
        std::printf("\n[A] +6 dB @ 1 kHz, Q = 1 (48 kHz)\n");
        std::printf("   freq(Hz)   mag(dB)   phase(deg)   group delay(samples)  (ms)\n");
        for (double f : {100.0, 250.0, 500.0, 707.0, 1000.0, 1414.0, 2000.0, 4000.0, 10000.0}) {
            const double gd = eq.getGroupDelaySamples(f);
            std::printf("   %7.0f    %+6.2f     %+7.2f        %7.2f           %.3f\n", f, eq.getMagnitudeDb(f),
                        eq.getPhaseDegrees(f), gd, samplesToMs(gd, sr));
        }
        std::printf("   -> At the center of the boost the phase = 0; below the center the phase is positive (leading) and above it negative (lagging);\n");
        std::printf("      the group delay is only a few tenths of a ms and only near the boost -- a hallmark of minimum phase.\n");

        // For comparison: change the same band to a -6 dB cut, and the phase flips sign
        eq.setBand(0, {EqBandType::Peak, 1000.0f, 1.0f, -6.0f, true});
        std::printf("   Phase of the -6 dB cut at 707 Hz = %+.2f deg (the opposite sign of the boost)\n", eq.getPhaseDegrees(707.0));
    }

    // ---- B. Nyquist cramping ----
    {
        constexpr double sr = 44100.0;
        const double f0 = 10000.0, q = 1.0, g = 6.0;
        ParametricEq eq;
        eq.prepare(sr, 512, 1);
        eq.setBand(0, {EqBandType::Peak, static_cast<float>(f0), static_cast<float>(q), static_cast<float>(g), true});
        eq.reset();
        std::printf("\n[B] Nyquist cramping: +6 dB @ 10 kHz, Q = 1 (44.1 kHz, Nyquist = 22.05 kHz)\n");
        std::printf("   freq(Hz)   analog(dB)   digital(dB)   diff\n");
        for (double f : {2500.0, 5000.0, 7000.0, 10000.0, 14000.0, 17000.0, 20000.0, 22000.0, 22050.0}) {
            const double a = analogPeakDb(f, f0, q, g), d = eq.getMagnitudeDb(f);
            std::printf("   %7.0f     %+6.2f       %+6.2f      %+6.2f\n", f, a, d, d - a);
        }
        std::printf("   -> The two agree at the 10 kHz center (the bilinear transform pre-warps); at Nyquist the digital version is forced back to 0 dB,\n");
        std::printf("      while the analog one still has %+.2f dB. The right half of the bell is \"squashed\" (cramped).\n", analogPeakDb(sr / 2, f0, q, g));

        // Show it directly: the response of an RBJ peak at z = -1 is always 1
        const BiquadCoefficients c = ParametricEq::makeCoefficients(sr, eq.getBand(0));
        std::printf("   H(z=-1) = (b0-b1+b2)/(1-a1+a2) = %.6f (always 1)\n", (c.b0 - c.b1 + c.b2) / (1.0 - c.a1 + c.a2));
    }

    // ---- C. Coefficient interpolation ----
    {
        constexpr double sr = 48000.0;
        const BiquadCoefficients lo = BiquadCoefficients::makePeak(sr, 100.0, 2.0, 12.0);
        const BiquadCoefficients hi = BiquadCoefficients::makePeak(sr, 10000.0, 2.0, 12.0);
        const BiquadCoefficients mid = BiquadCoefficients::makePeak(sr, 1000.0, 2.0, 12.0); // parameters interpolated to the halfway point
        const BiquadCoefficients lerped = lerpCoefficients(lo, hi, 0.5); // coefficients interpolated to the halfway point

        std::printf("\n[C] Coefficient interpolation vs parameter interpolation: a +12 dB Q=2 peak, the \"midpoint\" between 100 Hz and 10 kHz\n");
        std::printf("   freq(Hz)   param interp (1 kHz peak)   coeff interp\n");
        double lerpedMax = -100.0, lerpedMaxFreq = 0.0;
        for (double f = 20.0; f <= 20000.0; f *= 1.02) {
            const double db = lerped.magnitudeDb(f, sr);
            if (db > lerpedMax) {
                lerpedMax = db;
                lerpedMaxFreq = f;
            }
        }
        for (double f : {100.0, 300.0, 1000.0, 3000.0, 10000.0})
            std::printf("   %7.0f        %+6.2f             %+6.2f\n", f, mid.magnitudeDb(f, sr), lerped.magnitudeDb(f, sr));
        std::printf("   The maximum gain of the coefficient interpolation is %+.2f dB, at %.0f Hz (neither 1 kHz nor +12 dB)\n", lerpedMax, lerpedMaxFreq);
        std::printf("   Stability (|a2| < 1 and |a1| < 1 + a2): %s -- the 2nd-order stability triangle is convex, so linear interpolation cannot leave it,\n",
                    lerped.isStable() ? "stable" : "unstable");
        std::printf("   but the response in between is meaningless (the frequency ends up above 6 kHz).\n");
    }

    // ---- D. When coefficient interpolation becomes "unstable" ----
    {
        constexpr double sr = 48000.0;
        std::printf("\n[D] When coefficient interpolation becomes unstable\n");

        // D1. Interpolating a high-order polynomial: a 24 dB/oct high cut (4th-order Butterworth) @ 100 Hz and @ 10 kHz.
        //     If the two biquads are first multiplied into a single 4th-order polynomial and then interpolated, the pole at the midpoint leaves the unit circle.
        const Poly4 lo = cascade(BiquadCoefficients::makeLowpass(sr, 100.0, butterworthQ(4, 0)),
                                 BiquadCoefficients::makeLowpass(sr, 100.0, butterworthQ(4, 1)));
        const Poly4 hi = cascade(BiquadCoefficients::makeLowpass(sr, 10000.0, butterworthQ(4, 0)),
                                 BiquadCoefficients::makeLowpass(sr, 10000.0, butterworthQ(4, 1)));
        Poly4 mid4;
        for (int k = 0; k < 5; ++k) {
            mid4.b[k] = 0.5 * (lo.b[k] + hi.b[k]);
            mid4.a[k] = 0.5 * (lo.a[k] + hi.a[k]);
        }
        std::printf("   D1. 4th-order Butterworth lowpass, taking the midpoint of the \"4th-order polynomial coefficients\" at 100 Hz and 10 kHz:\n");
        std::printf("       max|pole|: 100 Hz version %.5f, 10 kHz version %.5f, interpolated midpoint %.5f (> 1 = unstable)\n",
                    maxPoleMagnitude(lo.a), maxPoleMagnitude(hi.a), maxPoleMagnitude(mid4.a));
        std::printf("       Impulse response of the midpoint: |h| = %.3g at n = 100, |h| = %.3g at n = 1000 (diverging)\n",
                    std::abs(impulseResponseAt(mid4, 100)), std::abs(impulseResponseAt(mid4, 1000)));
        // For comparison: split into two biquads and interpolate each separately (the 2nd-order stability triangle is convex)
        const BiquadCoefficients s1 = lerpCoefficients(BiquadCoefficients::makeLowpass(sr, 100.0, butterworthQ(4, 0)),
                                                       BiquadCoefficients::makeLowpass(sr, 10000.0, butterworthQ(4, 0)), 0.5);
        const BiquadCoefficients s2 = lerpCoefficients(BiquadCoefficients::makeLowpass(sr, 100.0, butterworthQ(4, 1)),
                                                       BiquadCoefficients::makeLowpass(sr, 10000.0, butterworthQ(4, 1)), 0.5);
        std::printf("       For comparison: interpolating the two biquads separately -> both are %s (each is still inside the convex stability triangle)\n",
                    (s1.isStable() && s2.isStable()) ? "stable" : "unstable");

        // D2. "Stable at every instant" != "the time-varying system is stable":
        //     switch every sample back and forth between two lowpass coefficient sets that are each stable (50 Hz / 16 kHz, Q = 0.707),
        //     and run an impulse through the real Biquad (TDF-II) from Chapter 02.
        const BiquadCoefficients c50 = BiquadCoefficients::makeLowpass(sr, 50.0, BiquadCoefficients::kButterworthQ);
        const BiquadCoefficients c16k = BiquadCoefficients::makeLowpass(sr, 16000.0, BiquadCoefficients::kButterworthQ);
        Biquad bq;
        bq.prepare(sr, 1, 1);
        std::printf("   D2. Alternating every sample between lowpass 50 Hz and 16 kHz (each stable on its own: %s / %s), feeding an impulse:\n",
                    c50.isStable() ? "stable" : "unstable", c16k.isStable() ? "stable" : "unstable");
        std::printf("       ");
        for (int n = 0; n <= 80; ++n) {
            bq.setCoefficients((n % 2 == 0) ? c50 : c16k);
            const float y = bq.processSample(0, n == 0 ? 1.0f : 0.0f);
            if (n > 0 && n % 20 == 0)
                std::printf("n=%d |y|=%.3g   ", n, std::abs(static_cast<double>(y)));
        }
        std::printf("\n");
        // For comparison: also go back and forth between 50 Hz and 16 kHz, but make each leg a smooth 20 ms walk in log frequency (what ParametricEq does)
        Biquad slow;
        slow.prepare(sr, 1, 1);
        NoiseGenerator noise(1);
        const int leg = msToSamples(20.0, sr);
        float slowPeak = 0.0f;
        for (int n = 0; n < secondsToSamples(1.0, sr); ++n) {
            const double phase = static_cast<double>(n % (2 * leg)) / leg; // 0..2: up and then back down
            const double t = phase < 1.0 ? phase : 2.0 - phase;
            const double f = 50.0 * std::pow(16000.0 / 50.0, t);
            slow.setCoefficients(BiquadCoefficients::makeLowpass(sr, f, BiquadCoefficients::kButterworthQ));
            slowPeak = std::max(slowPeak, std::abs(slow.processSample(0, 0.5f * noise.next())));
        }
        std::printf("       For comparison: a smooth sweep between 50 Hz <-> 16 kHz every 20 ms (recomputed at every sample), 1 second of white noise (peak 0.5)\n");
        std::printf("       output peak = %.3f (bounded)\n", slowPeak);
        std::printf("   -> When the parameters change slowly (relative to the filter's time constant), the time-varying effect is small;\n");
        std::printf("      when the coefficients \"jump\" or \"flip back and forth quickly\", the whole thing can diverge even though each coefficient set is stable on its own.\n");
    }
    return 0;
}
