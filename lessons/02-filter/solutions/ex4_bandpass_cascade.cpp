// Exercise 4 solution: build a bandpass from a cascaded HPF + LPF and compare it with RBJ's second-order bandpass.
//
// Cascade a Butterworth highpass (f_low) with a Butterworth lowpass (f_high) and you get a
// bandpass that rolls off at 12 dB/oct on both sides: H(z) = H_hp(z) · H_lp(z), and the dB values simply add.
// Compared with RBJ's second-order bandpass (6 dB/oct on each side, with just one fc and Q):
//   - The cascade lets you set the lower and upper edges independently, and the slopes are
//     steeper (the attenuation grows twice as fast away from the passband).
//   - The center of the passband is not exactly 0 dB: each filter still attenuates a little near
//     the other's cutoff, and the closer f_low and f_high are, the more the center sags (here
//     500 Hz ~ 2 kHz, with about -0.5 dB at the 1 kHz center).
#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>

#include <cstdio>

using namespace dawlab;
using dawlab::dsp::Biquad;
using dawlab::dsp::BiquadCoefficients;

constexpr double kSampleRate = 48000.0;

class BandpassCascade {
public:
    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        sampleRate_ = sampleRate;
        hp_.prepare(sampleRate, maxBlockSize, numChannels);
        lp_.prepare(sampleRate, maxBlockSize, numChannels);
        setBand(lowHz_, highHz_);
    }
    void reset() noexcept {
        hp_.reset();
        lp_.reset();
    }
    // Below f_low and above f_high the response rolls off at 12 dB/oct (two second-order Butterworths)
    void setBand(float lowHz, float highHz) noexcept {
        lowHz_ = lowHz;
        highHz_ = highHz;
        hp_.setCoefficients(BiquadCoefficients::makeHighpass(sampleRate_, lowHz, BiquadCoefficients::kButterworthQ));
        lp_.setCoefficients(BiquadCoefficients::makeLowpass(sampleRate_, highHz, BiquadCoefficients::kButterworthQ));
    }
    float processSample(int ch, float x) noexcept { return lp_.processSample(ch, hp_.processSample(ch, x)); }
    void process(AudioBlock block) noexcept {
        hp_.process(block);
        lp_.process(block);
    }
    // Cascade = responses multiply = dB values add
    double magnitudeDb(double f) const {
        return hp_.getCoefficients().magnitudeDb(f, sampleRate_) + lp_.getCoefficients().magnitudeDb(f, sampleRate_);
    }

private:
    double sampleRate_ = 48000.0;
    float lowHz_ = 500.0f, highHz_ = 2000.0f;
    Biquad hp_, lp_;
};

