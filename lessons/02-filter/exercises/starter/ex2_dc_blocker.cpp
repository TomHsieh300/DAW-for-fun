// Exercise 2: DC blocker (starter code).
//
// Implement a first-order DC blocker:
//     y[n] = x[n] - x[n-1] + R y[n-1]        H(z) = (1 - z^-1) / (1 - R z^-1)
// and verify: DC is removed, 1 kHz is barely affected, and the -3 dB point ~ fs (1 - R) / (2π).
//
// Once every TODO is done, all the self-checks should pass when you run it.
#include <dawlab/Common.h>

#include <complex>
#include <cstdio>
#include <vector>

using namespace dawlab;

constexpr double kSampleRate = 48000.0;

class DcBlocker {
public:
    void prepare(double /*sampleRate*/, int /*maxBlockSize*/, int numChannels) {
        state_.assign(static_cast<size_t>(numChannels), State{});
    }
    void reset() noexcept {
        for (auto& s : state_)
            s = State{};
    }
    void setR(float r) noexcept { r_ = r; }

    float processSample(int channel, float x) noexcept {
        State& s = state_[static_cast<size_t>(channel)];
        // TODO: compute y from the difference equation and update s.x1 / s.y1
        (void)s;
        return x;
    }

    void process(AudioBlock block) noexcept {
        for (int ch = 0; ch < block.getNumChannels(); ++ch) {
            float* d = block.getChannelPointer(ch);
            for (int i = 0; i < block.getNumSamples(); ++i)
                d[i] = processSample(ch, d[i]);
        }
    }

    // Theoretical response: substitute z = e^{jω} into H(z)
    double magnitudeDb(double f, double sampleRate) const {
        // TODO: return 20 log10 |H(e^{jω})|, where ω = 2π f / fs
        (void)f;
        (void)sampleRate;
        return 0.0;
    }

private:
    struct State {
        double x1 = 0.0, y1 = 0.0;
    };
    double r_ = 0.995;
    std::vector<State> state_;
};

int main() {
    const float R = 0.995f;
    DcBlocker dc;
    dc.prepare(kSampleRate, 512, 1);
    dc.setR(R);

    // 1. Input = 0.5 of DC + a 0.25 1 kHz sine; the mean (DC) of the output should approach 0, and 1 kHz should be almost unchanged
    const int n = 48000;
    AudioBuffer x(1, n);
    for (int i = 0; i < n; ++i)
        x.setSample(0, i, 0.5f + static_cast<float>(0.25 * std::sin(kTwoPi * 1000.0 * i / kSampleRate)));
    AudioBuffer y = x;
    dc.process(y);

    double meanOut = 0.0;
    for (int i = n / 2; i < n; ++i)
        meanOut += y.getSample(0, i);
    meanOut /= n / 2;
    const double sineIn = goertzelAmplitude(x.getReadPointer(0) + n / 2, n / 2, 1000.0, kSampleRate);
    const double sineOut = goertzelAmplitude(y.getReadPointer(0) + n / 2, n / 2, 1000.0, kSampleRate);
    std::printf("DC: output mean %.6f; 1 kHz gain %.3f dB\n", meanOut, gainToDb(sineOut / sineIn));
    DAWLAB_CHECK(std::abs(meanOut) < 1e-3);
    DAWLAB_CHECK_NEAR(gainToDb(sineOut / sineIn), 0.0, 0.05);

    // 2. -3 dB point: theoretical approximation fs (1 - R) / (2π)
    const double f3Approx = kSampleRate * (1.0 - R) / kTwoPi;
    double f3 = 1.0;
    while (f3 < 1000.0 && dc.magnitudeDb(f3, kSampleRate) < -3.0103)
        f3 *= 1.001;
    std::printf("-3 dB point: theoretical approximation %.1f Hz, computed from H(z) %.1f Hz\n", f3Approx, f3);
    DAWLAB_CHECK_NEAR(f3, f3Approx, 1.0);

    // 3. Measurement
    dc.reset();
    const double measured3 = measureGainDb([&](float v) { return dc.processSample(0, v); }, f3, kSampleRate, 1.0, 0.5);
    std::printf("Measured: %.1f Hz = %.2f dB\n", f3, measured3);
    DAWLAB_CHECK_NEAR(measured3, -3.01, 0.1);

    return dawlab::test::summary("l02_ex2_starter");
}
