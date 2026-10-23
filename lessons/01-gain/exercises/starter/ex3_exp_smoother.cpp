// Exercise 3 (starter code): a one-pole exponential smoother and measuring its time constant
//
// Goal: implement ExpSmoother so that, for a step input 0 -> 1:
//   y[n] = y[n-1] + a * (target - y[n-1]),  a = 1 - exp(-1 / (tau * fs))
//   it reaches 63.2% at n = tau*fs and 99% at n = 4.605*tau*fs.
// You are done when every self-check passes after you complete the TODOs. The starter code prints FAIL when it runs; that is expected.

#include <dawlab/Common.h>
#include <dawlab/dsp/SmoothedValue.h>

#include <cmath>
#include <cstdio>

using namespace dawlab;

class ExpSmoother {
public:
    // tau = timeConstantSeconds: the time the output needs to reach 63.2%
    void reset(double sampleRate, double timeConstantSeconds) noexcept {
        (void)sampleRate;
        (void)timeConstantSeconds;
        // TODO: compute coeff_ from tau and the sample rate (coeff_ = 1 when tau = 0), and snap current_ to target_
        coeff_ = 1.0;
        current_ = target_;
    }

    void setCurrentAndTargetValue(float v) noexcept { current_ = target_ = v; }
    void setTargetValue(float v) noexcept { target_ = v; }

    float getNextValue() noexcept {
        // TODO: apply the difference equation; when |target - current| is below kSnapThreshold, snap straight to target
        //       (otherwise the target is never reached, isSmoothing() stays true forever, and denormals appear)
        return static_cast<float>(current_);
    }

    float getCurrentValue() const noexcept { return static_cast<float>(current_); }
    float getTargetValue() const noexcept { return static_cast<float>(target_); }
    bool isSmoothing() const noexcept { return current_ != target_; }
    float getCoefficient() const noexcept { return static_cast<float>(coeff_); }

private:
    // The state is a double: with a float, a*(target - current) falls below half an ulp near the target and the state gets stuck short of it
    static constexpr double kSnapThreshold = 1.0e-6;
    double coeff_ = 1.0;
    double current_ = 0.0;
    double target_ = 0.0;
};

// Measured on a 0 -> 1 step: the index (counting from 1) of the first output sample >= fraction; returns -1 if none is found
int samplesToReach(ExpSmoother& s, float fraction, int maxSamples) {
    (void)s;
    (void)fraction;
    (void)maxSamples;
    // TODO: set s to current = 0, target = 1, then call getNextValue() sample by sample until the output is >= fraction
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

    const int n632 = samplesToReach(s, 0.632f, 100000);
    const int n99 = samplesToReach(s, 0.99f, 100000);
    const int n999 = samplesToReach(s, 0.999f, 100000);
    std::printf("Reaches 63.2%%: %d samples (expected ~ %d)\n", n632, tauSamples);
    std::printf("Reaches 99%%  : %d samples (expected ~ %.0f)\n", n99, std::log(100.0) * tauSamples);
    std::printf("Reaches 99.9%%: %d samples (expected ~ %.0f)\n", n999, std::log(1000.0) * tauSamples);
    DAWLAB_CHECK_NEAR(n632, tauSamples, 3);
    DAWLAB_CHECK_NEAR(n99, std::log(100.0) * tauSamples, 0.01 * tauSamples * std::log(100.0));
    DAWLAB_CHECK_NEAR(n999, std::log(1000.0) * tauSamples, 0.01 * tauSamples * std::log(1000.0));

    // Compare with the closed-form solution y[n] = 1 - (1-a)^n; the output must also be monotonically increasing and never exceed the target
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
    DAWLAB_CHECK(maxErr < 1e-4f);
    DAWLAB_CHECK(monotonic);
    DAWLAB_CHECK(prev <= 1.0f);

    // After long enough it must actually reach the target and stop smoothing
    for (int n = 0; n < 20 * tauSamples; ++n)
        s.getNextValue();
    DAWLAB_CHECK(!s.isSmoothing());
    DAWLAB_CHECK(s.getCurrentValue() == 1.0f);

    return dawlab::test::summary("l01_ex3_starter");
}
