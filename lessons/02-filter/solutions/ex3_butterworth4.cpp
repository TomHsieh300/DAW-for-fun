// Exercise 3 solution: two cascaded biquads forming a 4th-order Butterworth lowpass (24 dB/oct).
//
// For an N-th order Butterworth, |H(jΩ)|² = 1 / (1 + (Ω/Ωc)^(2N)): the passband is maximally flat
// and the cutoff is always -3 dB. When split into second-order stages, the Q of each stage comes
// from the pole angle: for even N, Q_k = 1 / (2 cos((2k+1)π/(2N))),
// and N = 4 -> Q = 0.5412 (k=0) and 1.3066 (k=1). Both stages use the same fc.
// (For odd N the angle becomes (k+1)π/N, plus one extra one-pole stage; at the end we also verify the 3rd-order case.)
//
// Note: cascading two stages that both use Q = 0.707 is *not* a Butterworth -- the cutoff becomes -6 dB and the passband is no longer maximally flat.
#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>

#include <cstdio>

using namespace dawlab;
using dawlab::dsp::Biquad;
using dawlab::dsp::BiquadCoefficients;
using dawlab::dsp::butterworthQ;

constexpr double kSampleRate = 48000.0;

// Two cascaded biquad stages
class Butterworth4Lowpass {
public:
    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        sampleRate_ = sampleRate;
        for (Biquad& s : stages_)
            s.prepare(sampleRate, maxBlockSize, numChannels);
        setCutoff(cutoff_);
    }
    void reset() noexcept {
        for (Biquad& s : stages_)
            s.reset();
    }
    void setCutoff(float hz) noexcept {
        cutoff_ = hz;
        for (int k = 0; k < 2; ++k)
            stages_[k].setCoefficients(BiquadCoefficients::makeLowpass(sampleRate_, hz, butterworthQ(4, k)));
    }
    float processSample(int ch, float x) noexcept {
        return stages_[1].processSample(ch, stages_[0].processSample(ch, x));
    }
    void process(AudioBlock block) noexcept {
        for (Biquad& s : stages_)
            s.process(block);
    }
    // Overall response = product of the stage responses (dB values add)
    double magnitudeDb(double f) const {
        return stages_[0].getCoefficients().magnitudeDb(f, sampleRate_) +
               stages_[1].getCoefficients().magnitudeDb(f, sampleRate_);
    }

private:
    double sampleRate_ = 48000.0;
    float cutoff_ = 1000.0f;
    Biquad stages_[2];
};

