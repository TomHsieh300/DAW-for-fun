// Exercise 4 solution: Ring modulator -- multiply by a carrier, leaving only the sum and difference frequencies
//
//   y(t) = x(t) · sin(2π fc t)
//   If x = A·sin(2π f t): sin(a)·sin(b) = ½[cos(a−b) − cos(a+b)]
//   -> the output has only the two frequencies f − fc and f + fc (amplitude A/2 each); both the original f and the carrier fc are gone.
//
// The difference from tremolo (AM): AM's gain has a DC offset (it is always >= 0), so the carrier survives;
// the ring mod's "gain" crosses zero between −1..+1, which cancels the carrier.
// With a low carrier frequency (< 20 Hz) it sounds like a very deep tremolo (two "silences" per cycle);
// once the carrier enters the audible range, the sum and difference frequencies are not integer multiples of the original -> an "inharmonic" metallic / bell-like timbre (the Dalek voice).
//
// self-check:
//   1. 1000 Hz sine (A = 0.5) x 150 Hz carrier: the 850 and 1150 Hz amplitudes ≈ 0.25; 1000 and 150 Hz are below −60 dB
//   2. Carrier 0 Hz (phase fixed at sin = 1) -> output = input (the ring mod degenerates into a gain of 1)
//   3. The mix parameter: at mix 0.5, 1000 Hz keeps half (0.25) and each sideband is 0.125
// Output: dawlab_output/l10_ex4_ringmod_30hz.wav (plucked arpeggio x 30 Hz), l10_ex4_ringmod_500hz.wav (x 500 Hz, metallic)

#include <dawlab/Common.h>
#include <dawlab/dsp/Lfo.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

using dawlab::dsp::Lfo;
using dawlab::dsp::LfoShape;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

class RingMod {
public:
    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        (void)maxBlockSize;
        numChannels_ = std::max(1, numChannels);
        carrier_.prepare(sampleRate); // the carrier is just a sine LFO running in the audible frequency range
        carrier_.setShape(LfoShape::Sine);
        mixSm_.prepare(sampleRate, 20.0);
        reset();
    }

    void reset() noexcept {
        carrier_.reset();
        mixSm_.setCurrentAndTarget(mixSm_.getTarget());
        carrierValue_ = 0.0f;
    }

    void setCarrierHz(float hz) noexcept { carrier_.setRateHz(std::clamp(hz, 0.0f, 20000.0f)); }
    void setCarrierPhase(float phase01) noexcept { carrier_.setPhase(phase01); }
    void setMix(float mix) noexcept { mixSm_.setTarget(std::clamp(mix, 0.0f, 1.0f)); }

    float processSample(int channel, float x) noexcept {
        if (channel == 0)
            advance();
        return (1.0f - mix_) * x + mix_ * x * carrierValue_; // y = x · carrier (then mixed with the dry signal)
    }

    void process(dawlab::AudioBlock block) noexcept {
        const int n = block.getNumSamples();
        const int nch = std::min(block.getNumChannels(), numChannels_);
        for (int i = 0; i < n; ++i) {
            advance();
            for (int ch = 0; ch < nch; ++ch) {
                float* d = block.getChannelPointer(ch);
                d[i] = (1.0f - mix_) * d[i] + mix_ * d[i] * carrierValue_;
            }
        }
    }

private:
    void advance() noexcept {
        mix_ = mixSm_.next();
        carrierValue_ = carrier_.getNextValue();
    }

    int numChannels_ = 2;
    Lfo carrier_;
    float carrierValue_ = 0.0f;
    dawlab::dsp::detail::ModParamSmoother mixSm_{1.0f};
    float mix_ = 1.0f;
};

template <typename Effect>
void processInBlocks(Effect& fx, dawlab::AudioBuffer& buffer) {
    dawlab::AudioBlock whole(buffer);
    for (int start = 0; start < buffer.getNumSamples(); start += kBlockSize) {
        const int n = std::min(kBlockSize, buffer.getNumSamples() - start);
        fx.process(whole.getSubBlock(start, n));
    }
}

double amp(const dawlab::AudioBuffer& b, double freq, int skip) {
    return dawlab::goertzelAmplitude(b.getReadPointer(0) + skip, b.getNumSamples() - skip, freq, kSampleRate);
}

} // namespace

