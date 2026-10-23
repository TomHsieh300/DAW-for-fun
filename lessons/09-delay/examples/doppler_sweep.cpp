// 範例 3：delay time 掃動 —— Doppler（pitch shift）效應與 click
//
// 示範什麼：
//   1. delay time 連續變化時，讀取指標在「追」或「逃離」寫入指標，輸出的 pitch 會偏移：
//        f_out = f_in × (1 − dτ/dt)        τ = delay time（秒）
//      delay 從 10 ms 線性增加到 250 ms、花 2 秒 → dτ/dt = 0.12 → 440 Hz 變成 387.2 Hz；
//      反方向（250 → 10 ms）→ 492.8 Hz。本範例用 zero-crossing 量測實際頻率來驗證。
//   2. 如果 delay time 不平滑、直接跳到新值，波形會出現不連續（click）。
//      我們量測相鄰 sample 的最大差值：440 Hz、振幅 0.5 的正弦最陡斜率只有 0.029 / sample，
//      跳變版本會出現遠大於它的斷層（本例約 0.27；大小取決於切換瞬間新舊兩個讀取位置的波形差）。
//   3. self-check：很長的 delay 只改一點點（1900 → 1901 ms、15000 → 15001 ms）也不能 click。
//      讀取位置在 9 萬、72 萬 sample 附近時 float 的精度不夠（ulp = 1/128、1/16 sample），
//      所以 Delay 的 delay-time ramp 用 double 計算（見 Delay.h 的 DelayRamp）。
//      這一段用 DAWLAB_CHECK 驗證：ramp 期間相鄰 sample 的最大差值 < 1.5 × 正弦本身的斜率上限。
//
// 怎麼執行：不需要參數，直接執行 l09_doppler_sweep。
// 輸出檔案（dawlab_output/）：
//   l09_doppler_smooth.wav   440 Hz 正弦，delay time 平滑掃動（聽到 pitch 往下再往上滑）
//   l09_doppler_abrupt.wav   同樣的變化但 ramp = 0（聽到兩聲 click，pitch 不變）
// 該聽到什麼：smooth 版本像磁帶變速；abrupt 版本是「喀」的一聲。

#include <dawlab/Common.h>
#include <dawlab/dsp/Delay.h>

#include <cmath>
#include <cstdio>

using dawlab::dsp::Delay;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr double kToneHz = 440.0;
constexpr float kStartMs = 10.0f;
constexpr float kEndMs = 250.0f;
constexpr double kRampSeconds = 2.0;

// 用正向 zero-crossing 估計一段訊號的頻率：f ≈ (crossings − 1) / (最後一次 − 第一次)
double estimateFrequency(const float* x, int n) {
    int first = -1, last = -1, count = 0;
    for (int i = 1; i < n; ++i) {
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) {
            if (first < 0)
                first = i;
            last = i;
            ++count;
        }
    }
    if (count < 2)
        return 0.0;
    return (count - 1) * kSampleRate / static_cast<double>(last - first);
}

float maxSampleStep(const float* x, int n) {
    float m = 0.0f;
    for (int i = 1; i < n; ++i)
        m = std::max(m, std::abs(x[i] - x[i - 1]));
    return m;
}

// 跑整段：t = 1 s 時把 delay 設成 kEndMs，t = 4 s 時設回 kStartMs（在事件點切開 block）
dawlab::AudioBuffer render(double rampSeconds) {
    dawlab::AudioBuffer buffer = dawlab::makeSine(kToneHz, kSampleRate, 7.0, 0.5f, 1);
    Delay delay;
    delay.setMaxDelayMs(1000.0f);
    delay.prepare(kSampleRate, kBlockSize, 1);
    delay.setDelayRampSeconds(rampSeconds);
    delay.setDelayMs(kStartMs);
    delay.setFeedback(0.0f);
    delay.setMix(1.0f); // 只聽 wet，才看得到純粹的 pitch 變化
    delay.reset();

    const int upAt = dawlab::secondsToSamples(1.0, kSampleRate);
    const int downAt = dawlab::secondsToSamples(4.0, kSampleRate);
    dawlab::AudioBlock whole(buffer);
    const int total = buffer.getNumSamples();
    // 48000 不是 512 的倍數：參數改變的時間點可能落在 block 中間。
    // 做法和 host 處理 automation 一樣——把 block 在事件點切開，前半段用舊參數、後半段用新參數。
    int start = 0;
    while (start < total) {
        if (start == upAt)
            delay.setDelayMs(kEndMs);
        if (start == downAt)
            delay.setDelayMs(kStartMs);
        int n = std::min(kBlockSize, total - start);
        for (int event : {upAt, downAt})
            if (start < event && event < start + n)
                n = event - start; // 切到事件點為止
        delay.process(whole.getSubBlock(start, n));
        start += n;
    }
    return buffer;
}