int main() {
    std::printf("Q values of a 4th-order Butterworth: %.4f, %.4f\n", butterworthQ(4, 0), butterworthQ(4, 1));
    DAWLAB_CHECK_NEAR(butterworthQ(4, 0), 0.5412, 1e-4);
    DAWLAB_CHECK_NEAR(butterworthQ(4, 1), 1.3066, 1e-4);
    DAWLAB_CHECK_NEAR(butterworthQ(2, 0), 0.70711, 1e-4);

    // Odd orders: the pole angle (measured from the negative real axis) is kπ/N, not (2k+1)π/(2N).
    // 3rd order = one one-pole stage (the real pole at φ = 0) + one Q = 1 biquad (φ = π/3);
    // 5th order = one-pole + two stages with Q = 0.618 and 1.618.
    std::printf("Q values for odd orders: N=3 -> %.4f; N=5 -> %.4f, %.4f\n", butterworthQ(3, 0), butterworthQ(5, 0),
                butterworthQ(5, 1));
    DAWLAB_CHECK_NEAR(butterworthQ(3, 0), 1.0, 1e-9);
    DAWLAB_CHECK_NEAR(butterworthQ(5, 0), 0.6180, 1e-4);
    DAWLAB_CHECK_NEAR(butterworthQ(5, 1), 1.6180, 1e-4);
    {
        // Verify: a bilinear one-pole (-3 dB exactly at fc) cascaded with makeLowpass(fc, butterworthQ(3, 0)) = a 3rd-order Butterworth
        const double fc3 = 1000.0;
        const double g = std::tan(kPi * fc3 / kSampleRate);
        const BiquadCoefficients bq3 = BiquadCoefficients::makeLowpass(kSampleRate, fc3, butterworthQ(3, 0));
        const auto order3Db = [&](double f) {
            const std::complex<double> z1 = std::polar(1.0, -kTwoPi * f / kSampleRate);
            const double onePoleDb = gainToDb(std::abs(g * (1.0 + z1) / ((1.0 + g) - (1.0 - g) * z1)));
            return onePoleDb + bq3.magnitudeDb(f, kSampleRate);
        };
        const double analog500 = -10.0 * std::log10(1.0 + std::pow(500.0 / fc3, 6.0)); // |H|² = 1/(1 + (f/fc)^6)
        std::printf("3rd-order Butterworth (one-pole + Q=1 biquad): %.2f dB at fc, %.3f dB at 500 Hz (analog %.3f dB)\n",
                    order3Db(fc3), order3Db(500.0), analog500);
        DAWLAB_CHECK_NEAR(order3Db(fc3), -3.01, 0.01);
        DAWLAB_CHECK_NEAR(order3Db(500.0), analog500, 0.01);
    }

    const double fc = 1000.0;
    Butterworth4Lowpass lp;
    lp.prepare(kSampleRate, 512, 1);
    lp.setCutoff(static_cast<float>(fc));

    // Control groups: a single biquad (12 dB/oct), and two Q=0.707 stages (the wrong way to cascade)
    const BiquadCoefficients single = BiquadCoefficients::makeLowpass(kSampleRate, fc, BiquadCoefficients::kButterworthQ);

    std::printf("\n%8s  %12s  %12s  %12s  %14s\n", "f (Hz)", "analog BW4", "2-stage dB", "measured", "single biquad");
    for (double f : {100.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0}) {
        // Analog 4th-order Butterworth: -10 log10(1 + (f/fc)^8)
        const double analog = -10.0 * std::log10(1.0 + std::pow(f / fc, 8.0));
        lp.reset();
        const double meas = measureGainDb([&](float x) { return lp.processSample(0, x); }, f, kSampleRate, 0.5, 0.5);
        std::printf("%8.0f  %12.2f  %12.2f  %12.2f  %14.2f\n", f, analog, lp.magnitudeDb(f), meas,
                    single.magnitudeDb(f, kSampleRate));
        DAWLAB_CHECK_NEAR(meas, lp.magnitudeDb(f), 0.1);
        // Far from Nyquist digital ~ analog; cramping has already started at 4 kHz (fs/12), where digital is about 0.8 dB steeper than analog
        if (f <= 2000.0)
            DAWLAB_CHECK_NEAR(lp.magnitudeDb(f), analog, 0.3);
        else if (f <= 4000.0)
            DAWLAB_CHECK_NEAR(lp.magnitudeDb(f), analog, 1.0);
    }

    // Key numbers: -3.01 dB at the cutoff; ~ -24.1 dB one octave out (-10 log10(1 + 2^8) = -24.10);
    // two octaves out analog is -48.2 dB, and digital is about -48.9 dB because of cramping
    DAWLAB_CHECK_NEAR(lp.magnitudeDb(fc), -3.01, 0.05);
    DAWLAB_CHECK_NEAR(lp.magnitudeDb(2.0 * fc), -24.1, 0.3);
    DAWLAB_CHECK_NEAR(lp.magnitudeDb(4.0 * fc), -48.2, 1.0);
    // Maximally flat passband: only 0.017 dB down at 500 Hz
    DAWLAB_CHECK_NEAR(lp.magnitudeDb(500.0), -0.017, 0.02);

    // Counter-example: two cascaded Q = 0.707 stages give -6 dB at the cutoff
    const double wrong = 2.0 * single.magnitudeDb(fc, kSampleRate);
    std::printf("\nWith two Q=0.707 stages cascaded, the response at the cutoff = %.2f dB (not a Butterworth)\n", wrong);
    DAWLAB_CHECK_NEAR(wrong, -6.02, 0.1);

    // Listen: white noise through the 4th-order vs. the 2nd-order filter
    AudioBuffer noise = makeWhiteNoise(kSampleRate, 2.0, 0.25f, 2);
    AudioBuffer out4 = noise, out2 = noise;
    Butterworth4Lowpass lp2;
    lp2.prepare(kSampleRate, 512, 2);
    lp2.setCutoff(static_cast<float>(fc));
    Biquad bq2;
    bq2.prepare(kSampleRate, 512, 2);
    bq2.setCoefficients(single);
    AudioBlock w4(out4), w2(out2);
    for (int start = 0; start < noise.getNumSamples(); start += 512) {
        const int len = std::min(512, noise.getNumSamples() - start);
        lp2.process(w4.getSubBlock(start, len));
        bq2.process(w2.getSubBlock(start, len));
    }
    writeWav(outputPath("l02_ex3_noise_butterworth4.wav"), out4, kSampleRate);
    writeWav(outputPath("l02_ex3_noise_biquad2.wav"), out2, kSampleRate);
    std::printf("Noise RMS: dry %.2f dB -> 2nd order %.2f dB -> 4th order %.2f dB (steeper = less high-frequency energy left)\n",
                gainToDb(rms(noise, 0)), gainToDb(rms(out2, 0)), gainToDb(rms(out4, 0)));
    DAWLAB_CHECK(rms(out4, 0) < rms(out2, 0));

    return dawlab::test::summary("l02_ex3");
}
