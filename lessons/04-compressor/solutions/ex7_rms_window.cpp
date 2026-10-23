// Exercise 7 solution: the effect of the RMS detector window length on pumping.
//
// The shorter the RMS window, the more the detector behaves like a peak detector: every drum hit makes the GR jump up
// and down by a large amount (pumping / breathing); the longer the window, the smoother the level estimate and the
// more constant the GR, so the compression becomes "flattening the whole" rather than "breathing with every beat".
// Quantified by: recording getGainReductionDb() for every block and computing the "GR swing" = max - min (skipping
// the first 0.5 s of warm-up); the self-check requires the swing to decrease monotonically as the window gets longer.
// Output: dawlab_output/l04_ex7_rms_1ms.wav, l04_ex7_rms_10ms.wav, l04_ex7_rms_100ms.wav.

#include <dawlab/Common.h>
#include <dawlab/dsp/Compressor.h>

#include <cstdio>
#include <string>
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
    comp.setDetectorMode(mode);
    comp.setRmsWindowMs(rmsWindowMs);

    Outcome o{0.0f, 1000.0f, 0.0f, 0.0f, dry};
    AudioBlock whole(o.out);
    const int warmup = secondsToSamples(0.5, kSampleRate);
    for (int start = 0; start < o.out.getNumSamples(); start += kBlockSize) {
        comp.process(whole.getSubBlock(start, std::min(kBlockSize, o.out.getNumSamples() - start)));
        if (start >= warmup) {
            o.grMax = std::max(o.grMax, comp.getGainReductionDb());
            o.grMin = std::min(o.grMin, comp.getGainReductionDb());
        }
    }
    o.grSwing = o.grMax - o.grMin;
    float r = 0.0f;
    for (int ch = 0; ch < o.out.getNumChannels(); ++ch)
        r = std::max(r, rms(o.out, ch));
    o.crestDb = gainToDb(peakAbs(o.out)) - gainToDb(r);
    return o;
}

} // namespace

int main() {
    std::printf("Exercise 7: RMS window length vs pumping (T -24 dB, 4:1, attack 1 ms, release 50 ms)\n");
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
        if (w == 1.0f || w == 10.0f || w == 100.0f) {
            const std::string name = "l04_ex7_rms_" + std::to_string(static_cast<int>(w)) + "ms.wav";
            writeWav(outputPath(name), o.out, kSampleRate);
        }
    }
    // self-check: the swing decreases monotonically as the window gets longer; the shortest window's swing is at least 3x the longest's
    for (size_t i = 1; i < swings.size(); ++i)
        DAWLAB_CHECK(swings[i] < swings[i - 1]);
    DAWLAB_CHECK(swings.front() > 3.0f * swings.back());
    // The 1 ms RMS window behaves much like a peak detector (the swings differ by less than 3 dB)
    DAWLAB_CHECK(std::abs(swings.front() - peak.grSwing) < 3.0f);
    std::printf("  Wrote dawlab_output/l04_ex7_rms_1ms.wav, l04_ex7_rms_10ms.wav, l04_ex7_rms_100ms.wav\n");
    return dawlab::test::summary("l04_ex7");
}
