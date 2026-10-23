// Exercise 6 solution: first-order ADAA (antiderivative anti-aliasing) hard clip, measuring how much
// the aliasing improves.
//
// The idea (Parker et al. 2016, Bilbao et al. 2017):
//   naive: y[n] = f(x[n]) - the nonlinearity is only looked at on the sample points; whatever happens
//          between samples (sharp corners, the high frequencies they create) is ignored and then folds back.
//   ADAA:  assume x is a straight line between x[n-1] and x[n] and take the "average value" of f along it:
//       y[n] = 1/(x[n] - x[n-1]) * integral_{x[n-1]}^{x[n]} f(u) du = (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1])
//   where F is the antiderivative of f. This average equals applying a "continuous-time rectangular
//   lowpass" before sampling, so the high harmonics are attenuated before they can fold back.
//
// Hard clip: f(x) = clamp(x, -1, 1)
//   F(x) = x^2/2           (|x| <= 1)
//        = |x| - 1/2       (|x| > 1)   <- value (1/2) and slope (+/-1) are both continuous at |x| = 1
//
// Numerical issue: when x[n] ~ x[n-1] both numerator and denominator approach 0 (0/0). In that case use
// the midpoint approximation f((x[n] + x[n-1]) / 2) (which is exactly the limit of the average over that
// line segment as delta -> 0). Compute the numerator in double, otherwise the float subtraction error is large.
//
// The price:
//   - In the linear region (|x| < 1) y[n] = (x[n] + x[n-1]) / 2 - a 2-tap average: half a sample of extra
//     delay, plus a lowpass effect |H| = cos(w/2): 7 kHz @ 48 kHz -> cos(pi*7/48) = 0.897 -> -0.94 dB; 20 kHz -> -11.7 dB.
//   - First-order ADAA gives only limited improvement for "mild" aliasing; at high drive and high
//     frequency it is still best combined with 2x oversampling.
//
// Run: l06_ex6_solution

#include <dawlab/Common.h>
#include <dawlab/dsp/Oversampler.h>

#include <algorithm>
#include <cstdio>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr double kF0 = 7000.0;
constexpr float kDrive = 4.0f; // amplitude 0.5 x 4 = 2 -> deep clipping

class HardClipAdaa {
public:
    static double f(double x) noexcept { return std::clamp(x, -1.0, 1.0); }
    // F(x) = integral of f
    static double antiderivative(double x) noexcept {
        const double ax = std::abs(x);
        return ax <= 1.0 ? 0.5 * x * x : ax - 0.5;
    }

    void reset() noexcept { x1_ = 0.0; }

    float processSample(float xIn) noexcept {
        const double x = xIn;
        const double dx = x - x1_;
        double y;
        if (std::abs(dx) > 1.0e-6)
            y = (antiderivative(x) - antiderivative(x1_)) / dx; // (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1])
        else
            y = f(0.5 * (x + x1_));                             // the limit of 0/0: the midpoint
        x1_ = x;
        return static_cast<float>(y);
    }

private:
    double x1_ = 0.0;
};

struct Result {
    double h1Db = 0.0;     // fundamental in dB (shows the high-frequency loss of ADAA)
    double aliasDb = 0.0;  // sum of alias power / fundamental power
};

// All aliases fall on odd kHz values (gcd(7000, 48000) = 1000, and the hard clip is an odd function ->
// only odd harmonics)
Result analyse(const dawlab::AudioBuffer& b) {
    const int skip = 4800;
    const float* d = b.getReadPointer(0) + skip;
    const int n = b.getNumSamples() - skip;
    const double h1 = dawlab::goertzelAmplitude(d, n, kF0, kSampleRate);
    double p = 0.0;
    for (int k = 1; k <= 23; k += 2) {
        const double f = 1000.0 * k;
        if (f == kF0 || f == 3 * kF0)
            continue; // 7 kHz and 21 kHz are real harmonics
        const double a = dawlab::goertzelAmplitude(d, n, f, kSampleRate);
        p += a * a;
    }
    return {dawlab::gainToDb(h1), 10.0 * std::log10(std::max(p / (h1 * h1), 1e-15))};
}

// mode: 0 = naive, 1 = ADAA; log2: oversampling (0 = 1x, 1 = 2x)
Result run(int mode, int log2) {
    dawlab::AudioBuffer b = dawlab::makeSine(kF0, kSampleRate, 1.0, 0.5f, 1);
    dawlab::dsp::Oversampler os(log2);
    os.prepare(kSampleRate, kBlockSize, 1);
    HardClipAdaa adaa;
    dawlab::AudioBlock whole(b);
    for (int start = 0; start < b.getNumSamples(); start += kBlockSize) {
        dawlab::AudioBlock block = whole.getSubBlock(start, std::min(kBlockSize, b.getNumSamples() - start));
        dawlab::AudioBlock up = os.upsample(block);
        float* u = up.getChannelPointer(0);
        for (int i = 0; i < up.getNumSamples(); ++i) {
            const float x = kDrive * u[i];
            u[i] = (mode == 0) ? static_cast<float>(HardClipAdaa::f(x)) : adaa.processSample(x);
        }
        os.downsample(block);
    }
    return analyse(b);
}

} // namespace

