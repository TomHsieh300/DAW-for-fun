// 練習 7（起始碼）：host 端的「無爆音 bypass」—— latency 補償 + crossfade
//
// 直接把 plugin 拿掉（hard bypass）會出兩個問題：
//   (a) 波形不連續 → click；
//   (b) plugin 有 latency L 時，wet 比 dry 晚 L 個 sample，切換瞬間時間軸跳了 L → 更大的跳動，
//       而且整條 track 突然「提早」L 個 sample（跟其他 track 對不齊）。
// 解法：
//   dry 也延遲 L（跟 PDC 同一個道理），再用 10 ms 的線性 crossfade 在 dry 與 wet 之間切換：
//     y[n] = g[n]·wet[n] + (1 − g[n])·dry[n − L]，g 從 1 平滑走到 0（bypass）或反過來。
//   plugin 在 bypass 時仍然繼續處理（保持內部狀態「熱」的，取消 bypass 時才不會有暫態）。
// 這其實就是 plugin 內部 mix（parallel）參數的同一招：dry/wet 混合 + dry 對齊 latency。
//
// 完成 CrossfadeBypass::process() 的 TODO。目前 bypass 沒有作用，self-check 會印出 FAIL（這是預期的）。
//
// 驗證（DawLabLimiter：ceiling −20 dB、lookahead 5 ms → L = 240；輸入 440 Hz、振幅 0.3 的正弦波）：
//   1. bypass 穩定後，輸出 = 輸入延遲 L（時間軸沒有跳）；
//   2. crossfade 版的最大 sample-to-sample 跳動 < 0.025（正弦波本身最大斜率 0.3·2π·440/48000 ≈ 0.0173）；
//   3. 沒有補償、沒有 crossfade 的 hard bypass 跳動 > 0.05（聽得到的 click）。
// 輸出：dawlab_output/l13_ex7_crossfade.wav、l13_ex7_hard.wav（每 0.5 秒切換一次 bypass；hard 版有明顯的「喀」聲）

#include "../../examples/L13Common.h"

#include <dawlab/dsp/DelayLine.h>
#include <dawlab/dsp/SmoothedValue.h>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

class CrossfadeBypass {
public:
    // message thread：配置記憶體。latencySamples 取自 plugin.getLatencySamples()。
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

    // audio thread：不 allocate
    void process(dawlab::AudioBlock block, dawlab::host::HostedPlugin& plugin) noexcept {
        // TODO：
        //   1. 每個 channel、每個 sample：delay_.push(ch, x)，dry_[ch][i] = delay_.read(ch, latency_)
        //      （dry 對齊 plugin 的 latency）；
        //   2. plugin.process(block) —— bypass 時也要跑，保持 plugin 內部狀態；
        //   3. 每個 sample 取 g = gain_.getNextValue()，y = dry + g·(wet − dry)。
        // 目前：永遠只輸出 wet，setBypassed() 完全沒有作用。
        plugin.process(block);
    }

private:
    dawlab::dsp::DelayLine delay_;
    dawlab::AudioBuffer dry_;
    dawlab::dsp::SmoothedValue gain_{1.0f};
    int latency_ = 0;
    bool bypassed_ = false;
};

// 每 0.5 秒切換一次：0.5 s bypass、1.0 s 取消、1.5 s 再 bypass
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

    // ---- crossfade + latency 補償 --------------------------------------------------
    plugin->prepare(kSampleRate, kBlockSize, 2);
    const int latency = plugin->getLatencySamples();
    CrossfadeBypass bypass;
    bypass.prepare(kSampleRate, kBlockSize, 2, latency);
    dawlab::AudioBuffer smooth = input;
    dawlab::AudioBlock smoothBlock(smooth);
    for (int start = 0; start < total; start += kBlockSize) {
        bypass.setBypassed(bypassAt(start)); // automation：只在 block 邊界改
        bypass.process(smoothBlock.getSubBlock(start, std::min(kBlockSize, total - start)), *plugin);
    }
    plugin->releaseResources();

    // ---- 天真的 hard bypass：bypass 時直接輸出原音（沒有延遲、沒有 fade）------------------------
    plugin->prepare(kSampleRate, kBlockSize, 2);
    dawlab::AudioBuffer hard = input;
    dawlab::AudioBlock hardBlock(hard);
    for (int start = 0; start < total; start += kBlockSize) {
        auto sub = hardBlock.getSubBlock(start, std::min(kBlockSize, total - start));
        if (!bypassAt(start))
            plugin->process(sub); // bypass 時什麼都不做 = 原音
    }
    plugin->releaseResources();

    // ---- 量測 -----------------------------------------------------------------------
    const int skip = static_cast<int>(0.1 * kSampleRate); // 跳過開頭（limiter 剛開始的暫態）
    const float stepSmooth = maxStep(smooth, skip);
    const float stepHard = maxStep(hard, skip);
    const float signalStep = static_cast<float>(0.3 * dawlab::kTwoPi * 440.0 / kSampleRate);

    // bypass 穩定區（0.7–0.95 s）：輸出應該 = 輸入延遲 latency
    float alignError = 0.0f;
    for (int i = static_cast<int>(0.7 * kSampleRate); i < static_cast<int>(0.95 * kSampleRate); ++i)
        alignError = std::max(alignError, std::abs(smooth.getSample(0, i) - input.getSample(0, i - latency)));
    // 作用區（0.3–0.45 s）：limiter 把 0.3 壓到 ceiling 0.1
    float activePeak = 0.0f;
    for (int i = static_cast<int>(0.3 * kSampleRate); i < static_cast<int>(0.45 * kSampleRate); ++i)
        activePeak = std::max(activePeak, std::abs(smooth.getSample(0, i)));

    std::printf("plugin latency：%d samples\n", latency);
    std::printf("正弦波本身的最大 sample-to-sample 變化：%.4f\n", signalStep);
    std::printf("crossfade + 補償：最大跳動 %.4f；hard bypass：最大跳動 %.4f\n", stepSmooth, stepHard);
    std::printf("bypass 時輸出與「輸入延遲 %d」的最大差：%.3g；作用時的 peak：%.4f（ceiling 0.1）\n", latency, alignError,
                activePeak);
    dawlab::writeWav(dawlab::outputPath("l13_ex7_crossfade.wav"), smooth, kSampleRate);
    dawlab::writeWav(dawlab::outputPath("l13_ex7_hard.wav"), hard, kSampleRate);

    DAWLAB_CHECK(latency == dawlab::msToSamples(5.0, kSampleRate));
    DAWLAB_CHECK(alignError < 1.0e-6f);
    DAWLAB_CHECK(activePeak <= 0.1f * 1.001f);
    DAWLAB_CHECK(stepSmooth < 0.025f);
    DAWLAB_CHECK(stepHard > 0.05f);
    return dawlab::test::summary("l13_ex7_bypass_crossfade");
}
