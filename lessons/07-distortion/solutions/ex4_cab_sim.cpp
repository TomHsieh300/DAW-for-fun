// Exercise 4 solution: a simple cabinet simulator built from biquads
//
// The frequency response of a real guitar speaker (12-inch driver + closed / open cabinet) is roughly:
//   - Falls quickly below 70-100 Hz (the cabinet cannot hold the bass, the driver cannot move enough air)
//   - A cabinet / driver resonance peak near 100 Hz
//   - Roughly flat mids (with many small ripples, omitted here)
//   - Falls steeply above 4-5 kHz (the mass of the cone cannot follow high frequencies), often steeper than 24 dB/oct
// We approximate it with four biquads: highpass (70 Hz) × peak (100 Hz, +4 dB, Q 1.5) × lowpass × lowpass (4.5 kHz, Q = 1/√2).
//
// Why are two Q = 1/√2 lowpass filters in cascade called a 4th-order Linkwitz-Riley (LR4)?
//   One 2nd-order Butterworth is -3.01 dB at the cutoff; two in cascade give -6.02 dB, and away from the cutoff the slope is 24 dB/oct.
//   The same structure was used for the crossover in Chapter 03 (the LR lowpass and highpass are each -6 dB at the cutoff, and they sum to exactly 0 dB).
//
// The total response of cascaded linear stages = the product of the stage responses = the sum of the stage dB values, so magnitudeDb() simply adds the four magnitudeDb values.
//
// Run: l07_ex4_solution

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
        lowCut_.prepare(sampleRate, maxBlockSize, numChannels);
        resonance_.prepare(sampleRate, maxBlockSize, numChannels);
        lp1_.prepare(sampleRate, maxBlockSize, numChannels);
        lp2_.prepare(sampleRate, maxBlockSize, numChannels);
        updateCoefficients();
    }
    void reset() noexcept {
        lowCut_.reset();
        resonance_.reset();
        lp1_.reset();
        lp2_.reset();
    }
    void setLowCutHz(float hz) noexcept { lowCutHz_ = hz; updateCoefficients(); }
    void setResonanceHz(float hz) noexcept { resHz_ = hz; updateCoefficients(); }
    void setResonanceDb(float db) noexcept { resDb_ = db; updateCoefficients(); }
    void setCutoffHz(float hz) noexcept { cutoffHz_ = hz; updateCoefficients(); }

    float processSample(int ch, float x) noexcept {
        return lp2_.processSample(ch, lp1_.processSample(ch, resonance_.processSample(ch, lowCut_.processSample(ch, x))));
    }
    void process(dawlab::AudioBlock block) noexcept {
        for (int ch = 0; ch < block.getNumChannels(); ++ch) {
            float* d = block.getChannelPointer(ch);
            for (int i = 0; i < block.getNumSamples(); ++i)
                d[i] = processSample(ch, d[i]);
        }
    }

    double magnitudeDb(double freq) const noexcept {
        return lowCut_.getCoefficients().magnitudeDb(freq, sampleRate_) +
               resonance_.getCoefficients().magnitudeDb(freq, sampleRate_) +
               lp1_.getCoefficients().magnitudeDb(freq, sampleRate_) + lp2_.getCoefficients().magnitudeDb(freq, sampleRate_);
    }

private:
    void updateCoefficients() noexcept {
        if (sampleRate_ <= 0.0)
            return;
        using dawlab::dsp::BiquadCoefficients;
        lowCut_.setCoefficients(BiquadCoefficients::makeHighpass(sampleRate_, lowCutHz_, kButterworthQ));
        resonance_.setCoefficients(BiquadCoefficients::makePeak(sampleRate_, resHz_, 1.5, resDb_));
        lp1_.setCoefficients(BiquadCoefficients::makeLowpass(sampleRate_, cutoffHz_, kButterworthQ));
        lp2_.setCoefficients(BiquadCoefficients::makeLowpass(sampleRate_, cutoffHz_, kButterworthQ));
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

    std::printf("%8s %12s %12s\n", "freq", "analytic", "measured");
    bool consistent = true;
    for (double f : {30.0, 70.0, 100.0, 200.0, 1000.0, 2250.0, 4500.0, 9000.0, 15000.0}) {
        cab.reset();
        const double analytic = cab.magnitudeDb(f);
        const double measured = dawlab::measureGainDb([&](float x) { return cab.processSample(0, x); }, f, kSampleRate);
        std::printf("%8.0f %12.2f %12.2f\n", f, analytic, measured);
        consistent = consistent && std::abs(analytic - measured) < 0.2;
    }
    DAWLAB_CHECK(consistent);
    DAWLAB_CHECK_NEAR(cab.magnitudeDb(4500.0), -6.02, 0.3);
    // One octave up: the analog LR4 would be -24.6 dB, but the frequency warping of the bilinear transform makes the digital filter
    // fall faster at high frequencies: the equivalent analog frequency ratio = tan(π f/fs) / tan(π fc/fs) (9 kHz / 4.5 kHz @ 48 kHz → 2.20 rather than 2)
    {
        const double r = std::tan(dawlab::kPi * 9000.0 / kSampleRate) / std::tan(dawlab::kPi * 4500.0 / kSampleRate);
        const double expectedDb = -2.0 * 10.0 * std::log10(1.0 + r * r * r * r); // two 2nd-order Butterworth sections
        std::printf("9 kHz: warped ratio %.3f → expected %.2f dB (unwarped analog value -24.6 dB)\n", r, expectedDb);
        DAWLAB_CHECK_NEAR(cab.magnitudeDb(9000.0), expectedDb, 0.5);
    }
    DAWLAB_CHECK(cab.magnitudeDb(1000.0) > -0.5 && cab.magnitudeDb(1000.0) < 0.5);
    DAWLAB_CHECK(cab.magnitudeDb(100.0) > 2.5 && cab.magnitudeDb(100.0) < 4.5);
    DAWLAB_CHECK(cab.magnitudeDb(30.0) < -12.0);
    DAWLAB_CHECK(cab.magnitudeDb(15000.0) < -40.0);

    // For listening: white noise through the cab sim (sounds like "noise recorded in front of a speaker cabinet")
    {
        dawlab::AudioBuffer noise = dawlab::makeWhiteNoise(kSampleRate, 2.0, 0.25f, 2);
        CabSim cab2;
        cab2.prepare(kSampleRate, kBlockSize, 2);
        dawlab::AudioBlock whole(noise);
        for (int start = 0; start < noise.getNumSamples(); start += kBlockSize)
            cab2.process(whole.getSubBlock(start, std::min(kBlockSize, noise.getNumSamples() - start)));
        dawlab::writeWav(dawlab::outputPath("l07_ex4_cab_noise.wav"), noise, kSampleRate);
        std::printf("Wrote dawlab_output/l07_ex4_cab_noise.wav\n");
    }

    return dawlab::test::summary("l07_ex4");
}