int main() {
    // ---- 1. Correctness of the antiderivative: F' = f, and continuous at |x| = 1 -------------
    for (double x = -3.0; x <= 3.0; x += 0.01) {
        const double h = 1e-5;
        const double derivative = (HardClipAdaa::antiderivative(x + h) - HardClipAdaa::antiderivative(x - h)) / (2 * h);
        DAWLAB_CHECK(std::abs(derivative - HardClipAdaa::f(x)) < 1e-4);
    }
    DAWLAB_CHECK_NEAR(HardClipAdaa::antiderivative(1.0), 0.5, 1e-12);
    DAWLAB_CHECK_NEAR(HardClipAdaa::antiderivative(-2.0), 1.5, 1e-12);

    // ---- 2. Linear region: ADAA = 2-tap average ---------------------------------------------
    {
        HardClipAdaa a;
        a.reset();
        a.processSample(0.2f);
        DAWLAB_CHECK_NEAR(a.processSample(0.6f), 0.4, 1e-6);  // (0.2 + 0.6) / 2
        DAWLAB_CHECK_NEAR(a.processSample(0.6f), 0.6, 1e-6);  // delta = 0 -> midpoint
        DAWLAB_CHECK_NEAR(a.processSample(3.0f), (2.5 - 0.18) / 2.4, 1e-6); // (F(3) - F(0.6)) / 2.4
    }

    // ---- 3. Aliasing measurement -------------------------------------------------------------
    const Result naive1 = run(0, 0), adaa1 = run(1, 0), naive2 = run(0, 1), adaa2 = run(1, 1);
    std::printf("7 kHz sine (peak 0.5 x drive 4 = 2) -> hard clip, fs = 48 kHz\n");
    std::printf("%-16s %14s %16s\n", "Method", "fundamental (dBFS)", "alias total (dB)");
    std::printf("%-16s %14.2f %16.1f\n", "naive 1x", naive1.h1Db, naive1.aliasDb);
    std::printf("%-16s %14.2f %16.1f\n", "ADAA 1x", adaa1.h1Db, adaa1.aliasDb);
    std::printf("%-16s %14.2f %16.1f\n", "naive 2x", naive2.h1Db, naive2.aliasDb);
    std::printf("%-16s %14.2f %16.1f\n", "ADAA + 2x", adaa2.h1Db, adaa2.aliasDb);
    // For comparison: naive hard clip with oversampling only. The harmonics decay only as 1/k, so each
    // doubling of the factor improves things by only a dozen or so dB
    const Result naive4 = run(0, 2), naive8 = run(0, 3);
    std::printf("%-16s %14.2f %16.1f\n", "naive 4x", naive4.h1Db, naive4.aliasDb);
    std::printf("%-16s %14.2f %16.1f\n", "naive 8x", naive8.h1Db, naive8.aliasDb);
    std::printf("ADAA 1x improves on naive 1x by %.1f dB; ADAA + 2x improves on naive 2x by %.1f dB\n",
                naive1.aliasDb - adaa1.aliasDb, naive2.aliasDb - adaa2.aliasDb);

    DAWLAB_CHECK(adaa1.aliasDb < naive1.aliasDb - 6.0);
    DAWLAB_CHECK(adaa2.aliasDb < naive2.aliasDb - 6.0);
    DAWLAB_CHECK(naive2.aliasDb < naive1.aliasDb);
    DAWLAB_CHECK(naive4.aliasDb < naive2.aliasDb && naive8.aliasDb < naive4.aliasDb);
    // Even at 8x the hard clip still has measurable alias (compare: the tanh of exercise 7 reaches -85 dB at 4x)
    DAWLAB_CHECK(naive8.aliasDb > -75.0);
    // ADAA + 2x (far less computation than naive 8x) is in the same ballpark as naive 8x
    DAWLAB_CHECK(std::abs(adaa2.aliasDb - naive8.aliasDb) < 10.0);
    // The price of ADAA: about 1 dB of fundamental loss (the 2-tap average is -0.94 dB at 7 kHz; slightly
    // different in the clipped region)
    DAWLAB_CHECK(naive1.h1Db - adaa1.h1Db > 0.3 && naive1.h1Db - adaa1.h1Db < 2.0);

    return dawlab::test::summary("l06_ex6");
}
