// 範例 2：attack / release 的 step response。
//
// 示範什麼：
//   (A) AttackReleaseFilter 單獨對一個「dB step」的反應：量 t63（到 63.2%）與 t90（到 90%），
//       驗證 α = exp(-1/(τ·fs)) 的 τ 就是「到 63.2% 的時間」、t90 = ln(10)·τ ≈ 2.30τ。
//   (B) 把 Compressor 餵 tone bursts（-30 → -10 → -30 dBFS，各 0.5 秒），印出 gain reduction 隨時間的變化，
//       量測 attack / release 時間，並比較：
//         - Decoupled vs Branching 平滑（週期訊號下 Branching 會低估 peak）
//         - Peak vs RMS detector
//         - log(dB) domain 平滑（Compressor 的做法）vs linear domain 平滑（EnvelopeFollower → 靜態曲線）
//       最後把 B1、B2、B4 的 release 曲線疊成一張 ASCII 圖（GR vs 時間）。
// 怎麼執行：直接執行 l04_step_response。
// 輸出檔案：dawlab_output/l04_step_in.wav、l04_step_out.wav（tone bursts 壓縮前 / 後）。
// 該聽到什麼：第二段大聲的音在剛進來的前 10 ms 會「衝」出來（attack 還沒壓下去），
//            然後被壓到比較小聲；回到小聲段時，前 100 ms 音量會慢慢「浮」回來（release）。

#include <dawlab/Common.h>
#include <dawlab/dsp/Compressor.h>
#include <dawlab/dsp/EnvelopeFollower.h>

#include <cstdio>
#include <vector>

using namespace dawlab;
using dawlab::dsp::AttackReleaseFilter;
using dawlab::dsp::Compressor;
using dawlab::dsp::DetectorMode;
using dawlab::dsp::EnvelopeFollower;
using dawlab::dsp::SmoothingMode;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr float kThresholdDb = -20.0f;
constexpr float kRatio = 4.0f;
constexpr float kAttackMs = 10.0f;
constexpr float kReleaseMs = 100.0f;
constexpr double kSegmentSeconds = 0.5;

// 在 timeline 裡找「第一次 >= target」的時間（毫秒）；從 startIndex 開始找
double firstTimeReaching(const std::vector<float>& v, int startIndex, float target, bool rising) {
    for (size_t i = static_cast<size_t>(startIndex); i < v.size(); ++i) {
        if (rising ? v[i] >= target : v[i] <= target)
            return samplesToMs(static_cast<double>(i) - startIndex, kSampleRate);
    }
    return -1.0;
}

// 把 tone bursts 丟進 compressor，逐 block 處理，並記錄每個 sample 的 gain（由輸出 / 輸入算出的 GR）
struct Run {
    std::vector<float> grDb; // 每個 sample 的 gain reduction（dB，由 getGainReductionDb 以 block 為單位是不夠細的，
                             // 這裡用輸入 / 輸出的比值反推）
    AudioBuffer output;
};

Run runCompressor(const AudioBuffer& input, Compressor& comp) {
    Run run;
    run.output = input;
    const int n = input.getNumSamples();
    run.grDb.assign(static_cast<size_t>(n), 0.0f);
    AudioBlock whole(run.output);
    for (int start = 0; start < n; start += kBlockSize) {
        const int len = std::min(kBlockSize, n - start);
        comp.process(whole.getSubBlock(start, len));
    }
    // 由 |y| / |x| 反推 gain：正弦波過零點附近除不準，所以用「每 sample 取最近 1 ms 視窗的 peak 比」
    const int win = msToSamples(1.0, kSampleRate);
    for (int i = 0; i < n; ++i) {
        const int a = std::max(0, i - win + 1);
        const float px = peakAbs(input.getReadPointer(0) + a, i - a + 1);
        const float py = peakAbs(run.output.getReadPointer(0) + a, i - a + 1);
        run.grDb[static_cast<size_t>(i)] = (px > 1e-6f && py > 1e-9f) ? gainToDb(px) - gainToDb(py) : 0.0f;
    }
    return run;
}

