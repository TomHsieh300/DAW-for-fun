// 範例 1：zipper noise 與 click —— 瞬間改 gain vs 平滑改 gain
//
// 示範什麼：
//   (A) 單次跳變：1 kHz 正弦波播到約 1.0 s 時 gain 從 0 dB 跳到 -20 dB。
//       比較「瞬間跳」與「50 ms 線性 ramp」兩種做法，用一階差分最大值量化 click 的大小。
//   (B) 推桿連續移動（zipper）：模擬 UI 每個 block（512 sample ≈ 10.7 ms）送一次新參數，
//       gain 從 0 dB 每個 block 降 1 dB 直到 -40 dB。比較「每個 block 階梯式跳變」與
//       「SmoothedValue 平滑」，並用 Goertzel 量測 1 kHz 旁邊的 sideband（階梯調變產生的雜音）。
//
// 怎麼執行：
//   cd <build 目錄> && bin/Debug/l01_zipper_noise.exe
//
// 輸出檔案（在 <build 目錄>/dawlab_output/）：
//   l01_step_instant.wav   單次跳變，瞬間        → 約 1.0 s 處有明顯的「喀」一聲
//   l01_step_smoothed.wav  單次跳變，50 ms ramp  → 只聽到音量變小，沒有 click
//   l01_zipper_stepped.wav 推桿移動，每個 block 跳一階 → 聽到像拉拉鍊的「茲茲茲」雜音
//   l01_zipper_smoothed.wav 推桿移動，平滑        → 乾淨的 fade out
//
// 該聽到什麼：瞬間跳變的檔案在改 gain 的瞬間有 click / 拉鍊聲；平滑版本只有音量變化。

#include <dawlab/Common.h>
#include <dawlab/dsp/Gain.h>

#include <cstdio>
#include <vector>

using namespace dawlab;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr double kToneHz = 1000.0;
constexpr float kAmplitude = 0.5f; // -6.02 dBFS

// 一階差分的最大值 max |y[n] - y[n-1]|：純正弦波的理論上限是 A·2πf/fs，
// 超過這個值代表波形出現了「不該有的陡坡」= click
float maxFirstDifference(const AudioBuffer& b, int channel) {
    const float* x = b.getReadPointer(channel);
    float m = 0.0f;
    for (int i = 1; i < b.getNumSamples(); ++i)
        m = std::max(m, std::abs(x[i] - x[i - 1]));
    return m;
}

// 用 Gain 一塊一塊處理（模擬 audio callback），每個 block 開始前呼叫 onBlock(blockIndex, gain)
template <typename OnBlock>
AudioBuffer renderWithGain(const AudioBuffer& input, double rampSeconds, OnBlock&& onBlock) {
    AudioBuffer out = input;
    dsp::Gain gain;
    gain.setRampSeconds(rampSeconds);
    gain.prepare(kSampleRate, kBlockSize, out.getNumChannels());
    AudioBlock all(out);
    const int total = out.getNumSamples();
    for (int start = 0, blockIndex = 0; start < total; start += kBlockSize, ++blockIndex) {
        const int len = std::min(kBlockSize, total - start);
        onBlock(blockIndex, gain);
        gain.process(all.getSubBlock(start, len));
    }
    return out;
}

double sidebandDb(const AudioBuffer& b, int start, int n, double freq) {
    const double carrier = goertzelAmplitude(b.getReadPointer(0) + start, n, kToneHz, kSampleRate);
    const double side = goertzelAmplitude(b.getReadPointer(0) + start, n, freq, kSampleRate);
    return gainToDb(side / carrier);
}

} // namespace

