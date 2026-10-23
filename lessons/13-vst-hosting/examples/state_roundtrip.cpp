// l13_state_roundtrip: saving and restoring plugin state (getState / setState)
//
// When a DAW saves a project it does not read the parameters one by one; it asks the plugin to pack "all of its state" into one binary blob
// (VST3: IComponent::getState + IEditController::getState), stores that in the project file, and hands it back unchanged when the project is opened.
// This program runs a complete round-trip test:
//
//   1. Set parameter set A -> save state -> render (output A)
//   2. Change to parameter set B -> render (output B, different from A)
//   3. setState(A) -> check that every parameter is back to A -> render (must be identical to output A)
//   4. Open a brand-new plugin instance -> setState(A) -> render (must also be identical to output A)
//
// Run: l13_state_roundtrip            (no arguments; uses the DawLabCompressor built by this chapter)
// Output (dawlab_output/): l13_state_A.wav, l13_state_B.wav, l13_state_restored.wav
// What to listen for: A (threshold -36 dB, ratio 8:1, makeup +8 dB, mix 70 %) is clearly parallel compression;
//       B is the defaults (-20 dB, 4:1, mix 100 %); restored sounds (and, numerically, is) exactly the same as A.

#include "L13Common.h"

#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

struct Setting {
    const char* name;
    float normalized;
};

// Always re-prepare before rendering: so the DSP's internal state (envelope, smoothers) starts from the same point,
// which makes the two renders comparable sample by sample.
dawlab::AudioBuffer render(dawlab::host::HostedPlugin& plugin, const dawlab::AudioBuffer& input) {
    plugin.prepare(kSampleRate, kBlockSize, 2);
    auto out = l13::renderOffline(plugin, input, kBlockSize);
    plugin.releaseResources();
    return out;
}

void printState(const juce::MemoryBlock& state) {
    std::printf("  state size: %zu bytes\n", state.getSize());
    // JUCE's VST3 hosting wraps the component state and the controller state in XML (the copyXmlToBinary format),
    // and our plugin's own XML (the APVTS ValueTree) is hidden inside as base64: two layers of wrapping.
    if (auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(), static_cast<int>(state.getSize()))) {
        auto text = xml->toString(juce::XmlElement::TextFormat().singleLine());
        if (text.length() > 300)
            text = text.substring(0, 300) + "...";
        std::printf("  outer layer (the host's wrapper): %s\n", text.toRawUTF8());
    }
}

} // namespace

