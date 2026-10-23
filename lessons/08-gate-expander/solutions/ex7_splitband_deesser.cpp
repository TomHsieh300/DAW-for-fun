// Exercise 7 solution: a 2-band split-band de-esser built on a Linkwitz-Riley 4th-order crossover, verifying that the
// crossover sums flat
//
// Linkwitz-Riley 4th order (LR4) = two identical 2nd-order Butterworth filters (Q = 1/sqrt(2)) in cascade.
// At the crossover frequency fc: each Butterworth is -3 dB, so the cascade is -6 dB; LP and HP are in phase at fc,
// so LP + HP at fc = 2 x (-6 dB) = 0 dB, and over the whole
// band |LP + HP| = 1 (the sum is an allpass: flat in magnitude, not flat in phase).
//
// De-esser: the HP band goes to the Chapter 04 Compressor (threshold -30, ratio 8:1, attack 1 ms, release 40 ms),
// detection uses the HP band itself (the sibilance energy lives there), the LP band is left untouched, and the two
// are summed at the end.
//
// Self-check:
//   - Between 100 Hz and 15 kHz, |LP + HP| is within 0 dB +/- 0.1 dB
//   - At fc, LP and HP are each -6.02 dB +/- 0.2 dB
//   - During the sibilance the level of the 220 Hz vowel changes by < 0.3 dB; the 7 kHz sibilance is reduced by >= 8 dB
// Run: l08_ex7_solution -> writes dawlab_output/l08_ex7_input.wav, l08_ex7_output.wav.

#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>
#include <dawlab/dsp/Compressor.h>

#include <cstdio>
#include <vector>

using namespace dawlab;
using dawlab::dsp::Biquad;
using dawlab::dsp::BiquadCoefficients;
using dawlab::dsp::Compressor;

namespace {

constexpr int kBlockSize = 512;
constexpr double kSampleRate = 48000.0;
constexpr double kVowelHz = 220.0;
constexpr double kSibilanceHz = 7000.0;

// ---- LR4 crossover --------------------------------------------------------------
class LinkwitzRileyCrossover {
public:
    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        sampleRate_ = sampleRate;
        for (Biquad* f : {&lp1_, &lp2_, &hp1_, &hp2_})
            f->prepare(sampleRate, maxBlockSize, numChannels);
        setCrossoverHz(crossoverHz_);
        reset();
    }
    void reset() noexcept {
        for (Biquad* f : {&lp1_, &lp2_, &hp1_, &hp2_})
            f->reset();
    }
    void setCrossoverHz(float hz) noexcept {
        crossoverHz_ = hz;
        const auto lp = BiquadCoefficients::makeLowpass(sampleRate_, hz, BiquadCoefficients::kButterworthQ);
        const auto hp = BiquadCoefficients::makeHighpass(sampleRate_, hz, BiquadCoefficients::kButterworthQ);
        lp1_.setCoefficients(lp);
        lp2_.setCoefficients(lp);
        hp1_.setCoefficients(hp);
        hp2_.setCoefficients(hp);
    }
    // One sample in, two bands out
    void processSample(int ch, float x, float& low, float& high) noexcept {
        low = lp2_.processSample(ch, lp1_.processSample(ch, x));
        high = hp2_.processSample(ch, hp1_.processSample(ch, x));
    }

private:
    double sampleRate_ = 48000.0;
    float crossoverHz_ = 5000.0f;
    Biquad lp1_, lp2_, hp1_, hp2_;
};

