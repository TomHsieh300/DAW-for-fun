// l13_scan_plugins: scans VST3 plugins and lists their information (PluginHost::scan + the KnownPluginList cache)
//
// Demonstrates:
//   1. The difference between only "finding files" (no DLL is loaded) and really "scanning" (loading the DLL and reading the factory);
//   2. The KnownPluginList cache: scanning the same file a second time uses the cache and does not load it again;
//   3. Saving the scan result as XML (so a DAW need not rescan at its next startup).
//
// Run:
//   l13_scan_plugins                 scans the plugin folder built by this chapter; the system VST3 folders only have their file names listed, nothing is loaded
//   l13_scan_plugins <folder or .vst3>   scans the given location
//   l13_scan_plugins --system        really scans the system default VST3 folders (in-process: a broken plugin can crash this program)
//
// Output: dawlab_output/l13_known_plugins.xml (the KnownPluginList cache file)
// This program makes no sound; the point is the table it prints.

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
    // Creating and querying VST3 plugins must be done on the message thread:
    // a console program uses ScopedJuceInitialiser_GUI to create the MessageManager, which makes the main thread the message thread.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    dawlab::host::PluginHost host;

    const juce::String arg = argc > 1 ? juce::String::fromUTF8(argv[1]) : juce::String();
    const bool scanSystem = arg == "--system";

    // ---- 1. System default VST3 folders -------------------------------------------------
    const auto defaults = host.getDefaultSearchPaths();
    std::printf("== System default VST3 search paths ==\n");
    for (int i = 0; i < defaults.getNumPaths(); ++i) {
        const juce::File dir = defaults[i];
        const auto files = host.findPluginFiles(dir, true); // file names only, nothing is loaded
        std::printf("  %s  (%s, %d .vst3)\n", dir.getFullPathName().toRawUTF8(),
                    dir.isDirectory() ? "exists" : "does not exist", files.size());
        for (const auto& f : files)
            std::printf("     - %s\n", f.toRawUTF8());
    }

    // ---- 2. Decide where to really scan -------------------------------------------------
    juce::Array<juce::File> targets;
    if (scanSystem) {
        for (int i = 0; i < defaults.getNumPaths(); ++i)
            targets.add(defaults[i]);
    } else if (arg.isNotEmpty()) {
        targets.add(juce::File(arg));
    } else {
        // This chapter's plugin folder: .../plugin/DawLabCompressor_artefacts/Debug/VST3/DawLabCompressor.vst3 up 4 levels
        targets.add(l13::defaultCompressorVst3().getParentDirectory().getParentDirectory().getParentDirectory()
                        .getParentDirectory());
    }

    std::printf("\n== Scan (load every .vst3 and read the factory information) ==\n");
    juce::Array<juce::PluginDescription> all;
    double firstScanMs = 0.0;
    for (const auto& t : targets) {
        std::printf("Scanning: %s\n", t.getFullPathName().toRawUTF8());
        const auto t0 = std::chrono::steady_clock::now();
        const auto found = host.scan(t, true);
        firstScanMs += millisecondsSince(t0);
        all.addArray(found);
        for (const auto& failed : host.getLastScanFailures())
            std::printf("  [failed] %s\n", failed.toRawUTF8());
    }
    printDescriptions(all);
    std::printf("(ins / outs = 0 means \"not known yet\": the information comes from moduleinfo.json in the bundle, no DLL was loaded)\n");
    std::printf("%d plugin(s) in total; the first scan took %.1f ms\n", all.size(), firstScanMs);

    // ---- 3. Second scan: the KnownPluginList cache hits, no DLL is loaded again -----------
    const auto t1 = std::chrono::steady_clock::now();
    int again = 0;
    for (const auto& t : targets)
        again += host.scan(t, true).size();
    const double secondScanMs = millisecondsSince(t1);
    std::printf("Second scan (cached): %d plugin(s), %.1f ms\n", again, secondScanMs);

    // ---- 4. Save as an XML cache file and read it back into another PluginHost -------------
    const juce::File cacheFile(juce::File::getCurrentWorkingDirectory().getChildFile(
        juce::String::fromUTF8(dawlab::outputPath("l13_known_plugins.xml").c_str())));
    const bool saved = host.saveKnownPluginList(cacheFile);
    dawlab::host::PluginHost host2;
    const bool loaded = host2.loadKnownPluginList(cacheFile);
    std::printf("KnownPluginList -> %s (%s), %d plugin(s) after reading it back\n", cacheFile.getFullPathName().toRawUTF8(),
                saved ? "saved" : "save failed", host2.getKnownPluginList().getNumTypes());

    // ---- 5. Really instantiate: look at the actual I/O ---------------------------------
    // A VST3 with moduleinfo.json (a format since VST3 SDK 3.7.5; JUCE generates it automatically after the build) lets us read the name, vendor, category and uid
    // without loading the DLL, which is why the scan above is fast; but the channel counts are only known after instantiating (the 0s in the table above).
    if (arg.isNotEmpty())
        return 0; // user-specified folder: do not automatically instantiate other people's plugins

    std::printf("\n== Instantiate this chapter's plugins and read the actual bus configuration ==\n");
    int stereoPlugins = 0;
    bool foundComp = false, foundLim = false;
    for (const auto& d : all) {
        juce::String error;
        auto instance = host.createInstance(d, 48000.0, 512, error);
        if (instance == nullptr) {
            std::printf("  %s: %s\n", d.name.toRawUTF8(), error.toRawUTF8());
            continue;
        }
        const auto full = instance->getPluginDescription();
        std::printf("  %-18s ins %d  outs %d  latency %d  editor %s\n", full.name.toRawUTF8(), full.numInputChannels,
                    full.numOutputChannels, instance->getLatencySamples(), instance->hasEditor() ? "yes" : "no");
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