int main() {
    const juce::ScopedJuceInitialiser_GUI juceInit;
    dawlab::host::PluginHost host;
    auto plugin = l13::mustLoad(host, l13::defaultCompressorVst3(), kSampleRate, kBlockSize);
    if (plugin == nullptr)
        return 1;

    const dawlab::AudioBuffer input = dawlab::makeDrumLoop(kSampleRate, 2);

    // Parameter set A (normalized values; the comments give the real values)
    const std::vector<Setting> settingA = {
        {"Threshold", l13::toNormalized(-36.0f, -60.0f, 0.0f)},      // -36 dB
        {"Ratio", l13::toNormalized(8.0f, 1.0f, 20.0f, 0.5f)},       // 8:1
        {"Knee", l13::toNormalized(3.0f, 0.0f, 24.0f)},              // 3 dB
        {"Attack", 0.4f},                                            // ~ 3.86 ms (see the formula in exercise 1)
        {"Release", 0.6f},                                           // ~ 216.6 ms
        {"Makeup", l13::toNormalized(8.0f, -12.0f, 24.0f)},          // +8 dB
        {"Mix", l13::toNormalized(70.0f, 0.0f, 100.0f)},             // 70 %
    };

    // ---- 1. A: set -> save state -> render ------------------------------------------
    for (const auto& s : settingA)
        l13::setParam(*plugin, s.name, s.normalized);
    std::printf("== Parameter set A ==\n");
    for (const auto& s : settingA)
        std::printf("  %-10s %.4f  (%s)\n", s.name, l13::getParam(*plugin, s.name),
                    plugin->getParameterText(plugin->findParameter(s.name)).toRawUTF8());
    const juce::MemoryBlock stateA = plugin->getState();
    printState(stateA);
    const dawlab::AudioBuffer outA = render(*plugin, input);

    // ---- 2. B: back to the defaults (each parameter's default) -> render -------------------
    for (int i = 0; i < plugin->getNumParameters(); ++i)
        plugin->setParameterValue(i, plugin->getInstance().getParameters()[i]->getDefaultValue());
    std::printf("\n== Parameter set B (defaults) ==\n");
    for (const auto& s : settingA)
        std::printf("  %-10s %.4f  (%s)\n", s.name, l13::getParam(*plugin, s.name),
                    plugin->getParameterText(plugin->findParameter(s.name)).toRawUTF8());
    const dawlab::AudioBuffer outB = render(*plugin, input);
    const float diffAB = l13::maxAbsDifference(outA, outB);
    std::printf("Max difference between A and B: %.4f\n", diffAB);

    // ---- 3. setState(A) -> are the parameters back? -> render ----------------------------
    plugin->setState(stateA);
    std::printf("\n== After setState(A) ==\n");
    float maxParamError = 0.0f;
    for (const auto& s : settingA) {
        const float v = l13::getParam(*plugin, s.name);
        // The plugin snaps values to the interval (e.g. 0.1 dB), so a normalized error of 1e-3 is tolerated
        maxParamError = std::max(maxParamError, std::abs(v - s.normalized));
        std::printf("  %-10s %.4f  (%s)\n", s.name, v, plugin->getParameterText(plugin->findParameter(s.name)).toRawUTF8());
    }
    const dawlab::AudioBuffer outRestored = render(*plugin, input);
    const float diffRestored = l13::maxAbsDifference(outA, outRestored);
    std::printf("Max difference between A and restored: %.3g\n", diffRestored);

    // ---- 4. Brand-new instance + setState(A) -------------------------------------------
    auto fresh = l13::mustLoad(host, l13::defaultCompressorVst3(), kSampleRate, kBlockSize);
    if (fresh == nullptr)
        return 1;
    fresh->setState(stateA);
    const dawlab::AudioBuffer outFresh = render(*fresh, input);
    const float diffFresh = l13::maxAbsDifference(outA, outFresh);
    std::printf("Max difference between A and \"new instance + setState(A)\": %.3g\n", diffFresh);

    dawlab::writeWav(dawlab::outputPath("l13_state_A.wav"), outA, kSampleRate);
    dawlab::writeWav(dawlab::outputPath("l13_state_B.wav"), outB, kSampleRate);
    dawlab::writeWav(dawlab::outputPath("l13_state_restored.wav"), outRestored, kSampleRate);
    std::printf("Output: dawlab_output/l13_state_A.wav, l13_state_B.wav, l13_state_restored.wav\n");

    // normalized <-> real value (the answer to exercise 1): verified with the plugin's own display text
    auto textOf = [&](const char* name, float normalized) {
        return plugin->getParameterText(plugin->findParameter(name), normalized).getFloatValue();
    };
    std::printf("\nExercise 1 check: Threshold 0.6 -> %.1f dB, Ratio 0.60698 -> %.2f, Attack 0.4 -> %.2f ms, "
                "Release 0.6 -> %.1f ms, Release 0.25 -> %.1f ms\n",
                textOf("Threshold", 0.6f), textOf("Ratio", 0.60698f), textOf("Attack", 0.4f), textOf("Release", 0.6f),
                textOf("Release", 0.25f));
    DAWLAB_CHECK_NEAR(textOf("Threshold", 0.6f), -24.0, 0.051);
    DAWLAB_CHECK_NEAR(textOf("Ratio", 0.60698f), 8.0, 0.006);
    DAWLAB_CHECK_NEAR(textOf("Attack", 0.4f), 3.86, 0.006);
    DAWLAB_CHECK_NEAR(textOf("Release", 0.6f), 216.6, 0.051);
    DAWLAB_CHECK_NEAR(textOf("Release", 0.25f), 9.5, 0.051);

    DAWLAB_CHECK(stateA.getSize() > 0);
    DAWLAB_CHECK(diffAB > 0.01f);               // A and B really differ (otherwise this test is meaningless)
    DAWLAB_CHECK(maxParamError < 1.0e-3f);      // the parameters are back
    DAWLAB_CHECK(diffRestored < 1.0e-6f);       // the sound is identical too
    DAWLAB_CHECK(diffFresh < 1.0e-6f);          // reopening a project file = new instance + setState
    return dawlab::test::summary("l13_state_roundtrip");
}
