// Exercise 7 (**): compare the alias energy and CPU cost of 1x / 2x / 4x / 8x oversampling
//
// Goal: using dawlab::dsp::Saturation (Tanh, drive +18 dB)
//   1. Write aliasDb(log2): process a 7 kHz sine with peak 0.5, then return the total power at all alias
//      positions (odd kHz, excluding 7 and 21 kHz) divided by the fundamental power (in dB)
//   2. Write timeMs(log2, noise): the number of milliseconds taken to process 10 seconds of stereo noise
//      (std::chrono::steady_clock; run 3 times and take the minimum)
//   3. Observe: alias falls with the factor, time rises with the factor; is anything beyond 4x worth it?
//
// Hints:
//   - Why are all the aliases at odd kHz? See README section 2.5 (gcd(7000, 48000) = 1000, and tanh has only odd harmonics).
//   - Skip the first 0.1 s (latency and transient) before measuring.
//   - Do not include "copying the buffer" or "prepare" in the timing.
//
// Until all TODOs are done, running this prints FAIL (that is expected).
// Run: l06_ex7_starter

#include <dawlab/Common.h>
#include <dawlab/dsp/Saturation.h>

#include <chrono>
#include <cstdio>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr double kF0 = 7000.0;

using dawlab::dsp::Saturation;
using dawlab::dsp::SaturationCurve;

[[maybe_unused]] void configure(Saturation& sat, int log2, int numChannels) {
    sat.setOversamplingFactorLog2(log2);
    sat.prepare(kSampleRate, kBlockSize, numChannels);
    sat.setCurve(SaturationCurve::Tanh);
    sat.setDriveDb(18.0f);
    sat.setMix(1.0f);
    sat.setOutputGainDb(-6.0f);
    sat.reset();
}

[[maybe_unused]] void processAll(Saturation& sat, dawlab::AudioBuffer& b) {
    dawlab::AudioBlock whole(b);
    for (int start = 0; start < b.getNumSamples(); start += kBlockSize)
        sat.process(whole.getSubBlock(start, std::min(kBlockSize, b.getNumSamples() - start)));
}

// TODO 1
double aliasDb(int log2) {
    (void)log2;
    return 0.0;
}

// TODO 2
double timeMs(int log2, const dawlab::AudioBuffer& noise) {
    (void)log2;
    (void)noise;
    return 0.0;
}

} // namespace

int main() {
    const double seconds = 10.0;
    const dawlab::AudioBuffer noise = dawlab::makeWhiteNoise(kSampleRate, seconds, 0.5f, 2);

    std::vector<double> alias, ms;
    for (int log2 = 0; log2 <= 3; ++log2) {
        alias.push_back(aliasDb(log2));
        ms.push_back(timeMs(log2, noise));
        std::printf("%dx: alias %.1f dB, %.1f ms\n", 1 << log2, alias.back(), ms.back());
    }
    DAWLAB_CHECK(alias[1] < alias[0] - 10.0);
    DAWLAB_CHECK(alias[2] < alias[1] - 20.0);
    DAWLAB_CHECK(alias[3] <= alias[2] + 1.0);
    DAWLAB_CHECK(alias[2] < -70.0);
    DAWLAB_CHECK(ms[2] > ms[0] * 1.5);
    DAWLAB_CHECK(ms[3] > ms[1]);
    return dawlab::test::summary("l06_ex7_starter");
}
