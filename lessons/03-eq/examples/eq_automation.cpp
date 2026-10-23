// 範例 3：EQ 參數自動化（掃 peak frequency）—— naive vs 平滑版本，量化 click
//
// 示範什麼：
//   - 一顆 +12 dB、Q = 6 的 peak，中心頻率從 150 Hz 對數掃到 6 kHz，歷時 4 秒（經典的 wah 聲）。
//   - host 每 100 ms 才送一次新的 frequency（像轉 MIDI controller 旋鈕、或解析度很粗的 automation）。
//   - 五種做法：
//       reference  ：每個 sample 都照理想掃頻曲線重算係數（最貴、最平滑，當作標準答案）。
//       naive      ：收到參數就直接換 Biquad 係數，中間不平滑 → 階梯狀的參數 = zipper noise。
//       smooth/1   ：交給 ParametricEq 平滑（100 ms linear ramp），每 1 個 sample 重算係數。
//       smooth/32  ：同上，但每 32 個 sample 重算一次係數（ParametricEq 的預設值）。
//       smooth/512 ：同上，每 512 個 sample（= 一個 block）才重算一次。
//   - 量化 click：分析訊號用 1 kHz 正弦波（振幅 0.25）。peak 掃過 1 kHz 時，輸出的「音量包絡」
//     應該平滑地起伏；把輸出切成 1 ms 的小框、算每框的 RMS（dB），相鄰兩框的 dB 差就是
//     「每毫秒的音量跳變」。平滑的掃頻每毫秒只變零點幾 dB；一次跳好幾 dB 就是聽得到的喀聲。
//   - 試聽訊號用 110 Hz 的 band-limited 鋸齒波（30 個諧波），比較像樂器。
//   - 第 3 部分：「開關 band」與「換 band type」這種沒辦法內插的改變。naive 做法（直接切換 Biquad）
//     vs ParametricEq（mix 淡出 → 切換 → 淡入）。量化方式是「正弦波預測殘差」：
//     純正弦波滿足 y[n] = 2cos(ω)·y[n-1] − y[n-2]，所以 r[n] = y[n] − 2cos(ω)·y[n-1] + y[n-2]
//     對穩定的正弦波是 0；振幅或相位「慢慢」變時 r 很小，瞬間跳變（click）時 r 會突然變大。
// 怎麼執行：l03_eq_automation（不需參數）
// 輸出（build 目錄/dawlab_output/）：
//   l03_sweep_sine_naive.wav / l03_sweep_sine_smooth.wav（分析用的 1 kHz 正弦波）
//   l03_sweep_saw_reference.wav / l03_sweep_saw_naive.wav / l03_sweep_saw_smooth.wav（鋸齒波 wah）
//   l03_switch_naive.wav / l03_switch_smooth.wav（第 3 部分：band 開關與換 type）
// 該聽到什麼：naive 版在 peak 掃過訊號頻率附近有規律的「噠噠噠」（每 100 ms 一次，等於 10 Hz 的
//   顫動）；smooth 與 reference 版是連續、滑順的 wah。正弦波版本差異最明顯。
//   最後用 DAWLAB_CHECK 自我檢查（平滑版沒有 > 1 dB/ms 的跳變、開關 / 換 type 的殘差比 naive 小 5 倍以上），
//   全部成立時印 [PASS]；不成立會回傳非 0，ctest 判定失敗。

#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>
#include <dawlab/dsp/ParametricEq.h>

#include <cstdio>
#include <vector>

using namespace dawlab;
using namespace dawlab::dsp;