// 長 delay 的小幅改變：delay 固定在 baseMs，一段時間後改成 baseMs + 1（預設 50 ms ramp），
// 回傳改變前 0.05 秒到改變後 0.15 秒之間相鄰 sample 的最大差值（ramp 本身只有 0.05 秒）。
float longDelayNudgeMaxStep(float baseMs, float maxDelayMs) {
    Delay delay;
    delay.setMaxDelayMs(maxDelayMs);
    delay.prepare(kSampleRate, kBlockSize, 1);
    delay.setDelayMs(baseMs);
    delay.setFeedback(0.0f);
    delay.setMix(1.0f);
    delay.reset();

    // 等 delay line 填滿正弦（baseMs 之後 wet 才有聲音），再過 100 個 block 才改值
    const int changeAt = dawlab::msToSamples(static_cast<double>(baseMs), kSampleRate) + kBlockSize * 100;
    const int total = changeAt + dawlab::secondsToSamples(0.5, kSampleRate);
    dawlab::AudioBuffer buffer = dawlab::makeSine(kToneHz, kSampleRate, total / kSampleRate + 0.01, 0.5f, 1);
    dawlab::AudioBlock whole(buffer);
    int start = 0;
    while (start < total) {
        if (start == changeAt)
            delay.setDelayMs(baseMs + 1.0f);
        int n = std::min(kBlockSize, total - start);
        if (start < changeAt && changeAt < start + n)
            n = changeAt - start; // 在事件點切開 block
        delay.process(whole.getSubBlock(start, n));
        start += n;
    }
    const int from = changeAt - dawlab::secondsToSamples(0.05, kSampleRate);
    return maxSampleStep(buffer.getReadPointer(0) + from, dawlab::secondsToSamples(0.2, kSampleRate));
}

} // namespace

