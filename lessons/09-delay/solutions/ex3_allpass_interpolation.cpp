// Exercise 3 solution: allpass interpolation, compared with linear / cubic
//
// A first-order allpass interpolator:
//     H(z) = (η + z⁻¹) / (1 + η z⁻¹),   η = (1 − Δ) / (1 + Δ)
//     y[n] = η·(v[n] − y[n−1]) + v[n−1]      v[n] = x[n − d] (the signal after the integer delay)
// Properties:
//   - |H(e^{jω})| = 1 at every frequency -> none of the high-frequency loss of linear / cubic.
//   - The price is that the "phase delay" equals Δ exactly only at low frequencies; the higher
//     the frequency, the further it strays:
//         τ(ω) = 1 − (2/ω)·atan( η·sin ω / (1 + η·cos ω) )     (unit: samples)
//     As ω → 0, τ → (1 − η)/(1 + η) = Δ.
//   - It has memory (y[n−1]), so changing the delay time leaves a short transient behind,
//     which makes it unsuitable for fast modulation.
// Implementation trick: put Δ in [0.5, 1.5) rather than [0, 1); then η lies in (−0.2, 1/3] and
// the pole stays well away from the unit circle.
//
// Run: l09_ex3_solution (no arguments needed). Writes dawlab_output/l09_ex3_allpass_noise.wav.

#include <dawlab/Common.h>
#include <dawlab/dsp/FractionalDelayLine.h>

#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

using dawlab::dsp::FractionalDelayLine;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

class AllpassInterpolator {
public:
    void prepare(int maxDelaySamples) {
        line_.prepare(maxDelaySamples + 2, 1); // 2 extra slots: we read d and d+1
        reset();
    }

    void reset() noexcept {
        line_.reset();
        y1_ = 0.0f;
    }

    // delaySamples = d + Δ. Put Δ in [0.5, 1.5) to keep η away from 1 (a pole hugging the unit circle means a long transient).
    void setDelay(float delaySamples) noexcept {
        const float delay = std::max(0.5f, delaySamples);
        d_ = static_cast<int>(std::floor(delay - 0.5f));
        const float frac = delay - static_cast<float>(d_); // in [0.5, 1.5)
        eta_ = (1.0f - frac) / (1.0f + frac);
    }

    float processSample(float x) noexcept {
        line_.push(0, x);
        const float v0 = line_.readInteger(0, d_);     // v[n]   = x[n − d]
        const float v1 = line_.readInteger(0, d_ + 1); // v[n−1] = x[n − d − 1]
        float y = eta_ * (v0 - y1_) + v1;              // y[n] = η(v[n] − y[n−1]) + v[n−1]
        if (std::abs(y) < 1.0e-20f)                    // recursive structure: flush denormals
            y = 0.0f;
        y1_ = y;
        return y;
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

// Measure the "phase delay" (in samples): project in / out onto e^{−jωn}, take the phase
// difference and divide by ω. The phase difference is only known modulo 2π, so use `expected`
// to unwrap the result to the nearest period.
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
            const double win = 0.5 - 0.5 * std::cos(dawlab::kTwoPi * k / (n - 1)); // Hann
            const std::complex<double> e(std::cos(w * i), -std::sin(w * i));
            sumIn += win * static_cast<double>(x) * e;
            sumOut += win * static_cast<double>(y) * e;
        }
    }
    const double phase = std::arg(sumOut * std::conj(sumIn)); // negative = the output lags
    double delay = -phase / w;
    const double period = dawlab::kTwoPi / w;
    delay += period * std::round((expected - delay) / period); // unwrap
    return delay;
}

double measureAllpassGainDb(float delay, double freq) {
    AllpassInterpolator ap;
    ap.prepare(64);
    ap.setDelay(delay);
    return dawlab::measureGainDb([&](float x) { return ap.processSample(x); }, freq, kSampleRate);
}

double measureLinearGainDb(float delay, double freq) {
    FractionalDelayLine line;
    line.prepare(64, 1);
    return dawlab::measureGainDb(
        [&](float x) {
            line.push(0, x);
            return line.readLinear(0, delay);
        },
        freq, kSampleRate);
}

double measureCubicGainDb(float delay, double freq) {
    FractionalDelayLine line;
    line.prepare(64, 1);
    return dawlab::measureGainDb(
        [&](float x) {
            line.push(0, x);
            return line.readCubic(0, delay);
        },
        freq, kSampleRate);
}

} // namespace

