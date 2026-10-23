// l00_block_processing：模擬 audio callback，把一整段訊號切成固定大小的 block 一塊一塊處理。
//
// 示範什麼：
//   - 真正的 audio 程式不會「一次處理整首歌」，而是 audio driver 每隔一小段時間呼叫一次
//     callback，給你 blockSize 個 sample，你必須在期限內處理完並交回去。
//   - latency = blockSize / sampleRate：印出常見 block size 在三種 sample rate 下的延遲表。
//   - 用 AudioBlock::getSubBlock() 切出子區塊，所有 DSP 都以 block 為單位 in-place 處理。
//   - 每個 block 的 peak / RMS（這就是 meter 的雛型）。
//   - 驗證「分塊處理」和「整段一次處理」結果完全相同（對沒有內部狀態的處理而言）。
//
// 怎麼執行：
//   cd <build 目錄>
//   bin/Debug/l00_block_processing.exe
//
// 會輸出：
//   l00_block_input.wav       原始鼓組 loop
//   l00_block_processed.wav   逐 block 套用 -6 dB gain 的結果
//
// 該聽到什麼：
//   processed 版本比 input 小聲一半（-6.02 dB），而且聽不出任何「塊」的接縫——
//   因為 gain 沒有內部狀態，block 邊界對結果沒有影響。

#include <dawlab/Common.h>

#include <cstdio>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

// 每個 block 的量測結果：peak 與 RMS（線性值）
struct BlockStats {
    float peak = 0.0f;
    float rms = 0.0f;
};

// 這個函式模擬「audio callback」。真實世界裡它會被 audio driver 的 thread 呼叫，
// 所以裡面不可以 allocate、不可以拿 lock、不可以做 I/O。這裡只做乘法與量測。
BlockStats audioCallback(dawlab::AudioBlock block, float gain) noexcept {
    BlockStats stats;
    const int numChannels = block.getNumChannels();
    const int numSamples = block.getNumSamples();
    double sumSquares = 0.0;
    for (int ch = 0; ch < numChannels; ++ch) {
        float* data = block.getChannelPointer(ch);
        for (int i = 0; i < numSamples; ++i) {
            data[i] *= gain;                                   // in-place 處理
            stats.peak = std::max(stats.peak, std::abs(data[i])); // 量測輸出
            sumSquares += double(data[i]) * data[i];
        }
    }
    const int count = numChannels * numSamples;
    stats.rms = count > 0 ? static_cast<float>(std::sqrt(sumSquares / count)) : 0.0f;
    return stats;
}

} // namespace

int main() {
    using namespace dawlab;

    // ---- 1. latency 表 ----------------------------------------------------
    std::printf("== Latency = blockSize / sampleRate（單位 ms；單程，不含 driver 額外 buffer）==\n");
    const double sampleRates[] = {44100.0, 48000.0, 96000.0};
    const int blockSizes[] = {32, 64, 128, 256, 512, 1024, 2048};
    std::printf("%10s", "block");
    for (double sr : sampleRates)
        std::printf("  %9.0f Hz", sr);
    std::printf("\n");
    for (int bs : blockSizes) {
        std::printf("%10d", bs);
        for (double sr : sampleRates)
            std::printf("  %9.2f ms", samplesToMs(bs, sr));
        std::printf("\n");
    }
    std::printf("輸入 + 輸出各一個 block 的 round-trip 至少是兩倍：例如 512 @ 48 kHz ≈ %.2f ms。\n\n",
                2.0 * samplesToMs(kBlockSize, kSampleRate));

    // ---- 2. 準備（prepare 階段：這裡可以 allocate）-------------------------
    AudioBuffer input = makeDrumLoop(kSampleRate);
    AudioBuffer processed = input; // 複製一份，等一下 in-place 處理
    const int totalSamples = input.getNumSamples();
    const int numBlocks = (totalSamples + kBlockSize - 1) / kBlockSize; // 最後一塊可能不滿
    std::vector<BlockStats> stats(static_cast<size_t>(numBlocks));      // 事先配置好，callback 裡不再 allocate
    const float gain = dbToGain(-6.0f);

    std::printf("== 模擬 audio callback：%d samples，block size %d → %d 個 block（最後一塊 %d samples）==\n",
                totalSamples, kBlockSize, numBlocks, totalSamples - (numBlocks - 1) * kBlockSize);

    // ---- 3. 「audio thread」：一塊一塊處理 -------------------------------
    AudioBlock whole(processed);
    for (int b = 0; b < numBlocks; ++b) {
        const int start = b * kBlockSize;
        const int length = std::min(kBlockSize, totalSamples - start);
        AudioBlock block = whole.getSubBlock(start, length);
        stats[static_cast<size_t>(b)] = audioCallback(block, gain);
    }

    // ---- 4. 印出部分 block 的 meter 讀數 -----------------------------------
    std::printf("%8s %10s %10s %10s\n", "block", "time(ms)", "peak dBFS", "RMS dBFS");
    const int stride = std::max(1, numBlocks / 16);
    for (int b = 0; b < numBlocks; b += stride) {
        const BlockStats& s = stats[static_cast<size_t>(b)];
        std::printf("%8d %10.1f %10.2f %10.2f\n", b, samplesToMs(b * kBlockSize, kSampleRate), gainToDb(s.peak),
                    gainToDb(s.rms));
    }

    float maxBlockPeak = 0.0f;
    for (const BlockStats& s : stats)
        maxBlockPeak = std::max(maxBlockPeak, s.peak);

    // ---- 5. 驗證：分塊處理 == 整段一次處理 --------------------------------
    AudioBuffer reference = input;
    reference.applyGain(gain);
    float maxDiff = 0.0f;
    for (int ch = 0; ch < reference.getNumChannels(); ++ch)
        for (int i = 0; i < totalSamples; ++i)
            maxDiff = std::max(maxDiff, std::abs(reference.getSample(ch, i) - processed.getSample(ch, i)));

    std::printf("\n輸入 peak %.2f dBFS → 輸出 peak %.2f dBFS（所有 block 的最大 peak %.2f dBFS）\n",
                gainToDb(peakAbs(input)), gainToDb(peakAbs(processed)), gainToDb(maxBlockPeak));
    std::printf("分塊處理 vs 整段處理的最大差異 = %g（gain 沒有狀態，所以應該是 0）\n", maxDiff);

    writeWav(outputPath("l00_block_input.wav"), input, kSampleRate);
    writeWav(outputPath("l00_block_processed.wav"), processed, kSampleRate);
    std::printf("已輸出 l00_block_input.wav、l00_block_processed.wav\n");
    return maxDiff == 0.0f ? 0 : 1;
}
