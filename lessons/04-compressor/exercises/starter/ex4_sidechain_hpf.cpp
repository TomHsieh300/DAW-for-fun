// Exercise 4 (starter code): sidechain high-pass filter (HPF).
//
// Goals:
//   1. Implement a one-pole HPF (lp[n] = lp[n-1] + b·(x[n] - lp[n-1]), hp = x - lp, b = 1 - exp(-2π·fc/fs));
//   2. Block by block: copy the block into a sidechain buffer → run it through the HPF → compress the main signal
//      with Compressor::process(block, sidechain);
//   3. Verify with a "loud 50 Hz + quiet 3 kHz" test signal: with the HPF, the 3 kHz component is barely pulled
//      down by the bass.
// You are done when all self-checks pass after you complete the TODOs. The starter code prints FAIL when run; that is expected.

#include <dawlab/Common.h>
#include <dawlab/dsp/Compressor.h>

#include <cstdio>
#include <vector>

using namespace dawlab;
using dawlab::dsp::Compressor;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

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
        // TODO: b_ = 1 - exp(-2π·fc/fs) (use dawlab::kTwoPi, and mind the double→float conversion)
        b_ = 0.0f;
    }
    float processSample(int ch, float x) noexcept {
        float& lp = lp_[static_cast<size_t>(ch)];
        (void)lp;
        // TODO: update lp, return x - lp
        return x;
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

// hpf == nullptr means "no HPF" (the sidechain is the original signal)
AudioBuffer compressWithSidechainHpf(const AudioBuffer& input, Compressor& comp, OnePoleHighpass* hpf) {
    AudioBuffer out = input;
    AudioBuffer scBuffer(input.getNumChannels(), kBlockSize); // Allocated once during the prepare stage; process does not allocate
    (void)comp;
    (void)hpf;
    (void)scBuffer;
    // TODO: for each block: sc.copyFrom(block) → if hpf is non-null, hpf->process(sc) → comp.process(block, sc)
    return out;
}

double steadyAmplitude(const AudioBuffer& b, double freq) {
    const int n = b.getNumSamples();
    return goertzelAmplitude(b.getReadPointer(0) + n / 2, n - n / 2, freq, kSampleRate);
}

} // namespace

int main() {
    std::printf("Exercise 4 (starter): sidechain HPF\n");

    AudioBuffer input = makeSine(50.0, kSampleRate, 1.0, dbToGain(-6.0f), 1);
    const AudioBuffer high = makeSine(3000.0, kSampleRate, 1.0, dbToGain(-24.0f), 1);
    input.addFrom(0, 0, high, 0, 0, input.getNumSamples());
    const double in3k = steadyAmplitude(input, 3000.0);
    std::printf("  Input: peak %.2f dBFS, 3 kHz component %.2f dBFS\n", gainToDb(peakAbs(input)), gainToDb(in3k));

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

    Compressor compA = makeCompressor(1);
    AudioBuffer outA = compressWithSidechainHpf(input, compA, nullptr);
    const float grA = compA.getGainReductionDb();
    const double red3kA = gainToDb(in3k) - gainToDb(steadyAmplitude(outA, 3000.0));
    std::printf("  No HPF: GR %.2f dB, the 3 kHz component is pulled down by %.2f dB\n", grA, red3kA);

    Compressor compB = makeCompressor(1);
    OnePoleHighpass hpf;
    hpf.prepare(kSampleRate, 1);
    hpf.setCutoff(400.0f);
    AudioBuffer outB = compressWithSidechainHpf(input, compB, &hpf);
    const float grB = compB.getGainReductionDb();
    const double red3kB = gainToDb(in3k) - gainToDb(steadyAmplitude(outB, 3000.0));
    std::printf("  HPF 400 Hz: GR %.2f dB, the 3 kHz component is pulled down by %.2f dB\n", grB, red3kB);

    const float expectedGrA = Compressor::computeGainReductionDb(gainToDb(dbToGain(-6.0f) + dbToGain(-24.0f)),
                                                                 -20.0f, 4.0f, 0.0f);
    DAWLAB_CHECK_NEAR(grA, expectedGrA, 0.5);
    DAWLAB_CHECK(grA > 9.0f);
    DAWLAB_CHECK(grB < 2.5f);
    DAWLAB_CHECK(red3kA > 7.0);
    DAWLAB_CHECK(red3kB < 2.5);
    DAWLAB_CHECK(grA - grB > 7.0f);
    DAWLAB_CHECK(!hasNanOrInf(outA) && !hasNanOrInf(outB));
    return dawlab::test::summary("l04_ex4_starter");
}
