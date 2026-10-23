// l13_offline_host：最小的「離線 host」—— 載入 VST3、列出參數、設定參數、處理 drum loop、輸出 WAV
//
// 示範 plugin 生命週期的每一步（由 host 這一側來看）：
//   scan / instantiate → prepareToPlay（bus 協商、latency）→ processBlock × N → releaseResources
// 以及 normalized parameter：host 只送 0..1，plugin 自己換算成 dB / ms。
//
// 執行：
//   l13_offline_host                         載入本章 build 出來的 DawLabCompressor
//   l13_offline_host <plugin.vst3> [in.wav]  載入任意 VST3（不帶 wav 就用合成 drum loop）
//
// 輸出（dawlab_output/）：
//   l13_offline_dry.wav   原始 drum loop
//   l13_offline_wet.wav   經過 plugin 的結果
// 該聽到什麼（預設 DawLabCompressor：threshold −30 dB、ratio 4:1、attack 5 ms、release 80 ms）：
//   wet 整體小聲很多（沒有開 makeup）；每一下鼓的「頭」（前幾 ms）還在，但後面的 body 被壓下去，
//   所以聽起來更短、更「有咬勁」。把音量補回來比較（或用 Makeup 參數）會更明顯。
//
// 驗證：
//   1. plugin 回報的參數文字（例如 "-30.0 dB"）= 我們用 normalized 公式算的值 → normalized 換算正確；
//   2. 輸出 peak 至少降 3 dB、RMS 至少降 6 dB → 真的有壓縮；crest factor（peak − RMS）反而變大
//      （attack 5 ms 讓鼓頭先通過，見 README「聽感」一節）；
//   3. 跟「直接呼叫 dawlab::dsp::Compressor」的結果完全相同（差異 = 0，bit-exact）→ plugin 包裝沒有改變 DSP。

#include "L13Common.h"

#include <dawlab/dsp/Compressor.h>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

void printParameters(const dawlab::host::HostedPlugin& plugin) {
    std::printf("  %3s  %-12s %-10s %10s   %s\n", "#", "name", "id", "normalized", "text");
    for (int i = 0; i < plugin.getNumParameters(); ++i)
        std::printf("  %3d  %-12s %-10s %10.4f   %s\n", i, plugin.getParameterName(i).toRawUTF8(),
                    plugin.getParameterId(i).toRawUTF8(), plugin.getParameterValue(i),
                    plugin.getParameterText(i).toRawUTF8());
}

} // namespace

