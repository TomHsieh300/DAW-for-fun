// PluginHost: scans .vst3 files, maintains the KnownPluginList, creates plugin instances.
// Every function is called on the message thread (see the thread rules in PluginHost.h).

#include <dawlab/host/PluginHost.h>

namespace dawlab::host {

PluginHost::PluginHost() {
    // juce::VST3PluginFormat (in juce_audio_processors) = the version with editor support;
    // plugins loaded through juce::VST3PluginFormatHeadless always have hasEditor() == false.
    // We want to open the plugin's window, so we use the former. Only VST3 is registered: AU exists only on macOS, and VST2 needs a separate SDK.
    formatManager_.addFormat(std::make_unique<juce::VST3PluginFormat>());
}

PluginHost::~PluginHost() = default;

juce::AudioPluginFormatManager& PluginHost::getFormatManager() { return formatManager_; }

juce::KnownPluginList& PluginHost::getKnownPluginList() { return knownPlugins_; }

const juce::StringArray& PluginHost::getLastScanFailures() const { return lastFailures_; }

juce::AudioPluginFormat* PluginHost::findVst3Format() const {
    for (auto* format : formatManager_.getFormats())
        if (format->getName() == juce::VST3PluginFormat::getFormatName())
            return format;
    return nullptr;
}

juce::FileSearchPath PluginHost::getDefaultSearchPaths() {
    if (auto* format = findVst3Format())
        return format->getDefaultLocationsToSearch();
    return {};
}

juce::StringArray PluginHost::findPluginFiles(const juce::File& folder, bool recursive) {
    auto* format = findVst3Format();
    if (format == nullptr || !folder.isDirectory())
        return {};
    // searchPathsForPlugins only looks at file extensions and folder structure; it does not load any DLL
    return format->searchPathsForPlugins(juce::FileSearchPath(folder.getFullPathName()), recursive, false);
}

juce::Array<juce::PluginDescription> PluginHost::scan(const juce::File& fileOrFolder, bool recursive) {
    juce::Array<juce::PluginDescription> result;
    lastFailures_.clear();

    auto* format = findVst3Format();
    if (format == nullptr)
        return result;

    // 1) Decide which files to load. Note that on Windows a .vst3 is usually a "bundle folder"
    //    (Foo.vst3/Contents/x86_64-win/Foo.vst3), so first ask the format "is this path itself a plugin?";
    //    only if not, treat it as an ordinary folder and search downward.
    juce::StringArray candidates;
    const auto path = fileOrFolder.getFullPathName();
    if (format->fileMightContainThisPluginType(path))
        candidates.add(path);
    else if (fileOrFolder.isDirectory())
        candidates = findPluginFiles(fileOrFolder, recursive);

    // 2) Load each one and query its factory. scanAndAddFile will:
    //    - if the file is already in the KnownPluginList with an unchanged modification time -> return the cached description without loading the DLL again;
    //    - if the file is blacklisted (it failed / crashed in an earlier scan) -> skip it;
    //    - otherwise call format.findAllTypesForFile(): really load the DLL, create the factory, and read each class's information.
    for (const auto& candidate : candidates) {
        juce::OwnedArray<juce::PluginDescription> found;
        knownPlugins_.scanAndAddFile(candidate, true, found, *format);
        if (found.isEmpty())
            lastFailures_.add(candidate);
        for (auto* desc : found)
            result.add(*desc);
    }
    knownPlugins_.scanFinished();
    return result;
}

std::unique_ptr<juce::AudioPluginInstance> PluginHost::loadFromFile(const juce::File& vst3File, double sampleRate,
                                                                    int blockSize, juce::String& error) {
    error.clear();
    if (!vst3File.exists()) {
        error = juce::String("File not found: ") + vst3File.getFullPathName();
        return nullptr;
    }
    const auto found = scan(vst3File, false);
    if (found.isEmpty()) {
        error = juce::String("This file contains no loadable VST3 plugin: ") + vst3File.getFullPathName();
        return nullptr;
    }
    // One .vst3 can contain several plugins (e.g. a shell plugin); here we take the first effect
    return createInstance(found.getReference(0), sampleRate, blockSize, error);
}

std::unique_ptr<juce::AudioPluginInstance> PluginHost::createInstance(const juce::PluginDescription& desc,
                                                                      double sampleRate, int blockSize,
                                                                      juce::String& error) {
    error.clear();
    // Synchronous creation: when called on the message thread, JUCE creates the instance directly on this thread.
    // (Formats that must be created asynchronously, such as AUv3, need createPluginInstanceAsync instead; this chapter only handles VST3.)
    auto instance = formatManager_.createPluginInstance(desc, sampleRate, blockSize, error);
    if (instance == nullptr && error.isEmpty())
        error = juce::String("Could not create the plugin instance: ") + desc.name;
    return instance;
}

bool PluginHost::saveKnownPluginList(const juce::File& xmlFile) const {
    if (auto xml = knownPlugins_.createXml())
        return xml->writeTo(xmlFile);
    return false;
}

bool PluginHost::loadKnownPluginList(const juce::File& xmlFile) {
    if (auto xml = juce::XmlDocument::parse(xmlFile)) {
        knownPlugins_.recreateFromXml(*xml);
        return true;
    }
    return false;
}

} // namespace dawlab::host
