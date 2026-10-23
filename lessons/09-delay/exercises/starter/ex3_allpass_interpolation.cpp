// Exercise 3 starter: allpass interpolation
//
// Goal: implement a first-order allpass interpolator so that the magnitude response of the
// fractional delay is 0 dB at every frequency, and measure its phase delay against theory.
//
//     H(z) = (η + z⁻¹) / (1 + η z⁻¹),   η = (1 − Δ) / (1 + Δ)
//     y[n] = η·(v[n] − y[n−1]) + v[n−1]      v[n] = x[n − d]
//
// After completing the TODOs, run l09_ex3_starter; all the checks should pass.
// Hint: keep Δ in [0.5, 1.5) (d = floor(delay − 0.5)) so that η stays away from 1.

#include <dawlab/Common.h>
#include <dawlab/dsp/FractionalDelayLine.h>

#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

using dawlab::dsp::FractionalDelayLine;

namespace {

constexpr double kSampleRate = 48000.0;

class AllpassInterpolator {
public:
    void prepare(int maxDelaySamples) {
        line_.prepare(maxDelaySamples + 2, 1);
        reset();
    }

    void reset() noexcept {
        line_.reset();
        y1_ = 0.0f;
    }

    void setDelay(float delaySamples) noexcept {
        // TODO: split delaySamples into an integer part d_ and a fraction Δ (Δ in [0.5, 1.5)
        //       is recommended), then compute eta_ = (1 − Δ) / (1 + Δ).
        d_ = static_cast<int>(delaySamples);
        eta_ = 0.0f;
    }

    float processSample(float x) noexcept {
        line_.push(0, x);
        // TODO: read v[n] = x[n−d] and v[n−1] = x[n−d−1], apply
        //       y[n] = η·(v[n] − y[n−1]) + v[n−1], update y1_, and flush denormals.
        return line_.readInteger(0, d_); // integer delay only for now: the phase-delay checks will FAIL
    }

    float getEta() const noexcept { return eta_; }
    int getIntegerDelay() const noexcept { return d_; }

private:
    FractionalDelayLine line_;
    int d_ = 0;
    float eta_ = 0.0f;
    float y1_ = 0.0f;
};

// Theoretical phase delay of the allpass (total delay, in samples)
double allpassTheoryDelay(int d, double eta, double freq) {
    const double w = dawlab::kTwoPi * freq / kSampleRate;
    return d + 1.0 - (2.0 / w) * std::atan(eta * std::sin(w) / (1.0 + eta * std::cos(w)));
}

// Measure the phase delay (samples): project in / out onto e^{−jωn}, take the phase
// difference, then use `expected` to resolve the 2π ambiguity.
template <typename Fn>
double measurePhaseDelaySamples(Fn&& fn, double freq, double expected) {
    const int skip = static_cast<int>(0.2 * kSampleRate);
    const int n = static_cast<int>(0.5 * kSampleRate);
    const double w = dawlab::kTwoPi * freq / kSampleRate;
    std::complex<double> sumIn(0.0, 0.0), sumOut(0.0, 0.0);
    for (int i = 0; i < skip + n; ++i) {
        const auto x = static_cast<float>(0.25 * std::sin(w * i));
        const float y = fn(x);
        if (i >= skip) {
            const int k = i - skip;
            const double win = 0.5 - 0.5 * std::cos(dawlab::kTwoPi * k / (n - 1));
            const std::complex<double> e(std::cos(w * i), -std::sin(w * i));
            sumIn += win * static_cast<double>(x) * e;
            sumOut += win * static_cast<double>(y) * e;
        }
    }
    const double phase = std::arg(sumOut * std::conj(sumIn));
    double delay = -phase / w;
    const double period = dawlab::kTwoPi / w;
    delay += period * std::round((expected - delay) / period);
    return delay;
}

double measureAllpassGainDb(float delay, double freq) {
    AllpassInterpolator ap;
    ap.prepare(64);
    ap.setDelay(delay);
    return dawlab::measureGainDb([&](float x) { return ap.processSample(x); }, freq, kSampleRate);
}

} // namespace

int main() {
    std::printf("=== Chapter 09, Exercise 3 (starter): allpass interpolation ===\n\n");
    const float delay = 10.5f;

    std::printf("Magnitude response (dB) should be ~ 0 everywhere:\n");
    for (double f : {100.0, 1000.0, 4000.0, 12000.0, 20000.0}) {
        const double g = measureAllpassGainDb(delay, f);
        std::printf("  %8.0f Hz  %+6.2f dB\n", f, g);
        DAWLAB_CHECK_NEAR(g, 0.0, 0.05);
    }

    std::printf("Phase delay (samples) should match theory:\n");
    for (double f : {100.0, 1000.0, 4000.0, 8000.0}) {
        AllpassInterpolator ap;
        ap.prepare(64);
        ap.setDelay(delay);
        const double t = measurePhaseDelaySamples([&](float x) { return ap.processSample(x); }, f, delay);
        const double theory = allpassTheoryDelay(ap.getIntegerDelay(), ap.getEta(), f);
        std::printf("  %8.0f Hz  measured %7.3f  theory %7.3f\n", f, t, theory);
        DAWLAB_CHECK_NEAR(t, theory, 0.02);
    }
    {
        AllpassInterpolator ap;
        ap.prepare(64);
        ap.setDelay(delay);
        DAWLAB_CHECK_NEAR(measurePhaseDelaySamples([&](float x) { return ap.processSample(x); }, 100.0, delay),
                          delay, 0.01);
    }
    for (float d : {10.1f, 10.3f, 10.7f, 10.9f}) {
        AllpassInterpolator ap;
        ap.prepare(64);
        ap.setDelay(d);
        DAWLAB_CHECK_NEAR(measurePhaseDelaySamples([&](float x) { return ap.processSample(x); }, 100.0, d), d, 0.01);
    }
    return dawlab::test::summary("l09_ex3_starter");
}