int main(int argc, char* argv[]) {
    const juce::ScopedJuceInitialiser_GUI juceInit; // main thread = message thread
    dawlab::host::PluginHost host;

    const juce::File pluginFile = argc > 1 ? juce::File(juce::String::fromUTF8(argv[1])) : l13::defaultCompressorVst3();
    const bool isDefaultPlugin = argc <= 1;

    // ---- 1. instantiate ------------------------------------------------------------
    std::printf("== 載入 %s ==\n", pluginFile.getFullPathName().toRawUTF8());
    auto plugin = l13::mustLoad(host, pluginFile, kSampleRate, kBlockSize);
    if (plugin == nullptr)
        return 1;

    // ---- 2. prepare（bus layout 協商 + prepareToPlay）--------------------------------
    plugin->prepare(kSampleRate, kBlockSize, 2);
    std::printf("名稱：%s   editor：%s   latency：%d samples\n", plugin->getName().toRawUTF8(),
                plugin->hasEditor() ? "有" : "沒有", plugin->getLatencySamples());
    std::printf("bus layout：%s\n", plugin->describeBusLayout().toRawUTF8());
    std::printf("\n參數（預設值）：\n");
    printParameters(*plugin);

    // ---- 3. 設定參數（host 只能送 0..1）-----------------------------------------------
    // DawLabCompressor 的範圍（見 plugin/compressor/CompressorProcessor.cpp）：
    //   Threshold −60..0 dB（線性）、Ratio 1..20（skew 0.5）、Attack 0.1..200 ms（centre 10 ms）、Release 5..2000 ms（centre 100 ms）
    const float thresholdDb = -30.0f, ratio = 4.0f, attackMs = 5.0f, releaseMs = 80.0f;
    const float nThreshold = l13::toNormalized(thresholdDb, -60.0f, 0.0f);
    const float nRatio = l13::toNormalized(ratio, 1.0f, 20.0f, 0.5f);
    const float nAttack = l13::toNormalized(attackMs, 0.1f, 200.0f, l13::skewForCentre(0.1f, 200.0f, 10.0f));
    const float nRelease = l13::toNormalized(releaseMs, 5.0f, 2000.0f, l13::skewForCentre(5.0f, 2000.0f, 100.0f));

    bool paramsOk = true;
    if (plugin->findParameter("Threshold") >= 0) {
        paramsOk &= l13::setParam(*plugin, "Threshold", nThreshold);
        paramsOk &= l13::setParam(*plugin, "Ratio", nRatio);
        paramsOk &= l13::setParam(*plugin, "Attack", nAttack);
        paramsOk &= l13::setParam(*plugin, "Release", nRelease);
        std::printf("\n設定：Threshold %.1f dB → %.4f，Ratio %.1f → %.4f，Attack %.1f ms → %.4f，Release %.1f ms → %.4f\n",
                    thresholdDb, nThreshold, ratio, nRatio, attackMs, nAttack, releaseMs, nRelease);
        printParameters(*plugin);
    } else {
        std::printf("\n（這個 plugin 沒有 Threshold 參數，維持預設值處理）\n");
    }

    // ---- 4. 準備輸入：drum loop 或使用者的 WAV ------------------------------------------
    dawlab::AudioBuffer dry = dawlab::makeDrumLoop(kSampleRate, 4);
    double sampleRate = kSampleRate;
    if (argc > 2) {
        dawlab::WavData wav;
        std::string err;
        if (dawlab::readWav(argv[2], wav, &err) && std::abs(wav.sampleRate - kSampleRate) < 1.0) {
            dry = wav.buffer;
            sampleRate = wav.sampleRate;
        } else {
            std::printf("讀不到 %s（或 sample rate 不是 48000），改用 drum loop\n", argv[2]);
        }
    }

    // ---- 5. process：一塊一塊送進 plugin（模擬 audio callback）----------------------------
    const dawlab::AudioBuffer wet = l13::renderOffline(*plugin, dry, kBlockSize);
    plugin->releaseResources();

    dawlab::writeWav(dawlab::outputPath("l13_offline_dry.wav"), dry, sampleRate);
    dawlab::writeWav(dawlab::outputPath("l13_offline_wet.wav"), wet, sampleRate);

    const float dryPeak = l13::peakDb(dry), wetPeak = l13::peakDb(wet);
    const float dryRms = l13::rmsDb(dry), wetRms = l13::rmsDb(wet);
    std::printf("\n%-8s %10s %10s %14s\n", "", "peak dBFS", "RMS dBFS", "crest (dB)");
    std::printf("%-8s %10.2f %10.2f %14.2f\n", "dry", dryPeak, dryRms, dryPeak - dryRms);
    std::printf("%-8s %10.2f %10.2f %14.2f\n", "wet", wetPeak, wetRms, wetPeak - wetRms);
    std::printf("%-8s %10.2f %10.2f %14.2f\n", "差", wetPeak - dryPeak, wetRms - dryRms,
                (wetPeak - wetRms) - (dryPeak - dryRms));
    std::printf("輸出：dawlab_output/l13_offline_dry.wav、l13_offline_wet.wav\n");

    if (!isDefaultPlugin)
        return 0;

    // ---- self-check（預設 DawLabCompressor）-------------------------------------------
    // (1) normalized 換算：plugin 自己顯示的值 = 我們想設的值
    DAWLAB_CHECK(paramsOk);
    DAWLAB_CHECK_NEAR(plugin->getParameterText(plugin->findParameter("Threshold")).getFloatValue(), thresholdDb, 0.051);
    DAWLAB_CHECK_NEAR(plugin->getParameterText(plugin->findParameter("Ratio")).getFloatValue(), ratio, 0.006);
    DAWLAB_CHECK_NEAR(plugin->getParameterText(plugin->findParameter("Attack")).getFloatValue(), attackMs, 0.006);
    DAWLAB_CHECK_NEAR(plugin->getParameterText(plugin->findParameter("Release")).getFloatValue(), releaseMs, 0.051);
    // (2) 真的有壓縮
    DAWLAB_CHECK(!dawlab::hasNanOrInf(wet));
    DAWLAB_CHECK(wetPeak < dryPeak - 3.0f);
    DAWLAB_CHECK(wetRms < dryRms - 6.0f);
    // 注意：crest factor 反而「變大」—— attack 5 ms 讓每個鼓頭的前幾 ms 先通過，之後的 body 才被壓，
    // 所以 RMS 掉得比 peak 多。這正是中等 attack 的 compressor 讓鼓聽起來更「有衝擊力（punch）」的原因。
    DAWLAB_CHECK((wetPeak - wetRms) > (dryPeak - dryRms));

    // (3) 跟直接呼叫 dsp::Compressor 比較（同樣參數、同樣 block size）
    dawlab::dsp::Compressor reference;
    reference.prepare(kSampleRate, kBlockSize, 2);
    reference.setThresholdDb(thresholdDb);
    reference.setRatio(ratio);
    reference.setKneeDb(6.0f); // plugin 預設值
    reference.setAttackMs(attackMs);
    reference.setReleaseMs(releaseMs);
    reference.setMakeupGainDb(0.0f);
    reference.reset();
    dawlab::AudioBuffer expected = dry;
    dawlab::AudioBlock all(expected);
    for (int start = 0; start < expected.getNumSamples(); start += kBlockSize)
        reference.process(all.getSubBlock(start, std::min(kBlockSize, expected.getNumSamples() - start)));
    const float diff = l13::maxAbsDifference(wet, expected);
    std::printf("plugin 與直接呼叫 dsp::Compressor 的最大差異：%.3g（%.1f dB）\n", diff, dawlab::gainToDb(diff));
    DAWLAB_CHECK(diff == 0.0f); // bit-exact：mix 100 % 與 makeup 0 dB 時 plugin 完全不碰 compressor 的輸出

    return dawlab::test::summary("l13_offline_host");
}
