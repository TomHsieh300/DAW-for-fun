// Exercise 7 solution: host-side "click-free bypass" -- latency compensation + crossfade
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
// Verification (DawLabLimiter: ceiling -20 dB, lookahead 5 ms -> L = 240; input is a 440 Hz sine wave with amplitude 0.3):
//   1. Once bypass has settled, the output = the input delayed by L (the timeline does not jump);
//   2. The crossfade version's maximum sample-to-sample jump is < 0.025 (the sine's own maximum slope is 0.3*2*pi*440/48000 ~ 0.0173);
//   3. The hard bypass with no compensation and no crossfade jumps by > 0.05 (an audible click).
// Extension: the plugin's own Bypass parameter (added automatically by the VST3 wrapper) must obey the same rule. DawLabLimiter overrides
// processBlockBypassed() and does the same "dry delayed by L + 10 ms crossfade" inside the plugin:
//   4. With Bypass = 1, an impulse at position 1000 appears at 1000 + L (not 1000), and it is the original audio (0.25);
//   5. Toggling the Bypass parameter every 0.5 s during playback: the maximum jump is likewise < 0.025, the bypass region = the input delayed by L, and the active-region peak <= the ceiling.
// Output: dawlab_output/l13_ex7_crossfade.wav, l13_ex7_hard.wav (bypass toggles every 0.5 s; the hard version has an obvious "click")

#include "../examples/L13Common.h"

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
        const int n = block.getNumSamples();
        const int numCh = std::min(block.getNumChannels(), dry_.getNumChannels());
        // 1) align dry: dry[n] = x[n - L]
        for (int ch = 0; ch < numCh; ++ch) {
            const float* x = block.getChannelPointer(ch);
            float* d = dry_.getWritePointer(ch);
            for (int i = 0; i < n; ++i) {
                delay_.push(ch, x[i]);
                d[i] = delay_.read(ch, latency_);
            }
        }
        // 2) wet: the plugin is always running
        plugin.process(block);
        // 3) crossfade (g = 1 -> fully wet; g = 0 -> fully dry)
        if (!gain_.isSmoothing() && gain_.getTargetValue() >= 1.0f)
            return;
        for (int i = 0; i < n; ++i) {
            const float g = gain_.getNextValue();
            for (int ch = 0; ch < numCh; ++ch) {
                float* y = block.getChannelPointer(ch);
                y[i] = dry_.getSample(ch, i) + g * (y[i] - dry_.getSample(ch, i));
            }
        }
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

    // ---- Extension: the plugin's own Bypass parameter (processBlockBypassed) ------------
    const int bypassParam = plugin->findParameter("Bypass");
    int bypassImpulseAt = -1;
    float bypassImpulseValue = 0.0f, stepParam = 1.0f, paramAlignError = 1.0f, paramActivePeak = 1.0f;
    if (bypassParam >= 0) {
        // (4) Bypass = 1 then prepare: the impulse should be delayed by L, and be the original audio
        plugin->setParameterValue(bypassParam, 1.0f);
        plugin->prepare(kSampleRate, kBlockSize, 2);
        const dawlab::AudioBuffer impulse = dawlab::makeImpulse(4096, 2, 1000, 0.25f);
        const dawlab::AudioBuffer bypassedOut = l13::renderOffline(*plugin, impulse, kBlockSize);
        for (int i = 0; i < bypassedOut.getNumSamples(); ++i)
            if (std::abs(bypassedOut.getSample(0, i)) > bypassImpulseValue) {
                bypassImpulseValue = std::abs(bypassedOut.getSample(0, i));
                bypassImpulseAt = i;
            }
        plugin->releaseResources();

        // (5) Toggle the Bypass parameter during playback (every 0.5 s, changing only at block boundaries); the plugin's internal crossfade keeps it click-free
        plugin->setParameterValue(bypassParam, 0.0f);
        plugin->prepare(kSampleRate, kBlockSize, 2);
        dawlab::AudioBuffer viaParam = input;
        dawlab::AudioBlock paramBlock(viaParam);
        for (int start = 0; start < total; start += kBlockSize) {
            plugin->setParameterValue(bypassParam, bypassAt(start) ? 1.0f : 0.0f);
            plugin->process(paramBlock.getSubBlock(start, std::min(kBlockSize, total - start)));
        }
        plugin->releaseResources();
        plugin->setParameterValue(bypassParam, 0.0f);
        stepParam = maxStep(viaParam, static_cast<int>(0.1 * kSampleRate));
        // Confirm that the parameter really takes effect: bypass region (0.7-0.95 s) = input delayed by L; active region (0.3-0.45 s) squashed to the ceiling 0.1
        paramAlignError = 0.0f;
        paramActivePeak = 0.0f;
        for (int i = static_cast<int>(0.7 * kSampleRate); i < static_cast<int>(0.95 * kSampleRate); ++i)
            paramAlignError = std::max(paramAlignError, std::abs(viaParam.getSample(0, i) - input.getSample(0, i - latency)));
        for (int i = static_cast<int>(0.3 * kSampleRate); i < static_cast<int>(0.45 * kSampleRate); ++i)
            paramActivePeak = std::max(paramActivePeak, std::abs(viaParam.getSample(0, i)));
    }

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
    std::printf("plugin Bypass parameter: impulse 1000 -> %d (%.4f), reported latency %d; max jump while toggling %.4f, max difference of the bypass region from \"input delayed by L\" %.3g, active-region peak %.4f\n",
                bypassImpulseAt, bypassImpulseValue, latency, stepParam, paramAlignError, paramActivePeak);
    dawlab::writeWav(dawlab::outputPath("l13_ex7_crossfade.wav"), smooth, kSampleRate);
    dawlab::writeWav(dawlab::outputPath("l13_ex7_hard.wav"), hard, kSampleRate);

    DAWLAB_CHECK(latency == dawlab::msToSamples(5.0, kSampleRate));
    DAWLAB_CHECK(alignError < 1.0e-6f);
    DAWLAB_CHECK(activePeak <= 0.1f * 1.001f);
    DAWLAB_CHECK(stepSmooth < 0.025f);
    DAWLAB_CHECK(stepHard > 0.05f);
    DAWLAB_CHECK(bypassParam >= 0);
    DAWLAB_CHECK(bypassImpulseAt == 1000 + latency);
    DAWLAB_CHECK(bypassImpulseValue == 0.25f);
    DAWLAB_CHECK(stepParam < 0.025f);
    DAWLAB_CHECK(paramAlignError < 1.0e-6f);
    DAWLAB_CHECK(paramActivePeak <= 0.1f * 1.001f);
    return dawlab::test::summary("l13_ex7_bypass_crossfade");
}
