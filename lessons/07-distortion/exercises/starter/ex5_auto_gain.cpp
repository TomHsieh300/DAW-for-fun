// Exercise 5 (starter): auto gain compensation -- make the distortion's output RMS equal to the input RMS
//
// Problem: turn the drive up and the level jumps with it, so you can't tell whether it got "better" or just "louder".
// Fix: measure the RMS envelope of the input and output (one-pole smoothed x², time constant about 100 ms),
//       gain = sqrt(inMeanSquare / outMeanSquare), then multiply the gain back into the output.
//
// Write an AutoGain class:
//   prepare(sampleRate, maxBlockSize, numChannels) / reset() / setWindowMs(ms)
//   void process(const AudioBlock& dry, AudioBlock wet): uses dry as the reference and corrects the level of wet in place
//   float getGainDb() const: the gain currently applied (for the UI)
// Notes:
//   - dry and wet must be aligned (Distortion has latency; here setOversamplingFactorLog2(0) makes latency = 0)
//   - do not divide by 0 when outMeanSquare is tiny; limit the gain to ±40 dB
//   - never allocate inside process()
//
// Run: l07_ex5_starter → it currently FAILs; once you finish, everything should PASS.

#include <dawlab/Common.h>
#include <dawlab/dsp/Distortion.h>

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

class AutoGain {
public:
    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        sampleRate_ = sampleRate;
        (void)maxBlockSize;
        inMs_.assign(static_cast<size_t>(numChannels), 0.0f);
        outMs_.assign(static_cast<size_t>(numChannels), 0.0f);
        updateCoefficient();
    }
    void reset() noexcept {
        std::fill(inMs_.begin(), inMs_.end(), 0.0f);
        std::fill(outMs_.begin(), outMs_.end(), 0.0f);
        gain_ = 1.0f;
    }
    void setWindowMs(float ms) noexcept {
        windowMs_ = std::max(1.0f, ms);
        updateCoefficient();
    }
    float getGainDb() const noexcept { return dawlab::gainToDb(gain_); }

    void process(const dawlab::AudioBlock& dry, dawlab::AudioBlock wet) noexcept {
        // TODO:
        //   For every channel, every sample:
        //     inMs  = a * inMs  + (1 - a) * dry²
        //     outMs = a * outMs + (1 - a) * wet²
        //     gain  = sqrt(inMs / max(outMs, 1e-12)), clamped to [0.01, 100]
        //     wet  *= gain
        //   store the last computed value in gain_ (for getGainDb)
        (void)dry;
        (void)wet;
    }

private:
    void updateCoefficient() noexcept {
        // One-pole coefficient: a = exp(-1 / (τ fs)), τ = window (seconds)
        a_ = static_cast<float>(std::exp(-1.0 / (windowMs_ * 0.001 * sampleRate_)));
    }

    double sampleRate_ = 48000.0;
    float windowMs_ = 100.0f;
    float a_ = 0.999f;
    float gain_ = 1.0f;
    std::vector<float> inMs_, outMs_;
};

// Distortion + AutoGain chained together
struct CompensatedDistortion {
    dawlab::dsp::Distortion distortion;
    AutoGain autoGain;
    dawlab::AudioBuffer dryScratch;

    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        distortion.setOversamplingFactorLog2(0); // latency = 0, so dry / wet line up
        distortion.prepare(sampleRate, maxBlockSize, numChannels);
        autoGain.prepare(sampleRate, maxBlockSize, numChannels);
        dryScratch.setSize(numChannels, maxBlockSize);
    }
    void process(dawlab::AudioBlock block) noexcept {
        dawlab::AudioBlock dry(dryScratch.getArrayOfWritePointers(), block.getNumChannels(), block.getNumSamples());
        dry.copyFrom(block);
        distortion.process(block);
        autoGain.process(dry, block);
    }
};

template <typename Processor>
void processInBlocks(dawlab::AudioBuffer& buffer, Processor& processor) {
    dawlab::AudioBlock whole(buffer);
    for (int start = 0; start < buffer.getNumSamples(); start += kBlockSize) {
        const int len = std::min(kBlockSize, buffer.getNumSamples() - start);
        processor.process(whole.getSubBlock(start, len));
    }
}

} // namespace

