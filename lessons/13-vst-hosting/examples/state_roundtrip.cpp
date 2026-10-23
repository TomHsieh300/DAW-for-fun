// l13_state_roundtrip：plugin state 的保存與還原（getState / setState）
//
// DAW 存專案時不會一個一個參數去讀，而是請 plugin 把「全部狀態」打包成一個 binary blob
// （VST3：IComponent::getState + IEditController::getState），存進專案檔；開專案時原封不動送回去。
// 這支程式做完整的來回測試：
//
//   1. 設定參數組 A → 存 state → render（輸出 A）
//   2. 改成參數組 B → render（輸出 B，跟 A 不同）
//   3. setState(A) → 檢查每個參數都回到 A → render（必須跟輸出 A 完全一樣）
//   4. 開一個「全新」的 plugin instance → setState(A) → render（也必須跟輸出 A 一樣）
//
// 執行：l13_state_roundtrip            （不帶參數，使用本章 build 出來的 DawLabCompressor）
// 輸出（dawlab_output/）：l13_state_A.wav、l13_state_B.wav、l13_state_restored.wav
// 聽感：A（threshold −36 dB、ratio 8:1、makeup +8 dB、mix 70 %）是很明顯的 parallel compression；
//       B 是預設值（−20 dB、4:1、mix 100 %）；restored 跟 A 聽起來（也在數字上）完全一樣。

#include "L13Common.h"

#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

struct Setting {
    const char* name;
    float normalized;
};

// render 之前一定重新 prepare：讓 DSP 的內部狀態（envelope、平滑器）從同一個起點開始，
// 這樣兩次 render 才能逐 sample 比較。
dawlab::AudioBuffer render(dawlab::host::HostedPlugin& plugin, const dawlab::AudioBuffer& input) {
    plugin.prepare(kSampleRate, kBlockSize, 2);
    auto out = l13::renderOffline(plugin, input, kBlockSize);
    plugin.releaseResources();
    return out;
}

