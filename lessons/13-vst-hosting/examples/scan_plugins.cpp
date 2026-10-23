// l13_scan_plugins：掃描 VST3 plugin 並列出資訊（PluginHost::scan + KnownPluginList 快取）
//
// 示範：
//   1. 只「找檔案」（不載入 DLL）和真正「掃描」（載入 DLL、讀 factory）的差別；
//   2. KnownPluginList 快取：同一個檔案第二次掃描直接用快取，不再載入；
//   3. 把掃描結果存成 XML（DAW 下次啟動就不必重掃）。
//
// 執行：
//   l13_scan_plugins                 掃描本章 build 出來的 plugin 資料夾；系統 VST3 資料夾只列檔名、不載入
//   l13_scan_plugins <資料夾或.vst3>   掃描指定位置
//   l13_scan_plugins --system        真的掃描系統預設 VST3 資料夾（in-process：壞掉的 plugin 可能讓本程式 crash）
//
// 輸出：dawlab_output/l13_known_plugins.xml（KnownPluginList 的快取檔）
// 這支程式沒有聲音；重點是印出來的表格。

#include "L13Common.h"

#include <chrono>

namespace {

void printDescriptions(const juce::Array<juce::PluginDescription>& found) {
    std::printf("  %-20s %-10s %-18s %-8s %5s %5s  %s\n", "name", "format", "category", "version", "ins", "outs",
                "uid");
    for (const auto& d : found)
        std::printf("  %-20s %-10s %-18s %-8s %5d %5d  %08x\n", d.name.toRawUTF8(), d.pluginFormatName.toRawUTF8(),
                    d.category.toRawUTF8(), d.version.toRawUTF8(), d.numInputChannels, d.numOutputChannels,
                    static_cast<unsigned>(d.uniqueId));
    for (const auto& d : found)
        std::printf("    %s\n      identifier: %s\n", d.fileOrIdentifier.toRawUTF8(),
                    d.createIdentifierString().toRawUTF8());
}

double millisecondsSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

} // namespace

