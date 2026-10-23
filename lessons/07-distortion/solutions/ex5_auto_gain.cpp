// Exercise 5 solution: auto gain compensation (output RMS = input RMS)
//
// Approach: two one-pole smoothed mean-square detectors (the same idea as the RMS mode of the Chapter 04 EnvelopeFollower):
//     ms[n] = a · ms[n-1] + (1 - a) · x[n]², a = exp(-1 / (τ fs))
// then gain[n] = sqrt(ms_in[n] / ms_out[n]).
//
// A few design details:
//   - Time constant 100 ms: too short and it follows the waveform's cycle (turning into tremolo / distortion); too long and the note onsets "shoot" out.
//   - A floor on the denominator (1e-12) avoids division by 0; the gain is limited to ±40 dB so it does not blow up at the moment silence turns into signal.
//   - Both detectors have the same time constant, so at steady state the gain converges to "input RMS / output RMS".
//   - Because the gain changes continuously every sample, no separate ramp is needed.
//
// Run: l07_ex5_solution

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
        const int nch = std::min({dry.getNumChannels(), wet.getNumChannels(), static_cast<int>(inMs_.size())});
        const int n = std::min(dry.getNumSamples(), wet.getNumSamples());
        const float a = a_, b = 1.0f - a_;
        for (int ch = 0; ch < nch; ++ch) {
            const float* x = dry.getChannelPointer(ch);
            float* y = wet.getChannelPointer(ch);
            float inMs = inMs_[static_cast<size_t>(ch)];
            float outMs = outMs_[static_cast<size_t>(ch)];
            for (int i = 0; i < n; ++i) {
                inMs = a * inMs + b * x[i] * x[i];
                outMs = a * outMs + b * y[i] * y[i];
                const float g = std::clamp(std::sqrt(inMs / std::max(outMs, 1.0e-12f)), 0.01f, 100.0f);
                y[i] *= g;
                gain_ = g;
            }
            // Flush tiny values to 0 to avoid denormals
            inMs_[static_cast<size_t>(ch)] = inMs < 1.0e-30f ? 0.0f : inMs;
            outMs_[static_cast<size_t>(ch)] = outMs < 1.0e-30f ? 0.0f : outMs;
        }
    }

private:
    void updateCoefficient() noexcept {
        a_ = static_cast<float>(std::exp(-1.0 / (windowMs_ * 0.001 * sampleRate_)));
    }

    double sampleRate_ = 48000.0;
    float windowMs_ = 100.0f;
    float a_ = 0.999f;
    float gain_ = 1.0f;
    std::vector<float> inMs_, outMs_;
};

struct CompensatedDistortion {
    dawlab::dsp::Distortion distortion;
    AutoGain autoGain;
    dawlab::AudioBuffer dryScratch;

    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        distortion.setOversamplingFactorLog2(0);
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

    // (1) Steady-state sine
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
        // 0.1 × 10^(30/20) = 3.16 → |u| < 1 occupies only asin(1/3.16)/(π/2) ≈ 20% of the time; the rest is clipped at ±1
        //   → output mean square ≈ (2/π)[10·(θ/2 - sin2θ/4) + (π/2 - θ)], θ = 0.3217 → 0.864, RMS ≈ 0.930 (-0.63 dBFS)
        //   input RMS = 0.0707 (-23.01 dBFS) → gain ≈ -23.01 - (-0.63) = -22.38 dB (a pure square-wave approximation would give -23.0 dB)
        DAWLAB_CHECK_NEAR(cd.autoGain.getGainDb(), -22.38, 0.5);
    }

    // (2) Pluck sequence
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
        DAWLAB_CHECK(rawDb - inDb > 6.0);
        DAWLAB_CHECK_NEAR(compDb, inDb, 1.0);
        DAWLAB_CHECK(!dawlab::hasNanOrInf(comp));
        dawlab::writeWav(dawlab::outputPath("l07_ex5_autogain.wav"), comp, kSampleRate);
        std::printf("    Wrote dawlab_output/l07_ex5_autogain.wav\n");
    }

    // (3) Silence
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
