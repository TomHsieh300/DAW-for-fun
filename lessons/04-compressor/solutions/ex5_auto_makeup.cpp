// Exercise 5 solution: auto makeup gain.
//
// Idea: the static curve tells us "how many dB a 0 dBFS input gets squashed to"; add that amount back and 0 dBFS in
// gives 0 dBFS out:
//   makeup = -computeOutputDb(0, T, R, W)
// Many plugins apply only half (makeup / 2), because real music rarely sits at 0 dBFS all the time and full makeup
// would be too loud.
// self-check:
//   - T -20 / R 4 / hard knee → makeup = 15 dB; with a 10 dB soft knee 0 dBFS is outside the knee, so the makeup is also 15 dB
//   - run a real Compressor + makeup on a 0 dBFS sine → steady-state output peak ≈ 0 dBFS
//   - -6 dBFS sine → output = computeOutputDb(-6) + 15 = -1.5 dBFS

#include <dawlab/Common.h>
#include <dawlab/dsp/Compressor.h>

#include <cstdio>

using namespace dawlab;
using dawlab::dsp::Compressor;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

// The makeup gain (dB) that maps a 0 dBFS input to a 0 dBFS output
float autoMakeupDb(float thresholdDb, float ratio, float kneeDb) {
    return -Compressor::computeOutputDb(0.0f, thresholdDb, ratio, kneeDb);
}

// Run a sine and return the steady-state (last 200 ms) output peak (dBFS)
float steadyOutputPeakDb(float inputDb, float thresholdDb, float ratio, float kneeDb, float makeupDb) {
    Compressor comp;
    comp.prepare(kSampleRate, kBlockSize, 1);
    comp.setThresholdDb(thresholdDb);
    comp.setRatio(ratio);
    comp.setKneeDb(kneeDb);
    comp.setAttackMs(1.0f);
    comp.setReleaseMs(100.0f);
    comp.setMakeupGainDb(makeupDb);
    AudioBuffer buf = makeSine(1000.0, kSampleRate, 1.0, dbToGain(inputDb), 1);
    AudioBlock whole(buf);
    for (int start = 0; start < buf.getNumSamples(); start += kBlockSize)
        comp.process(whole.getSubBlock(start, std::min(kBlockSize, buf.getNumSamples() - start)));
    const int tail = msToSamples(200.0, kSampleRate);
    return gainToDb(peakAbs(buf.getReadPointer(0) + buf.getNumSamples() - tail, tail));
}

} // namespace

int main() {
    std::printf("Exercise 5: auto makeup\n");

    struct Case {
        float thresholdDb, ratio, kneeDb, expectedMakeupDb;
    };
    const Case cases[] = {
        {-20.0f, 4.0f, 0.0f, 15.0f},   // 0 - (-20 + 20/4) = 15
        {-20.0f, 4.0f, 10.0f, 15.0f},  // 0 dBFS is outside the knee (-25..-15), same as the hard knee
        {-30.0f, 2.0f, 0.0f, 15.0f},   // 0 - (-30 + 30/2) = 15
        {-10.0f, 10.0f, 0.0f, 9.0f},   // 0 - (-10 + 10/10) = 9
        {-6.0f, 4.0f, 20.0f, 4.8f},    // 0 dBFS falls inside the knee (-16..+4): 0 + (1/4-1)·(0+6+10)²/(2·20) = -0.75·256/40 = -4.8
    };
    for (const Case& c : cases) {
        const float m = autoMakeupDb(c.thresholdDb, c.ratio, c.kneeDb);
        std::printf("  T %5.0f dB, R %4.0f:1, knee %4.0f dB → auto makeup %.3f dB (expected %.3f)\n", c.thresholdDb,
                    c.ratio, c.kneeDb, m, c.expectedMakeupDb);
        DAWLAB_CHECK_NEAR(m, c.expectedMakeupDb, 1e-4);
    }

    // Dynamic check: 0 dBFS sine + auto makeup → steady-state output 0 dBFS
    const float makeup = autoMakeupDb(-20.0f, 4.0f, 0.0f);
    const float out0 = steadyOutputPeakDb(0.0f, -20.0f, 4.0f, 0.0f, makeup);
    const float out6 = steadyOutputPeakDb(-6.0f, -20.0f, 4.0f, 0.0f, makeup);
    const float expected6 = Compressor::computeOutputDb(-6.0f, -20.0f, 4.0f, 0.0f) + makeup;
    std::printf("  Real run: 0 dBFS sine → %.2f dBFS (expected 0.00); -6 dBFS sine → %.2f dBFS (expected %.2f)\n", out0,
                out6, expected6);
    DAWLAB_CHECK_NEAR(out0, 0.0, 0.2);
    DAWLAB_CHECK_NEAR(out6, expected6, 0.2);

    // Comparison without makeup
    std::printf("  For comparison (no makeup): 0 dBFS sine → %.2f dBFS\n",
                steadyOutputPeakDb(0.0f, -20.0f, 4.0f, 0.0f, 0.0f));
    std::printf("  \"Half makeup\" version: makeup/2 = %.2f dB\n", makeup * 0.5f);
    return dawlab::test::summary("l04_ex5");
}