namespace {

constexpr double kSr = 48000.0;
constexpr int kBlockSize = 512;
constexpr double kSeconds = 4.0;
constexpr double kFreqStart = 150.0, kFreqEnd = 6000.0;
constexpr float kPeakGainDb = 12.0f, kPeakQ = 6.0f;
constexpr double kAutomationIntervalSec = 0.1; // host 每 100 ms 送一次參數
constexpr double kToneHz = 1000.0;             // 分析用正弦波
constexpr double kFrameMs = 1.0;               // 包絡分析的框長
constexpr float kJumpThresholdDb = 1.0f;       // 1 ms 內跳超過 1 dB 就算一次「喀」

// band-limited 鋸齒波：x = (2/π) Σ (-1)^(k+1) sin(2π k f t) / k，k = 1..harmonics
AudioBuffer makeSaw(double f0, int harmonics, double seconds, float amplitude) {
    AudioBuffer b(1, secondsToSamples(seconds, kSr));
    for (int i = 0; i < b.getNumSamples(); ++i) {
        double v = 0.0;
        for (int k = 1; k <= harmonics; ++k)
            v += ((k % 2) ? 1.0 : -1.0) * std::sin(kTwoPi * k * f0 * i / kSr) / k;
        b.setSample(0, i, static_cast<float>(amplitude * v * 2.0 / kPi));
    }
    return b;
}

// 掃頻曲線：對數插值（每秒走過一樣多的 octave）
double sweepFrequency(double t) {
    return kFreqStart * std::pow(kFreqEnd / kFreqStart, std::clamp(t / kSeconds, 0.0, 1.0));
}

// reference：逐 sample 重算係數（理想掃頻）
AudioBuffer runReference(const AudioBuffer& input) {
    AudioBuffer out = input;
    Biquad bq;
    bq.prepare(kSr, kBlockSize, 1);
    float* d = out.getWritePointer(0);
    for (int i = 0; i < out.getNumSamples(); ++i) {
        bq.setCoefficients(BiquadCoefficients::makePeak(kSr, sweepFrequency(i / kSr), kPeakQ, kPeakGainDb));
        d[i] = bq.processSample(0, d[i]);
    }
    return out;
}

// naive：每 100 ms 直接換係數，中間不平滑
AudioBuffer runNaive(const AudioBuffer& input) {
    AudioBuffer out = input;
    const int n = out.getNumSamples();
    const int automationInterval = secondsToSamples(kAutomationIntervalSec, kSr);
    Biquad bq;
    bq.prepare(kSr, kBlockSize, 1);
    float* d = out.getWritePointer(0);
    for (int start = 0; start < n; start += kBlockSize) {
        const int len = std::min(kBlockSize, n - start);
        for (int idx = start; idx < start + len; ++idx) {
            if (idx % automationInterval == 0)
                bq.setCoefficients(BiquadCoefficients::makePeak(kSr, sweepFrequency(idx / kSr), kPeakQ, kPeakGainDb));
            d[idx] = bq.processSample(0, d[idx]);
        }
    }
    return out;
}

// smooth：模擬 host 在 block 之間、每 100 ms 把新的 frequency 送給 ParametricEq，然後整塊處理
AudioBuffer runSmoothed(const AudioBuffer& input, int coefficientUpdateInterval) {
    AudioBuffer out = input;
    const int n = out.getNumSamples();
    const int automationInterval = secondsToSamples(kAutomationIntervalSec, kSr);
    ParametricEq eq;
    eq.setSmoothingSeconds(kAutomationIntervalSec); // ramp 跟自動化間隔一樣長，剛好銜接下一次更新
    eq.setCoefficientUpdateInterval(coefficientUpdateInterval);
    eq.prepare(kSr, kBlockSize, 1);
    eq.setBand(0, {EqBandType::Peak, static_cast<float>(kFreqStart), kPeakQ, kPeakGainDb, true});
    eq.reset();
    AudioBlock whole(out);
    for (int start = 0; start < n; start += kBlockSize) {
        const int len = std::min(kBlockSize, n - start);
        for (int idx = start; idx < start + len; ++idx)
            if (idx % automationInterval == 0)
                eq.setBandFrequency(0, static_cast<float>(sweepFrequency(idx / kSr)));
        eq.process(whole.getSubBlock(start, len));
    }
    return out;
}

struct EnvelopeStats {
    float maxJumpDb = 0.0f; // 相鄰 1 ms 框之間最大的 RMS 變化
    int jumpCount = 0;      // 變化超過門檻的次數
    float peakEnvDb = -100.0f;
    double peakAtMs = 0.0;
};

EnvelopeStats analyzeEnvelope(const AudioBuffer& y) {
    EnvelopeStats s;
    const float* x = y.getReadPointer(0);
    const int frameLen = secondsToSamples(kFrameMs * 0.001, kSr);
    const int frames = y.getNumSamples() / frameLen;
    float prevDb = gainToDb(rms(x, frameLen));
    for (int f = 1; f < frames; ++f) {
        const float db = gainToDb(rms(x + f * frameLen, frameLen));
        const float jump = std::abs(db - prevDb);
        s.maxJumpDb = std::max(s.maxJumpDb, jump);
        if (jump > kJumpThresholdDb)
            ++s.jumpCount;
        if (db > s.peakEnvDb) {
            s.peakEnvDb = db;
            s.peakAtMs = f * kFrameMs;
        }
        prevDb = db;
    }
    return s;
}

void printRow(const char* name, const AudioBuffer& y) {
    const EnvelopeStats s = analyzeEnvelope(y);
    std::printf("  %-11s     %5.2f          %3d          %+6.2f @ %4.0f ms\n", name, s.maxJumpDb, s.jumpCount,
                s.peakEnvDb, s.peakAtMs);
}

// ---- 第 3 部分：開關 band、換 type ----
// 輸入 1 kHz 正弦波（振幅 0.25）。事件（都在 block 邊界上）：
//   block 24（0.256 s）打開一顆 LowCut @ 2 kHz（1 kHz 處約 -12 dB，而且相位大幅偏移）
//   block 72（0.768 s）把它換成 Peak +6 dB @ 1 kHz（換 type）
//   block 120（1.28 s）關掉
constexpr double kSwitchSeconds = 1.6;
constexpr int kEventBlocks[3] = {24, 72, 120};

EqBand switchEventBand(int event) {
    if (event == 0)
        return {EqBandType::LowCut, 2000.0f, 0.707f, 0.0f, true};
    if (event == 1)
        return {EqBandType::Peak, 1000.0f, 1.0f, 6.0f, true};
    return {EqBandType::Peak, 1000.0f, 1.0f, 6.0f, false};
}

// naive：一顆 Biquad，事件發生時直接換係數 / 直接 bypass
AudioBuffer runSwitchNaive(const AudioBuffer& input) {
    AudioBuffer out = input;
    Biquad bq;
    bq.prepare(kSr, kBlockSize, 1);
    bool enabled = false;
    float* d = out.getWritePointer(0);
    const int n = out.getNumSamples();
    for (int start = 0, block = 0; start < n; start += kBlockSize, ++block) {
        for (int e = 0; e < 3; ++e)
            if (block == kEventBlocks[e]) {
                const EqBand band = switchEventBand(e);
                enabled = band.enabled;
                bq.setCoefficients(ParametricEq::makeCoefficients(kSr, band));
            }
        const int len = std::min(kBlockSize, n - start);
        for (int i = start; i < start + len; ++i)
            d[i] = enabled ? bq.processSample(0, d[i]) : d[i];
    }
    return out;
}

// ParametricEq：同樣的事件交給 setBand()，由它負責淡出 / 切換 / 淡入
AudioBuffer runSwitchSmooth(const AudioBuffer& input) {
    AudioBuffer out = input;
    ParametricEq eq;
    eq.prepare(kSr, kBlockSize, 1);
    AudioBlock whole(out);
    const int n = out.getNumSamples();
    for (int start = 0, block = 0; start < n; start += kBlockSize, ++block) {
        for (int e = 0; e < 3; ++e)
            if (block == kEventBlocks[e])
                eq.setBand(0, switchEventBand(e));
        eq.process(whole.getSubBlock(start, std::min(kBlockSize, n - start)));
    }
    return out;
}

// 正弦波預測殘差的最大值：r[n] = y[n] − 2cos(ω)·y[n-1] + y[n-2]
// （只看 [from, to) 這一段；to < 0 代表到結尾）
float maxSineResidual(const AudioBuffer& y, double freq, int from = 2, int to = -1) {
    const float* d = y.getReadPointer(0);
    const double c = 2.0 * std::cos(kTwoPi * freq / kSr);
    if (to < 0 || to > y.getNumSamples())
        to = y.getNumSamples();
    double m = 0.0;
    for (int i = std::max(2, from); i < to; ++i)
        m = std::max(m, std::abs(d[i] - c * d[i - 1] + d[i - 2]));
    return static_cast<float>(m);
}

} // namespace

