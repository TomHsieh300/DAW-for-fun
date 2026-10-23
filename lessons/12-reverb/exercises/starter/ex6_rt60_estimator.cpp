// Exercise 6 starter: estimating RT60 with Schroeder backward integration
//
// Goals:
//   1. Complete schroederDecayDb(): EDC[n] = sum_{k=n}^{N-1} h[k]^2, converted to dB (relative to EDC[0]).
//      Hint: accumulate backwards from the tail; a single loop is enough (O(N)).
//   2. Complete estimateRt60(): run a linear regression (t against dB) over the part of the EDC where
//      EDC_dB lies between startDb .. endDb; slope s (dB/s) -> RT60 = -60 / s.
//   3. Self-check: for an ideal exponentially decaying sine, the EDC at t = RT60/2 is -30 dB; the estimate
//      must be within 3% of the true value.
//
// At run time the self-checks print FAIL; after you complete the TODOs they should all PASS.

#include <dawlab/Common.h>
#include <dawlab/dsp/ConvolutionReverb.h>
#include <dawlab/dsp/ReverbBuildingBlocks.h>

#include <cmath>
#include <cstdio>
#include <vector>

using namespace dawlab;
using namespace dawlab::dsp;

namespace {

constexpr double kSampleRate = 48000.0;

std::vector<double> schroederDecayDb(const float* h, int n) {
    std::vector<double> edc(static_cast<size_t>(n), 0.0);
    // TODO: backward integration + convert to dB
    (void)h;
    return edc;
}

struct Rt60Estimate {
    double rt60 = 0.0;
    double slopeDbPerSecond = 0.0;
    bool valid = false;
};

Rt60Estimate estimateRt60(const std::vector<double>& edcDb, double sampleRate, double startDb = -5.0,
                          double endDb = -25.0) {
    Rt60Estimate r;
    // TODO: linear regression (using only the points of edcDb between startDb..endDb), RT60 = -60 / slope
    (void)edcDb;
    (void)sampleRate;
    (void)startDb;
    (void)endDb;
    return r;
}

AudioBuffer makeDecayingSine(double freq, double rt60, double seconds) {
    AudioBuffer b(1, secondsToSamples(seconds, kSampleRate));
    const double k = std::log(1000.0) / rt60;
    for (int i = 0; i < b.getNumSamples(); ++i) {
        const double t = i / kSampleRate;
        b.setSample(0, i, static_cast<float>(std::exp(-k * t) * std::sin(kTwoPi * freq * t)));
    }
    return b;
}

} // namespace

int main() {
    std::printf("=== Exercise 6: Schroeder backward integration (starter) ===\n");

    {
        AudioBuffer sine = makeDecayingSine(440.0, 1.0, 1.3);
        const std::vector<double> edc = schroederDecayDb(sine.getReadPointer(0), sine.getNumSamples());
        const double at05 = edc[static_cast<size_t>(secondsToSamples(0.5, kSampleRate))];
        std::printf("EDC @0.5 s of an exponentially decaying sine (RT60 1 s) = %.2f dB (theory: -30)\n", at05);
        DAWLAB_CHECK_NEAR(at05, -30.0, 0.5);
        const Rt60Estimate e = estimateRt60(edc, kSampleRate);
        std::printf("Estimated RT60 = %.3f s\n", e.rt60);
        DAWLAB_CHECK(e.valid);
        DAWLAB_CHECK_NEAR(e.rt60, 1.0, 0.03);
    }
    for (double truth : {0.5, 1.5}) {
        AudioBuffer ir = ConvolutionReverb::makeSyntheticImpulseResponse(kSampleRate, truth, 1, 11);
        const Rt60Estimate e = estimateRt60(schroederDecayDb(ir.getReadPointer(0), ir.getNumSamples()), kSampleRate);
        std::printf("Synthetic IR with true RT60 %.1f s -> estimated %.3f s\n", truth, e.rt60);
        DAWLAB_CHECK(e.valid);
        DAWLAB_CHECK_NEAR(e.rt60 / truth, 1.0, 0.1);
    }

    return dawlab::test::summary("l12_ex6_starter");
}