int main() {
    std::printf("=== Chapter 09, Exercise 3: allpass interpolation ===\n\n");

    // ---- 1. Magnitude response (delay = 10.5) --------------------------------------
    {
        const float delay = 10.5f;
        AllpassInterpolator ap;
        ap.prepare(64);
        ap.setDelay(delay);
        std::printf("delay = %.1f: d = %d, Δ = %.2f, η = %.4f\n", delay, ap.getIntegerDelay(),
                    delay - static_cast<float>(ap.getIntegerDelay()), ap.getEta());
        std::printf("Magnitude response (dB):\n   freq(Hz)   linear    cubic   allpass\n");
        const double freqs[] = {100, 1000, 4000, 8000, 12000, 16000, 20000};
        for (double f : freqs) {
            const double gl = measureLinearGainDb(delay, f);
            const double gc = measureCubicGainDb(delay, f);
            const double ga = measureAllpassGainDb(delay, f);
            std::printf("  %8.0f   %+6.2f   %+6.2f   %+6.2f\n", f, gl, gc, ga);
            DAWLAB_CHECK_NEAR(ga, 0.0, 0.05); // allpass: 0 dB at every frequency
        }
        DAWLAB_CHECK_NEAR(measureLinearGainDb(delay, 12000.0), -3.01, 0.1); // |cos(π/4)|
        DAWLAB_CHECK_NEAR(measureCubicGainDb(delay, 12000.0), -1.07, 0.1);  // |9/8·cos(π/4) − 1/8·cos(3π/4)|
        std::printf("  -> the allpass is 0 dB at all frequencies; linear drops 3 dB at 12 kHz, cubic 1.07 dB.\n\n");
    }

    // ---- 2. Phase delay vs frequency (delay = 10.5) ---------------------------------
    {
        const float delay = 10.5f;
        std::printf("Phase delay (samples), delay = %.1f:\n   freq(Hz)   linear   allpass   allpass theory\n", delay);
        const double freqs[] = {100, 1000, 2000, 4000, 8000};
        double allpassError8k = 0.0; // difference between the allpass phase delay at 8 kHz and the target delay
        for (double f : freqs) {
            FractionalDelayLine line;
            line.prepare(64, 1);
            const double tl = measurePhaseDelaySamples(
                [&](float x) {
                    line.push(0, x);
                    return line.readLinear(0, delay);
                },
                f, delay);
            AllpassInterpolator ap;
            ap.prepare(64);
            ap.setDelay(delay);
            const double ta = measurePhaseDelaySamples([&](float x) { return ap.processSample(x); }, f, delay);
            const double theory = allpassTheoryDelay(ap.getIntegerDelay(), ap.getEta(), f);
            std::printf("  %8.0f   %7.3f   %7.3f   %7.3f\n", f, tl, ta, theory);
            DAWLAB_CHECK_NEAR(ta, theory, 0.02);
            if (f == 8000.0)
                allpassError8k = ta - delay;
        }
        // At low frequencies the allpass delay is the 10.5 we want; linear (Δ = 0.5 is a symmetric FIR) has exactly linear phase
        AllpassInterpolator ap;
        ap.prepare(64);
        ap.setDelay(delay);
        DAWLAB_CHECK_NEAR(measurePhaseDelaySamples([&](float x) { return ap.processSample(x); }, 100.0, delay),
                          delay, 0.01);
        std::printf("  -> the allpass delay is accurate at low frequencies and strays more the higher the frequency (already %+.3f sample off at 8 kHz);\n",
                    allpassError8k);
        std::printf("     linear at Δ = 0.5 is a symmetric FIR with an accurate phase delay at all frequencies, but it attenuates the magnitude.\n\n");
    }

    // ---- 3. Different fractional parts: is the low-frequency delay always right? ---------
    {
        std::printf("Phase delay at 100 Hz for various fractional delays (allpass):\n");
        const float delays[] = {10.0f, 10.1f, 10.3f, 10.5f, 10.7f, 10.9f, 11.0f};
        for (float d : delays) {
            AllpassInterpolator ap;
            ap.prepare(64);
            ap.setDelay(d);
            const double t = measurePhaseDelaySamples([&](float x) { return ap.processSample(x); }, 100.0, d);
            std::printf("  delay %.1f -> d = %2d, η = %+.4f, measured %.3f\n", d, ap.getIntegerDelay(), ap.getEta(), t);
            DAWLAB_CHECK_NEAR(t, d, 0.01);
        }
        std::printf("\n");
    }

    // ---- 4. Listen: white noise through an allpass of 10.5 samples --------------------
    {
        dawlab::AudioBuffer noise = dawlab::makeWhiteNoise(kSampleRate, 3.0, 0.25f, 1);
        dawlab::AudioBuffer out(1, noise.getNumSamples());
        AllpassInterpolator ap;
        ap.prepare(64);
        ap.setDelay(10.5f);
        const float* src = noise.getReadPointer(0);
        float* dst = out.getWritePointer(0);
        for (int start = 0; start < noise.getNumSamples(); start += kBlockSize) {
            const int n = std::min(kBlockSize, noise.getNumSamples() - start);
            for (int i = 0; i < n; ++i)
                dst[start + i] = ap.processSample(src[start + i]);
        }
        dawlab::writeWav(dawlab::outputPath("l09_ex3_allpass_noise.wav"), out, kSampleRate);
        std::printf("White-noise RMS: dry %.4f, allpass %.4f (the allpass does not change the energy)\n", dawlab::rms(noise, 0),
                    dawlab::rms(out, 0));
        DAWLAB_CHECK_NEAR(dawlab::rms(out, 0), dawlab::rms(noise, 0), 0.002);
        DAWLAB_CHECK(!dawlab::hasNanOrInf(out));
        std::printf("Wrote dawlab_output/l09_ex3_allpass_noise.wav\n\n");
    }

    return dawlab::test::summary("l09_ex3");
}
