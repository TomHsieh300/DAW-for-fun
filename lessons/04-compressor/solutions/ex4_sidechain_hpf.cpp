// Exercise 4 solution: sidechain high-pass filter (HPF).
//
// Problem: low frequencies (kick, bass) carry a lot of energy and easily push the compressor down, squashing the
// high-frequency content along with them (pumping).
// Fix: the detector does not look at the original signal but at a "copy that has gone through an HPF"; the gain is
// still applied to the original signal.
// This solution brings its own one-pole HPF (y = x - lowpass(x)) and then uses Compressor::process(block, sidechain).
//
// Test signal: 50 Hz @ -6 dBFS (loud bass) + 3 kHz @ -24 dBFS (quiet treble).
//   Without the HPF: the detector sees a level of ≈ -5 dB → GR ≈ 11 dB, and the 3 kHz is "dragged" down by the bass.
//   HPF at 400 Hz: the 50 Hz is attenuated by about 18 dB in the sidechain → GR drops to only 1–2 dB, and the 3 kHz barely moves.
// Output: dawlab_output/l04_ex4_no_hpf.wav, l04_ex4_with_hpf.wav.

#include <dawlab/Common.h>
#include <dawlab/dsp/Compressor.h>

#include <cstdio>
#include <vector>

using namespace dawlab;
using dawlab::dsp::Compressor;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

// First-order high-pass: compute a one-pole lowpass first, then take x - lp.
//   lp[n] = lp[n-1] + b·(x[n] - lp[n-1]), b = 1 - exp(-2π·fc/fs)
//   hp[n] = x[n] - lp[n]
class OnePoleHighpass {
public:
    void prepare(double sampleRate, int numChannels) {
        sampleRate_ = sampleRate;
        lp_.assign(static_cast<size_t>(numChannels), 0.0f);
        setCutoff(cutoffHz_);
    }
    void reset() noexcept { std::fill(lp_.begin(), lp_.end(), 0.0f); }
    void setCutoff(float hz) noexcept {
        cutoffHz_ = hz;
        b_ = 1.0f - static_cast<float>(std::exp(-kTwoPi * hz / sampleRate_));
    }
    float processSample(int ch, float x) noexcept {
        float& lp = lp_[static_cast<size_t>(ch)];
        lp += b_ * (x - lp);
        return x - lp;
    }
    void process(AudioBlock block) noexcept {
        for (int ch = 0; ch < block.getNumChannels(); ++ch) {
            float* d = block.getChannelPointer(ch);
            for (int i = 0; i < block.getNumSamples(); ++i)
                d[i] = processSample(ch, d[i]);
        }
    }

private:
    double sampleRate_ = 48000.0;
    float cutoffHz_ = 400.0f;
    float b_ = 0.0f;
    std::vector<float> lp_;
};

Compressor makeCompressor(int numChannels) {
    Compressor c;
    c.prepare(kSampleRate, kBlockSize, numChannels);
    c.setThresholdDb(-20.0f);
    c.setRatio(4.0f);
    c.setKneeDb(0.0f);
    c.setAttackMs(1.0f);
    c.setReleaseMs(100.0f);
    return c;
}

// Block by block: copy the block into the sidechain buffer, run it through the HPF, then use it as the sidechain to compress the main signal
AudioBuffer compressWithSidechainHpf(const AudioBuffer& input, Compressor& comp, OnePoleHighpass* hpf) {
    AudioBuffer out = input;
    AudioBuffer scBuffer(input.getNumChannels(), kBlockSize); // Allocated once during the prepare stage; process does not allocate
    AudioBlock whole(out);
    AudioBlock scWhole(scBuffer);
    for (int start = 0; start < out.getNumSamples(); start += kBlockSize) {
        const int len = std::min(kBlockSize, out.getNumSamples() - start);
        AudioBlock block = whole.getSubBlock(start, len);
        AudioBlock sc = scWhole.getSubBlock(0, len);
        sc.copyFrom(block);
        if (hpf)
            hpf->process(sc);
        comp.process(block, sc);
    }
    return out;
}

