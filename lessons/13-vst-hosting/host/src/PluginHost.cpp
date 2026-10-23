// PluginHost：掃描 .vst3、維護 KnownPluginList、建立 plugin instance。
// 所有函式都在 message thread 呼叫（見 PluginHost.h 的 thread 規則）。

#include <dawlab/host/PluginHost.h>

namespace dawlab::host {

PluginHost::PluginHost() {
    // juce::VST3PluginFormat（在 juce_audio_processors）= 有 editor 支援的版本；
    // juce::VST3PluginFormatHeadless 載入的 plugin 永遠 hasEditor() == false。
    // 我們要開 plugin 的視窗，所以用前者。只註冊 VST3：AU 只在 macOS、VST2 需要另外的 SDK。
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
    // searchPathsForPlugins 只看副檔名與資料夾結構，不會載入 DLL
    return format->searchPathsForPlugins(juce::FileSearchPath(folder.getFullPathName()), recursive, false);
}

juce::Array<juce::PluginDescription> PluginHost::scan(const juce::File& fileOrFolder, bool recursive) {
    juce::Array<juce::PluginDescription> result;
    lastFailures_.clear();

    auto* format = findVst3Format();
    if (format == nullptr)
        return result;

    // 1) 決定要載入哪些檔案。注意 Windows 上的 .vst3 通常是一個「bundle 資料夾」
    //    （Foo.vst3/Contents/x86_64-win/Foo.vst3），所以要先問 format「這個路徑本身是不是 plugin」，
    //    不是的話才把它當成一般資料夾往下找。
    juce::StringArray candidates;
    const auto path = fileOrFolder.getFullPathName();
    if (format->fileMightContainThisPluginType(path))
        candidates.add(path);
    else if (fileOrFolder.isDirectory())
        candidates = findPluginFiles(fileOrFolder, recursive);

    // 2) 逐一載入、查詢 factory。scanAndAddFile 會：
    //    - 檔案已經在 KnownPluginList 且修改時間沒變 → 直接回傳快取的 description，不再載入 DLL；
    //    - 檔案在 blacklist（之前掃描失敗 / crash 過）→ 跳過；
    //    - 否則呼叫 format.findAllTypesForFile()：真的載入 DLL、建立 factory、讀出每個 class 的資訊。
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
        error = juce::String::fromUTF8("找不到檔案：") + vst3File.getFullPathName();
        return nullptr;
    }
    const auto found = scan(vst3File, false);
    if (found.isEmpty()) {
        error = juce::String::fromUTF8("這個檔案裡沒有可以載入的 VST3 plugin：") + vst3File.getFullPathName();
        return nullptr;
    }
    // 一個 .vst3 可以包含好幾個 plugin（例如 shell plugin），這裡取第一個效果器
    return createInstance(found.getReference(0), sampleRate, blockSize, error);
}

std::unique_ptr<juce::AudioPluginInstance> PluginHost::createInstance(const juce::PluginDescription& desc,
                                                                      double sampleRate, int blockSize,
                                                                      juce::String& error) {
    error.clear();
    // 同步建立：在 message thread 上呼叫時，JUCE 直接在這個 thread 建立 instance。
    // （AUv3 之類必須非同步建立的格式要改用 createPluginInstanceAsync，本章只處理 VST3。）
    auto instance = formatManager_.createPluginInstance(desc, sampleRate, blockSize, error);
    if (instance == nullptr && error.isEmpty())
        error = juce::String::fromUTF8("無法建立 plugin instance：") + desc.name;
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
