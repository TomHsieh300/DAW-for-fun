// Exercise 6 solution: FIR moving average vs. one-pole lowpass.
//
// N-point moving average: y[n] = (1/N) Σ_{k=0}^{N-1} x[n-k]
//   - FIR: the impulse response has only N nonzero values and ends completely after N samples
//     (the step response settles in exactly N samples).
//   - Frequency response H(e^{jω}) = (1/N) · sin(Nω/2) / sin(ω/2) · e^{-jω(N-1)/2}:
//       * exactly 0 (a null) at f = k · fs / N (k = 1, 2, ...)
//       * after the first null there are "sidelobes", the first about -13 dB, unlike a one-pole that keeps falling
//       * the phase is linear: a fixed delay of (N-1)/2 samples (linear phase)
//   - the -3 dB point ~ 0.443 · fs / N
// Implemented with a running sum: each sample needs just one addition and one subtraction, independent of N (O(1)).
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
        auto& h = history_[ch];
        auto& idx = writeIndex_[ch];
        // Running sum: add the new sample and subtract the one from N samples ago
        sum_[ch] += x - h[static_cast<size_t>(idx)];
        h[static_cast<size_t>(idx)] = x;
        idx = (idx + 1) % length_;
        return static_cast<float>(sum_[ch] / length_);
    }

    void process(AudioBlock block) noexcept {
        const int numChannels = std::min(block.getNumChannels(), static_cast<int>(history_.size()));
        for (int ch = 0; ch < numChannels; ++ch) {
            float* d = block.getChannelPointer(ch);
            for (int i = 0; i < block.getNumSamples(); ++i)
                d[i] = processSample(ch, d[i]);
        }
    }

    // |H(e^{jω})| = |sin(Nω/2) / (N sin(ω/2))|
    double magnitudeDb(double f, double sampleRate) const {
        const double w = kTwoPi * f / sampleRate;
        if (std::abs(w) < 1e-12)
            return 0.0;
        return gainToDb(std::abs(std::sin(length_ * w * 0.5) / (length_ * std::sin(w * 0.5))));
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
    const double firstNull = kSampleRate / N;        // 6 kHz
    const double f3Approx = 0.443 * kSampleRate / N; // ~ 2658 Hz

    // Find the moving average's true -3 dB point, then set a one-pole to the same cutoff for comparison
    double f3 = 10.0;
    while (ma.magnitudeDb(f3, kSampleRate) > -3.0103)
        f3 *= 1.0005;
    OnePole op;
    op.prepare(kSampleRate, 512, 1);
    op.setCutoff(static_cast<float>(f3));
    std::printf("Moving average with N = %d: the first null is at fs/N = %.0f Hz, the -3 dB point = %.0f Hz (approximation 0.443 fs/N = %.0f)\n\n",
                N, firstNull, f3, f3Approx);
    DAWLAB_CHECK_NEAR(f3, f3Approx, f3Approx * 0.02);

    std::printf("%9s  %12s  %12s  %12s\n", "f (Hz)", "MA theory", "MA measured", "one-pole");
    for (double f : {100.0, 1000.0, f3, 4000.0, firstNull, 1.5 * firstNull, 2.0 * firstNull, 20000.0}) {
        ma.reset();
        op.reset();
        const double theory = ma.magnitudeDb(f, kSampleRate);
        const double meas = measureGainDb([&](float x) { return ma.processSample(0, x); }, f, kSampleRate, 0.5, 0.1);
        const double onePole = measureGainDb([&](float x) { return op.processSample(0, x); }, f, kSampleRate, 0.5, 0.2);
        std::printf("%9.0f  %12.2f  %12.2f  %12.2f\n", f, theory, meas, onePole);
        if (theory > -50.0)
            DAWLAB_CHECK_NEAR(meas, theory, 0.1);
        else
            DAWLAB_CHECK(meas < -50.0);
    }
    // 1. Nulls: fs/N and 2 fs/N vanish completely; the one-pole attenuates only a few dB there
    // The floor of gainToDb() is -100 dB (kMinusInfinityDb), so a null prints -100.00
    DAWLAB_CHECK(ma.magnitudeDb(firstNull, kSampleRate) <= -99.9);
    DAWLAB_CHECK(ma.magnitudeDb(2.0 * firstNull, kSampleRate) <= -99.9);
    // 2. Sidelobe: after the first null the response "rises again" to about -13 dB at 1.5 fs/N (it does not keep falling)
    //    Exact value: |sin(1.5π)/(8 sin(1.5π/8))| = 1/(8 sin(0.589)) = 0.2250 -> -12.96 dB
    const double sidelobe = ma.magnitudeDb(1.5 * firstNull, kSampleRate);
    std::printf("\nFirst sidelobe (1.5 fs/N = %.0f Hz) = %.2f dB; the one-pole at the same frequency = %.2f dB\n", 1.5 * firstNull, sidelobe,
                measureGainDb([&](float x) { return op.processSample(0, x); }, 1.5 * firstNull, kSampleRate));
    DAWLAB_CHECK_NEAR(sidelobe, -12.96, 0.05);
    // 3. Both are 0 dB at DC
    DAWLAB_CHECK_NEAR(ma.magnitudeDb(0.0, kSampleRate), 0.0, 1e-9);

    // 4. Step response: the moving average settles in exactly N samples; the one-pole only ever "approaches"
    ma.reset();
    op.reset();
    int maSettle = -1, opSettle = -1;
    for (int i = 0; i < 4096; ++i) {
        const float a = ma.processSample(0, 1.0f);
        const float b = op.processSample(0, 1.0f);
        if (maSettle < 0 && std::abs(a - 1.0f) < 1e-6f)
            maSettle = i + 1;
        if (opSettle < 0 && std::abs(b - 1.0f) < 0.01f)
            opSettle = i + 1;
    }
    const double tau = -1.0 / std::log(op.getCoefficient());
    std::printf("Step response: the moving average equals exactly 1 at sample %d; the one-pole needs %d samples to reach 99%% (τ = %.1f, 4.6τ ~ %.0f)\n",
                maSettle, opSettle, tau, 4.605 * tau);
    DAWLAB_CHECK(maSettle == N);
    DAWLAB_CHECK_NEAR(opSettle, 4.605 * tau, 2.0);

    // 5. Impulse response: the FIR has finite length (all zeros after the N-th sample), the IIR is infinitely long
    ma.reset();
    op.reset();
    std::printf("Impulse response: MA  = ");
    for (int i = 0; i < N + 2; ++i)
        std::printf("%.3f ", ma.processSample(0, i == 0 ? 1.0f : 0.0f));
    std::printf("\n                  1-p = ");
    float opTail = 0.0f;
    for (int i = 0; i < N + 2; ++i)
        std::printf("%.3f ", opTail = op.processSample(0, i == 0 ? 1.0f : 0.0f));
    std::printf("...\n");
    DAWLAB_CHECK(opTail > 0.0f);

    // 6. Linear phase: the group delay of the moving average is a fixed (N-1)/2 samples.
    //    Verify: feed in a 1 kHz sine and measure the output's phase relative to the input -> delay = -phase / ω
    {
        const double f = 1000.0, w = kTwoPi * f / kSampleRate;
        ma.reset();
        const int n = 4800;
        std::vector<float> in(static_cast<size_t>(n)), out(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            in[static_cast<size_t>(i)] = static_cast<float>(std::sin(w * i));
            out[static_cast<size_t>(i)] = ma.processSample(0, in[static_cast<size_t>(i)]);
        }
        // Estimate the phase by quadrature demodulation: Σ y · e^{-jωn} (skipping the transient of the first N samples)
        std::complex<double> accIn = 0.0, accOut = 0.0;
        for (int i = N; i < n; ++i) {
            const std::complex<double> e = std::polar(1.0, -w * i);
            accIn += static_cast<double>(in[static_cast<size_t>(i)]) * e;
            accOut += static_cast<double>(out[static_cast<size_t>(i)]) * e;
        }
        const double phaseShift = std::arg(accOut / accIn); // negative = delay
        const double delaySamples = -phaseShift / w;
        std::printf("Group delay at 1 kHz: %.3f samples (theory (N-1)/2 = %.1f)\n", delaySamples, (N - 1) / 2.0);
        DAWLAB_CHECK_NEAR(delaySamples, (N - 1) / 2.0, 0.01);
    }

    // Listen: white noise through the moving average (with its comb-like nulls) vs. the one-pole
    AudioBuffer noise = makeWhiteNoise(kSampleRate, 2.0, 0.25f, 2);
    AudioBuffer outMa = noise, outOp = noise;
    MovingAverage ma2;
    ma2.prepare(N, 2);
    OnePole op2;
    op2.prepare(kSampleRate, 512, 2);
    op2.setCutoff(static_cast<float>(f3));
    AudioBlock wm(outMa), wo(outOp);
    for (int start = 0; start < noise.getNumSamples(); start += 512) {
        const int len = std::min(512, noise.getNumSamples() - start);
        ma2.process(wm.getSubBlock(start, len));
        op2.process(wo.getSubBlock(start, len));
    }
    writeWav(outputPath("l02_ex6_noise_moving_average.wav"), outMa, kSampleRate);
    writeWav(outputPath("l02_ex6_noise_one_pole.wav"), outOp, kSampleRate);
    std::printf("Noise RMS: dry %.2f dB -> moving average %.2f dB, one-pole %.2f dB\n", gainToDb(rms(noise, 0)),
                gainToDb(rms(outMa, 0)), gainToDb(rms(outOp, 0)));
    DAWLAB_CHECK(!hasNanOrInf(outMa) && !hasNanOrInf(outOp));

    return dawlab::test::summary("l02_ex6");
}