double steadyAmplitude(const AudioBuffer& b, double freq) {
    const int n = b.getNumSamples();
    return goertzelAmplitude(b.getReadPointer(0) + n / 2, n - n / 2, freq, kSampleRate);
}

} // namespace

int main() {
    std::printf("Exercise 4: sidechain HPF\n");

    // Test signal: 50 Hz @ -6 dBFS + 3 kHz @ -24 dBFS, 1 second, mono
    AudioBuffer input = makeSine(50.0, kSampleRate, 1.0, dbToGain(-6.0f), 1);
    const AudioBuffer high = makeSine(3000.0, kSampleRate, 1.0, dbToGain(-24.0f), 1);
    input.addFrom(0, 0, high, 0, 0, input.getNumSamples());
    const double in3k = steadyAmplitude(input, 3000.0);
    std::printf("  Input: peak %.2f dBFS, 3 kHz component %.2f dBFS\n", gainToDb(peakAbs(input)), gainToDb(in3k));

    // First confirm the HPF's own frequency response (one-pole: -3 dB at fc, -6 dB per octave)
    OnePoleHighpass probe;
    probe.prepare(kSampleRate, 1);
    probe.setCutoff(400.0f);
    const double g50 = measureGainDb([&](float x) { return probe.processSample(0, x); }, 50.0, kSampleRate);
    const double g400 = measureGainDb([&](float x) { return probe.processSample(0, x); }, 400.0, kSampleRate);
    const double g3k = measureGainDb([&](float x) { return probe.processSample(0, x); }, 3000.0, kSampleRate);
    std::printf("  HPF 400 Hz response: 50 Hz %.2f dB, 400 Hz %.2f dB, 3 kHz %.2f dB\n", g50, g400, g3k);
    DAWLAB_CHECK_NEAR(g400, -3.0, 0.5);
    DAWLAB_CHECK(g50 < -15.0);
    DAWLAB_CHECK(g3k > -0.5);

    // No HPF
    Compressor compA = makeCompressor(1);
    AudioBuffer outA = compressWithSidechainHpf(input, compA, nullptr);
    const float grA = compA.getGainReductionDb();
    const double red3kA = gainToDb(in3k) - gainToDb(steadyAmplitude(outA, 3000.0));
    std::printf("  No HPF: GR %.2f dB, the 3 kHz component is pulled down by %.2f dB\n", grA, red3kA);

    // With HPF
    Compressor compB = makeCompressor(1);
    OnePoleHighpass hpf;
    hpf.prepare(kSampleRate, 1);
    hpf.setCutoff(400.0f);
    AudioBuffer outB = compressWithSidechainHpf(input, compB, &hpf);
    const float grB = compB.getGainReductionDb();
    const double red3kB = gainToDb(in3k) - gainToDb(steadyAmplitude(outB, 3000.0));
    std::printf("  HPF 400 Hz: GR %.2f dB, the 3 kHz component is pulled down by %.2f dB\n", grB, red3kB);

    // self-check: static estimate — without the HPF the level ≈ 20log10(0.501 + 0.063) ≈ -5 dB → GR ≈ 11.2 dB
    const float expectedGrA = Compressor::computeGainReductionDb(gainToDb(dbToGain(-6.0f) + dbToGain(-24.0f)),
                                                                 -20.0f, 4.0f, 0.0f);
    std::printf("  Static estimate (no HPF) GR = %.2f dB\n", expectedGrA);
    DAWLAB_CHECK_NEAR(grA, expectedGrA, 0.5);
    DAWLAB_CHECK(grA > 9.0f);
    DAWLAB_CHECK(grB < 2.5f);
    DAWLAB_CHECK(red3kA > 7.0);
    DAWLAB_CHECK(red3kB < 2.5);
    DAWLAB_CHECK(grA - grB > 7.0f);
    DAWLAB_CHECK(!hasNanOrInf(outA) && !hasNanOrInf(outB));

    writeWav(outputPath("l04_ex4_no_hpf.wav"), outA, kSampleRate);
    writeWav(outputPath("l04_ex4_with_hpf.wav"), outB, kSampleRate);
    std::printf("  Wrote dawlab_output/l04_ex4_no_hpf.wav, l04_ex4_with_hpf.wav\n");
    return dawlab::test::summary("l04_ex4");
}