void printTimeline(const char* title, const std::vector<float>& gr, int stepStart, int releaseStart) {
    std::printf("\n  %s\n", title);
    std::printf("    attack 階段（t = 0 是大聲段開始）：\n      t(ms):");
    for (int ms : {0, 2, 5, 10, 15, 20, 30, 50, 100, 200})
        std::printf(" %6d", ms);
    std::printf("\n      GR(dB):");
    for (int ms : {0, 2, 5, 10, 15, 20, 30, 50, 100, 200})
        std::printf(" %6.2f", gr[static_cast<size_t>(stepStart + msToSamples(ms, kSampleRate))]);
    std::printf("\n    release 階段（t = 0 是回到小聲段）：\n      t(ms):");
    for (int ms : {0, 20, 50, 100, 150, 200, 300, 400, 490})
        std::printf(" %6d", ms);
    std::printf("\n      GR(dB):");
    for (int ms : {0, 20, 50, 100, 150, 200, 300, 400, 490})
        std::printf(" %6.2f", gr[static_cast<size_t>(releaseStart + msToSamples(ms, kSampleRate))]);
    std::printf("\n");
}

// 把幾條 release 曲線（GR 隨時間）疊在同一張 ASCII 圖上。
// 橫軸：回到小聲段之後 0..490 ms，每欄 10 ms；縱軸：GR，每列 0.5 dB。
// 每條曲線用一個字元表示，兩條以上落在同一格時印 '*'；'-' 是 refLevelDb（B1 的 36.8% 參考線）。
struct Curve {
    char symbol;
    const std::vector<float>* gr;
};

void printReleaseChart(const Curve* curves, int numCurves, int releaseStart, float refLevelDb) {
    constexpr int kCols = 50;    // 0, 10, ..., 490 ms
    constexpr int kRows = 16;    // 8.0 dB 到 0.5 dB；0 dB 那一列當作橫軸
    constexpr float kRowDb = 0.5f;
    const int refRow = static_cast<int>(std::lround(refLevelDb / kRowDb));
    std::printf("\n  release 曲線疊圖（GR vs 時間，t = 0 是回到小聲段）\n");
    for (int row = kRows; row >= 0; --row) {
        if (row % 2 == 0)
            std::printf("  %4.1f |", row * kRowDb);
        else
            std::printf("       |");
        for (int c = 0; c < kCols; ++c) {
            char ch = (row == refRow) ? '-' : ' ';
            if (row == 0)
                ch = '_';
            int hits = 0;
            for (int k = 0; k < numCurves; ++k) {
                const float v = (*curves[k].gr)[static_cast<size_t>(releaseStart + msToSamples(c * 10.0, kSampleRate))];
                if (static_cast<int>(std::lround(v / kRowDb)) == row) {
                    ch = (hits == 0) ? curves[k].symbol : '*';
                    ++hits;
                }
            }
            std::putchar(ch);
        }
        std::printf("\n");
    }
    // 第 c 欄印在第 7 + c 個字元，所以 0 / 100 / 200 ms 的刻度分別對齊第 0 / 10 / 20 欄
    std::printf("       0         100       200       300       400      (ms)\n");
}

void measureAndPrint(const std::vector<float>& gr, int stepStart, int releaseStart, float staticGrDb) {
    // 穩態 GR：取大聲段最後 50 ms 的平均
    double sum = 0.0;
    const int tail = msToSamples(50.0, kSampleRate);
    for (int i = releaseStart - tail; i < releaseStart; ++i)
        sum += gr[static_cast<size_t>(i)];
    const float settled = static_cast<float>(sum / tail);
    const double t63 = firstTimeReaching(gr, stepStart, 0.632f * settled, true);
    const double t90 = firstTimeReaching(gr, stepStart, 0.90f * settled, true);
    const double r37 = firstTimeReaching(gr, releaseStart, 0.368f * settled, false);
    const double r10 = firstTimeReaching(gr, releaseStart, 0.10f * settled, false);
    std::printf("    穩態 GR = %.2f dB（靜態曲線 %.2f dB）；attack: t63 = %.1f ms, t90 = %.1f ms；"
                "release: t37 = %.1f ms, t10 = %.1f ms\n",
                settled, staticGrDb, t63, t90, r37, r10);
}

} // namespace

