// 練習 4（起始碼）：host 端的 automation —— 把 audio block 切成 sub-block，每段開頭設定一次參數
//
// 目標：讓 DawLabCompressor 的 Threshold 在 8 秒內從 0 dB 線性降到 −40 dB（snap 到 0.1 dB），
//       時間解析度 32 samples：每個 512-sample 的 block 切成 16 段，每段開頭呼叫一次 setParameterValue。
// 完成後 self-check 會全部 PASS；目前的程式沒有做 automation，所以會印出 FAIL（這是預期的）。

#include "../../examples/L13Common.h"

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
        // TODO：把這個 block 切成長度 kSubBlock 的 sub-block（all.getSubBlock(...)），每一段：
        //   1. 用 thresholdAt(start + sub, total) 算出 dB 值；
        //   2. 用 l13::toNormalized(db, -60.0f, 0.0f) 換成 0..1，呼叫 plugin->setParameterValue(thresholdIndex, ...)；
        //   3. ++parameterChanges；
        //   4. plugin->process(那一段 sub-block)。
        // 目前整個 block 一次處理、從不改參數：
        plugin->process(all.getSubBlock(start, blockLen));
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

    std::printf("參數變化次數：%d\n", parameterChanges);
    std::printf("前 0.5 秒 RMS 變化：%+.2f dB；最後 1 秒：%+.2f dB\n", early, late);
    std::printf("與參考（dsp::Compressor）的最大差異：%.3g\n", diff);

    DAWLAB_CHECK(parameterChanges >= total / kSubBlock);
    DAWLAB_CHECK(diff < 1.0e-4f);
    DAWLAB_CHECK(early > -1.0f);
    DAWLAB_CHECK(late < -6.0f);
    return dawlab::test::summary("l13_ex4_automation");
}