void printState(const juce::MemoryBlock& state) {
    std::printf("  state 大小：%zu bytes\n", state.getSize());
    // JUCE 的 VST3 hosting 把 component state 與 controller state 用 XML 包起來（copyXmlToBinary 格式），
    // 我們 plugin 自己的 XML（APVTS 的 ValueTree）以 base64 藏在裡面：兩層包裝。
    if (auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(), static_cast<int>(state.getSize()))) {
        auto text = xml->toString(juce::XmlElement::TextFormat().singleLine());
        if (text.length() > 300)
            text = text.substring(0, 300) + "...";
        std::printf("  外層（host 的包裝）：%s\n", text.toRawUTF8());
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

    // 參數組 A（normalized 值；註解是實際值）
    const std::vector<Setting> settingA = {
        {"Threshold", l13::toNormalized(-36.0f, -60.0f, 0.0f)},      // −36 dB
        {"Ratio", l13::toNormalized(8.0f, 1.0f, 20.0f, 0.5f)},       // 8:1
        {"Knee", l13::toNormalized(3.0f, 0.0f, 24.0f)},              // 3 dB
        {"Attack", 0.4f},                                            // ≈ 3.86 ms（見練習 1 的算式）
        {"Release", 0.6f},                                           // ≈ 216.6 ms
        {"Makeup", l13::toNormalized(8.0f, -12.0f, 24.0f)},          // +8 dB
        {"Mix", l13::toNormalized(70.0f, 0.0f, 100.0f)},             // 70 %
    };

    // ---- 1. A：設定 → 存 state → render ---------------------------------------------
    for (const auto& s : settingA)
        l13::setParam(*plugin, s.name, s.normalized);
    std::printf("== 參數組 A ==\n");
    for (const auto& s : settingA)
        std::printf("  %-10s %.4f  (%s)\n", s.name, l13::getParam(*plugin, s.name),
                    plugin->getParameterText(plugin->findParameter(s.name)).toRawUTF8());
    const juce::MemoryBlock stateA = plugin->getState();
    printState(stateA);
    const dawlab::AudioBuffer outA = render(*plugin, input);

    // ---- 2. B：回到預設值（每個參數的 default）→ render -----------------------------------
    for (int i = 0; i < plugin->getNumParameters(); ++i)
        plugin->setParameterValue(i, plugin->getInstance().getParameters()[i]->getDefaultValue());
    std::printf("\n== 參數組 B（預設值）==\n");
    for (const auto& s : settingA)
        std::printf("  %-10s %.4f  (%s)\n", s.name, l13::getParam(*plugin, s.name),
                    plugin->getParameterText(plugin->findParameter(s.name)).toRawUTF8());
    const dawlab::AudioBuffer outB = render(*plugin, input);
    const float diffAB = l13::maxAbsDifference(outA, outB);
    std::printf("A 與 B 的最大差異：%.4f\n", diffAB);

    // ---- 3. setState(A) → 參數回來了嗎？→ render ---------------------------------------
    plugin->setState(stateA);
    std::printf("\n== setState(A) 之後 ==\n");
    float maxParamError = 0.0f;
    for (const auto& s : settingA) {
        const float v = l13::getParam(*plugin, s.name);
        // plugin 會把值 snap 到 interval（例如 0.1 dB），所以容許 1e-3 的 normalized 誤差
        maxParamError = std::max(maxParamError, std::abs(v - s.normalized));
        std::printf("  %-10s %.4f  (%s)\n", s.name, v, plugin->getParameterText(plugin->findParameter(s.name)).toRawUTF8());
    }
    const dawlab::AudioBuffer outRestored = render(*plugin, input);
    const float diffRestored = l13::maxAbsDifference(outA, outRestored);
    std::printf("A 與 restored 的最大差異：%.3g\n", diffRestored);

    // ---- 4. 全新的 instance + setState(A) ----------------------------------------------
    auto fresh = l13::mustLoad(host, l13::defaultCompressorVst3(), kSampleRate, kBlockSize);
    if (fresh == nullptr)
        return 1;
    fresh->setState(stateA);
    const dawlab::AudioBuffer outFresh = render(*fresh, input);
    const float diffFresh = l13::maxAbsDifference(outA, outFresh);
    std::printf("A 與「新 instance + setState(A)」的最大差異：%.3g\n", diffFresh);

    dawlab::writeWav(dawlab::outputPath("l13_state_A.wav"), outA, kSampleRate);
    dawlab::writeWav(dawlab::outputPath("l13_state_B.wav"), outB, kSampleRate);
    dawlab::writeWav(dawlab::outputPath("l13_state_restored.wav"), outRestored, kSampleRate);
    std::printf("輸出：dawlab_output/l13_state_A.wav、l13_state_B.wav、l13_state_restored.wav\n");

    // normalized ↔ 實際值（練習 1 的答案）：用 plugin 自己的顯示文字驗證
    auto textOf = [&](const char* name, float normalized) {
        return plugin->getParameterText(plugin->findParameter(name), normalized).getFloatValue();
    };
    std::printf("\n練習 1 對照：Threshold 0.6 → %.1f dB，Ratio 0.60698 → %.2f，Attack 0.4 → %.2f ms，"
                "Release 0.6 → %.1f ms，Release 0.25 → %.1f ms\n",
                textOf("Threshold", 0.6f), textOf("Ratio", 0.60698f), textOf("Attack", 0.4f), textOf("Release", 0.6f),
                textOf("Release", 0.25f));
    DAWLAB_CHECK_NEAR(textOf("Threshold", 0.6f), -24.0, 0.051);
    DAWLAB_CHECK_NEAR(textOf("Ratio", 0.60698f), 8.0, 0.006);
    DAWLAB_CHECK_NEAR(textOf("Attack", 0.4f), 3.86, 0.006);
    DAWLAB_CHECK_NEAR(textOf("Release", 0.6f), 216.6, 0.051);
    DAWLAB_CHECK_NEAR(textOf("Release", 0.25f), 9.5, 0.051);

    DAWLAB_CHECK(stateA.getSize() > 0);
    DAWLAB_CHECK(diffAB > 0.01f);               // A 與 B 確實不同（不然這個測試沒意義）
    DAWLAB_CHECK(maxParamError < 1.0e-3f);      // 參數都回來了
    DAWLAB_CHECK(diffRestored < 1.0e-6f);       // 聲音也一模一樣
    DAWLAB_CHECK(diffFresh < 1.0e-6f);          // 專案檔重新開啟 = 新 instance + setState
    return dawlab::test::summary("l13_state_roundtrip");
}
