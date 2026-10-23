// Exercise 3 solution: a one-pole exponential smoother and measuring its time constant
//
//   y[n] = y[n-1] + a * (target - y[n-1]),   a = 1 - exp(-1 / (tau * fs))
//
// This is the simplest first-order IIR low-pass filter (chapter 02 introduces it properly). For a step input
// the output is y[n] = target * (1 - (1-a)^n) = target * (1 - e^{-n/(tau*fs)}):
//   n = tau*fs        -> 63.2%
//   n = 4.605*tau*fs  -> 99%    (ln 100 = 4.605)
//   n = 6.908*tau*fs  -> 99.9%  (ln 1000 = 6.908, i.e. a remaining error of -60 dB)
//
// The difference from a linear ramp: a linear ramp finishes in a fixed time with a constant slope; exponential smoothing never "really" reaches
// the target, but it moves quickly at first and slowly near the target, which sounds more like an analog fader / a charging capacitor.
//
// A precision trap (you really will hit it when implementing this): if current_ is stored as a float, then once the remaining distance d = target - current
// becomes so small that a*d is below half an ulp of current, current_ += a*d adds nothing -- the state gets "stuck".
// For current just below 1 (in [0.5, 1)) the ulp is 2^-24 = 6e-8 and half an ulp is about 3e-8, so it gets stuck at
// d ~ 3e-8 / a = eps/(4a) (eps = 2^-23 = 1.19e-7 is the ulp of 1.0; with tau = 10 ms, a = 0.0021
// -> stuck at about 1.4e-5), never reaching the snap threshold of 1e-6, and isSmoothing() stays true forever. The fix: keep the state in a double (eps = 2.2e-16) and convert to float on return.
// The one-pole filter in chapter 02 runs into the same thing again.

#include <dawlab/Common.h>
#include <dawlab/dsp/SmoothedValue.h>

#include <cmath>
#include <cstdio>

using namespace dawlab;

class ExpSmoother {
public:
    // tau = timeConstantSeconds: the time the output needs to reach 63.2%
    void reset(double sampleRate, double timeConstantSeconds) noexcept {
        const double tauSamples = timeConstantSeconds * sampleRate;
        // a = 1 - e^{-1/(tau*fs)}; when tau = 0, a = 1 (jump straight to the target)
        coeff_ = tauSamples > 0.0 ? 1.0 - std::exp(-1.0 / tauSamples) : 1.0;
        current_ = target_;
    }

    void setCurrentAndTargetValue(float v) noexcept { current_ = target_ = v; }
    void setTargetValue(float v) noexcept { target_ = v; }

    float getNextValue() noexcept {
        // Difference equation: each sample moves a fraction a of the remaining distance
        current_ += coeff_ * (target_ - current_);
        // Once the remaining error is too small to hear (which also avoids denormals), snap to the target
        if (std::abs(target_ - current_) < kSnapThreshold)
            current_ = target_;
        return static_cast<float>(current_);
    }

    float getCurrentValue() const noexcept { return static_cast<float>(current_); }
    float getTargetValue() const noexcept { return static_cast<float>(target_); }
    bool isSmoothing() const noexcept { return current_ != target_; }
    float getCoefficient() const noexcept { return static_cast<float>(coeff_); }

private:
    static constexpr double kSnapThreshold = 1.0e-6; // a gap below -120 dB counts as arrived
    double coeff_ = 1.0;
    double current_ = 0.0;
    double target_ = 0.0;
};

// Measured on a 0 -> 1 step: the index (counting from 1) of the first output sample >= fraction
int samplesToReach(ExpSmoother& s, float fraction, int maxSamples) {
    s.setCurrentAndTargetValue(0.0f);
    s.setTargetValue(1.0f);
    for (int n = 1; n <= maxSamples; ++n)
        if (s.getNextValue() >= fraction)
            return n;
    return -1;
}

