// Exercise 4 (starter): a simple cabinet simulator built from biquads
//
// Goal: write a CabSim class whose frequency response =
//     low cut (2nd-order Butterworth highpass, default 70 Hz)
//   × resonance peak (makePeak, default 100 Hz, Q 1.5, +4 dB, modeling the driver / cabinet resonance)
//   × 4th-order Linkwitz-Riley lowpass (two Q = 1/√2 Butterworth lowpass filters in cascade, default 4.5 kHz)
//
// Required: prepare / reset / setLowCutHz / setResonanceHz / setResonanceDb / setCutoffHz /
//          processSample(ch, x) / process(block) / double magnitudeDb(freq) (for analysis: sum the magnitudeDb of every stage)
//
// Run: l07_ex4_starter → it currently FAILs; once you finish, everything should PASS.

#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>

#include <cmath>
#include <cstdio>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr double kButterworthQ = 0.70710678118654752; // 1/√2: the Q of a 2nd-order Butterworth

class CabSim {
public:
    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        sampleRate_ = sampleRate;
        // TODO: prepare the four biquads, then updateCoefficients()
        (void)maxBlockSize;
        (void)numChannels;
    }
    void reset() noexcept {
        // TODO
    }
    void setLowCutHz(float hz) noexcept { lowCutHz_ = hz; updateCoefficients(); }
    void setResonanceHz(float hz) noexcept { resHz_ = hz; updateCoefficients(); }
    void setResonanceDb(float db) noexcept { resDb_ = db; updateCoefficients(); }
    void setCutoffHz(float hz) noexcept { cutoffHz_ = hz; updateCoefficients(); }

    float processSample(int ch, float x) noexcept {
        // TODO: lowCut → resonance → lp1 → lp2
        (void)ch;
        return x;
    }
    void process(dawlab::AudioBlock block) noexcept {
        for (int ch = 0; ch < block.getNumChannels(); ++ch) {
            float* d = block.getChannelPointer(ch);
            for (int i = 0; i < block.getNumSamples(); ++i)
                d[i] = processSample(ch, d[i]);
        }
    }

    // Cascaded linear stages → the dB values of the stages add
    double magnitudeDb(double freq) const noexcept {
        // TODO
        (void)freq;
        return 0.0;
    }

private:
    void updateCoefficients() noexcept {
        if (sampleRate_ <= 0.0)
            return;
        // TODO: compute the coefficients with BiquadCoefficients::makeHighpass / makePeak / makeLowpass
    }

    double sampleRate_ = 0.0;
    float lowCutHz_ = 70.0f, resHz_ = 100.0f, resDb_ = 4.0f, cutoffHz_ = 4500.0f;
    dawlab::dsp::Biquad lowCut_, resonance_, lp1_, lp2_;
};

} // namespace

int main() {
    CabSim cab;
    cab.prepare(kSampleRate, kBlockSize, 1);
    cab.setLowCutHz(70.0f);
    cab.setResonanceHz(100.0f);
    cab.setResonanceDb(4.0f);
    cab.setCutoffHz(4500.0f);

    // Key points of the analytic response (magnitudeDb)
    std::printf("%8s %12s %12s\n", "freq", "analytic", "measured");
    bool consistent = true;
    for (double f : {30.0, 70.0, 100.0, 200.0, 1000.0, 2250.0, 4500.0, 9000.0, 15000.0}) {
        cab.reset();
        const double analytic = cab.magnitudeDb(f);
        const double measured = dawlab::measureGainDb([&](float x) { return cab.processSample(0, x); }, f, kSampleRate);
        std::printf("%8.0f %12.2f %12.2f\n", f, analytic, measured);
        consistent = consistent && std::abs(analytic - measured) < 0.2;
    }
    DAWLAB_CHECK(consistent);                                        // actual processing = analytic formula
    DAWLAB_CHECK_NEAR(cab.magnitudeDb(4500.0), -6.02, 0.3);          // LR4: -6 dB at the cutoff
    // One octave up: the analog LR4 would be -24.6 dB, but the frequency warping of the bilinear transform makes the digital filter
    // fall faster at high frequencies: the equivalent analog frequency ratio = tan(π f/fs) / tan(π fc/fs) (9 kHz / 4.5 kHz @ 48 kHz → 2.20 rather than 2)
    {
        const double r = std::tan(dawlab::kPi * 9000.0 / kSampleRate) / std::tan(dawlab::kPi * 4500.0 / kSampleRate);
        const double expectedDb = -2.0 * 10.0 * std::log10(1.0 + r * r * r * r); // two 2nd-order Butterworth sections
        std::printf("9 kHz: warped ratio %.3f → expected %.2f dB (unwarped analog value -24.6 dB)\n", r, expectedDb);
        DAWLAB_CHECK_NEAR(cab.magnitudeDb(9000.0), expectedDb, 0.5);
    }
    DAWLAB_CHECK(cab.magnitudeDb(1000.0) > -0.5 && cab.magnitudeDb(1000.0) < 0.5); // transparent mids
    DAWLAB_CHECK(cab.magnitudeDb(100.0) > 2.5 && cab.magnitudeDb(100.0) < 4.5);    // resonance (minus a little loss from the low cut)
    DAWLAB_CHECK(cab.magnitudeDb(30.0) < -12.0);                                     // low cut
    DAWLAB_CHECK(cab.magnitudeDb(15000.0) < -40.0);

    return dawlab::test::summary("l07_ex4");
}
