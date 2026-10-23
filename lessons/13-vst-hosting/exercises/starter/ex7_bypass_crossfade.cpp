// Exercise 7 (starter): host-side "click-free bypass" -- latency compensation + crossfade
//
// Simply removing the plugin (hard bypass) causes two problems:
//   (a) the waveform is discontinuous -> a click;
//   (b) when the plugin has latency L, wet is L samples later than dry, so at the moment of the switch the timeline jumps by L -> an even larger jump,
//       and the whole track suddenly arrives L samples "early" (out of alignment with the other tracks).
// The fix:
//   delay dry by L too (the same idea as PDC), then switch between dry and wet with a 10 ms linear crossfade:
//     y[n] = g[n]*wet[n] + (1 - g[n])*dry[n - L], with g moving smoothly from 1 to 0 (bypass) or the other way round.
//   The plugin keeps processing while bypassed (to keep its internal state "warm", so there is no transient when bypass is released).
// This is in fact the same trick as the plugin's internal mix (parallel) parameter: dry/wet blending + aligning dry to the latency.
//
// Complete the TODO in CrossfadeBypass::process(). Currently bypass has no effect, so the self-check prints FAIL (this is expected).
//
// Verification (DawLabLimiter: ceiling -20 dB, lookahead 5 ms -> L = 240; input is a 440 Hz sine wave with amplitude 0.3):
//   1. Once bypass has settled, the output = the input delayed by L (the timeline does not jump);
//   2. The crossfade version's maximum sample-to-sample jump is < 0.025 (the sine's own maximum slope is 0.3*2*pi*440/48000 ~ 0.0173);
//   3. The hard bypass with no compensation and no crossfade jumps by > 0.05 (an audible click).
// Output: dawlab_output/l13_ex7_crossfade.wav, l13_ex7_hard.wav (bypass toggles every 0.5 s; the hard version has an obvious "click")

#include "../../examples/L13Common.h"

#include <dawlab/dsp/DelayLine.h>
#include <dawlab/dsp/SmoothedValue.h>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

class CrossfadeBypass {
public:
    // message thread: allocates memory. latencySamples comes from plugin.getLatencySamples().
    void prepare(double sampleRate, int maxBlockSize, int numChannels, int latencySamples, double fadeSeconds = 0.01) {
        latency_ = latencySamples;
        delay_.prepare(std::max(1, latencySamples), numChannels);
        dry_.setSize(numChannels, maxBlockSize);
        gain_.reset(sampleRate, fadeSeconds);
        gain_.setCurrentAndTargetValue(bypassed_ ? 0.0f : 1.0f);
    }

    void setBypassed(bool bypassed) noexcept {
        bypassed_ = bypassed;
        gain_.setTargetValue(bypassed ? 0.0f : 1.0f);
    }

    // audio thread: does not allocate
    void process(dawlab::AudioBlock block, dawlab::host::HostedPlugin& plugin) noexcept {
        // TODO:
        //   1. for every channel and every sample: delay_.push(ch, x), dry_[ch][i] = delay_.read(ch, latency_)
        //      (align dry to the plugin's latency);
        //   2. plugin.process(block) -- must also run while bypassed, to keep the plugin's internal state;
        //   3. for every sample take g = gain_.getNextValue() and y = dry + g*(wet - dry).
        // Currently: it always outputs only wet, and setBypassed() has no effect at all.
        plugin.process(block);
    }

private:
    dawlab::dsp::DelayLine delay_;
    dawlab::AudioBuffer dry_;
    dawlab::dsp::SmoothedValue gain_{1.0f};
    int latency_ = 0;
    bool bypassed_ = false;
};

// Toggles every 0.5 s: bypassed at 0.5 s, released at 1.0 s, bypassed again at 1.5 s
bool bypassAt(int sample) { return (sample / static_cast<int>(0.5 * kSampleRate)) % 2 == 1; }

float maxStep(const dawlab::AudioBuffer& b, int from) {
    float m = 0.0f;
    for (int i = std::max(1, from); i < b.getNumSamples(); ++i)
        m = std::max(m, std::abs(b.getSample(0, i) - b.getSample(0, i - 1)));
    return m;
}

} // namespace

