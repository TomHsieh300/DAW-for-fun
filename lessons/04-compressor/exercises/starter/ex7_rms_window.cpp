// Exercise 7 (starter code): the effect of the RMS detector window length on pumping.
//
// Goals:
//   1. Complete run(): compress the drum loop block by block with a Compressor (RMS mode, given window length),
//      record getGainReductionDb() for every block (skipping the first 0.5 s of warm-up), and compute the GR swing
//      = max - min;
//   2. Observe: the longer the window, the smaller the swing (less pumping); a 1 ms window behaves much like a peak detector;
//   3. Pass the self-check (the swing decreases monotonically as the window gets longer).
// You are done when all self-checks pass after you complete the TODOs. The starter code prints FAIL when run; that is expected.

#include <dawlab/Common.h>
#include <dawlab/dsp/Compressor.h>

#include <cstdio>
#include <vector>

using namespace dawlab;
using dawlab::dsp::Compressor;
using dawlab::dsp::DetectorMode;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

struct Outcome {
    float grMax, grMin, grSwing, crestDb;
    AudioBuffer out;
};

Outcome run(const AudioBuffer& dry, DetectorMode mode, float rmsWindowMs) {
    Compressor comp;
    comp.prepare(kSampleRate, kBlockSize, dry.getNumChannels());
    comp.setThresholdDb(-24.0f);
    comp.setRatio(4.0f);
    comp.setKneeDb(3.0f);
    comp.setAttackMs(1.0f);
    comp.setReleaseMs(50.0f);
    // TODO: comp.setDetectorMode(mode), comp.setRmsWindowMs(rmsWindowMs)
    (void)mode;
    (void)rmsWindowMs;

    Outcome o{0.0f, 1000.0f, 0.0f, 0.0f, dry};
    // TODO: call comp.process() block by block; once start >= 0.5 seconds, update o.grMax / o.grMin
    o.grSwing = o.grMax - o.grMin;
    float r = 0.0f;
    for (int ch = 0; ch < o.out.getNumChannels(); ++ch)
        r = std::max(r, rms(o.out, ch));
    o.crestDb = gainToDb(peakAbs(o.out)) - gainToDb(r);
    return o;
}

} // namespace

int main() {
    std::printf("Exercise 7 (starter): RMS window length vs pumping\n");
    const AudioBuffer dry = makeDrumLoop(kSampleRate, 2, 120.0);

    const Outcome peak = run(dry, DetectorMode::Peak, 10.0f);
    std::printf("  Peak detector (reference): GR max %.2f / min %.2f, swing %.2f dB, output crest %.2f dB\n", peak.grMax,
                peak.grMin, peak.grSwing, peak.crestDb);

    const float windows[] = {1.0f, 5.0f, 10.0f, 30.0f, 100.0f, 300.0f, 1000.0f};
    std::vector<float> swings;
    for (float w : windows) {
        const Outcome o = run(dry, DetectorMode::Rms, w);
        swings.push_back(o.grSwing);
        std::printf("  RMS window %5.0f ms: GR max %.2f / min %.2f, swing %.2f dB, output crest %.2f dB\n", w, o.grMax,
                    o.grMin, o.grSwing, o.crestDb);
        DAWLAB_CHECK(!hasNanOrInf(o.out));
    }
    for (size_t i = 1; i < swings.size(); ++i)
        DAWLAB_CHECK(swings[i] < swings[i - 1]);
    DAWLAB_CHECK(swings.front() > 3.0f * swings.back());
    DAWLAB_CHECK(std::abs(swings.front() - peak.grSwing) < 3.0f);
    return dawlab::test::summary("l04_ex7_starter");
}