int main() {
    std::printf("=== Chapter 10 Exercise 4: Ring modulator ===\n\n");
    const int skip = dawlab::secondsToSamples(0.5, kSampleRate);

    // ---- 1. Sum and difference frequencies ---------------------------------------
    {
        const double f = 1000.0, fc = 150.0;
        const float A = 0.5f;
        dawlab::AudioBuffer tone = dawlab::makeSine(f, kSampleRate, 3.0, A, 1);
        RingMod rm;
        rm.prepare(kSampleRate, kBlockSize, 1);
        rm.setCarrierHz(static_cast<float>(fc));
        rm.setMix(1.0f);
        rm.reset();
        processInBlocks(rm, tone);
        std::printf("%.0f Hz (A = %.2f) x %.0f Hz carrier:\n", f, A, fc);
        std::printf("   freq(Hz)   amplitude   theory\n");
        const double sideband = amp(tone, f - fc, skip), sideband2 = amp(tone, f + fc, skip);
        const double carrierLeft = amp(tone, f, skip), lfoLeft = amp(tone, fc, skip);
        std::printf("   %8.0f   %.4f   %.4f (difference frequency, A/2)\n", f - fc, sideband, A / 2.0);
        std::printf("   %8.0f   %.4f   %.4f (sum frequency, A/2)\n", f + fc, sideband2, A / 2.0);
        std::printf("   %8.0f   %.4f   0 (the original is gone, %.1f dB)\n", f, carrierLeft, dawlab::gainToDb(carrierLeft / A));
        std::printf("   %8.0f   %.4f   0 (the carrier does not appear, %.1f dB)\n\n", fc, lfoLeft, dawlab::gainToDb(lfoLeft / A));
        DAWLAB_CHECK_NEAR(sideband, A / 2.0, 0.005);
        DAWLAB_CHECK_NEAR(sideband2, A / 2.0, 0.005);
        DAWLAB_CHECK(dawlab::gainToDb(carrierLeft / A) < -60.0);
        DAWLAB_CHECK(dawlab::gainToDb(lfoLeft / A) < -60.0);
    }

    // ---- 2. Carrier 0 Hz, phase 0.25 (sin = 1) -> output = input -------------------
    {
        dawlab::AudioBuffer tone = dawlab::makeSine(1000.0, kSampleRate, 1.0, 0.5f, 1);
        const dawlab::AudioBuffer ref = tone;
        RingMod rm;
        rm.prepare(kSampleRate, kBlockSize, 1);
        rm.setCarrierHz(0.0f);
        rm.setCarrierPhase(0.25f);
        rm.reset();
        processInBlocks(rm, tone);
        float maxDiff = 0.0f;
        for (int i = 0; i < tone.getNumSamples(); ++i)
            maxDiff = std::max(maxDiff, std::abs(tone.getSample(0, i) - ref.getSample(0, i)));
        std::printf("Carrier 0 Hz, phase 0.25: largest output-vs-input difference %.2e (should be 0)\n\n", maxDiff);
        DAWLAB_CHECK(maxDiff < 1.0e-5f);
    }

    // ---- 3. mix 0.5 --------------------------------------------------------
    {
        dawlab::AudioBuffer tone = dawlab::makeSine(1000.0, kSampleRate, 3.0, 0.5f, 1);
        RingMod rm;
        rm.prepare(kSampleRate, kBlockSize, 1);
        rm.setCarrierHz(150.0f);
        rm.setMix(0.5f);
        rm.reset();
        processInBlocks(rm, tone);
        const double dry = amp(tone, 1000.0, skip), sb = amp(tone, 850.0, skip);
        std::printf("mix 0.5: 1000 Hz amplitude %.4f (theory 0.25), 850 Hz amplitude %.4f (theory 0.125)\n\n", dry, sb);
        DAWLAB_CHECK_NEAR(dry, 0.25, 0.005);
        DAWLAB_CHECK_NEAR(sb, 0.125, 0.005);
    }

    // ---- Listening ----------------------------------------------------------------
    {
        for (float fc : {30.0f, 500.0f}) {
            dawlab::AudioBuffer pluck = dawlab::makePluckSequence(kSampleRate, 4.0, 0.4f, 2);
            RingMod rm;
            rm.prepare(kSampleRate, kBlockSize, 2);
            rm.setCarrierHz(fc);
            rm.setMix(1.0f);
            rm.reset();
            processInBlocks(rm, pluck);
            const std::string name = fc < 100.0f ? "l10_ex4_ringmod_30hz.wav" : "l10_ex4_ringmod_500hz.wav";
            dawlab::writeWav(dawlab::outputPath(name), pluck, kSampleRate);
            std::printf("Output %s (carrier %.0f Hz): peak %.3f\n", name.c_str(), fc, dawlab::peakAbs(pluck));
            DAWLAB_CHECK(!dawlab::hasNanOrInf(pluck));
        }
        std::printf("  -> 30 Hz acts like an extremely deep tremolo (60 silences per second); at 500 Hz the sum and difference frequencies are not integer multiples of the original -> a metallic bell sound.\n");
    }

    return dawlab::test::summary("l10_ex4");
}
