// 範例 1：fast-attack compressor vs lookahead limiter 的 overshoot 對照
//
// 示範什麼：
//   同一段「突然變大」的訊號（1 kHz，-30 dBFS → -6 dBFS → -30 dBFS），
//   分別送進第 04 章的 Compressor（threshold -20 dB、ratio 100:1、attack 1 ms）
//   與本章的 Limiter（ceiling -20 dB、lookahead 2 ms）。
//   Compressor 的 envelope follower 要「看到」訊號變大才開始壓，所以剛變大的那幾毫秒
//   會整段衝過 threshold（overshoot）；Limiter 因為把音訊延後 2 ms，gain 可以提前降下來，
//   輸出 peak 永遠 ≤ ceiling。
//
// 怎麼執行：l05_compressor_vs_limiter（不需要參數）
//
// 輸出檔案（在 dawlab_output/）：
//   l05_cmp_vs_lim_input.wav       原始 tone bursts
//   l05_cmp_vs_lim_compressor.wav  Compressor 輸出
//   l05_cmp_vs_lim_limiter.wav     Limiter 輸出（已比輸入晚 2 ms）
//
// 該聽到 / 看到：
//   Compressor 版在第 0.4 秒處有一個短促的「啪」（overshoot 那幾毫秒比較大聲）；
//   Limiter 版的音量變化平順，表格裡的 peak 不會超過 -20 dBFS。

#include <dawlab/Common.h>
#include <dawlab/dsp/Compressor.h>
#include <dawlab/dsp/Limiter.h>

#include <cstdio>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

// 模擬 audio callback：一次處理一個 block
template <typename Processor>
void processInBlocks(dawlab::AudioBuffer& buffer, Processor& processor) {
    dawlab::AudioBlock whole(buffer);
    for (int start = 0; start < buffer.getNumSamples(); start += kBlockSize) {
        const int len = std::min(kBlockSize, buffer.getNumSamples() - start);
        processor.process(whole.getSubBlock(start, len));
    }
}

// 在 [start, start + length) 這段裡的最大 |sample|（dBFS）
float peakDbIn(const dawlab::AudioBuffer& b, int start, int length) {
    const int n = std::min(length, std::max(0, b.getNumSamples() - start));
    return dawlab::gainToDb(dawlab::peakAbs(b.getReadPointer(0) + start, n));
}

} // namespace

int main() {
    const float thresholdDb = -20.0f;
    const double segmentSeconds = 0.4;
    dawlab::AudioBuffer input = dawlab::makeToneBursts(1000.0, kSampleRate, {-30.0f, -6.0f, -30.0f}, segmentSeconds);
    const int stepIndex = dawlab::secondsToSamples(segmentSeconds, kSampleRate); // 訊號在這裡突然變大 24 dB

    // ---- Compressor：盡量模擬 limiter（ratio 100:1、attack 1 ms、hard knee）----
    dawlab::AudioBuffer compressed = input;
    dawlab::dsp::Compressor compressor;
    compressor.prepare(kSampleRate, kBlockSize, 2);
    compressor.setThresholdDb(thresholdDb);
    compressor.setRatio(100.0f);
    compressor.setKneeDb(0.0f);
    compressor.setAttackMs(1.0f);
    compressor.setReleaseMs(100.0f);
    compressor.setMakeupGainDb(0.0f);
    compressor.setStereoLink(true);
    processInBlocks(compressed, compressor);

    // ---- Limiter：ceiling 設成同一個 -20 dB，lookahead 2 ms ----
    dawlab::AudioBuffer limited = input;
    dawlab::dsp::Limiter limiter;
    limiter.prepare(kSampleRate, kBlockSize, 2);
    limiter.setCeilingDb(thresholdDb);
    limiter.setLookaheadMs(2.0f);
    limiter.setReleaseMs(100.0f);
    processInBlocks(limited, limiter);
    const int latency = limiter.getLatencySamples();

    std::printf("輸入：1 kHz tone bursts  -30 → -6 → -30 dBFS（每段 %.1f s），在第 %d 個 sample 變大\n",
                segmentSeconds, stepIndex);
    std::printf("Compressor：threshold %.0f dB, ratio 100:1, attack 1 ms   |   Limiter：ceiling %.0f dB, lookahead 2 ms (= %d samples)\n\n",
                thresholdDb, thresholdDb, latency);

    // 變大之後每 1 ms 的輸出 peak（Limiter 的輸出晚了 latency 個 sample，比較時要對齊）
    const int ms = dawlab::msToSamples(1.0, kSampleRate);
    std::printf("%-14s %14s %14s %14s\n", "變大後的時間", "輸入 (dBFS)", "Compressor", "Limiter");
    for (int k : {0, 1, 2, 3, 5, 8, 12, 20, 50}) {
        const int start = stepIndex + k * ms;
        std::printf("%4d ms ~ %-5d %12.2f   %12.2f   %12.2f\n", k, k + 1, peakDbIn(input, start, ms),
                    peakDbIn(compressed, start, ms), peakDbIn(limited, start + latency, ms));
    }

    const float cmpOvershoot = peakDbIn(compressed, stepIndex, 20 * ms) - thresholdDb;
    const float limOvershoot = peakDbIn(limited, stepIndex + latency, 20 * ms) - thresholdDb;
    const float cmpOverall = dawlab::gainToDb(dawlab::peakAbs(compressed)) - thresholdDb;
    const float limOverall = dawlab::gainToDb(dawlab::peakAbs(limited)) - thresholdDb;
    std::printf("\n變大後 20 ms 內相對 threshold/ceiling 的 overshoot：Compressor %+.2f dB，Limiter %+.2f dB\n",
                cmpOvershoot, limOvershoot);
    std::printf("整段輸出的 peak 相對 threshold/ceiling：          Compressor %+.2f dB，Limiter %+.2f dB\n",
                cmpOverall, limOverall);
    std::printf("Limiter 的 safety clamp 觸發次數：%llu（應為 0）\n",
                static_cast<unsigned long long>(limiter.getSafetyClipCount()));

    dawlab::writeWav(dawlab::outputPath("l05_cmp_vs_lim_input.wav"), input, kSampleRate);
    dawlab::writeWav(dawlab::outputPath("l05_cmp_vs_lim_compressor.wav"), compressed, kSampleRate);
    dawlab::writeWav(dawlab::outputPath("l05_cmp_vs_lim_limiter.wav"), limited, kSampleRate);
    std::printf("\n已輸出 dawlab_output/l05_cmp_vs_lim_{input,compressor,limiter}.wav\n");

    // Limiter 的保證是硬性的：超過 ceiling 就算失敗。safety clamp 也必須沒觸發——
    // 它會把演算法的任何錯誤夾回 ceiling，只看 peak 會漏掉 bug。
    return (limOverall <= 1.0e-4f && limiter.getSafetyClipCount() == 0) ? 0 : 1;
}