int main() {
    std::printf("=== 第 03 章 範例 3：掃 peak frequency，naive vs smooth vs reference ===\n");
    std::printf("\npeak +%.0f dB Q=%.0f，%.0f Hz -> %.0f Hz，%.0f 秒；host 每 %.0f ms 送一次參數\n", kPeakGainDb, kPeakQ,
                kFreqStart, kFreqEnd, kSeconds, kAutomationIntervalSec * 1000.0);

    // ---- 1. 分析：1 kHz 正弦波的音量包絡 ----
    const AudioBuffer sine = makeSine(kToneHz, kSr, kSeconds, 0.25f, 1);
    const AudioBuffer sineRef = runReference(sine);
    const AudioBuffer sineNaive = runNaive(sine);
    const AudioBuffer sineSmooth1 = runSmoothed(sine, 1);
    const AudioBuffer sineSmooth32 = runSmoothed(sine, ParametricEq::kDefaultUpdateInterval);
    const AudioBuffer sineSmooth512 = runSmoothed(sine, kBlockSize);

    std::printf("\n[分析] 1 kHz 正弦波，每 %.0f ms 一框算 RMS，看相鄰框之間的音量跳變\n", kFrameMs);
    std::printf("  version      max jump(dB/ms)  跳變>%.0f dB 次數   包絡最高點\n", double(kJumpThresholdDb));
    printRow("reference", sineRef);
    printRow("naive", sineNaive);
    printRow("smooth/1", sineSmooth1);
    printRow("smooth/32", sineSmooth32);
    printRow("smooth/512", sineSmooth512);
    std::printf("\n  讀法：\n");
    std::printf("  - reference 每毫秒只變零點幾 dB：這是 +12 dB、Q=6 的 peak 用 1.33 oct/s 掃過 1 kHz 時\n");
    std::printf("    「音樂上」應有的包絡變化速度。\n");
    std::printf("  - naive 一次跳好幾 dB（參數每 100 ms 跳一階，音量也跟著跳一階）：這就是 zipper noise。\n");
    std::printf("    它的包絡最高點也比 reference 低——階梯剛好跨過了 peak 的中心。\n");
    std::printf("  - smooth/1 與 smooth/32 跟 reference 一樣平滑，只是晚了約一個自動化間隔（約 90–100 ms）到達最高點：\n");
    std::printf("    ramp 是「追」著目標走的，這個延遲聽不出來。\n");
    std::printf("  - smooth/512 每個 block 才換一次係數，跳變介於兩者之間：更新間隔是 CPU 與平滑度的取捨。\n");

    // ---- 2. 試聽：鋸齒波 wah ----
    const AudioBuffer saw = makeSaw(110.0, 30, kSeconds, 0.4f);
    const AudioBuffer sawRef = runReference(saw);
    const AudioBuffer sawNaive = runNaive(saw);
    const AudioBuffer sawSmooth = runSmoothed(saw, ParametricEq::kDefaultUpdateInterval);
    std::printf("\n[試聽] 110 Hz 鋸齒波（30 個諧波）：peak(dBFS) reference %+.2f / naive %+.2f / smooth %+.2f\n",
                gainToDb(peakAbs(sawRef)), gainToDb(peakAbs(sawNaive)), gainToDb(peakAbs(sawSmooth)));

    // ---- 3. 開關 band 與換 type ----
    const AudioBuffer tone = makeSine(kToneHz, kSr, kSwitchSeconds, 0.25f, 1);
    const AudioBuffer switchNaive = runSwitchNaive(tone);
    const AudioBuffer switchSmooth = runSwitchSmooth(tone);
    const float resDry = maxSineResidual(tone, kToneHz);
    const float resNaive = maxSineResidual(switchNaive, kToneHz);
    const float resSmooth = maxSineResidual(switchSmooth, kToneHz);
    std::printf("\n[開關 / 換 type] 1 kHz 正弦波（振幅 0.25）：0.256 s 開 LowCut@2k、0.768 s 換成 Peak +6 dB@1k、1.28 s 關掉\n");
    std::printf("  正弦波預測殘差 max|r|（越小越乾淨；以振幅 0.25 為 0 dB）\n");
    std::printf("  原始正弦波                 %.6f  (%+6.1f dB)\n", resDry, gainToDb(resDry / 0.25f));
    std::printf("  naive（直接切換）          %.6f  (%+6.1f dB)\n", resNaive, gainToDb(resNaive / 0.25f));
    std::printf("  ParametricEq（淡出 / 淡入） %.6f  (%+6.1f dB)\n", resSmooth, gainToDb(resSmooth / 0.25f));
    std::printf("  各事件附近（事件前 5 ms 到事件後 100 ms）的 max|r|：\n");
    std::printf("      時間       naive      ParametricEq   事件\n");
    const char* eventNames[3] = {"開 LowCut@2k", "換成 Peak +6 dB@1k", "關掉"};
    for (int e = 0; e < 3; ++e) {
        const int at = kEventBlocks[e] * kBlockSize;
        const int from = at - secondsToSamples(0.005, kSr), to = at + secondsToSamples(0.1, kSr);
        std::printf("    %6.3f s   %.6f   %.6f       %s\n", at / kSr, maxSineResidual(switchNaive, kToneHz, from, to),
                    maxSineResidual(switchSmooth, kToneHz, from, to), eventNames[e]);
    }
    std::printf("  -> naive 每個事件都有一個尖銳的殘差（= 波形瞬間轉折，聽得到的 click）；\n");
    std::printf("     ParametricEq 把同樣的改變攤在約 20–40 ms 裡，殘差小了一個數量級以上。\n");
    writeWav(outputPath("l03_switch_naive.wav"), switchNaive, kSr);
    writeWav(outputPath("l03_switch_smooth.wav"), switchSmooth, kSr);

    writeWav(outputPath("l03_sweep_sine_naive.wav"), sineNaive, kSr);
    writeWav(outputPath("l03_sweep_sine_smooth.wav"), sineSmooth32, kSr);
    writeWav(outputPath("l03_sweep_saw_reference.wav"), sawRef, kSr);
    writeWav(outputPath("l03_sweep_saw_naive.wav"), sawNaive, kSr);
    writeWav(outputPath("l03_sweep_saw_smooth.wav"), sawSmooth, kSr);
    std::printf("\n輸出：dawlab_output/l03_sweep_sine_naive.wav, l03_sweep_sine_smooth.wav,\n"
                "      l03_sweep_saw_reference.wav, l03_sweep_saw_naive.wav, l03_sweep_saw_smooth.wav,\n"
                "      l03_switch_naive.wav, l03_switch_smooth.wav\n");

    // 自我檢查：上面「讀法」說的結論要真的成立，否則 ctest 判定失敗
    std::printf("\n");
    const EnvelopeStats stNaive = analyzeEnvelope(sineNaive);
    for (const AudioBuffer* y : {&sineSmooth1, &sineSmooth32}) {
        const EnvelopeStats st = analyzeEnvelope(*y);
        DAWLAB_CHECK(st.maxJumpDb < 0.5f); // 平滑版每毫秒只變零點幾 dB
        DAWLAB_CHECK(st.jumpCount == 0);
    }
    DAWLAB_CHECK(stNaive.jumpCount > 0); // naive 確實有跳變（對照組要成立，比較才有意義）
    DAWLAB_CHECK(resSmooth < resNaive / 5.0f);
    DAWLAB_CHECK(!hasNanOrInf(sawSmooth));
    return dawlab::test::summary("l03_eq_automation");
}
