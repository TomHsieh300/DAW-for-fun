// Exercise 4: build a bandpass from a cascaded HPF + LPF (starter code).
//
// Complete the TODOs:
//   1. setBand(): set the two stages with makeHighpass(f_low) and makeLowpass(f_high) (both with Butterworth Q = 0.7071).
//   2. processSample(): run through the HPF first, then the LPF.
//   3. magnitudeDb(): add the dB values of the two stages.
// Verification: about -3 dB at f_low and f_high, about -0.5 dB at the center of the passband, about -24 dB two octaves outside the edges.
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
    void setBand(float lowHz, float highHz) noexcept {
        lowHz_ = lowHz;
        highHz_ = highHz;
        // TODO: set hp_ to highpass(lowHz) and lp_ to lowpass(highHz), both with kButterworthQ
    }
    float processSample(int ch, float x) noexcept {
        // TODO: chain the two stages
        return hp_.processSample(ch, x);
    }
    void process(AudioBlock block) noexcept {
        hp_.process(block);
        lp_.process(block);
    }
    double magnitudeDb(double f) const {
        // TODO: add the dB values of the two stages
        return hp_.getCoefficients().magnitudeDb(f, sampleRate_);
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

    std::printf("%8s  %12s  %12s\n", "f (Hz)", "cascade dB", "measured");
    for (double f : {125.0, 500.0, 1000.0, 2000.0, 8000.0}) {
        bp.reset();
        const double meas = measureGainDb([&](float x) { return bp.processSample(0, x); }, f, kSampleRate, 0.5, 0.5);
        std::printf("%8.1f  %12.2f  %12.2f\n", f, bp.magnitudeDb(f), meas);
        DAWLAB_CHECK_NEAR(meas, bp.magnitudeDb(f), 0.1);
    }
    const double otherSide = -10.0 * std::log10(1.0 + std::pow(fLow / fHigh, 4.0));
    DAWLAB_CHECK_NEAR(bp.magnitudeDb(fLow), -3.01 + otherSide, 0.05);
    DAWLAB_CHECK_NEAR(bp.magnitudeDb(fHigh), -3.01 + otherSide, 0.05);
    DAWLAB_CHECK_NEAR(bp.magnitudeDb(1000.0), -20.0 * std::log10(1.0 + std::pow(0.5, 4.0)), 0.05);
    DAWLAB_CHECK_NEAR(bp.magnitudeDb(fLow / 4.0), -24.1, 0.5);

    return dawlab::test::summary("l02_ex4_starter");
}
