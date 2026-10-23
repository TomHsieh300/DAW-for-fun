// 練習 1 解答（計算題的數字驗證）：BPM → ms、feedback 的衰減時間
//
// 計算題的答案寫在 solutions/README.md；這支程式把同樣的數字「跑一次」驗證：
//   1. Delay::noteToMs() 對各種音符長度（含附點、三連音）的換算。
//   2. ms → sample（48 kHz）。
//   3. feedback g 時第 k 個 echo 的振幅 = g^(k−1)：用 impulse 打進 Delay，直接讀出每個 echo 的峰值。
//   4. echo 衰減 60 dB 所需的時間：T60 = D · 60 / (−20·log10 g) = 3D / (−log10 g)，
//      以及反過來「給定 T60 求 g」：g = 10^(−3D / T60)。
//   5. setFeedback 會把 ≥ 1 的值 clamp 到 0.99（穩定性保護）。
//
// 執行：l09_ex1_solution（不需要參數，不輸出 WAV）。

#include <dawlab/Common.h>
#include <dawlab/dsp/Delay.h>

#include <cmath>
#include <cstdio>

using dawlab::dsp::Delay;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

double t60Seconds(double delaySeconds, double g) { return 3.0 * delaySeconds / -std::log10(g); }

double feedbackForT60(double delaySeconds, double t60) { return std::pow(10.0, -3.0 * delaySeconds / t60); }

} // namespace

int main() {
    std::printf("=== 第 09 章 練習 1：tempo sync 與 feedback 衰減的計算 ===\n\n");

    // ---- 1. BPM → ms -----------------------------------------------------------
    struct Case {
        double bpm, beats, expectMs;
        const char* name;
    };
    const Case cases[] = {
        {120.0, 1.0, 500.0, "120 BPM 四分音符"},
        {120.0, 0.5, 250.0, "120 BPM 八分音符"},
        {120.0, 0.75, 375.0, "120 BPM 附點八分"},
        {120.0, 1.0 / 3.0, 166.6667, "120 BPM 三連音八分"},
        {120.0, 0.25, 125.0, "120 BPM 十六分音符"},
        {95.0, 0.75, 473.6842, "95 BPM 附點八分"},
        {140.0, 2.0 / 3.0, 285.7143, "140 BPM 四分三連音"},
        {90.0, 1.5, 1000.0, "90 BPM 附點四分"},
        {174.0, 0.25, 86.2069, "174 BPM 十六分音符"},
    };
    std::printf("  %-22s  noteToMs    預期       samples@48k\n", "音符");
    for (const auto& c : cases) {
        const float ms = Delay::noteToMs(c.bpm, c.beats);
        std::printf("  %-22s  %8.3f  %8.3f   %9.1f\n", c.name, ms, c.expectMs, ms * 0.001 * kSampleRate);
        DAWLAB_CHECK_NEAR(ms, c.expectMs, 1.0e-3);
    }
    DAWLAB_CHECK_NEAR(Delay::noteToMs(95.0, 0.75) * 0.001 * kSampleRate, 22736.84, 0.05); // 不是整數 → fractional
    std::printf("\n");

    // ---- 2. feedback 0.5 的 echo 振幅 ---------------------------------------------
    {
        const float delayMs = 375.0f;
        const float g = 0.5f;
        Delay delay;
        delay.setMaxDelayMs(1000.0f);
        delay.prepare(kSampleRate, kBlockSize, 1);
        delay.setDelayMs(delayMs);
        delay.setFeedback(g);
        delay.setMix(1.0f);
        delay.setLowpassHz(20000.0f); // 關掉 feedback 濾波，只看 g
        delay.setHighpassHz(20.0f);
        delay.reset();

        dawlab::AudioBuffer ir = dawlab::makeImpulse(dawlab::secondsToSamples(4.0, kSampleRate), 1);
        dawlab::AudioBlock whole(ir);
        for (int start = 0; start < ir.getNumSamples(); start += kBlockSize)
            delay.process(whole.getSubBlock(start, std::min(kBlockSize, ir.getNumSamples() - start)));

        const int D = dawlab::msToSamples(delayMs, kSampleRate);
        std::printf("impulse → Delay（%.0f ms、feedback %.1f、mix 1）：\n", delayMs, g);
        std::printf("   echo k   位置(sample)   振幅      理論 g^(k−1)   dB\n");
        for (int k = 1; k <= 8; ++k) {
            const float a = ir.getSample(0, k * D);
            const double expect = std::pow(g, k - 1);
            std::printf("   %5d   %11d   %.6f   %.6f   %+7.2f\n", k, k * D, a, expect, dawlab::gainToDb(a));
            DAWLAB_CHECK_NEAR(a, expect, 1.0e-4);
        }
        DAWLAB_CHECK_NEAR(ir.getSample(0, D - 1), 0.0, 1.0e-6); // 整數 delay：echo 前一個 sample 是 0
        std::printf("  -> 每一圈 −6.02 dB；衰減 60 dB 需要 60/6.02 = 9.97 圈 ≈ 3.74 s\n\n");
    }

    // ---- 3. T60 公式 ------------------------------------------------------------
    {
        const double t1 = t60Seconds(0.375, 0.5);
        const double t2 = t60Seconds(0.375, 0.7);
        const double t3 = t60Seconds(0.250, 0.9);
        const double g4 = feedbackForT60(0.5, 2.0);
        std::printf("T60 = 3D / (−log10 g)：\n");
        std::printf("  D = 375 ms, g = 0.5 → %.3f s\n", t1);
        std::printf("  D = 375 ms, g = 0.7 → %.3f s\n", t2);
        std::printf("  D = 250 ms, g = 0.9 → %.3f s\n", t3);
        std::printf("  反算：D = 500 ms，要 2 s 衰減 60 dB → g = 10^(−0.75) = %.4f\n\n", g4);
        DAWLAB_CHECK_NEAR(t1, 3.737, 0.001);
        DAWLAB_CHECK_NEAR(t2, 7.263, 0.001);
        DAWLAB_CHECK_NEAR(t3, 16.391, 0.001);
        DAWLAB_CHECK_NEAR(g4, 0.1778, 1.0e-4);
    }

    // ---- 4. 穩定性保護 ----------------------------------------------------------
    {
        Delay delay;
        delay.setFeedback(1.5f);
        std::printf("setFeedback(1.5) → getFeedback() = %.2f（clamp 到 kMaxFeedback）\n", delay.getFeedback());
        DAWLAB_CHECK_NEAR(delay.getFeedback(), Delay::kMaxFeedback, 1.0e-6);
        delay.setFeedback(-0.5f);
        DAWLAB_CHECK_NEAR(delay.getFeedback(), 0.0, 1.0e-6);
    }

    return dawlab::test::summary("l09_ex1");
}