// ---- 2-band de-esser ---------------------------------------------------------
class SplitBandDeEsser {
public:
    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        crossover_.prepare(sampleRate, maxBlockSize, numChannels);
        crossover_.setCrossoverHz(5000.0f);
        compressor_.prepare(sampleRate, maxBlockSize, numChannels);
        compressor_.setThresholdDb(-30.0f);
        compressor_.setRatio(8.0f);
        compressor_.setKneeDb(3.0f);
        compressor_.setAttackMs(1.0f);
        compressor_.setReleaseMs(40.0f);
        compressor_.setStereoLink(true);
        low_.setSize(numChannels, maxBlockSize);
        high_.setSize(numChannels, maxBlockSize);
        reset();
    }
    void reset() noexcept {
        crossover_.reset();
        compressor_.reset();
    }
    float getGainReductionDb() const noexcept { return compressor_.getGainReductionDb(); }

    void process(AudioBlock block) noexcept {
        const int n = std::min(block.getNumSamples(), low_.getNumSamples());
        const int nch = std::min(block.getNumChannels(), low_.getNumChannels());
        // 1. Split into scratch buffers (allocated in prepare; no allocation inside process)
        for (int ch = 0; ch < nch; ++ch) {
            const float* x = block.getChannelPointer(ch);
            float* lo = low_.getWritePointer(ch);
            float* hi = high_.getWritePointer(ch);
            for (int i = 0; i < n; ++i)
                crossover_.processSample(ch, x[i], lo[i], hi[i]);
        }
        // 2. Compress only the high band (detection also looks at the high band itself)
        AudioBlock highBlock(high_.getArrayOfWritePointers(), nch, n);
        compressor_.process(highBlock);
        // 3. Sum
        for (int ch = 0; ch < nch; ++ch) {
            float* y = block.getChannelPointer(ch);
            const float* lo = low_.getReadPointer(ch);
            const float* hi = high_.getReadPointer(ch);
            for (int i = 0; i < n; ++i)
                y[i] = lo[i] + hi[i];
        }
    }

private:
    LinkwitzRileyCrossover crossover_;
    Compressor compressor_;
    AudioBuffer low_, high_;
};

// ---- Test material (same as Example 3) ----------------------------------------------
AudioBuffer makeVowel(double seconds, float amplitude) {
    AudioBuffer b(2, secondsToSamples(seconds, kSampleRate));
    for (int i = 0; i < b.getNumSamples(); ++i) {
        double v = 0.0;
        for (int k = 1; k <= 12; ++k)
            v += std::sin(kTwoPi * kVowelHz * k * i / kSampleRate) / k;
        const auto s = static_cast<float>(amplitude * v / 1.6);
        b.setSample(0, i, s);
        b.setSample(1, i, s);
    }
    return b;
}

void addSibilance(AudioBuffer& target, double startSec, double lengthSec, float peakAmplitude) {
    const int start = secondsToSamples(startSec, kSampleRate);
    const int len = secondsToSamples(lengthSec, kSampleRate);
    Biquad bp;
    bp.prepare(kSampleRate, kBlockSize, 1);
    bp.setCoefficients(BiquadCoefficients::makeBandpass(kSampleRate, kSibilanceHz, 1.5));
    NoiseGenerator noise(777);
    std::vector<float> burst(static_cast<size_t>(len));
    float peak = 0.0f;
    for (int i = 0; i < len; ++i) {
        const double t = i / kSampleRate;
        const double env = std::min(1.0, t / 0.010) * std::min(1.0, (lengthSec - t) / 0.030);
        burst[static_cast<size_t>(i)] = static_cast<float>(env * bp.processSample(0, noise.next()));
        peak = std::max(peak, std::abs(burst[static_cast<size_t>(i)]));
    }
    for (int i = 0; i < len && start + i < target.getNumSamples(); ++i)
        for (int ch = 0; ch < target.getNumChannels(); ++ch)
            target.addSample(ch, start + i, burst[static_cast<size_t>(i)] * peakAmplitude / peak);
}

double levelDb(const AudioBuffer& b, double startSec, double lengthSec, double freq) {
    const int start = secondsToSamples(startSec, kSampleRate);
    const int n = secondsToSamples(lengthSec, kSampleRate);
    return gainToDb(goertzelAmplitude(b.getReadPointer(0) + start, n, freq, kSampleRate));
}

} // namespace