int main() {
    using dawlab::dsp::DistortionType;

    // (1) Steady-state sine: after hard clip at drive 30 dB the RMS should return to the input RMS (±0.2 dB)
    {
        dawlab::AudioBuffer in = dawlab::makeSine(220.0, kSampleRate, 2.0, 0.1f, 1);
        dawlab::AudioBuffer out = in;
        CompensatedDistortion cd;
        cd.prepare(kSampleRate, kBlockSize, 1);
        cd.distortion.setType(DistortionType::HardClip);
        cd.distortion.setDriveDb(30.0f);
        processInBlocks(out, cd);
        const int skip = dawlab::secondsToSamples(1.0, kSampleRate), n = in.getNumSamples() - skip;
        const double inDb = dawlab::gainToDb(dawlab::rms(in.getReadPointer(0) + skip, n));
        const double outDb = dawlab::gainToDb(dawlab::rms(out.getReadPointer(0) + skip, n));
        std::printf("(1) Sine -20 dBFS, hard clip 30 dB: input RMS %.2f dBFS, output RMS %.2f dBFS, gain = %.2f dB\n", inDb,
                    outDb, cd.autoGain.getGainDb());
        DAWLAB_CHECK_NEAR(outDb, inDb, 0.2);
        // Estimate it yourself first: after a sine of 0.1 × 10^(30/20) = 3.16 is clipped at ±1, what is the RMS? How many dB of compensation is needed?
        // (The answer and derivation are in solutions/README.md, Exercise 5)
        DAWLAB_CHECK_NEAR(cd.autoGain.getGainDb(), -22.38, 0.5);
    }

    // (2) Pluck sequence (large dynamics): the RMS of the whole run is within ±1 dB, with no NaN
    {
        dawlab::AudioBuffer in = dawlab::makePluckSequence(kSampleRate, 4.0, 0.4f, 2);
        dawlab::AudioBuffer raw = in, comp = in;
        dawlab::dsp::Distortion plain;
        plain.setOversamplingFactorLog2(0);
        plain.prepare(kSampleRate, kBlockSize, 2);
        plain.setType(DistortionType::Fuzz);
        plain.setDriveDb(30.0f);
        processInBlocks(raw, plain);

        CompensatedDistortion cd;
        cd.prepare(kSampleRate, kBlockSize, 2);
        cd.distortion.setType(DistortionType::Fuzz);
        cd.distortion.setDriveDb(30.0f);
        processInBlocks(comp, cd);

        const double inDb = dawlab::gainToDb(dawlab::rms(in, 0));
        const double rawDb = dawlab::gainToDb(dawlab::rms(raw, 0));
        const double compDb = dawlab::gainToDb(dawlab::rms(comp, 0));
        std::printf("(2) pluck, fuzz 30 dB: input RMS %.2f, uncompensated %.2f, compensated %.2f dBFS\n", inDb, rawDb, compDb);
        DAWLAB_CHECK(rawDb - inDb > 6.0);     // without compensation it is much louder
        DAWLAB_CHECK_NEAR(compDb, inDb, 1.0); // close after compensation
        DAWLAB_CHECK(!dawlab::hasNanOrInf(comp));
        dawlab::writeWav(dawlab::outputPath("l07_ex5_autogain.wav"), comp, kSampleRate);
    }

    // (3) A silent input must not blow up (division by 0)
    {
        dawlab::AudioBuffer silence(1, 4096);
        CompensatedDistortion cd;
        cd.prepare(kSampleRate, kBlockSize, 1);
        cd.distortion.setDriveDb(40.0f);
        processInBlocks(silence, cd);
        DAWLAB_CHECK(!dawlab::hasNanOrInf(silence));
        DAWLAB_CHECK(dawlab::peakAbs(silence) == 0.0f);
    }

    return dawlab::test::summary("l07_ex5");
}
