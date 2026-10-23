// Exercise 5 (starter): save the plugin's state as a preset file, then load it back
// Complete the two TODOs savePreset / loadPreset; currently they do nothing, so the self-check prints FAIL (this is expected).
//
// File format (a custom XML, extension .dawpreset):
//
//   <DAWLAB_PRESET version="1" format="VST3" pluginName="DawLabCompressor" uid="...">
//     <PARAMS>                                  <-- for humans, easy to diff; not used when loading
//       <PARAM index="0" name="Threshold" value="0.4" text="-36.0 dB"/> ...
//     </PARAMS>
//     <STATE>base64...</STATE>                  <-- the binary blob from HostedPlugin::getState()
//   </DAWLAB_PRESET>
//
// Key points:
//   - The real data is the plugin's own state blob; the host does not interpret it and only stores it (just like a project file);
//   - Identify the plugin by pluginName + uid (do not use createIdentifierString(): it contains a hash of the file path,
//     so a preset would no longer be recognized once the plugin moves to another folder);
//   - When loading, reject "a preset for a different plugin" and corrupted files.
//
// Output: dawlab_output/l13_ex5_preset.dawpreset

#include "../../examples/L13Common.h"

#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

bool savePreset(dawlab::host::HostedPlugin& plugin, const juce::File& file) {
    // TODO:
    //   1. get plugin.getInstance().getPluginDescription();
    //   2. create juce::XmlElement root("DAWLAB_PRESET") and set the version / format / pluginName / uid attributes
    //      (uid via juce::String::toHexString(desc.uniqueId));
    //   3. (optional) add a <PARAMS> child listing each parameter's name / value / text, for humans to read;
    //   4. add a <STATE> child whose content is plugin.getState().toBase64Encoding();
    //   5. return root.writeTo(file);
    juce::ignoreUnused(plugin, file);
    return false;
}

bool loadPreset(dawlab::host::HostedPlugin& plugin, const juce::File& file, juce::String& error) {
    // TODO:
    //   1. juce::XmlDocument::parse(file); if it is not DAWLAB_PRESET, set error = "..." and return false;
    //   2. pluginName / uid must equal plugin.getInstance().getPluginDescription(), otherwise reject;
    //   3. take the text of <STATE> and decode it with juce::MemoryBlock::fromBase64Encoding (failure or length 0 -> reject);
    //   4. plugin.setState(state); return true;
    juce::ignoreUnused(plugin, file);
    error = "TODO: loadPreset not implemented";
    return false;
}

dawlab::AudioBuffer render(dawlab::host::HostedPlugin& plugin, const dawlab::AudioBuffer& input) {
    plugin.prepare(kSampleRate, kBlockSize, 2);
    auto out = l13::renderOffline(plugin, input, kBlockSize);
    plugin.releaseResources();
    return out;
}

} // namespace

int main() {
    const juce::ScopedJuceInitialiser_GUI juceInit;
    dawlab::host::PluginHost host;
    auto comp = l13::mustLoad(host, l13::defaultCompressorVst3(), kSampleRate, kBlockSize);
    auto lim = l13::mustLoad(host, l13::defaultLimiterVst3(), kSampleRate, kBlockSize);
    if (comp == nullptr || lim == nullptr)
        return 1;

    const dawlab::AudioBuffer input = dawlab::makeDrumLoop(kSampleRate, 2);

    // 1. Dial in a "preset" and record the parameters and the sound
    l13::setParam(*comp, "Threshold", l13::toNormalized(-32.0f, -60.0f, 0.0f));
    l13::setParam(*comp, "Ratio", l13::toNormalized(6.0f, 1.0f, 20.0f, 0.5f));
    l13::setParam(*comp, "Makeup", l13::toNormalized(6.0f, -12.0f, 24.0f));
    l13::setParam(*comp, "Mix", l13::toNormalized(60.0f, 0.0f, 100.0f));
    std::vector<float> saved;
    for (int i = 0; i < comp->getNumParameters(); ++i)
        saved.push_back(comp->getParameterValue(i));
    const auto reference = render(*comp, input);

    // 2. Save to a file
    const juce::File file = juce::File::getCurrentWorkingDirectory().getChildFile(
        juce::String::fromUTF8(dawlab::outputPath("l13_ex5_preset.dawpreset").c_str()));
    const bool savedOk = savePreset(*comp, file);
    std::printf("preset -> %s (%lld bytes)\n", file.getFullPathName().toRawUTF8(), static_cast<long long>(file.getSize()));

    // 3. Scramble the parameters, then load back from the file
    for (int i = 0; i < comp->getNumParameters(); ++i)
        comp->setParameterValue(i, 0.123f);
    juce::String error;
    const bool loadedOk = loadPreset(*comp, file, error);
    float maxParamError = 0.0f;
    for (int i = 0; i < comp->getNumParameters(); ++i)
        maxParamError = std::max(maxParamError, std::abs(comp->getParameterValue(i) - saved[static_cast<size_t>(i)]));
    const float diff = l13::maxAbsDifference(reference, render(*comp, input));
    std::printf("After loading back: max parameter error %.2g, max sound difference %.2g\n", maxParamError, diff);

    // 4. Error handling: a preset for another plugin, a corrupted file
    juce::String wrongError, corruptError;
    const bool wrongPlugin = loadPreset(*lim, file, wrongError);
    const juce::File corrupt = file.getSiblingFile("l13_ex5_corrupt.dawpreset");
    {
        // The outer information is all correct and only STATE is broken (not valid base64)
        juce::XmlElement bad("DAWLAB_PRESET");
        const auto desc = comp->getInstance().getPluginDescription();
        bad.setAttribute("pluginName", desc.name);
        bad.setAttribute("uid", juce::String::toHexString(desc.uniqueId));
        bad.createNewChildElement("STATE")->addTextElement("@@@ not base64 @@@");
        bad.writeTo(corrupt);
    }
    const bool corruptLoaded = loadPreset(*comp, corrupt, corruptError);
    std::printf("Load into DawLabLimiter: %s (%s)\n", wrongPlugin ? "succeeded?!" : "rejected", wrongError.toRawUTF8());
    std::printf("Load the corrupted file: %s (%s)\n", corruptLoaded ? "succeeded?!" : "rejected", corruptError.toRawUTF8());

    DAWLAB_CHECK(savedOk && file.getSize() > 0);
    DAWLAB_CHECK(loadedOk);
    DAWLAB_CHECK(maxParamError < 1.0e-3f);
    DAWLAB_CHECK(diff < 1.0e-6f);
    DAWLAB_CHECK(!wrongPlugin);
    DAWLAB_CHECK(!corruptLoaded);
    return dawlab::test::summary("l13_ex5_preset_file");
}
