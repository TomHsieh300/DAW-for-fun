// 練習 4 解答：host 端的 automation —— 把 audio block 切成 sub-block，每段開頭設定一次參數
//
// 情境：DAW 的 automation lane 上畫了一條線，讓 DawLabCompressor 的 Threshold 在 8 秒內從 0 dB 降到 −40 dB。
// plugin 每個 processBlock 只讀一次參數，所以 host 想要更細的時間解析度，就把 512 個 sample 的 block
// 切成 32 個 sample 的 sub-block，每段開頭送一次新值（VST3 其實允許 sample-accurate 的 parameter queue，
// 但 JUCE 的 plugin 端只在 block 開頭套用，切 sub-block 是最通用、所有 host 都能做的方法）。
//
// 驗證：
//   1. 跟「直接呼叫 dsp::Compressor、每 32 個 sample 改一次 threshold」的參考結果相同（差 < 1e-4）；
//   2. 開頭 0.5 秒幾乎沒壓（RMS 變化 > −1 dB），最後 1 秒壓很多（RMS 變化 < −6 dB）。
// 輸出：dawlab_output/l13_ex4_automation.wav（聽起來：鼓一開始很鬆，越來越扁、越來越「被擠壓」）

#include "../examples/L13Common.h"

#include <dawlab/dsp/Compressor.h>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr int kSubBlock = 32;

// automation 曲線：t = 0 → 0 dB，t = 結尾 → −40 dB；snap 到 plugin 的 interval（0.1 dB）
float thresholdAt(int sample, int totalSamples) {
    const double t = static_cast<double>(sample) / totalSamples;
    const double db = -40.0 * t;
    return static_cast<float>(std::round(db * 10.0) / 10.0);
}

float rmsChangeDb(const dawlab::AudioBuffer& in, const dawlab::AudioBuffer& out, int start, int length) {
    const float a = dawlab::rms(in.getReadPointer(0) + start, length);
    const float b = dawlab::rms(out.getReadPointer(0) + start, length);
    return dawlab::gainToDb(b / a);
}

} // namespace

int main() {
    const juce::ScopedJuceInitialiser_GUI juceInit;
    dawlab::host::PluginHost host;
    auto plugin = l13::mustLoad(host, l13::defaultCompressorVst3(), kSampleRate, kBlockSize);
    if (plugin == nullptr)
        return 1;

    const dawlab::AudioBuffer input = dawlab::makeDrumLoop(kSampleRate, 4); // 8 秒
    const int total = input.getNumSamples();
    const int thresholdIndex = plugin->findParameter("Threshold");
    DAWLAB_CHECK(thresholdIndex >= 0);

    plugin->prepare(kSampleRate, kBlockSize, 2);
    dawlab::AudioBuffer output = input;
    dawlab::AudioBlock all(output);
    int parameterChanges = 0;
    for (int start = 0; start < total; start += kBlockSize) {
        const int blockLen = std::min(kBlockSize, total - start);
        // 一個 audio callback（512 samples）切成 16 個 sub-block
        for (int sub = 0; sub < blockLen; sub += kSubBlock) {
            const int len = std::min(kSubBlock, blockLen - sub);
            const float db = thresholdAt(start + sub, total);
            plugin->setParameterValue(thresholdIndex, l13::toNormalized(db, -60.0f, 0.0f));
            ++parameterChanges;
            plugin->process(all.getSubBlock(start + sub, len));
        }
    }
    plugin->releaseResources();

    // 參考答案：同樣的 automation，直接套在 dsp::Compressor 上（其他參數 = plugin 預設值）
    dawlab::dsp::Compressor reference;
    reference.prepare(kSampleRate, kBlockSize, 2);
    reference.setRatio(4.0f);
    reference.setKneeDb(6.0f);
    reference.setAttackMs(10.0f);
    reference.setReleaseMs(100.0f);
    reference.reset();
    dawlab::AudioBuffer expected = input;
    dawlab::AudioBlock ref(expected);
    for (int s = 0; s < total; s += kSubBlock) {
        reference.setThresholdDb(thresholdAt(s, total));
        reference.process(ref.getSubBlock(s, std::min(kSubBlock, total - s)));
    }

    const float diff = l13::maxAbsDifference(output, expected);
    const int half = static_cast<int>(0.5 * kSampleRate);
    const int second = static_cast<int>(kSampleRate);
    const float early = rmsChangeDb(input, output, 0, half);
    const float late = rmsChangeDb(input, output, total - second, second);

    std::printf("參數變化次數：%d（每 %d samples 一次，時間解析度 %.2f ms）\n", parameterChanges, kSubBlock,
                1000.0 * kSubBlock / kSampleRate);
    std::printf("前 0.5 秒 RMS 變化：%+.2f dB；最後 1 秒：%+.2f dB\n", early, late);
    std::printf("與參考（dsp::Compressor）的最大差異：%.3g\n", diff);
    dawlab::writeWav(dawlab::outputPath("l13_ex4_automation.wav"), output, kSampleRate);

    DAWLAB_CHECK(parameterChanges >= total / kSubBlock);
    DAWLAB_CHECK(diff < 1.0e-4f);
    DAWLAB_CHECK(early > -1.0f);
    DAWLAB_CHECK(late < -6.0f);
    return dawlab::test::summary("l13_ex4_automation");
}
