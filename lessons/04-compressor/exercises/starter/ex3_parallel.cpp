// Exercise 3 (starter code): parallel (New York) compression.
//
// Goals:
//   1. Make a copy of the dry signal and compress it block by block with a "heavy" setting (low threshold, high
//      ratio, fast attack) to get the wet signal;
//   2. Apply makeup gain so the wet peak returns to the dry peak;
//   3. Implement parallelMix(): y = (1 - mix)·dry + mix·wet;
//   4. Print the crest factor for different mix values and pass the self-check.
// You are done when all self-checks pass after you complete the TODOs. The starter code prints FAIL when run; that is expected.

#include <dawlab/Common.h>
#include <dawlab/dsp/Compressor.h>

#include <cstdio>

using namespace dawlab;
using dawlab::dsp::Compressor;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

// crest factor = peak(dB) - RMS(dB), using the largest RMS over all channels
float crestFactorDb(const AudioBuffer& b) {
    float r = 0.0f;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        r = std::max(r, rms(b, ch));
    return gainToDb(peakAbs(b)) - gainToDb(r);
}

// Compress the whole buffer block by block (mimicking an audio callback), return the new compressed buffer, and record
// the largest gain reduction over the whole run
AudioBuffer compressBlocks(const AudioBuffer& input, Compressor& comp, float& maxGrDb) {
    AudioBuffer out = input;
    (void)comp;
    maxGrDb = 0.0f;
    // TODO: use AudioBlock whole(out) and getSubBlock() to call comp.process() once every kBlockSize samples,
    //       and update maxGrDb with comp.getGainReductionDb() after each block
    return out;
}

// y = (1 - mix)·dry + mix·wet, computed sample by sample (dry and wet must have the same length and channel count)
AudioBuffer parallelMix(const AudioBuffer& dry, const AudioBuffer& wet, float mix) {
    (void)wet;
    (void)mix;
    AudioBuffer out = dry;
    // TODO: clamp mix to 0..1, then do a linear blend for every channel and every sample
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
    std::printf("Exercise 3 (starter): parallel compression\n");
    const AudioBuffer dry = makeDrumLoop(kSampleRate, 2, 120.0);

    // Heavy compression: T -30 dB, 10:1, knee 3 dB, attack 0.02 ms (very fast, ≈ 1 sample, so the drum peaks get caught), release 60 ms
    Compressor comp;
    comp.prepare(kSampleRate, kBlockSize, dry.getNumChannels());
    comp.setThresholdDb(-30.0f);
    comp.setRatio(10.0f);
    comp.setKneeDb(3.0f);
    comp.setAttackMs(0.02f);
    comp.setReleaseMs(60.0f);
    float maxGr = 0.0f;
    AudioBuffer wet = compressBlocks(dry, comp, maxGr);

    // TODO: compute makeupDb = dry peak (dB) - wet peak (dB), and apply it with wet.applyGain()
    const float makeupDb = 0.0f;
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
    DAWLAB_CHECK_NEAR(half.getSample(0, 1000), 0.5f * (dry.getSample(0, 1000) + wet.getSample(0, 1000)), 1e-6);

    writeWav(outputPath("l04_ex3_parallel_50.wav"), half, kSampleRate);
    return dawlab::test::summary("l04_ex3_starter");
}
