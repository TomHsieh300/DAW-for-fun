// Exercise 7 solution: compare the alias energy and CPU cost of 1x / 2x / 4x / 8x oversampling.
//
// Method:
//   - Quality: a 7 kHz sine with peak 0.5 through Saturation (Tanh, drive +18 dB). Measure the total power
//     at all alias positions (odd kHz, excluding 7 and 21 kHz) relative to the fundamental (same as the
//     l06_aliasing_demo example).
//   - Cost: process 10 seconds of stereo white noise (in 512-sample blocks) with the same Saturation
//     settings, timed with steady_clock. To reduce noise, run each factor 3 times and take the minimum.
//   - Then compute "CPU time per second of audio" = processing time / 10 s, which is roughly the percentage
//     of one core this plugin would use in real time.
//
// Expected (on the author's machine Release is about 3-4x faster than Debug; the relative factors differ slightly too):
//   alias total: 1x ~ -18 dB, 2x ~ -38 dB, 4x ~ -85 dB, 8x ~ -111 dB
//   cost: grows roughly linearly with the factor (tanh calls x L, plus a little more for each extra halfband stage)
//   conclusion: for a smooth curve such as tanh, 4x already pushes the alias down near the 16-bit
//   quantization noise floor (about -96 dB); 8x costs almost twice the CPU for an improvement you cannot hear.
//
// Run: l06_ex7_solution

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

void configure(Saturation& sat, int log2, int numChannels) {
    sat.setOversamplingFactorLog2(log2);
    sat.prepare(kSampleRate, kBlockSize, numChannels);
    sat.setCurve(SaturationCurve::Tanh);
    sat.setDriveDb(18.0f);
    sat.setMix(1.0f);
    sat.setOutputGainDb(-6.0f);
    sat.reset();
}

void processAll(Saturation& sat, dawlab::AudioBuffer& b) {
    dawlab::AudioBlock whole(b);
    for (int start = 0; start < b.getNumSamples(); start += kBlockSize)
        sat.process(whole.getSubBlock(start, std::min(kBlockSize, b.getNumSamples() - start)));
}

double aliasDb(int log2) {
    dawlab::AudioBuffer b = dawlab::makeSine(kF0, kSampleRate, 2.0, 0.5f, 1);
    Saturation sat;
    configure(sat, log2, 1);
    processAll(sat, b);
    const int skip = 4800;
    const float* d = b.getReadPointer(0) + skip;
    const int n = b.getNumSamples() - skip;
    const double h1 = dawlab::goertzelAmplitude(d, n, kF0, kSampleRate);
    double p = 0.0;
    for (int k = 1; k <= 23; k += 2) {
        const double f = 1000.0 * k;
        if (f == kF0 || f == 3 * kF0)
            continue;
        const double a = dawlab::goertzelAmplitude(d, n, f, kSampleRate);
        p += a * a;
    }
    return 10.0 * std::log10(std::max(p / (h1 * h1), 1e-15));
}

double timeMs(int log2, const dawlab::AudioBuffer& noise) {
    double best = 1e30;
    for (int rep = 0; rep < 3; ++rep) {
        dawlab::AudioBuffer b = noise; // the copy is not timed
        Saturation sat;
        configure(sat, log2, 2);
        const auto t0 = std::chrono::steady_clock::now();
        processAll(sat, b);
        const auto t1 = std::chrono::steady_clock::now();
        best = std::min(best, std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    return best;
}

} // namespace

int main() {
    const double seconds = 10.0;
    const dawlab::AudioBuffer noise = dawlab::makeWhiteNoise(kSampleRate, seconds, 0.5f, 2);

    std::vector<double> alias, ms;
    std::printf("%-6s %-10s %16s %12s %12s %14s\n", "Factor", "latency", "alias total (dB)", "time (ms)", "vs 1x",
                "CPU load (%)");
    for (int log2 = 0; log2 <= 3; ++log2) {
        Saturation probe;
        configure(probe, log2, 1);
        alias.push_back(aliasDb(log2));
        ms.push_back(timeMs(log2, noise));
        std::printf("%-6d %-10d %16.1f %12.1f %12.2f %14.2f\n", 1 << log2, probe.getLatencySamples(), alias.back(),
                    ms.back(), ms.back() / ms[0], 100.0 * ms.back() / (seconds * 1000.0));
    }
#ifdef NDEBUG
    std::printf("(This is a Release build; absolute times vary by machine, look at the relative factors.)\n");
#else
    std::printf("(This is a Debug build, so absolute times are slow; look at the relative factors. Build with -Config Release for realistic numbers.)\n");
#endif

    // Quality: each doubling must be cleaner (8x already hits the measurement floor, so only require no regression)
    DAWLAB_CHECK(alias[1] < alias[0] - 10.0);
    DAWLAB_CHECK(alias[2] < alias[1] - 20.0);
    DAWLAB_CHECK(alias[3] <= alias[2] + 1.0);
    DAWLAB_CHECK(alias[2] < -70.0);
    // Cost: a higher factor is more expensive (timing is noisy, so only check the "obvious" relationships)
    DAWLAB_CHECK(ms[2] > ms[0] * 1.5);
    DAWLAB_CHECK(ms[3] > ms[1]);

    return dawlab::test::summary("l06_ex7");
}
