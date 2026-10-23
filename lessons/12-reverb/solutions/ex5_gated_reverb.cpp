// 練習 5 解答：gated reverb（1980 年代鼓聲的招牌音色）
//
//   snare ──┬──────────────────────────────► dry
//           │
//           └─► Reverb（RT60 3 s, mix 1）──► Gate（sidechain = dry snare）──► wet
//
// 原理：把一個很長的 reverb 用 gate「砍斷」。gate 的偵測訊號用 dry 的鼓聲（sidechain），
// 所以每打一下門就開（attack 快），hold 一段固定時間（例如 100 ms），然後 release 很快關上：
// 尾巴不是自然衰減而是突然消失，聽起來又大又緊。用 reverb 自己的輸出當偵測訊號也可以，
// 但 hold 時間會跟 reverb 的音量糾纏在一起，不好控制。
//
// 輸出：dawlab_output/l12_ex5_dry.wav、l12_ex5_reverb.wav（沒 gate）、l12_ex5_gated.wav

#include <dawlab/Common.h>
#include <dawlab/dsp/Gate.h>
#include <dawlab/dsp/Reverb.h>

#include <cmath>
#include <cstdio>

using namespace dawlab;
using namespace dawlab::dsp;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr double kHitInterval = 0.6; // 每 0.6 s 一下 snare

// 合成 snare：185 Hz body + 雜訊 rattle，與 Signals.h 的 drum loop 一樣的配方
AudioBuffer makeSnareHits(double seconds, int numChannels) {
    AudioBuffer b(numChannels, secondsToSamples(seconds, kSampleRate));
    NoiseGenerator noise(4321);
    const int len = secondsToSamples(0.25, kSampleRate);
    for (double t = 0.0; t < seconds; t += kHitInterval) {
        const int start = secondsToSamples(t, kSampleRate);
        for (int i = 0; i < len && start + i < b.getNumSamples(); ++i) {
            const double s = i / kSampleRate;
            const double body = std::sin(kTwoPi * 185.0 * s) * std::exp(-s * 25.0);
            const double rattle = noise.next() * std::exp(-s * 18.0);
            const float v = static_cast<float>(0.8 * (0.35 * body + 0.45 * rattle));
            for (int ch = 0; ch < numChannels; ++ch)
                b.addSample(ch, start + i, v);
        }
    }
    return b;
}

float rmsDbWindow(const AudioBuffer& b, double t0, double t1) {
    const int s = secondsToSamples(t0, kSampleRate);
    const int n = secondsToSamples(t1 - t0, kSampleRate);
    return gainToDb(rms(b.getReadPointer(0) + s, n));
}

} // namespace

int main() {
    std::printf("=== 練習 5：gated reverb ===\n");

    AudioBuffer dry = makeSnareHits(2.4, 2);
    const int n = dry.getNumSamples();

    // 長 reverb（RT60 3 s）、純 wet
    Reverb reverb;
    reverb.prepare(kSampleRate, kBlockSize, 2);
    reverb.setMix(1.0f);
    reverb.setDecaySeconds(3.0f);
    reverb.setDampingHz(8000.0f);
    reverb.setSize(0.7f);
    reverb.setPreDelayMs(0.0f);
    reverb.reset(); // 讓 mix = 1 立刻生效（否則前 ~0.1 s 會從預設 mix 0.3 滑過來，漏出 dry）

    AudioBuffer wet = dry;
    for (int start = 0; start < n; start += kBlockSize)
        reverb.process(wet.toBlock().getSubBlock(start, std::min(kBlockSize, n - start)));
    AudioBuffer ungated = wet; // 留一份沒 gate 的做比較

    // Gate：sidechain = dry snare；hold 100 ms 之後 30 ms 內關掉
    Gate gate;
    gate.prepare(kSampleRate, kBlockSize, 2);
    gate.setThresholdDb(-30.0f);
    gate.setHysteresisDb(6.0f);
    gate.setRangeDb(-80.0f);
    gate.setAttackMs(0.5f);
    gate.setHoldMs(100.0f);
    gate.setReleaseMs(30.0f);

    for (int start = 0; start < n; start += kBlockSize) {
        const int len = std::min(kBlockSize, n - start);
        AudioBlock main = wet.toBlock().getSubBlock(start, len);
        const AudioBlock key = dry.toBlock().getSubBlock(start, len);
        gate.process(main, key);
    }

    // ---- self-check：看第一下（t = 0）之後的 wet 能量 ----------------------------------
    // 開門期間（0.02 .. 0.10 s）：gated 與 ungated 幾乎一樣
    const float openGated = rmsDbWindow(wet, 0.02, 0.10);
    const float openUngated = rmsDbWindow(ungated, 0.02, 0.10);
    // 關門之後（0.40 .. 0.58 s，下一下在 0.6 s）：gated 應該幾乎無聲，ungated 的 3 s 尾巴還很大
    const float closedGated = rmsDbWindow(wet, 0.40, 0.58);
    const float closedUngated = rmsDbWindow(ungated, 0.40, 0.58);
    std::printf("%-22s %12s %12s\n", "window", "ungated(dB)", "gated(dB)");
    std::printf("%-22s %12.1f %12.1f\n", "open   0.02..0.10 s", openUngated, openGated);
    std::printf("%-22s %12.1f %12.1f\n", "closed 0.40..0.58 s", closedUngated, closedGated);
    std::printf("gate 最後狀態：%s，gain reduction %.1f dB\n", gate.isOpen() ? "open" : "closed", gate.getGainReductionDb());

    DAWLAB_CHECK_NEAR(openGated, openUngated, 1.0);     // 門開著時不影響
    DAWLAB_CHECK(closedUngated > openUngated - 15.0f);  // 沒 gate：3 s 的尾巴在 0.4 s 時只掉了幾 dB
    DAWLAB_CHECK(closedGated < openGated - 50.0f);      // 有 gate：尾巴被砍掉
    DAWLAB_CHECK(closedGated < closedUngated - 40.0f);
    DAWLAB_CHECK(!hasNanOrInf(wet));

    // 第二下（t = 0.6 s）門要再開：0.62..0.70 s 的 gated 能量與 ungated 接近
    DAWLAB_CHECK_NEAR(rmsDbWindow(wet, 0.62, 0.70), rmsDbWindow(ungated, 0.62, 0.70), 1.5);

    // 聽感用：dry + gated wet
    AudioBuffer mix = dry;
    for (int ch = 0; ch < 2; ++ch)
        mix.addFrom(ch, 0, wet, ch, 0, n, 0.7f);
    writeWav(outputPath("l12_ex5_dry.wav"), dry, kSampleRate);
    writeWav(outputPath("l12_ex5_reverb.wav"), ungated, kSampleRate);
    writeWav(outputPath("l12_ex5_gated.wav"), mix, kSampleRate);
    std::printf("輸出：dawlab_output/l12_ex5_dry.wav, l12_ex5_reverb.wav, l12_ex5_gated.wav\n");

    return dawlab::test::summary("l12_ex5");
}
