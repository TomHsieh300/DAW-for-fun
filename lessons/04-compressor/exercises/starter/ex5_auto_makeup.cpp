// Exercise 5 (starter code): auto makeup gain.
//
// Goals:
//   1. Implement autoMakeupDb(): the makeup (dB) needed for a 0 dBFS input to return to 0 dBFS after passing
//      through the static curve;
//   2. Implement steadyOutputPeakDb(): run a sine through a real Compressor + makeup and measure the steady-state
//      output peak;
//   3. Pass the self-check (e.g. T -20 / R 4 / hard knee → makeup = 15 dB).
// You are done when all self-checks pass after you complete the TODOs. The starter code prints FAIL when run; that is expected.

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
    (void)thresholdDb;
    (void)ratio;
    (void)kneeDb;
    // TODO: use Compressor::computeOutputDb(0, T, R, W) to find how many dB 0 dBFS gets squashed to, then negate it
    return 0.0f;
}

// Run a sine (1 second) and return the steady-state (last 200 ms) output peak (dBFS)
float steadyOutputPeakDb(float inputDb, float thresholdDb, float ratio, float kneeDb, float makeupDb) {
    (void)thresholdDb;
    (void)ratio;
    (void)kneeDb;
    (void)makeupDb;
    AudioBuffer buf = makeSine(1000.0, kSampleRate, 1.0, dbToGain(inputDb), 1);
    // TODO: create a Compressor (attack 1 ms, release 100 ms, setMakeupGainDb(makeupDb)) and process buf block by block
    const int tail = msToSamples(200.0, kSampleRate);
    return gainToDb(peakAbs(buf.getReadPointer(0) + buf.getNumSamples() - tail, tail));
}

} // namespace

int main() {
    std::printf("Exercise 5 (starter): auto makeup\n");

    struct Case {
        float thresholdDb, ratio, kneeDb, expectedMakeupDb;
    };
    const Case cases[] = {
        {-20.0f, 4.0f, 0.0f, 15.0f},
        {-20.0f, 4.0f, 10.0f, 15.0f},
        {-30.0f, 2.0f, 0.0f, 15.0f},
        {-10.0f, 10.0f, 0.0f, 9.0f},
        {-6.0f, 4.0f, 20.0f, 4.8f}, // 0 dBFS falls inside the knee, so use the quadratic section
    };
    for (const Case& c : cases) {
        const float m = autoMakeupDb(c.thresholdDb, c.ratio, c.kneeDb);
        std::printf("  T %5.0f dB, R %4.0f:1, knee %4.0f dB → auto makeup %.3f dB (expected %.3f)\n", c.thresholdDb,
                    c.ratio, c.kneeDb, m, c.expectedMakeupDb);
        DAWLAB_CHECK_NEAR(m, c.expectedMakeupDb, 1e-4);
    }

    const float makeup = autoMakeupDb(-20.0f, 4.0f, 0.0f);
    const float out0 = steadyOutputPeakDb(0.0f, -20.0f, 4.0f, 0.0f, makeup);
    const float out6 = steadyOutputPeakDb(-6.0f, -20.0f, 4.0f, 0.0f, makeup);
    const float expected6 = Compressor::computeOutputDb(-6.0f, -20.0f, 4.0f, 0.0f) + makeup;
    std::printf("  Real run: 0 dBFS sine → %.2f dBFS (expected 0.00); -6 dBFS sine → %.2f dBFS (expected %.2f)\n", out0,
                out6, expected6);
    DAWLAB_CHECK_NEAR(out0, 0.0, 0.2);
    DAWLAB_CHECK_NEAR(out6, expected6, 0.2);
    return dawlab::test::summary("l04_ex5_starter");
}
