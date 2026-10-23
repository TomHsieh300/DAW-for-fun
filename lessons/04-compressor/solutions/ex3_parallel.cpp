// Exercise 3 solution: parallel (New York) compression.
//
// Approach: make a copy of the dry signal, compress it heavily (low threshold, high ratio, very fast attack), apply
// makeup so the wet peak returns to the dry peak, then blend the two with mix: y = (1 - mix)·dry + mix·wet.
// Note that the attack must be "very fast" (here 0.02 ms ≈ 1 sample, similar to the fastest ~20 µs attack of an FET hardware
// compressor): the drum peaks are noise spikes a few samples wide, and a 1 ms attack lets them slip through and only
// squashes the body, so the crest factor actually rises (see the attack sweep in example 3).
// Self-check: mix = 0 → exactly dry; mix = 1 → exactly wet; the crest factor at mix = 0.5 lies between the two.
// Output: dawlab_output/l04_ex3_parallel_50.wav (mix 0.5), l04_ex3_parallel_wet.wav (mix 1).

#include <dawlab/Common.h>
#include <dawlab/dsp/Compressor.h>

#include <cstdio>

using namespace dawlab;
using dawlab::dsp::Compressor;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

float crestFactorDb(const AudioBuffer& b) {
    float r = 0.0f;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        r = std::max(r, rms(b, ch));
    return gainToDb(peakAbs(b)) - gainToDb(r);
}

// Compress the whole buffer block by block (mimicking an audio callback), also recording the largest gain reduction over the run
AudioBuffer compressBlocks(const AudioBuffer& input, Compressor& comp, float& maxGrDb) {
    AudioBuffer out = input;
    AudioBlock whole(out);
    maxGrDb = 0.0f;
    for (int start = 0; start < out.getNumSamples(); start += kBlockSize) {
        comp.process(whole.getSubBlock(start, std::min(kBlockSize, out.getNumSamples() - start)));
        maxGrDb = std::max(maxGrDb, comp.getGainReductionDb());
    }
    return out;
}

// y = (1 - mix)·dry + mix·wet, computed sample by sample (dry and wet must have the same length and channel count)
AudioBuffer parallelMix(const AudioBuffer& dry, const AudioBuffer& wet, float mix) {
    mix = std::clamp(mix, 0.0f, 1.0f);
    AudioBuffer out(dry.getNumChannels(), dry.getNumSamples());
    for (int ch = 0; ch < dry.getNumChannels(); ++ch) {
        const float* d = dry.getReadPointer(ch);
        const float* w = wet.getReadPointer(ch);
        float* o = out.getWritePointer(ch);
        for (int i = 0; i < dry.getNumSamples(); ++i)
            o[i] = (1.0f - mix) * d[i] + mix * w[i];
    }
    return out;
}

float maxAbsDifference(const AudioBuffer& a, const AudioBuffer& b) {
    float m = 0.0f;
    for (int ch = 0; ch < a.getNumChannels(); ++ch)
        for (int i = 0; i < a.getNumSamples(); ++i)
            m = std::max(m, std::abs(a.getSample(ch, i) - b.getSample(ch, i)));
    return m;
}

} // namespace

int main() {
    std::printf("Exercise 3: parallel compression\n");
    const AudioBuffer dry = makeDrumLoop(kSampleRate, 2, 120.0);

    // Heavy compression: T -30 dB, 10:1, attack 0.02 ms (≈ 1 sample), release 60 ms
    Compressor comp;
    comp.prepare(kSampleRate, kBlockSize, dry.getNumChannels());
    comp.setThresholdDb(-30.0f);
    comp.setRatio(10.0f);
    comp.setKneeDb(3.0f);
    comp.setAttackMs(0.02f);
    comp.setReleaseMs(60.0f);
    float maxGr = 0.0f;
    AudioBuffer wet = compressBlocks(dry, comp, maxGr);

    // Makeup: make the wet peak equal to the dry peak
    const float makeupDb = gainToDb(peakAbs(dry)) - gainToDb(peakAbs(wet));
    wet.applyGain(dbToGain(makeupDb));
    std::printf("  wet makeup = %+.2f dB (max GR %.2f dB)\n", makeupDb, maxGr);

    const float crestDry = crestFactorDb(dry);
    const float crestWet = crestFactorDb(wet);
    std::printf("  crest factor: dry %.2f dB, wet %.2f dB\n", crestDry, crestWet);
    DAWLAB_CHECK(crestWet < crestDry - 3.0f); // after heavy compression the crest drops noticeably

    const float mixes[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
    for (float mix : mixes) {
        AudioBuffer y = parallelMix(dry, wet, mix);
        std::printf("  mix %.2f: peak %6.2f dBFS, crest %5.2f dB\n", mix, gainToDb(peakAbs(y)), crestFactorDb(y));
        DAWLAB_CHECK(!hasNanOrInf(y));
    }

    // self-check
    DAWLAB_CHECK(maxAbsDifference(parallelMix(dry, wet, 0.0f), dry) < 1e-6f);
    DAWLAB_CHECK(maxAbsDifference(parallelMix(dry, wet, 1.0f), wet) < 1e-6f);
    const AudioBuffer half = parallelMix(dry, wet, 0.5f);
    const float crestHalf = crestFactorDb(half);
    DAWLAB_CHECK(crestHalf < crestDry && crestHalf > crestWet);
    // Linear blend: every sample at mix 0.5 should be the average of dry and wet
    DAWLAB_CHECK_NEAR(half.getSample(0, 1000), 0.5f * (dry.getSample(0, 1000) + wet.getSample(0, 1000)), 1e-6);

    writeWav(outputPath("l04_ex3_parallel_50.wav"), half, kSampleRate);
    writeWav(outputPath("l04_ex3_parallel_wet.wav"), wet, kSampleRate);
    std::printf("  Wrote dawlab_output/l04_ex3_parallel_50.wav, l04_ex3_parallel_wet.wav\n");
    return dawlab::test::summary("l04_ex3");
}