int main() {
    std::printf("=== 第 09 章 範例 3：delay time 掃動的 Doppler 效應 ===\n\n");

    const double slope = (kEndMs - kStartMs) * 0.001 / kRampSeconds; // dτ/dt（無單位）
    const double expectDown = kToneHz * (1.0 - slope);
    const double expectUp = kToneHz * (1.0 + slope);
    std::printf("delay %g ms -> %g ms，花 %.1f 秒：dτ/dt = %.3f\n", kStartMs, kEndMs, kRampSeconds, slope);
    std::printf("理論：往上掃時 f_out = %.1f × (1 − %.3f) = %.1f Hz；往回掃時 = %.1f Hz\n\n", kToneHz, slope,
                expectDown, expectUp);

    // 注意：delay 在 1 s 與 4 s 時改變，各花 kRampSeconds 秒到達新值
    dawlab::AudioBuffer smooth = render(kRampSeconds);
    dawlab::AudioBuffer abrupt = render(0.0);

    auto window = [&](const dawlab::AudioBuffer& b, double t0, double t1) {
        const int s = dawlab::secondsToSamples(t0, kSampleRate);
        const int n = dawlab::secondsToSamples(t1 - t0, kSampleRate);
        return estimateFrequency(b.getReadPointer(0) + s, n);
    };
    std::printf("平滑版本的量測頻率（zero-crossing）：\n");
    std::printf("  0.3–0.8 s（delay 固定 %g ms）   : %.1f Hz（預期 %.1f）\n", kStartMs, window(smooth, 0.3, 0.8),
                kToneHz);
    std::printf("  1.5–2.5 s（delay 往上掃）        : %.1f Hz（預期 %.1f）\n", window(smooth, 1.5, 2.5), expectDown);
    std::printf("  3.2–3.8 s（delay 固定 %g ms）  : %.1f Hz（預期 %.1f）\n", kEndMs, window(smooth, 3.2, 3.8),
                kToneHz);
    std::printf("  4.5–5.5 s（delay 往回掃）        : %.1f Hz（預期 %.1f）\n\n", window(smooth, 4.5, 5.5), expectUp);

    const double maxSlope = 0.5 * dawlab::kTwoPi * kToneHz / kSampleRate; // 正弦的最大 |Δy|
    auto stepAround = [&](const dawlab::AudioBuffer& b, double t) {
        const int s = dawlab::secondsToSamples(t, kSampleRate) - 2; // 從改變前 2 個 sample 開始看
        return maxSampleStep(b.getReadPointer(0) + s, dawlab::secondsToSamples(0.1, kSampleRate));
    };
    std::printf("相鄰 sample 的最大差值（正弦本身的上限 = %.4f）：\n", maxSlope);
    std::printf("  平滑版本 1.0–1.1 s: %.4f   4.0–4.1 s: %.4f（往回掃時 pitch 升高 %.0f%%，上限變成 %.4f）\n",
                stepAround(smooth, 1.0), stepAround(smooth, 4.0), slope * 100.0, maxSlope * (1.0 + slope));
    std::printf("  跳變版本 1.0–1.1 s: %.4f   4.0–4.1 s: %.4f   <- 遠大於上限 = click\n\n",
                stepAround(abrupt, 1.0), stepAround(abrupt, 4.0));

    // ---- 3. 長 delay 的小幅改變：不能因為浮點精度而 click ----------------------------
    // 改 1 ms、ramp 50 ms → dτ/dt = 0.02，正弦的斜率上限只會變成 1.02 倍。
    const float step1900 = longDelayNudgeMaxStep(1900.0f, 2000.0f);
    const float step15000 = longDelayNudgeMaxStep(15000.0f, 16000.0f);
    std::printf("長 delay 改 1 ms（50 ms ramp）的相鄰 sample 最大差值（上限 %.4f × 1.02 = %.4f）：\n", maxSlope,
                maxSlope * 1.02);
    std::printf("  1900 -> 1901 ms  : %.4f\n", step1900);
    std::printf("  15000 -> 15001 ms: %.4f\n", step15000);
    std::printf("  （若 ramp 用 float 逐 sample 累加，讀取位置的誤差會一路累積、ramp 結束時跳回目標 = click）\n\n");
    DAWLAB_CHECK(step1900 < 1.5 * maxSlope);
    DAWLAB_CHECK(step15000 < 1.5 * maxSlope);
    // 第 2 節的結論也一併驗證：平滑版本沒有斷層、跳變版本有
    DAWLAB_CHECK(stepAround(smooth, 1.0) < 1.1 * maxSlope);
    DAWLAB_CHECK(stepAround(abrupt, 1.0) > 5.0 * maxSlope);
    {
        // 防呆：prepare() 之前呼叫 reset() / process() 不能 crash（host 可能在 prepare 前就 reset 一次）
        Delay unprepared;
        unprepared.reset();
        dawlab::AudioBuffer tiny(1, 16);
        tiny.getWritePointer(0)[0] = 1.0f;
        unprepared.process(dawlab::AudioBlock(tiny));
        DAWLAB_CHECK(tiny.getReadPointer(0)[0] == 1.0f); // 未 prepare：原封不動
    }

    dawlab::writeWav(dawlab::outputPath("l09_doppler_smooth.wav"), smooth, kSampleRate);
    dawlab::writeWav(dawlab::outputPath("l09_doppler_abrupt.wav"), abrupt, kSampleRate);
    std::printf("已輸出 dawlab_output/l09_doppler_smooth.wav, l09_doppler_abrupt.wav\n\n");
    return dawlab::test::summary("l09_doppler_sweep");
}