int main() {
    std::printf("=== 第 01 章 範例 1：zipper noise 與 click ===\n\n");

    const AudioBuffer tone = makeSine(kToneHz, kSampleRate, 2.0, kAmplitude, 1);
    const float naturalDiff = maxFirstDifference(tone, 0);
    const double theoreticalDiff = kAmplitude * kTwoPi * kToneHz / kSampleRate;
    std::printf("輸入：%.0f Hz 正弦波，振幅 %.2f（%.2f dBFS）\n", kToneHz, kAmplitude, gainToDb(kAmplitude));
    std::printf("純正弦波的一階差分最大值：實測 %.4f，理論 A·2πf/fs = %.4f\n\n", naturalDiff, theoreticalDiff);

    // ---- (A) 單次跳變：約 1.0 s 時 0 dB → -20 dB -----------------------------
    // 注意：不能隨便挑跳變位置。block 93 的起點是 sample 47616 = 992 個完整的 1 kHz 週期，
    // 剛好落在零交越（zero crossing），訊號值是 0，跳變時 0×1 和 0×0.1 沒有差別——不會有 click！
    // （這就是 DAW「zero-crossing 編輯」的原理。）所以選 block 94：起點 sample 48128，
    // 相位 = 2π·(48128 mod 48)/48 = 240°，訊號在 −0.43 附近，跳變會很明顯。
    const int jumpBlock = secondsToSamples(1.0, kSampleRate) / kBlockSize + 1; // 第 94 個 block
    const int jumpSample = jumpBlock * kBlockSize;
    auto jumpAt = [jumpBlock](int blockIndex, dsp::Gain& g) {
        if (blockIndex == jumpBlock)
            g.setGainDb(-20.0f);
    };
    AudioBuffer instant = renderWithGain(tone, 0.0, jumpAt);
    AudioBuffer smoothed = renderWithGain(tone, 0.05, jumpAt);

    const float diffInstant = maxFirstDifference(instant, 0);
    const float diffSmoothed = maxFirstDifference(smoothed, 0);
    std::printf("(A) 單次跳變 0 dB → -20 dB（在 sample %d = %.4f s，該處原始訊號值 %.3f）\n", jumpSample,
                jumpSample / kSampleRate, tone.getSample(0, jumpSample));
    std::printf("    瞬間跳：跳變前一個 sample %.4f → 跳變後 %.4f，一階差分 %.4f\n",
                instant.getSample(0, jumpSample - 1), instant.getSample(0, jumpSample),
                std::abs(instant.getSample(0, jumpSample) - instant.getSample(0, jumpSample - 1)));
    std::printf("    一階差分最大值：瞬間跳 %.4f（是正弦波自然值的 %.1f 倍）\n", diffInstant,
                diffInstant / naturalDiff);
    std::printf("    一階差分最大值：50 ms ramp %.4f（%.2f 倍，幾乎等於自然值）\n", diffSmoothed,
                diffSmoothed / naturalDiff);
    std::printf("    ramp 結束後的穩態振幅：%.4f（期望 %.4f）\n\n", peakAbs(smoothed.getReadPointer(0) + 70000, 20000),
                kAmplitude * dbToGain(-20.0f));
    writeWav(outputPath("l01_step_instant.wav"), instant, kSampleRate);
    writeWav(outputPath("l01_step_smoothed.wav"), smoothed, kSampleRate);

    // ---- (B) 推桿連續移動：每個 block 降 1 dB，0 → -40 dB ----------------------
    const int sweepStartBlock = secondsToSamples(0.5, kSampleRate) / kBlockSize;
    constexpr int kSteps = 40;
    auto sweep = [sweepStartBlock](int blockIndex, dsp::Gain& g) {
        const int step = blockIndex - sweepStartBlock;
        if (step >= 0 && step <= kSteps)
            g.setGainDb(-static_cast<float>(step));
    };
    AudioBuffer stepped = renderWithGain(tone, 0.0, sweep);
    AudioBuffer zipperSmoothed = renderWithGain(tone, 0.05, sweep);

    const int winStart = sweepStartBlock * kBlockSize;
    const int winLen = kSteps * kBlockSize;
    const double blockRate = kSampleRate / kBlockSize; // 93.75 Hz：參數更新頻率
    std::printf("(B) 推桿移動：每個 block（%.2f ms，更新率 %.2f Hz）降 1 dB，共 %d 步\n",
                1000.0 * kBlockSize / kSampleRate, blockRate, kSteps);
    std::printf("    一階差分最大值：階梯式 %.4f（自然值的 %.2f 倍），平滑 %.4f（%.2f 倍）\n",
                maxFirstDifference(stepped, 0), maxFirstDifference(stepped, 0) / naturalDiff,
                maxFirstDifference(zipperSmoothed, 0), maxFirstDifference(zipperSmoothed, 0) / naturalDiff);
    std::printf("    1 kHz 旁邊的 sideband（相對於 1 kHz 載波，量測區間 = 推桿移動的 %.2f s）：\n",
                winLen / kSampleRate);
    std::printf("    %-22s %12s %12s\n", "頻率", "階梯式 (dB)", "平滑 (dB)");
    const double probes[] = {kToneHz + blockRate, kToneHz + 2 * blockRate, kToneHz + 4 * blockRate,
                             kToneHz + 8 * blockRate, kToneHz + 16 * blockRate};
    for (double f : probes)
        std::printf("    %7.2f Hz (1k+%2.0f·fb) %12.1f %12.1f\n", f, (f - kToneHz) / blockRate,
                    sidebandDb(stepped, winStart, winLen, f), sidebandDb(zipperSmoothed, winStart, winLen, f));
    std::printf("    （sideband 越低越乾淨；階梯式的 sideband 以 1/k 緩慢衰減，就是「拉鍊聲」）\n\n");
    writeWav(outputPath("l01_zipper_stepped.wav"), stepped, kSampleRate);
    writeWav(outputPath("l01_zipper_smoothed.wav"), zipperSmoothed, kSampleRate);

    // ---- SmoothedValue 本身的行為 ---------------------------------------------
    dsp::SmoothedValue sv;
    sv.reset(kSampleRate, 0.05);
    sv.setCurrentAndTargetValue(1.0f);
    sv.setTargetValue(0.0f);
    int count = 0;
    while (sv.isSmoothing()) {
        sv.getNextValue();
        ++count;
    }
    std::printf("SmoothedValue：50 ms ramp 在 %.0f Hz 下走了 %d 個 sample（期望 %d），結束值 %.3f\n", kSampleRate,
                count, msToSamples(50.0, kSampleRate), sv.getCurrentValue());

    std::printf("\n輸出：dawlab_output/l01_step_instant.wav, l01_step_smoothed.wav, "
                "l01_zipper_stepped.wav, l01_zipper_smoothed.wav\n");
    return 0;
}
