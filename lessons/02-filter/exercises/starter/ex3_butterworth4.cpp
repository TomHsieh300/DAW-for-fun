// Exercise 3: two cascaded biquads forming a 4th-order Butterworth lowpass (starter code).
//
// Complete the TODOs:
//   1. Work out the Q of each of the two stages of a 4th-order Butterworth (hint: for even N,
//      Q_k = 1 / (2 cos((2k+1)π/(2N))), or use dsp::butterworthQ).
//   2. In setCutoff(), set the two stages with the same fc but different Qs.
//   3. processSample() chains the two stages; magnitudeDb() adds the dB values of the two stages.
// Verification: -3.01 dB at the cutoff, about -24.1 dB at 2 fc.
#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>

#include <cstdio>

using namespace dawlab;
using dawlab::dsp::Biquad;
using dawlab::dsp::BiquadCoefficients;

constexpr double kSampleRate = 48000.0;

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
        // TODO: the two stages have different Q values (0.5412 and 1.3066); for now both use 0.707 -- this is wrong, please fix it
        for (Biquad& s : stages_)
            s.setCoefficients(BiquadCoefficients::makeLowpass(sampleRate_, hz, 0.7071));
    }
    float processSample(int ch, float x) noexcept {
        // TODO: chain the two stages
        return stages_[0].processSample(ch, x);
    }
    void process(AudioBlock block) noexcept {
        for (Biquad& s : stages_)
            s.process(block);
    }
    double magnitudeDb(double f) const {
        // TODO: return the sum of the two stages' dB values
        return stages_[0].getCoefficients().magnitudeDb(f, sampleRate_);
    }

private:
    double sampleRate_ = 48000.0;
    float cutoff_ = 1000.0f;
    Biquad stages_[2];
};

int main() {
    const double fc = 1000.0;
    Butterworth4Lowpass lp;
    lp.prepare(kSampleRate, 512, 1);
    lp.setCutoff(static_cast<float>(fc));

    std::printf("%8s  %12s  %12s  %12s\n", "f (Hz)", "analog BW4", "2-stage dB", "measured");
    for (double f : {100.0, 500.0, 1000.0, 2000.0, 4000.0}) {
        const double analog = -10.0 * std::log10(1.0 + std::pow(f / fc, 8.0));
        lp.reset();
        const double meas = measureGainDb([&](float x) { return lp.processSample(0, x); }, f, kSampleRate, 0.5, 0.5);
        std::printf("%8.0f  %12.2f  %12.2f  %12.2f\n", f, analog, lp.magnitudeDb(f), meas);
        DAWLAB_CHECK_NEAR(meas, lp.magnitudeDb(f), 0.1);
        // Cramping has already started at 4 kHz (the digital response is about 0.8 dB steeper than analog), so the tolerance is relaxed
        DAWLAB_CHECK_NEAR(lp.magnitudeDb(f), analog, f <= 2000.0 ? 0.3 : 1.0);
    }
    DAWLAB_CHECK_NEAR(lp.magnitudeDb(fc), -3.01, 0.05);
    DAWLAB_CHECK_NEAR(lp.magnitudeDb(2.0 * fc), -24.1, 0.3);

    return dawlab::test::summary("l02_ex3_starter");
}