int main() {
    const juce::ScopedJuceInitialiser_GUI juceInit;
    dawlab::host::PluginHost host;
    auto plugin = l13::mustLoad(host, l13::defaultLimiterVst3(), kSampleRate, kBlockSize);
    if (plugin == nullptr)
        return 1;
    l13::setParam(*plugin, "Ceiling", l13::toNormalized(-20.0f, -24.0f, 0.0f));
    l13::setParam(*plugin, "Lookahead", l13::toNormalized(5.0f, 0.0f, 20.0f));

    const dawlab::AudioBuffer input = dawlab::makeSine(440.0, kSampleRate, 2.0, 0.3f, 2);
    const int total = input.getNumSamples();

    // ---- crossfade + latency compensation -----------------------------------------
    plugin->prepare(kSampleRate, kBlockSize, 2);
    const int latency = plugin->getLatencySamples();
    CrossfadeBypass bypass;
    bypass.prepare(kSampleRate, kBlockSize, 2, latency);
    dawlab::AudioBuffer smooth = input;
    dawlab::AudioBlock smoothBlock(smooth);
    for (int start = 0; start < total; start += kBlockSize) {
        bypass.setBypassed(bypassAt(start)); // automation: changes only at block boundaries
        bypass.process(smoothBlock.getSubBlock(start, std::min(kBlockSize, total - start)), *plugin);
    }
    plugin->releaseResources();

    // ---- Naive hard bypass: output the original audio directly when bypassed (no delay, no fade) ----
    plugin->prepare(kSampleRate, kBlockSize, 2);
    dawlab::AudioBuffer hard = input;
    dawlab::AudioBlock hardBlock(hard);
    for (int start = 0; start < total; start += kBlockSize) {
        auto sub = hardBlock.getSubBlock(start, std::min(kBlockSize, total - start));
        if (!bypassAt(start))
            plugin->process(sub); // do nothing when bypassed = the original audio
    }
    plugin->releaseResources();

    // ---- Measurement ---------------------------------------------------------------
    const int skip = static_cast<int>(0.1 * kSampleRate); // skip the start (the limiter's initial transient)
    const float stepSmooth = maxStep(smooth, skip);
    const float stepHard = maxStep(hard, skip);
    const float signalStep = static_cast<float>(0.3 * dawlab::kTwoPi * 440.0 / kSampleRate);

    // Bypass steady region (0.7-0.95 s): the output should = the input delayed by latency
    float alignError = 0.0f;
    for (int i = static_cast<int>(0.7 * kSampleRate); i < static_cast<int>(0.95 * kSampleRate); ++i)
        alignError = std::max(alignError, std::abs(smooth.getSample(0, i) - input.getSample(0, i - latency)));
    // Active region (0.3-0.45 s): the limiter squashes 0.3 down to the ceiling 0.1
    float activePeak = 0.0f;
    for (int i = static_cast<int>(0.3 * kSampleRate); i < static_cast<int>(0.45 * kSampleRate); ++i)
        activePeak = std::max(activePeak, std::abs(smooth.getSample(0, i)));

    std::printf("plugin latency: %d samples\n", latency);
    std::printf("Sine's own maximum sample-to-sample change: %.4f\n", signalStep);
    std::printf("crossfade + compensation: max jump %.4f; hard bypass: max jump %.4f\n", stepSmooth, stepHard);
    std::printf("Max difference between the bypassed output and \"input delayed by %d\": %.3g; peak when active: %.4f (ceiling 0.1)\n", latency, alignError,
                activePeak);
    dawlab::writeWav(dawlab::outputPath("l13_ex7_crossfade.wav"), smooth, kSampleRate);
    dawlab::writeWav(dawlab::outputPath("l13_ex7_hard.wav"), hard, kSampleRate);

    DAWLAB_CHECK(latency == dawlab::msToSamples(5.0, kSampleRate));
    DAWLAB_CHECK(alignError < 1.0e-6f);
    DAWLAB_CHECK(activePeak <= 0.1f * 1.001f);
    DAWLAB_CHECK(stepSmooth < 0.025f);
    DAWLAB_CHECK(stepHard > 0.05f);
    return dawlab::test::summary("l13_ex7_bypass_crossfade");
}