int main(int argc, char* argv[]) {
    // VST3 plugin 的建立、查詢都要在 message thread 上做：
    // console 程式用 ScopedJuceInitialiser_GUI 建立 MessageManager，main thread 就成為 message thread。
    const juce::ScopedJuceInitialiser_GUI juceInit;
    dawlab::host::PluginHost host;

    const juce::String arg = argc > 1 ? juce::String::fromUTF8(argv[1]) : juce::String();
    const bool scanSystem = arg == "--system";

    // ---- 1. 系統預設 VST3 資料夾 -------------------------------------------------------
    const auto defaults = host.getDefaultSearchPaths();
    std::printf("== 系統預設的 VST3 搜尋路徑 ==\n");
    for (int i = 0; i < defaults.getNumPaths(); ++i) {
        const juce::File dir = defaults[i];
        const auto files = host.findPluginFiles(dir, true); // 只看檔名，不載入
        std::printf("  %s  （%s，%d 個 .vst3）\n", dir.getFullPathName().toRawUTF8(),
                    dir.isDirectory() ? "存在" : "不存在", files.size());
        for (const auto& f : files)
            std::printf("     - %s\n", f.toRawUTF8());
    }

    // ---- 2. 決定要真正掃描哪裡 ----------------------------------------------------------
    juce::Array<juce::File> targets;
    if (scanSystem) {
        for (int i = 0; i < defaults.getNumPaths(); ++i)
            targets.add(defaults[i]);
    } else if (arg.isNotEmpty()) {
        targets.add(juce::File(arg));
    } else {
        // 本章的 plugin 資料夾：.../plugin/DawLabCompressor_artefacts/Debug/VST3/DawLabCompressor.vst3 往上 4 層
        targets.add(l13::defaultCompressorVst3().getParentDirectory().getParentDirectory().getParentDirectory()
                        .getParentDirectory());
    }

    std::printf("\n== 掃描（載入每個 .vst3、讀出 factory 資訊）==\n");
    juce::Array<juce::PluginDescription> all;
    double firstScanMs = 0.0;
    for (const auto& t : targets) {
        std::printf("掃描：%s\n", t.getFullPathName().toRawUTF8());
        const auto t0 = std::chrono::steady_clock::now();
        const auto found = host.scan(t, true);
        firstScanMs += millisecondsSince(t0);
        all.addArray(found);
        for (const auto& failed : host.getLastScanFailures())
            std::printf("  [失敗] %s\n", failed.toRawUTF8());
    }
    printDescriptions(all);
    std::printf("（ins / outs = 0 代表「還不知道」：資訊來自 bundle 裡的 moduleinfo.json，沒有載入 DLL）\n");
    std::printf("共 %d 個 plugin，第一次掃描花了 %.1f ms\n", all.size(), firstScanMs);

    // ---- 3. 第二次掃描：KnownPluginList 快取命中，不再載入 DLL ------------------------------
    const auto t1 = std::chrono::steady_clock::now();
    int again = 0;
    for (const auto& t : targets)
        again += host.scan(t, true).size();
    const double secondScanMs = millisecondsSince(t1);
    std::printf("第二次掃描（快取）：%d 個 plugin，%.1f ms\n", again, secondScanMs);

    // ---- 4. 存成 XML 快取檔，再讀回另一個 PluginHost ---------------------------------------
    const juce::File cacheFile(juce::File::getCurrentWorkingDirectory().getChildFile(
        juce::String::fromUTF8(dawlab::outputPath("l13_known_plugins.xml").c_str())));
    const bool saved = host.saveKnownPluginList(cacheFile);
    dawlab::host::PluginHost host2;
    const bool loaded = host2.loadKnownPluginList(cacheFile);
    std::printf("KnownPluginList → %s（%s），讀回後有 %d 個 plugin\n", cacheFile.getFullPathName().toRawUTF8(),
                saved ? "已儲存" : "儲存失敗", host2.getKnownPluginList().getNumTypes());

    // ---- 5. 真的 instantiate：看實際的 I/O ---------------------------------------------
    // 有 moduleinfo.json 的 VST3（VST3 SDK 3.7.5 起的格式，JUCE 會在 build 後自動產生）可以「不載入 DLL」
    // 就讀到名稱、廠商、類別、uid，所以上面的掃描很快；但 channel 數要等 instantiate 之後才知道（上表的 0）。
    if (arg.isNotEmpty())
        return 0; // 使用者指定的資料夾：不自動 instantiate 別人的 plugin

    std::printf("\n== instantiate 本章的 plugin，讀出實際的 bus 設定 ==\n");
    int stereoPlugins = 0;
    bool foundComp = false, foundLim = false;
    for (const auto& d : all) {
        juce::String error;
        auto instance = host.createInstance(d, 48000.0, 512, error);
        if (instance == nullptr) {
            std::printf("  %s：%s\n", d.name.toRawUTF8(), error.toRawUTF8());
            continue;
        }
        const auto full = instance->getPluginDescription();
        std::printf("  %-18s ins %d  outs %d  latency %d  editor %s\n", full.name.toRawUTF8(), full.numInputChannels,
                    full.numOutputChannels, instance->getLatencySamples(), instance->hasEditor() ? "有" : "沒有");
        if (full.numInputChannels == 2 && full.numOutputChannels == 2)
            ++stereoPlugins;
        foundComp |= d.name == "DawLabCompressor" && d.pluginFormatName == "VST3";
        foundLim |= d.name == "DawLabLimiter" && d.pluginFormatName == "VST3";
    }

    DAWLAB_CHECK(foundComp);
    DAWLAB_CHECK(foundLim);
    DAWLAB_CHECK(stereoPlugins == all.size());
    DAWLAB_CHECK(again == all.size());
    DAWLAB_CHECK(saved && loaded);
    DAWLAB_CHECK(host2.getKnownPluginList().getNumTypes() == all.size());
    return dawlab::test::summary("l13_scan_plugins");
}