int main() {
    constexpr double sr = 48000.0;
    const double tauMs = 10.0;
    const int tauSamples = msToSamples(tauMs, sr); // 480

    ExpSmoother s;
    s.reset(sr, tauMs * 0.001);
    std::printf("tau = %.1f ms = %d samples, a = %.6f (theory 1 - e^(-1/480) = %.6f)\n", tauMs, tauSamples,
                s.getCoefficient(), 1.0 - std::exp(-1.0 / tauSamples));
    DAWLAB_CHECK_NEAR(s.getCoefficient(), 1.0 - std::exp(-1.0 / tauSamples), 1e-6);

    // Time-constant measurement: 63.2% should arrive at ~ tau, 99% at ~ 4.605 tau, and 99.9% at ~ 6.908 tau
    const int n632 = samplesToReach(s, 0.632f, 100000);
    const int n99 = samplesToReach(s, 0.99f, 100000);
    const int n999 = samplesToReach(s, 0.999f, 100000);
    std::printf("Reaches 63.2%%: %d samples (%.2f tau, expected 1.00 tau)\n", n632, double(n632) / tauSamples);
    std::printf("Reaches 99%%  : %d samples (%.2f tau, expected ln(100) = %.3f tau)\n", n99, double(n99) / tauSamples, std::log(100.0));
    std::printf("Reaches 99.9%%: %d samples (%.2f tau, expected ln(1000) = %.3f tau)\n", n999, double(n999) / tauSamples,
                std::log(1000.0));
    DAWLAB_CHECK_NEAR(n632, tauSamples, 3);
    DAWLAB_CHECK_NEAR(n99, std::log(100.0) * tauSamples, 0.01 * tauSamples * std::log(100.0));
    DAWLAB_CHECK_NEAR(n999, std::log(1000.0) * tauSamples, 0.01 * tauSamples * std::log(1000.0));

    // Compare with the closed-form solution: y[n] = 1 - (1-a)^n
    s.setCurrentAndTargetValue(0.0f);
    s.setTargetValue(1.0f);
    float maxErr = 0.0f;
    bool monotonic = true;
    float prev = 0.0f;
    for (int n = 1; n <= 5 * tauSamples; ++n) {
        const float y = s.getNextValue();
        const double expected = 1.0 - std::pow(1.0 - double(s.getCoefficient()), n);
        maxErr = std::max(maxErr, static_cast<float>(std::abs(y - expected)));
        monotonic = monotonic && (y >= prev);
        prev = y;
    }
    std::printf("Max error vs. the closed form 1 - (1-a)^n: %.2e; monotonically increasing: %s; never exceeds the target: %s\n", maxErr, monotonic ? "yes" : "no",
                prev <= 1.0f ? "yes" : "no");
    DAWLAB_CHECK(maxErr < 1e-4f);
    DAWLAB_CHECK(monotonic);
    DAWLAB_CHECK(prev <= 1.0f);

    // snap: after long enough it must "really" reach the target and stop smoothing (otherwise isSmoothing() stays true forever)
    for (int n = 0; n < 20 * tauSamples; ++n)
        s.getNextValue();
    DAWLAB_CHECK(!s.isSmoothing());
    DAWLAB_CHECK(s.getCurrentValue() == 1.0f);

    // Compare with the linear ramp: for the same "50 ms", the linear ramp finishes exactly at sample 2400; the exponential with tau = 50 ms is only 63% of the way there
    dsp::SmoothedValue lin;
    lin.reset(sr, 0.05);
    lin.setCurrentAndTargetValue(0.0f);
    lin.setTargetValue(1.0f);
    ExpSmoother e;
    e.reset(sr, 0.05);
    e.setCurrentAndTargetValue(0.0f);
    e.setTargetValue(1.0f);
    float linAt = 0.0f, expAt = 0.0f;
    for (int n = 0; n < msToSamples(50.0, sr); ++n) {
        linAt = lin.getNextValue();
        expAt = e.getNextValue();
    }
    std::printf("After 50 ms: linear ramp = %.3f, exponential (tau = 50 ms) = %.3f\n", linAt, expAt);
    DAWLAB_CHECK_NEAR(linAt, 1.0, 1e-6);
    DAWLAB_CHECK_NEAR(expAt, 1.0 - std::exp(-1.0), 2e-3);

    return dawlab::test::summary("l01_ex3");
}
