// Exercise 6: FIR moving average vs. one-pole lowpass (starter code).
//
// Complete the TODOs:
//   1. processSample(): implement an N-point moving average with a running sum (add the new sample, subtract the one from N samples ago).
//   2. magnitudeDb(): |H(e^{jω})| = |sin(Nω/2) / (N sin(ω/2))|.
// Verification: a null at fs/N (-100 dB, the floor of gainToDb), the -3 dB point ~ 0.443 fs/N, and the step response settles in exactly N samples.
#include <dawlab/Common.h>
#include <dawlab/dsp/OnePole.h>

#include <complex>
#include <cstdio>
#include <vector>

using namespace dawlab;
using dawlab::dsp::OnePole;

constexpr double kSampleRate = 48000.0;

class MovingAverage {
public:
    void prepare(int length, int numChannels) {
        length_ = std::max(1, length);
        history_.assign(static_cast<size_t>(numChannels), std::vector<double>(static_cast<size_t>(length_), 0.0));
        sum_.assign(static_cast<size_t>(numChannels), 0.0);
        writeIndex_.assign(static_cast<size_t>(numChannels), 0);
    }
    void reset() noexcept {
        for (auto& h : history_)
            std::fill(h.begin(), h.end(), 0.0);
        std::fill(sum_.begin(), sum_.end(), 0.0);
        std::fill(writeIndex_.begin(), writeIndex_.end(), 0);
    }
    int getLength() const noexcept { return length_; }

    float processSample(int channel, float x) noexcept {
        const auto ch = static_cast<size_t>(channel);
        // TODO: sum += x - history[writeIndex]; history[writeIndex] = x; advance writeIndex; return sum / N
        (void)ch;
        return x;
    }

    void process(AudioBlock block) noexcept {
        const int numChannels = std::min(block.getNumChannels(), static_cast<int>(history_.size()));
        for (int ch = 0; ch < numChannels; ++ch) {
            float* d = block.getChannelPointer(ch);
            for (int i = 0; i < block.getNumSamples(); ++i)
                d[i] = processSample(ch, d[i]);
        }
    }

    double magnitudeDb(double f, double sampleRate) const {
        // TODO: ω = 2π f / fs; return 0 dB when ω ~ 0, otherwise 20 log10 |sin(Nω/2) / (N sin(ω/2))|
        (void)f;
        (void)sampleRate;
        return 0.0;
    }

private:
    int length_ = 1;
    std::vector<std::vector<double>> history_;
    std::vector<double> sum_;
    std::vector<int> writeIndex_;
};

int main() {
    const int N = 8;
    MovingAverage ma;
    ma.prepare(N, 1);
    const double firstNull = kSampleRate / N;
    const double f3Approx = 0.443 * kSampleRate / N;

    double f3 = 10.0;
    while (f3 < 20000.0 && ma.magnitudeDb(f3, kSampleRate) > -3.0103)
        f3 *= 1.0005;
    std::printf("-3 dB point = %.0f Hz (approximation %.0f)\n", f3, f3Approx);
    DAWLAB_CHECK_NEAR(f3, f3Approx, f3Approx * 0.02);

    for (double f : {100.0, 1000.0, 4000.0, firstNull, 1.5 * firstNull}) {
        ma.reset();
        const double theory = ma.magnitudeDb(f, kSampleRate);
        const double meas = measureGainDb([&](float x) { return ma.processSample(0, x); }, f, kSampleRate, 0.5, 0.1);
        std::printf("%9.0f Hz  theory %8.2f dB  measured %8.2f dB\n", f, theory, meas);
        if (theory > -50.0)
            DAWLAB_CHECK_NEAR(meas, theory, 0.1);
        else
            DAWLAB_CHECK(meas < -50.0);
    }
    DAWLAB_CHECK(ma.magnitudeDb(firstNull, kSampleRate) <= -99.9); // the floor of gainToDb is -100 dB
    DAWLAB_CHECK_NEAR(ma.magnitudeDb(1.5 * firstNull, kSampleRate), -12.96, 0.05);

    ma.reset();
    int maSettle = -1;
    for (int i = 0; i < 64; ++i)
        if (maSettle < 0 && std::abs(ma.processSample(0, 1.0f) - 1.0f) < 1e-6f)
            maSettle = i + 1;
    std::printf("The step response takes %d samples to settle (should be exactly N = %d)\n", maSettle, N);
    DAWLAB_CHECK(maSettle == N);

    return dawlab::test::summary("l02_ex6_starter");
}