int main() {
    std::printf("第 04 章 範例 2：attack / release step response（fs = %.0f Hz）\n", kSampleRate);

    // ---------- (A) AttackReleaseFilter 對 dB step 的反應 ----------
    std::printf("\n=== (A) AttackReleaseFilter 單獨測試：目標值 0 → 10 → 0，attack %.0f ms、release %.0f ms ===\n",
                kAttackMs, kReleaseMs);
    const float alphaA = dsp::attackReleaseCoefficient(kAttackMs, kSampleRate);
    const float alphaR = dsp::attackReleaseCoefficient(kReleaseMs, kSampleRate);
    std::printf("  αA = exp(-1/(0.010·48000)) = %.6f，αR = exp(-1/(0.100·48000)) = %.6f\n", alphaA, alphaR);
    std::printf("  反推：τA = %.3f ms，τR = %.3f ms\n", dsp::coefficientToTimeMs(alphaA, kSampleRate),
                dsp::coefficientToTimeMs(alphaR, kSampleRate));
    for (SmoothingMode mode : {SmoothingMode::Branching, SmoothingMode::Decoupled}) {
        AttackReleaseFilter f;
        f.prepare(kSampleRate);
        f.setAttackMs(kAttackMs);
        f.setReleaseMs(kReleaseMs);
        f.setMode(mode);
        const int half = msToSamples(500.0, kSampleRate);
        std::vector<float> y(static_cast<size_t>(2 * half));
        for (int i = 0; i < 2 * half; ++i)
            y[static_cast<size_t>(i)] = f.process(i < half ? 10.0f : 0.0f);
        const double t63 = firstTimeReaching(y, 0, 6.32f, true);
        const double t90 = firstTimeReaching(y, 0, 9.0f, true);
        const double r37 = firstTimeReaching(y, half, 3.68f, false);
        const double r10 = firstTimeReaching(y, half, 1.0f, false);
        std::printf("  %-9s attack: t63 = %6.2f ms (理論 τ = %.0f), t90 = %6.2f ms (理論 ln10·τ = %.2f) | "
                    "release: t37 = %6.2f ms (理論 %.0f), t10 = %6.2f ms (理論 %.2f)\n",
                    mode == SmoothingMode::Branching ? "Branching" : "Decoupled", t63, kAttackMs, t90,
                    kAttackMs * 2.302585, r37, kReleaseMs, r10, kReleaseMs * 2.302585);
    }
    std::printf("  （Decoupled 的 release 多了 attack 濾波器的延遲，所以 t37 ≈ τR + τA 左右）\n");

    // ---------- (B) Compressor 對 tone bursts 的反應 ----------
    const AudioBuffer input = makeToneBursts(1000.0, kSampleRate, {-30.0f, -10.0f, -30.0f}, kSegmentSeconds, 2);
    const int segLen = secondsToSamples(kSegmentSeconds, kSampleRate);
    const int stepStart = segLen;
    const int releaseStart = 2 * segLen;
    const float staticGr = Compressor::computeGainReductionDb(-10.0f, kThresholdDb, kRatio, 0.0f);
    std::printf("\n=== (B) Compressor：1 kHz tone bursts -30 → -10 → -30 dBFS，T = %.0f dB, R = %.0f:1, hard knee, "
                "attack %.0f ms, release %.0f ms ===\n",
                kThresholdDb, kRatio, kAttackMs, kReleaseMs);
    std::printf("  靜態曲線：輸入 -10 dB → 輸出 %.2f dB，GR = %.2f dB\n",
                Compressor::computeOutputDb(-10.0f, kThresholdDb, kRatio, 0.0f), staticGr);

    auto makeComp = [&](SmoothingMode mode, DetectorMode det) {
        Compressor c;
        c.prepare(kSampleRate, kBlockSize, 2);
        c.setThresholdDb(kThresholdDb);
        c.setRatio(kRatio);
        c.setKneeDb(0.0f);
        c.setAttackMs(kAttackMs);
        c.setReleaseMs(kReleaseMs);
        c.setSmoothingMode(mode);
        c.setDetectorMode(det);
        c.setRmsWindowMs(5.0f);
        return c;
    };

    // 給最後的 release 疊圖用
    std::vector<float> grB1, grB2, grB4;
    float settledB1 = 0.0f;

    // B1：預設（Decoupled + Peak）—— 這是會輸出 WAV 的那一組
    {
        Compressor comp = makeComp(SmoothingMode::Decoupled, DetectorMode::Peak);
        Run run = runCompressor(input, comp);
        printTimeline("B1. Decoupled 平滑 + Peak detector（Compressor 預設）", run.grDb, stepStart, releaseStart);
        measureAndPrint(run.grDb, stepStart, releaseStart, staticGr);
        grB1 = run.grDb;
        settledB1 = run.grDb[static_cast<size_t>(releaseStart - 1)];
        std::printf("    getGainReductionDb()（最後一個 block）= %.3f dB\n", comp.getGainReductionDb());
        writeWav(outputPath("l04_step_in.wav"), input, kSampleRate);
        writeWav(outputPath("l04_step_out.wav"), run.output, kSampleRate);
        std::printf("    已輸出 dawlab_output/l04_step_in.wav、l04_step_out.wav\n");
        std::printf("    輸出 peak：第 1 段 %.2f dBFS、第 2 段 %.2f dBFS（前 10 ms 的 overshoot 峰值 %.2f dBFS）\n",
                    gainToDb(peakAbs(run.output.getReadPointer(0), segLen)),
                    gainToDb(peakAbs(run.output.getReadPointer(0) + stepStart + msToSamples(100.0, kSampleRate),
                                     segLen - msToSamples(100.0, kSampleRate))),
                    gainToDb(peakAbs(run.output.getReadPointer(0) + stepStart, msToSamples(10.0, kSampleRate))));
    }

    // B2：Branching 平滑
    {
        Compressor comp = makeComp(SmoothingMode::Branching, DetectorMode::Peak);
        Run run = runCompressor(input, comp);
        printTimeline("B2. Branching 平滑 + Peak detector", run.grDb, stepStart, releaseStart);
        measureAndPrint(run.grDb, stepStart, releaseStart, staticGr);
        grB2 = run.grDb;
        std::printf("    → 正弦波每個週期過零點時瞬間 level 掉到 -inf dB，Branching 會在那段用 release 往下拉，\n"
                    "      所以穩態 GR 低於靜態曲線（低估 peak）；Decoupled 的 peak-hold 不會。\n");
    }

    // B3：RMS detector
    {
        Compressor comp = makeComp(SmoothingMode::Decoupled, DetectorMode::Rms);
        Run run = runCompressor(input, comp);
        printTimeline("B3. Decoupled 平滑 + RMS detector（視窗 5 ms）", run.grDb, stepStart, releaseStart);
        measureAndPrint(run.grDb, stepStart, releaseStart,
                        Compressor::computeGainReductionDb(-10.0f - 3.01f, kThresholdDb, kRatio, 0.0f));
        std::printf("    → 正弦波的 RMS 比 peak 低 3.01 dB，所以 GR 以「-13 dB 輸入」來算 = %.2f dB；\n"
                    "      RMS 視窗本身也是一層平滑，attack 會比標稱值慢一點。\n",
                    Compressor::computeGainReductionDb(-13.01f, kThresholdDb, kRatio, 0.0f));
    }

    // B4：linear domain 平滑（EnvelopeFollower → 靜態曲線），對照 log domain
    {
        EnvelopeFollower env;
        env.prepare(kSampleRate);
        env.setMode(DetectorMode::Peak);
        env.setSmoothingMode(SmoothingMode::Decoupled);
        env.setAttackMs(kAttackMs);
        env.setReleaseMs(kReleaseMs);
        const int n = input.getNumSamples();
        std::vector<float> gr(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            const float e = env.processSample(input.getSample(0, i));
            gr[static_cast<size_t>(i)] = Compressor::computeGainReductionDb(gainToDb(e), kThresholdDb, kRatio, 0.0f);
        }
        printTimeline("B4. 對照：linear domain 平滑（EnvelopeFollower 的 envelope 再丟進靜態曲線）", gr, stepStart,
                      releaseStart);
        measureAndPrint(gr, stepStart, releaseStart, staticGr);
        grB4 = gr;
        std::printf("    → release 時 envelope 在線性域指數衰減 = 在 dB 上「等速下降」（%.1f dB/s），\n"
                    "      GR 直線掉到 0 後就停住；log domain 的 GR 則是指數衰減，尾巴拖很長、聽起來比較自然。\n",
                    20.0 * std::log10(std::exp(1.0)) / (kReleaseMs * 0.001));
    }

    // ---------- release 曲線疊圖 ----------
    const Curve curves[] = {{'1', &grB1}, {'2', &grB2}, {'4', &grB4}};
    printReleaseChart(curves, 3, releaseStart, 0.368f * settledB1);
    std::printf("  1 = B1 decoupled（log domain）、2 = B2 branching、4 = B4 linear domain、* = 重疊；\n"
                "  '-' 是 B1 穩態 GR 的 36.8%%（%.2f dB）：B1 約在 τR + τA ≈ 110 ms 穿過它。\n",
                0.368f * settledB1);
    std::printf("  B1、B2 是指數衰減（尾巴拖長、慢慢收到 0）；B4 在 dB 上是直線下降，約 150 ms 就直接歸 0。\n");

    return 0;
}
