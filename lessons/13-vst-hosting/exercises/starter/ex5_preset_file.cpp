// 練習 5（起始碼）：把 plugin 的 state 存成 preset 檔案，再載回來
// 完成 savePreset / loadPreset 兩個 TODO；目前它們什麼都不做，所以 self-check 會印出 FAIL（這是預期的）。
//
// 檔案格式（自訂的 XML，副檔名 .dawpreset）：
//
//   <DAWLAB_PRESET version="1" format="VST3" pluginName="DawLabCompressor" uid="…">
//     <PARAMS>                                  ← 給人看、方便 diff；載入時不使用
//       <PARAM index="0" name="Threshold" value="0.4" text="-36.0 dB"/> …
//     </PARAMS>
//     <STATE>base64…</STATE>                    ← HostedPlugin::getState() 的 binary blob
//   </DAWLAB_PRESET>
//
// 重點：
//   - 真正的資料是 plugin 自己的 state blob，host 不解讀它，只負責保存（跟專案檔一樣）；
//   - 用 pluginName + uid 辨識 plugin（不要用 createIdentifierString()：它含有檔案路徑的 hash，
//     plugin 換資料夾 preset 就認不得了）；
//   - 載入時要擋掉「別的 plugin 的 preset」與壞掉的檔案。
//
// 輸出：dawlab_output/l13_ex5_preset.dawpreset

#include "../../examples/L13Common.h"

#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

bool savePreset(dawlab::host::HostedPlugin& plugin, const juce::File& file) {
    // TODO：
    //   1. 取得 plugin.getInstance().getPluginDescription()；
    //   2. 建立 juce::XmlElement root("DAWLAB_PRESET")，設定 version / format / pluginName / uid 屬性
    //      （uid 用 juce::String::toHexString(desc.uniqueId)）；
    //   3. （選做）加一個 <PARAMS> 子元素，列出每個參數的 name / value / text，方便人看；
    //   4. 加 <STATE> 子元素，內容是 plugin.getState().toBase64Encoding()；
    //   5. return root.writeTo(file);
    juce::ignoreUnused(plugin, file);
    return false;
}

bool loadPreset(dawlab::host::HostedPlugin& plugin, const juce::File& file, juce::String& error) {
    // TODO：
    //   1. juce::XmlDocument::parse(file)；不是 DAWLAB_PRESET 就 error = "..." 並 return false；
    //   2. pluginName / uid 必須跟 plugin.getInstance().getPluginDescription() 相同，否則拒絕；
    //   3. 取出 <STATE> 的文字，用 juce::MemoryBlock::fromBase64Encoding 解碼（失敗或長度 0 → 拒絕）；
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

    // 1. 調一組「preset」並記下參數與聲音
    l13::setParam(*comp, "Threshold", l13::toNormalized(-32.0f, -60.0f, 0.0f));
    l13::setParam(*comp, "Ratio", l13::toNormalized(6.0f, 1.0f, 20.0f, 0.5f));
    l13::setParam(*comp, "Makeup", l13::toNormalized(6.0f, -12.0f, 24.0f));
    l13::setParam(*comp, "Mix", l13::toNormalized(60.0f, 0.0f, 100.0f));
    std::vector<float> saved;
    for (int i = 0; i < comp->getNumParameters(); ++i)
        saved.push_back(comp->getParameterValue(i));
    const auto reference = render(*comp, input);

    // 2. 存檔
    const juce::File file = juce::File::getCurrentWorkingDirectory().getChildFile(
        juce::String::fromUTF8(dawlab::outputPath("l13_ex5_preset.dawpreset").c_str()));
    const bool savedOk = savePreset(*comp, file);
    std::printf("preset → %s（%lld bytes）\n", file.getFullPathName().toRawUTF8(), static_cast<long long>(file.getSize()));

    // 3. 亂改參數，再從檔案載回
    for (int i = 0; i < comp->getNumParameters(); ++i)
        comp->setParameterValue(i, 0.123f);
    juce::String error;
    const bool loadedOk = loadPreset(*comp, file, error);
    float maxParamError = 0.0f;
    for (int i = 0; i < comp->getNumParameters(); ++i)
        maxParamError = std::max(maxParamError, std::abs(comp->getParameterValue(i) - saved[static_cast<size_t>(i)]));
    const float diff = l13::maxAbsDifference(reference, render(*comp, input));
    std::printf("載回後：參數最大誤差 %.2g，聲音最大差異 %.2g\n", maxParamError, diff);

    // 4. 錯誤處理：別的 plugin 的 preset、壞掉的檔案
    juce::String wrongError, corruptError;
    const bool wrongPlugin = loadPreset(*lim, file, wrongError);
    const juce::File corrupt = file.getSiblingFile("l13_ex5_corrupt.dawpreset");
    {
        // 外層資訊都正確、只有 STATE 壞掉（不是合法的 base64）
        juce::XmlElement bad("DAWLAB_PRESET");
        const auto desc = comp->getInstance().getPluginDescription();
        bad.setAttribute("pluginName", desc.name);
        bad.setAttribute("uid", juce::String::toHexString(desc.uniqueId));
        bad.createNewChildElement("STATE")->addTextElement("@@@ not base64 @@@");
        bad.writeTo(corrupt);
    }
    const bool corruptLoaded = loadPreset(*comp, corrupt, corruptError);
    std::printf("載入到 DawLabLimiter：%s（%s）\n", wrongPlugin ? "成功？！" : "被拒絕", wrongError.toRawUTF8());
    std::printf("載入壞掉的檔案：%s（%s）\n", corruptLoaded ? "成功？！" : "被拒絕", corruptError.toRawUTF8());

    DAWLAB_CHECK(savedOk && file.getSize() > 0);
    DAWLAB_CHECK(loadedOk);
    DAWLAB_CHECK(maxParamError < 1.0e-3f);
    DAWLAB_CHECK(diff < 1.0e-6f);
    DAWLAB_CHECK(!wrongPlugin);
    DAWLAB_CHECK(!corruptLoaded);
    return dawlab::test::summary("l13_ex5_preset_file");
}
