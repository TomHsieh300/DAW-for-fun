// Exercise 6 (***): first-order ADAA (antiderivative anti-aliasing) hard clip
//
// Goal:
//   naive hard clip: y[n] = clamp(x[n], -1, 1)
//   ADAA:            y[n] = (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1]), where F is the antiderivative of clamp
//   Measure the total alias energy of a 7 kHz (peak 2) hard clip at 48 kHz; ADAA must be at least 6 dB
//   lower than naive.
//
// Hints:
//   - F(x) has two pieces: |x| <= 1 and |x| > 1. Check that F'(x) = f(x) and that F is continuous at |x| = 1.
//   - When |x[n] - x[n-1]| is tiny it is 0/0: use f((x[n] + x[n-1]) / 2) instead.
//   - Compute in double.
//
// Until all TODOs are done, running this prints FAIL (that is expected).
// Run: l06_ex6_starter

#include <dawlab/Common.h>

#include <algorithm>
#include <cstdio>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr double kF0 = 7000.0;
constexpr float kDrive = 4.0f;

class HardClipAdaa {
public:
    static double f(double x) noexcept { return std::clamp(x, -1.0, 1.0); }

    // TODO 1: the antiderivative of the hard clip
    static double antiderivative(double x) noexcept {
        (void)x;
        return 0.0;
    }

    void reset() noexcept { x1_ = 0.0; }

    // TODO 2: first-order ADAA (remember to handle delta ~ 0)
    float processSample(float xIn) noexcept {
        x1_ = xIn;
        return static_cast<float>(f(xIn));
    }

private:
    double x1_ = 0.0;
};

// Total alias energy (dB relative to the fundamental): all aliases fall on odd kHz values, excluding 7 kHz and 21 kHz
double aliasDb(const dawlab::AudioBuffer& b) {
    const int skip = 4800;
    const float* d = b.getReadPointer(0) + skip;
    const int n = b.getNumSamples() - skip;
    const double h1 = dawlab::goertzelAmplitude(d, n, kF0, kSampleRate);
    double p = 0.0;
    for (int k = 1; k <= 23; k += 2) {
        const double f = 1000.0 * k;
        if (f == kF0 || f == 3 * kF0)
            continue;
        const double a = dawlab::goertzelAmplitude(d, n, f, kSampleRate);
        p += a * a;
    }
    return 10.0 * std::log10(std::max(p / (h1 * h1), 1e-15));
}

double run(bool useAdaa) {
    dawlab::AudioBuffer b = dawlab::makeSine(kF0, kSampleRate, 1.0, 0.5f, 1);
    HardClipAdaa adaa;
    float* d = b.getWritePointer(0);
    for (int i = 0; i < b.getNumSamples(); ++i) {
        const float x = kDrive * d[i];
        d[i] = useAdaa ? adaa.processSample(x) : static_cast<float>(HardClipAdaa::f(x));
    }
    return aliasDb(b);
}

} // namespace

int main() {
    double worst = 0.0; // max |F'(x) - f(x)|
    for (double x = -3.0; x <= 3.0; x += 0.01) {
        const double h = 1e-5;
        const double derivative = (HardClipAdaa::antiderivative(x + h) - HardClipAdaa::antiderivative(x - h)) / (2 * h);
        worst = std::max(worst, std::abs(derivative - HardClipAdaa::f(x)));
    }
    DAWLAB_CHECK(worst < 1e-4);
    {
        HardClipAdaa a;
        a.reset();
        a.processSample(0.2f);
        DAWLAB_CHECK_NEAR(a.processSample(0.6f), 0.4, 1e-6);
    }
    const double naive = run(false), adaa = run(true);
    std::printf("alias total: naive %.1f dB, ADAA %.1f dB (improvement %.1f dB)\n", naive, adaa, naive - adaa);
    DAWLAB_CHECK(adaa < naive - 6.0);
    return dawlab::test::summary("l06_ex6_starter");
}
