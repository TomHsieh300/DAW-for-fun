// 練習 4 解答：Chebyshev polynomial 諧波 exciter——精確地「點」出指定的諧波
//
// 關鍵恆等式：T_n(cos θ) = cos(nθ)。
// 所以把振幅 1 的正弦波丟進 T_n，出來的就是「純」n 次諧波，沒有其他成分。
// 遞迴：T_0 = 1、T_1 = x、T_{n+1}(x) = 2x T_n(x) - T_{n-1}(x)
//   T_2 = 2x² - 1、T_3 = 4x³ - 3x、T_4 = 8x⁴ - 8x² + 1、T_5 = 16x⁵ - 20x³ + 5x
//
// exciter：y = x + Σ_{n=2..5} g_n T_n(x)
//   - 輸入振幅 1 時：H1 = 1，H_n = |g_n| → 想要 H2 = -20 dB 就設 g_2 = 0.1。
//   - 輸入振幅 A < 1 時不再精確：T_2(A cos θ) = A² cos 2θ + (A² - 1)
//       → H2 振幅變成 g_2 A²，而且多了 DC g_2 (A² - 1)（所以實務上後面要接 DC blocker）。
//     一般來說 T_n(A cos θ) 的 n 次諧波振幅 = A^n，還會漏出 n-2、n-4…… 次的成分。
//   - 多項式的次數 N 決定了最高諧波 = N 次 → 你「確切知道」需要多少 oversampling 才不會 alias（N 倍即可）。
//
// 執行：l06_ex4_solution

#include <dawlab/Common.h>

#include <algorithm>
#include <array>
#include <cstdio>

namespace {

// T_n(x)：用遞迴算，不用查表
float chebyshev(int n, float x) noexcept {
    if (n == 0)
        return 1.0f;
    float tPrev = 1.0f, t = x;
    for (int k = 1; k < n; ++k) {
        const float next = 2.0f * x * t - tPrev;
        tPrev = t;
        t = next;
    }
    return t;
}

class ChebyshevExciter {
public:
    static constexpr int kMaxHarmonic = 5;

    // harmonic = 2..5；gain = 輸入振幅 1 時，該次諧波相對於基頻的振幅（線性）
    void setHarmonicGain(int harmonic, float gain) noexcept {
        if (harmonic >= 2 && harmonic <= kMaxHarmonic)
            gains_[static_cast<size_t>(harmonic)] = gain;
    }

    float processSample(float x) const noexcept {
        // 把 x 限制在 [-1, 1]：Chebyshev 多項式在 |x| > 1 時會爆炸性成長（T_5(2) = 362）
        const float xc = std::clamp(x, -1.0f, 1.0f);
        float y = x;
        // 同一個遞迴一路算上去，順便把各次加進來（比呼叫 chebyshev(n, x) 四次省）
        float tPrev = 1.0f, t = xc;
        for (int n = 2; n <= kMaxHarmonic; ++n) {
            const float next = 2.0f * xc * t - tPrev;
            tPrev = t;
            t = next;
            y += gains_[static_cast<size_t>(n)] * t;
        }
        return y;
    }

private:
    std::array<float, kMaxHarmonic + 1> gains_{};
};

constexpr double kSampleRate = 48000.0;
constexpr double kF0 = 1000.0;

} // namespace

int main() {
    // ---- 1. T_n 的恆等式 ---------------------------------------------------------------------
    for (int n = 0; n <= 5; ++n) {
        double worst = 0.0;
        for (double th = 0.0; th < dawlab::kTwoPi; th += 0.01)
            worst = std::max(worst, std::abs(chebyshev(n, static_cast<float>(std::cos(th))) - std::cos(n * th)));
        DAWLAB_CHECK(worst < 1e-5);
    }
    DAWLAB_CHECK_NEAR(chebyshev(3, 0.5f), 4 * 0.125 - 1.5, 1e-6);   // 4x³ - 3x
    DAWLAB_CHECK_NEAR(chebyshev(4, 0.5f), 8 * 0.0625 - 2 + 1, 1e-6); // 8x⁴ - 8x² + 1

    // ---- 2. 振幅 1：諧波正好等於設定值 --------------------------------------------------------
    ChebyshevExciter ex;
    const float g2 = 0.1f, g3 = 0.05f, g5 = 0.02f; // -20 dB、-26.02 dB、-33.98 dB；H4 不要
    ex.setHarmonicGain(2, g2);
    ex.setHarmonicGain(3, g3);
    ex.setHarmonicGain(5, g5);

    auto run = [&](float amplitude) {
        dawlab::AudioBuffer b = dawlab::makeSine(kF0, kSampleRate, 1.0, amplitude, 1);
        float* d = b.getWritePointer(0);
        for (int i = 0; i < b.getNumSamples(); ++i)
            d[i] = ex.processSample(d[i]);
        return b;
    };
    {
        const auto b = run(1.0f);
        const auto lv = dawlab::harmonicLevelsDb(b.getReadPointer(0), b.getNumSamples(), kF0, kSampleRate, 6);
        std::printf("振幅 1.0：H2 %.2f dB（目標 %.2f）、H3 %.2f（%.2f）、H4 %.2f（無）、H5 %.2f（%.2f）、H6 %.2f（無）\n",
                    lv[1], dawlab::gainToDb(g2), lv[2], dawlab::gainToDb(g3), lv[3], lv[4], dawlab::gainToDb(g5), lv[5]);
        DAWLAB_CHECK_NEAR(lv[1], dawlab::gainToDb(g2), 0.1);
        DAWLAB_CHECK_NEAR(lv[2], dawlab::gainToDb(g3), 0.1);
        DAWLAB_CHECK_NEAR(lv[4], dawlab::gainToDb(g5), 0.1);
        DAWLAB_CHECK(lv[3] < -80.0); // H4 沒設 → 不存在
        DAWLAB_CHECK(lv[5] < -80.0); // 5 次多項式 → 沒有 6 次以上
    }
    // ---- 3. 振幅 0.5：不再精確，且出現 DC --------------------------------------------------------
    {
        const float A = 0.5f;
        const auto b = run(A);
        const float* d = b.getReadPointer(0);
        const int n = b.getNumSamples();
        double mean = 0.0;
        for (int i = 0; i < n; ++i)
            mean += d[i];
        mean /= n;
        const double h2 = dawlab::goertzelAmplitude(d, n, 2 * kF0, kSampleRate);
        // DC 只來自偶次：T_2 → g2 (A² - 1)；T_4 沒用到
        std::printf("振幅 0.5：DC %.4f（理論 g2(A²-1) = %.4f）、H2 振幅 %.4f（理論 g2·A² = %.4f）\n", mean,
                    g2 * (A * A - 1.0f), h2, g2 * A * A);
        DAWLAB_CHECK_NEAR(mean, g2 * (A * A - 1.0f), 1e-3);
        DAWLAB_CHECK_NEAR(h2, g2 * A * A, 1e-3);
    }
    return dawlab::test::summary("l06_ex4");
}
