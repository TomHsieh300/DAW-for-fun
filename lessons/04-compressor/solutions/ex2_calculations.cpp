// 練習 2 解答：靜態曲線與時間常數的手算題，用程式逐一驗證。
//
// 這支程式不做任何訊號處理，只把 solutions/README.md 裡的手算答案拿去和
// Compressor::computeOutputDb()、attackReleaseCoefficient() 對答案，確保書上的數字沒有算錯。
// 執行：l04_ex2_solution（不帶參數），不輸出 WAV。
#include <dawlab/Common.h>
#include <dawlab/dsp/Compressor.h>
#include <dawlab/dsp/EnvelopeFollower.h>

#include <cmath>
#include <cstdio>

using namespace dawlab;
using dawlab::dsp::attackReleaseCoefficient;
using dawlab::dsp::coefficientToTimeMs;
using dawlab::dsp::Compressor;

int main() {
    std::printf("練習 2：靜態曲線與時間常數\n");

    // ---- 1. T = -24, R = 3, hard knee ----
    std::printf("\n(1) T = -24 dB, R = 3:1, hard knee\n");
    struct Row {
        float in, out;
    };
    const Row hard[] = {{-30.0f, -30.0f}, {-24.0f, -24.0f}, {-12.0f, -20.0f}, {0.0f, -16.0f}};
    for (const Row& r : hard) {
        const float y = Compressor::computeOutputDb(r.in, -24.0f, 3.0f, 0.0f);
        std::printf("  輸入 %6.1f dB → 輸出 %7.3f dB，GR %6.3f dB（手算 %.3f / %.3f）\n", r.in, y, r.in - y, r.out,
                    r.in - r.out);
        DAWLAB_CHECK_NEAR(y, r.out, 1e-4);
    }

    // ---- 2. 同上，soft knee W = 12 ----
    // knee 範圍 T ± W/2 = -30 .. -18 dB
    // x = -24：y = -24 + (1/3 - 1)·6² / 24 = -24 - 1 = -25
    // x = -20：y = -20 + (1/3 - 1)·10² / 24 = -20 - 2.7778 = -22.7778
    std::printf("\n(2) soft knee W = 12 dB（knee 範圍 -30 .. -18 dB）\n");
    const Row soft[] = {{-30.0f, -30.0f}, {-24.0f, -25.0f}, {-20.0f, -22.777778f}};
    for (const Row& r : soft) {
        const float y = Compressor::computeOutputDb(r.in, -24.0f, 3.0f, 12.0f);
        std::printf("  輸入 %6.1f dB → 輸出 %8.4f dB（手算 %.4f）\n", r.in, y, r.out);
        DAWLAB_CHECK_NEAR(y, r.out, 1e-4);
    }

    // ---- 3. knee 上緣 -18 dB：二次曲線 = 直線 = -22 ----
    const float upperSoft = -18.0f + (1.0f / 3.0f - 1.0f) * 12.0f * 12.0f / 24.0f; // 直接用二次式
    const float upperHard = Compressor::computeOutputDb(-18.0f, -24.0f, 3.0f, 0.0f);
    std::printf("\n(3) knee 上緣 -18 dB：二次式 %.4f dB，hard knee 直線 %.4f dB\n", upperSoft, upperHard);
    DAWLAB_CHECK_NEAR(upperSoft, -22.0, 1e-4);
    DAWLAB_CHECK_NEAR(upperHard, -22.0, 1e-4);
    // 斜率也要接上：在上緣兩側各取 0.001 dB 做數值微分，應該都是 1/R
    const float h = 0.001f;
    const float slopeBelow = (Compressor::computeOutputDb(-18.0f, -24.0f, 3.0f, 12.0f) -
                              Compressor::computeOutputDb(-18.0f - h, -24.0f, 3.0f, 12.0f)) / h;
    std::printf("    knee 內側的斜率 ≈ %.3f（應為 1/R = 0.333）\n", slopeBelow);
    DAWLAB_CHECK_NEAR(slopeBelow, 1.0 / 3.0, 0.01);

    // ---- 4. fs = 44.1 kHz，attack 5 ms、release 200 ms ----
    const double fs = 44100.0;
    const float aA = attackReleaseCoefficient(5.0, fs);
    const float aR = attackReleaseCoefficient(200.0, fs);
    std::printf("\n(4) fs = 44100：αA = exp(-1/220.5) = %.6f，αR = exp(-1/8820) = %.6f\n", aA, aR);
    DAWLAB_CHECK_NEAR(aA, 0.995475, 2e-6);
    DAWLAB_CHECK_NEAR(aR, 0.999887, 2e-6);
    std::printf("    反推 τA = %.3f ms，τR = %.2f ms\n", coefficientToTimeMs(aA, fs), coefficientToTimeMs(aR, fs));

    // ---- 5. 到 90% / 99% 的時間：數值模擬 step response，與 τ·ln10、τ·ln100 比較 ----
    auto stepTimeMs = [&](float alpha, double fraction) {
        double y = 0.0;
        for (int n = 1; n < 1000000; ++n) {
            y = alpha * y + (1.0 - alpha) * 1.0;
            if (y >= fraction)
                return n * 1000.0 / fs;
        }
        return -1.0;
    };
    const double t90 = stepTimeMs(aA, 0.90);
    const double t99 = stepTimeMs(aA, 0.99);
    const double r10 = stepTimeMs(aR, 0.90); // release 衰減到 10% = 「已經走完 90%」，與 attack 同一條式子
    std::printf("\n(5) attack 到 90%%：%.2f ms（τ·ln10 = 11.51）；到 99%%：%.2f ms（τ·ln100 = 23.03）\n", t90, t99);
    std::printf("    release 衰減到 10%%：%.1f ms（τ·ln10 = 460.5）\n", r10);
    DAWLAB_CHECK_NEAR(t90, 5.0 * std::log(10.0), 0.05);
    DAWLAB_CHECK_NEAR(t99, 5.0 * std::log(100.0), 0.05);
    DAWLAB_CHECK_NEAR(r10, 200.0 * std::log(10.0), 0.1);

    // ---- 6. 面板「10 ms 到 90%」換成 63.2% 定義：τ = 10 / ln10 = 4.343 ms ----
    const double tau = 10.0 / std::log(10.0);
    const double check90 = stepTimeMs(attackReleaseCoefficient(tau, fs), 0.90);
    std::printf("\n(6) setAttackMs(%.3f) → 實際到 90%% 的時間 %.2f ms（目標 10）\n", tau, check90);
    DAWLAB_CHECK_NEAR(tau, 4.343, 0.001);
    DAWLAB_CHECK_NEAR(check90, 10.0, 0.05);

    return dawlab::test::summary("l04_ex2");
}
