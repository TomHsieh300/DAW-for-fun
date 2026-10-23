// 範例 2：tempo-synced ping-pong delay
//
// 示範什麼：
//   1. BPM → 毫秒的換算（Delay::noteToMs），含附點與三連音。
//   2. 用 impulse 驗證 ping-pong 的訊號路徑：左 → 右 → 左 → 右，每一跳乘上 feedback。
//   3. 把一段 120 BPM 的撥弦琶音（makePluckSequence，八分音符 = 250 ms）
//      送進「附點八分音符 = 375 ms」的 ping-pong delay —— 這是流行 / 搖滾最常見的 delay 設定，
//      echo 會落在拍子之間，產生左右跳動的節奏感。
//
// 怎麼執行：不需要參數，直接執行 l09_pingpong_tempo。
// 輸出檔案（dawlab_output/）：
//   l09_pingpong_dry.wav   原始琶音
//   l09_pingpong_wet.wav   附點八分 ping-pong delay（feedback 0.45、mix 0.35、feedback 內 150 Hz HP + 3.5 kHz LP）
//   l09_echo_wet.wav       同一段琶音、普通（非 ping-pong）八分音符 stereo echo，作為對照
// 該聽到什麼：wet 版本的 echo 在左右聲道交替出現，而且越來越暗（feedback 內的 lowpass）。

#include <dawlab/Common.h>
#include <dawlab/dsp/Delay.h>

#include <cmath>
#include <cstdio>
#include <vector>

using dawlab::dsp::Delay;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr double kBpm = 120.0;

// 一塊一塊處理（模擬 audio callback）
void processInBlocks(Delay& delay, dawlab::AudioBuffer& buffer) {
    dawlab::AudioBlock whole(buffer);
    for (int start = 0; start < buffer.getNumSamples(); start += kBlockSize) {
        const int n = std::min(kBlockSize, buffer.getNumSamples() - start);
        delay.process(whole.getSubBlock(start, n));
    }
}

// 找出 buffer 某個 channel 裡「超過 threshold」的局部峰值（用來看 impulse response 的 echo 位置）
void printEchoes(const dawlab::AudioBuffer& b, int ch, const char* name, float threshold) {
    const float* d = b.getReadPointer(ch);
    std::printf("  %s：", name);
    int count = 0;
    for (int i = 1; i + 1 < b.getNumSamples() && count < 6; ++i) {
        const float v = std::abs(d[i]);
        if (v > threshold && v >= std::abs(d[i - 1]) && v >= std::abs(d[i + 1])) {
            std::printf("[n=%d (%.0f ms) amp=%.4f] ", i, dawlab::samplesToMs(i, kSampleRate), d[i]);
            ++count;
        }
    }
    std::printf("\n");
}

} // namespace

