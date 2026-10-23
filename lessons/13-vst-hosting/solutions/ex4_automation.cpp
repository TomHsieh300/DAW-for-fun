// Exercise 4 solution: host-side automation -- split the audio block into sub-blocks and set a parameter at the start of each one
//
// Scenario: a line is drawn on a DAW automation lane that takes DawLabCompressor's Threshold from 0 dB to -40 dB over 8 seconds.
// The plugin reads its parameters only once per processBlock, so if the host wants a finer time resolution it splits the 512-sample block
// into 32-sample sub-blocks and sends a new value at the start of each (VST3 actually allows sample-accurate parameter queues,
// but on the JUCE plugin side values are only applied at the start of a block; splitting into sub-blocks is the most general method, and every host can do it).
//
// Verification:
//   1. Matches the reference result of "calling dsp::Compressor directly and changing the threshold every 32 samples" (difference < 1e-4);
//   2. The first 0.5 s is barely compressed (RMS change > -1 dB) and the last 1 s is compressed a lot (RMS change < -6 dB).
// Output: dawlab_output/l13_ex4_automation.wav (what you hear: the drums start out loose and get flatter and more "squeezed")

#include "../examples/L13Common.h"

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
        // One audio callback (512 samples) is split into 16 sub-blocks
        for (int sub = 0; sub < blockLen; sub += kSubBlock) {
            const int len = std::min(kSubBlock, blockLen - sub);
            const float db = thresholdAt(start + sub, total);
            plugin->setParameterValue(thresholdIndex, l13::toNormalized(db, -60.0f, 0.0f));
            ++parameterChanges;
            plugin->process(all.getSubBlock(start + sub, len));
        }
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

    std::printf("Parameter changes: %d (once every %d samples, time resolution %.2f ms)\n", parameterChanges, kSubBlock,
                1000.0 * kSubBlock / kSampleRate);
    std::printf("RMS change in the first 0.5 s: %+.2f dB; in the last 1 s: %+.2f dB\n", early, late);
    std::printf("Max difference from the reference (dsp::Compressor): %.3g\n", diff);
    dawlab::writeWav(dawlab::outputPath("l13_ex4_automation.wav"), output, kSampleRate);

    DAWLAB_CHECK(parameterChanges >= total / kSubBlock);
    DAWLAB_CHECK(diff < 1.0e-4f);
    DAWLAB_CHECK(early > -1.0f);
    DAWLAB_CHECK(late < -6.0f);
    return dawlab::test::summary("l13_ex4_automation");
}