int main() {
    std::printf("Chapter 08 Exercise 7: LR4 crossover + split-band de-esser\n");

    // ---- 1. The crossover sums flat ----
    LinkwitzRileyCrossover xo;
    xo.prepare(kSampleRate, kBlockSize, 1);
    xo.setCrossoverHz(5000.0f);
    std::printf("\n[1] LR4 @ 5 kHz: |LP + HP|, |LP|, |HP| (dB)\n    freq(Hz)   LP+HP      LP       HP\n");
    for (double f : {100.0, 500.0, 1000.0, 2000.0, 3500.0, 5000.0, 7000.0, 10000.0, 15000.0}) {
        xo.reset();
        const double sum = measureGainDb([&](float x) { float lo, hi; xo.processSample(0, x, lo, hi); return lo + hi; }, f, kSampleRate);
        xo.reset();
        const double lo = measureGainDb([&](float x) { float l, h; xo.processSample(0, x, l, h); return l; }, f, kSampleRate);
        xo.reset();
        const double hi = measureGainDb([&](float x) { float l, h; xo.processSample(0, x, l, h); return h; }, f, kSampleRate);
        std::printf("    %8.0f  %7.3f  %7.2f  %7.2f\n", f, sum, lo, hi);
        DAWLAB_CHECK_NEAR(sum, 0.0, 0.1);
        if (f == 5000.0) {
            DAWLAB_CHECK_NEAR(lo, -6.02, 0.2);
            DAWLAB_CHECK_NEAR(hi, -6.02, 0.2);
        }
    }

    // ---- 2. de-esser ----
    AudioBuffer input = makeVowel(3.0, dbToGain(-14.0f));
    for (double t : {0.5, 1.5, 2.5})
        addSibilance(input, t, 0.120, dbToGain(-12.0f));

    SplitBandDeEsser de;
    de.prepare(kSampleRate, kBlockSize, 2);
    AudioBuffer output = input;
    float maxGr = 0.0f;
    {
        AudioBlock whole(output);
        for (int start = 0; start < output.getNumSamples(); start += kBlockSize) {
            de.process(whole.getSubBlock(start, std::min(kBlockSize, output.getNumSamples() - start)));
            maxGr = std::max(maxGr, de.getGainReductionDb());
        }
    }
    writeWav(outputPath("l08_ex7_input.wav"), input, kSampleRate);
    writeWav(outputPath("l08_ex7_output.wav"), output, kSampleRate);

    const double vowelInQuiet = levelDb(input, 1.0, 0.4, kVowelHz);
    const double vowelOutQuiet = levelDb(output, 1.0, 0.4, kVowelHz);
    const double vowelInBurst = levelDb(input, 0.52, 0.08, kVowelHz);
    const double vowelOutBurst = levelDb(output, 0.52, 0.08, kVowelHz);
    const double sibIn = levelDb(input, 0.52, 0.08, kSibilanceHz);
    const double sibOut = levelDb(output, 0.52, 0.08, kSibilanceHz);
    std::printf("\n[2] de-esser: maximum GR %.2f dB\n", maxGr);
    std::printf("    220 Hz vowel: vowel-only section input %.2f / output %.2f; during sibilance input %.2f / output %.2f dB\n", vowelInQuiet,
                vowelOutQuiet, vowelInBurst, vowelOutBurst);
    std::printf("    7 kHz sibilance: input %.2f / output %.2f dB (reduced by %.2f dB)\n", sibIn, sibOut, sibIn - sibOut);
    DAWLAB_CHECK_NEAR(vowelOutQuiet, vowelInQuiet, 0.1);
    DAWLAB_CHECK_NEAR(vowelOutBurst, vowelInBurst, 0.3);
    DAWLAB_CHECK(sibIn - sibOut >= 8.0);
    DAWLAB_CHECK(!hasNanOrInf(output));
    return dawlab::test::summary("l08_ex7_splitband_deesser");
}