int main() {
    std::printf("=== 第 09 章 範例 2：tempo-synced ping-pong delay ===\n\n");

    // ---- 1. BPM 換算表 -------------------------------------------------------
    std::printf("BPM = %.0f 的音符長度（ms = beats × 60000 / BPM）：\n", kBpm);
    struct Note { const char* name; double beats; };
    const Note notes[] = {
        {"四分音符 (1 拍)", 1.0},        {"八分音符", 0.5},          {"附點八分 (0.75 拍)", 0.75},
        {"三連音八分 (1/3 拍)", 1.0 / 3.0}, {"十六分音符", 0.25},      {"附點十六分 (0.375 拍)", 0.375},
        {"四分三連音 (2/3 拍)", 2.0 / 3.0}, {"二分音符 (2 拍)", 2.0},
    };
    for (const auto& n : notes)
        std::printf("  %8.2f ms   %s\n", Delay::noteToMs(kBpm, n.beats), n.name); // 中文字寬不固定，數字放前面才對得齊
    std::printf("\n");

    // ---- 2. impulse 驗證 ping-pong 路徑 ---------------------------------------
    {
        const float delayMs = Delay::noteToMs(kBpm, 0.75); // 375 ms = 18000 samples
        Delay delay;
        delay.setMaxDelayMs(1000.0f);
        delay.prepare(kSampleRate, kBlockSize, 2);
        delay.setDelayMs(delayMs);
        delay.setFeedback(0.5f);
        delay.setMix(1.0f);
        delay.setPingPong(true);
        delay.setLowpassHz(20000.0f); // 關閉濾波，讓 echo 保持乾淨的 impulse
        delay.setHighpassHz(20.0f);
        delay.reset();

        dawlab::AudioBuffer impulse(2, dawlab::secondsToSamples(2.0, kSampleRate));
        impulse.setSample(0, 0, 1.0f); // 只打在左聲道：ping-pong 會先混成 mono (0.5)
        processInBlocks(delay, impulse);
        std::printf("impulse（左聲道，振幅 1）經過 %.0f ms ping-pong、feedback 0.5 的回聲位置：\n", delayMs);
        printEchoes(impulse, 0, "左", 0.01f);
        printEchoes(impulse, 1, "右", 0.01f);
        std::printf("  -> 左右交替，間隔 %.0f ms；mono 混合後第一聲是 0.5，之後每跳 × 0.5。\n\n", delayMs);
    }

    // ---- 3. 音樂素材：撥弦琶音 + 附點八分 ping-pong --------------------------
    {
        const double seqSeconds = 4.0;
        const double totalSeconds = 8.0; // 留 4 秒讓 delay 尾巴衰減
        dawlab::AudioBuffer seq = dawlab::makePluckSequence(kSampleRate, seqSeconds, 0.4f);
        dawlab::AudioBuffer dry(2, dawlab::secondsToSamples(totalSeconds, kSampleRate));
        for (int ch = 0; ch < 2; ++ch)
            dry.copyFrom(ch, 0, seq, ch, 0, seq.getNumSamples());

        // (a) 附點八分 ping-pong
        dawlab::AudioBuffer pingpong = dry;
        {
            Delay delay;
            delay.setMaxDelayMs(2000.0f);
            delay.prepare(kSampleRate, kBlockSize, 2);
            delay.setDelayMs(Delay::noteToMs(kBpm, 0.75));
            delay.setFeedback(0.45f);
            delay.setMix(0.35f);
            delay.setPingPong(true);
            delay.setHighpassHz(150.0f);  // 去掉回聲裡的低頻轟隆
            delay.setLowpassHz(3500.0f);  // 每一次回圈都更暗一點：tape echo 的味道
            delay.reset();
            processInBlocks(delay, pingpong);
        }

        // (b) 普通八分音符 stereo echo（兩聲道獨立）作為對照
        dawlab::AudioBuffer echo = dry;
        {
            Delay delay;
            delay.prepare(kSampleRate, kBlockSize, 2);
            delay.setDelayMs(Delay::noteToMs(kBpm, 0.5));
            delay.setFeedback(0.35f);
            delay.setMix(0.3f);
            delay.setPingPong(false);
            delay.setLowpassHz(5000.0f);
            delay.reset();
            processInBlocks(delay, echo);
        }

        dawlab::writeWav(dawlab::outputPath("l09_pingpong_dry.wav"), dry, kSampleRate);
        dawlab::writeWav(dawlab::outputPath("l09_pingpong_wet.wav"), pingpong, kSampleRate);
        dawlab::writeWav(dawlab::outputPath("l09_echo_wet.wav"), echo, kSampleRate);

        // 尾巴：琶音在 4 秒結束，之後只剩 delay
        const int tailStart = dawlab::secondsToSamples(4.5, kSampleRate);
        const int tailLen = dawlab::secondsToSamples(0.5, kSampleRate);
        std::printf("琶音 + ping-pong：peak dry %.3f / wet %.3f；4.5–5.0 s 的尾巴 RMS：dry %.5f / wet %.5f\n",
                    dawlab::peakAbs(dry), dawlab::peakAbs(pingpong),
                    dawlab::rms(dry.getReadPointer(0) + tailStart, tailLen),
                    dawlab::rms(pingpong.getReadPointer(0) + tailStart, tailLen));
        std::printf("NaN/Inf 檢查：%s\n", dawlab::hasNanOrInf(pingpong) ? "FAIL" : "OK");
        std::printf("已輸出 dawlab_output/l09_pingpong_dry.wav, l09_pingpong_wet.wav, l09_echo_wet.wav\n");
    }
    return 0;
}