int main() {
    const double fLow = 500.0, fHigh = 2000.0;
    BandpassCascade bp;
    bp.prepare(kSampleRate, 512, 1);
    bp.setBand(static_cast<float>(fLow), static_cast<float>(fHigh));

    // Control group: an RBJ bandpass centered at the geometric mean sqrt(500·2000) = 1000 Hz, Q = fc / BW = 1000 / 1500
    const double fc = std::sqrt(fLow * fHigh);
    const double q = fc / (fHigh - fLow);
    const BiquadCoefficients rbj = BiquadCoefficients::makeBandpass(kSampleRate, fc, q);
    std::printf("HPF %.0f Hz + LPF %.0f Hz (each Q=0.707) vs. RBJ bandpass fc=%.0f Hz, Q=%.3f\n\n", fLow, fHigh, fc, q);

    std::printf("%8s  %12s  %12s  %12s\n", "f (Hz)", "cascade dB", "measured", "RBJ BPF dB");
    for (double f : {62.5, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0}) {
        bp.reset();
        const double meas = measureGainDb([&](float x) { return bp.processSample(0, x); }, f, kSampleRate, 0.5, 0.5);
        std::printf("%8.1f  %12.2f  %12.2f  %12.2f\n", f, bp.magnitudeDb(f), meas, rbj.magnitudeDb(f, kSampleRate));
        DAWLAB_CHECK_NEAR(meas, bp.magnitudeDb(f), 0.1);
    }

    // 1. About -3 dB at each edge (the other filter contributes only -0.02 dB there: -10 log10(1 + 0.25^4))
    const double otherSide = -10.0 * std::log10(1.0 + std::pow(fLow / fHigh, 4.0));
    std::printf("\nAttenuation of the other filter at the edge = %.3f dB\n", otherSide);
    DAWLAB_CHECK_NEAR(bp.magnitudeDb(fLow), -3.01 + otherSide, 0.05);
    DAWLAB_CHECK_NEAR(bp.magnitudeDb(fHigh), -3.01 + otherSide, 0.05);

    // 2. Center of the passband (1 kHz): the HPF and LPF each drop -10 log10(1 + 0.5^4) = -0.26 dB, about -0.53 dB in total
    const double centreTheory = -20.0 * std::log10(1.0 + std::pow(0.5, 4.0));
    std::printf("Passband center %.0f Hz: %.2f dB (analog theory %.2f dB); the RBJ bandpass is always 0 dB at fc\n", fc,
                bp.magnitudeDb(fc), centreTheory);
    DAWLAB_CHECK_NEAR(bp.magnitudeDb(fc), centreTheory, 0.05);
    DAWLAB_CHECK_NEAR(rbj.magnitudeDb(fc, kSampleRate), 0.0, 0.01);

    // 3. Slope: 2 octaves from the edges (125 Hz, 8 kHz) the cascade is ~ -24 dB (12 dB/oct).
    //    The analog prototype of the RBJ second-order bandpass is |H| = 1 / sqrt(1 + Q² (f/fc - fc/f)²),
    //    which is -14.55 dB at fc/8 and 8 fc (far from fc it only drops 6 dB per octave).
    //    On the 8 kHz side both are another 1~2 dB steeper than analog because of cramping.
    const double cascadeLow = bp.magnitudeDb(fLow / 4.0), cascadeHigh = bp.magnitudeDb(fHigh * 4.0);
    const double rbjLow = rbj.magnitudeDb(fLow / 4.0, kSampleRate), rbjHigh = rbj.magnitudeDb(fHigh * 4.0, kSampleRate);
    const double rbjAnalog = -10.0 * std::log10(1.0 + q * q * std::pow(8.0 - 1.0 / 8.0, 2.0));
    std::printf("2 octaves outside the edges: cascade %.1f / %.1f dB, RBJ %.1f / %.1f dB (RBJ analog theory %.2f dB)\n", cascadeLow,
                cascadeHigh, rbjLow, rbjHigh, rbjAnalog);
    DAWLAB_CHECK_NEAR(cascadeLow, -24.1, 0.5);
    DAWLAB_CHECK_NEAR(cascadeHigh, -24.1, 2.0); // 8 kHz is already affected by cramping (about -25.7 dB)
    DAWLAB_CHECK_NEAR(rbjLow, rbjAnalog, 0.2);
    DAWLAB_CHECK_NEAR(rbjHigh, rbjAnalog, 1.5);
    DAWLAB_CHECK(rbjLow > cascadeLow + 6.0 && rbjHigh > cascadeHigh + 6.0); // the cascade is clearly much steeper

    // Listen: white noise through the two bandpasses
    AudioBuffer noise = makeWhiteNoise(kSampleRate, 2.0, 0.25f, 2);
    AudioBuffer outCascade = noise, outRbj = noise;
    BandpassCascade bp2;
    bp2.prepare(kSampleRate, 512, 2);
    bp2.setBand(static_cast<float>(fLow), static_cast<float>(fHigh));
    Biquad bq;
    bq.prepare(kSampleRate, 512, 2);
    bq.setCoefficients(rbj);
    AudioBlock wc(outCascade), wr(outRbj);
    for (int start = 0; start < noise.getNumSamples(); start += 512) {
        const int len = std::min(512, noise.getNumSamples() - start);
        bp2.process(wc.getSubBlock(start, len));
        bq.process(wr.getSubBlock(start, len));
    }
    writeWav(outputPath("l02_ex4_noise_cascade.wav"), outCascade, kSampleRate);
    writeWav(outputPath("l02_ex4_noise_rbj_bpf.wav"), outRbj, kSampleRate);
    std::printf("Noise RMS: dry %.2f dB -> cascade %.2f dB, RBJ %.2f dB (the steeper version leaves less energy)\n",
                gainToDb(rms(noise, 0)), gainToDb(rms(outCascade, 0)), gainToDb(rms(outRbj, 0)));
    DAWLAB_CHECK(rms(outCascade, 0) < rms(outRbj, 0));
    DAWLAB_CHECK(!hasNanOrInf(outCascade) && !hasNanOrInf(outRbj));

    return dawlab::test::summary("l02_ex4");
}
