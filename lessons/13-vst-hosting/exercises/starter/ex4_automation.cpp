// Exercise 4 (starter): host-side automation -- split the audio block into sub-blocks and set a parameter at the start of each one
//
// Goal: make DawLabCompressor's Threshold fall linearly from 0 dB to -40 dB over 8 seconds (snapped to 0.1 dB),
//       with a time resolution of 32 samples: each 512-sample block is split into 16 segments and setParameterValue is called once at the start of each.
// Once done, every self-check will PASS; the current code does no automation, so it prints FAIL (this is expected).

#include "../../examples/L13Common.h"

#include <dawlab/dsp/Compressor.h>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr int kSubBlock = 32;

// Automation curve: t = 0 -> 0 dB, t = end -> -40 dB; snapped to the plugin's interval (0.1 dB)
float thresholdAt(int sample, int totalSamples) {
    const double t = static_cast<double>(sample) / totalSamples;
    const double db = -40.0 * t;
    return static_cast<float>(std::round(db * 10.0) / 10.0);
}

float rmsChangeDb(const dawlab::AudioBuffer& in, const dawlab::AudioBuffer& out, int start, int length) {
    const float a = dawlab::rms(in.getReadPointer(0) + start, length);
    const float b = dawlab::rms(out.getReadPointer(0) + start, length);
    return dawlab::gainToDb(b / a);
}

} // namespace

int main() {
    const juce::ScopedJuceInitialiser_GUI juceInit;
    dawlab::host::PluginHost host;
    auto plugin = l13::mustLoad(host, l13::defaultCompressorVst3(), kSampleRate, kBlockSize);
    if (plugin == nullptr)
        return 1;

    const dawlab::AudioBuffer input = dawlab::makeDrumLoop(kSampleRate, 4); // 8 seconds
    const int total = input.getNumSamples();
    const int thresholdIndex = plugin->findParameter("Threshold");
    DAWLAB_CHECK(thresholdIndex >= 0);

    plugin->prepare(kSampleRate, kBlockSize, 2);
    dawlab::AudioBuffer output = input;
    dawlab::AudioBlock all(output);
    int parameterChanges = 0;
    for (int start = 0; start < total; start += kBlockSize) {
        const int blockLen = std::min(kBlockSize, total - start);
        // TODO: split this block into sub-blocks of length kSubBlock (all.getSubBlock(...)); for each one:
        //   1. compute the dB value with thresholdAt(start + sub, total);
        //   2. convert it to 0..1 with l13::toNormalized(db, -60.0f, 0.0f) and call plugin->setParameterValue(thresholdIndex, ...);
        //   3. ++parameterChanges;
        //   4. plugin->process(that sub-block).
        // Currently the whole block is processed at once and the parameter is never changed:
        plugin->process(all.getSubBlock(start, blockLen));
    }
    plugin->releaseResources();

    // Reference answer: the same automation applied directly to dsp::Compressor (other parameters = the plugin defaults)
    dawlab::dsp::Compressor reference;
    reference.prepare(kSampleRate, kBlockSize, 2);
    reference.setRatio(4.0f);
    reference.setKneeDb(6.0f);
    reference.setAttackMs(10.0f);
    reference.setReleaseMs(100.0f);
    reference.reset();
    dawlab::AudioBuffer expected = input;
    dawlab::AudioBlock ref(expected);
    for (int s = 0; s < total; s += kSubBlock) {
        reference.setThresholdDb(thresholdAt(s, total));
        reference.process(ref.getSubBlock(s, std::min(kSubBlock, total - s)));
    }

    const float diff = l13::maxAbsDifference(output, expected);
    const int half = static_cast<int>(0.5 * kSampleRate);
    const int second = static_cast<int>(kSampleRate);
    const float early = rmsChangeDb(input, output, 0, half);
    const float late = rmsChangeDb(input, output, total - second, second);

    std::printf("Parameter changes: %d\n", parameterChanges);
    std::printf("RMS change in the first 0.5 s: %+.2f dB; in the last 1 s: %+.2f dB\n", early, late);
    std::printf("Max difference from the reference (dsp::Compressor): %.3g\n", diff);

    DAWLAB_CHECK(parameterChanges >= total / kSubBlock);
    DAWLAB_CHECK(diff < 1.0e-4f);
    DAWLAB_CHECK(early > -1.0f);
    DAWLAB_CHECK(late < -6.0f);
    return dawlab::test::summary("l13_ex4_automation");
}
