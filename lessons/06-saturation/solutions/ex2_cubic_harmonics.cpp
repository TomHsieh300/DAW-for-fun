// 練習 2 解答：手算 x² 與 x³ 對正弦波產生的諧波，再用 Goertzel 量測驗證
//
// 手算（x = A sin θ，θ = 2π f0 t）：
//   x² = A² sin²θ = (A²/2) - (A²/2) cos 2θ
//        → DC = A²/2、2 次諧波振幅 A²/2，沒有基頻
//   x³ = A³ sin³θ = (3A³/4) sin θ - (A³/4) sin 3θ
//        → 基頻振幅 3A³/4、3 次諧波振幅 A³/4（H3/H1 = 1/3 → -9.54 dB，與 A 無關！）
//   cubic soft clip y = x - x³/3（|x| ≤ 1）
//        → 基頻 A - A³/4、3 次 A³/12；H3/H1 = A²/(12 - 3A²)
//          A = 0.5：H1 = 0.46875、H3 = 0.0104167 → -33.06 dB、THD 2.222 %
//          A = 1.0：H1 = 0.75、H3 = 0.0833 → -19.08 dB、THD 11.11 %
//   y = x + k x²（最簡單的不對稱曲線）：
//        → DC = kA²/2、H1 = A、H2 = kA²/2 → H2/H1 = kA/2（k = 0.5、A = 0.5 → 0.125 → -18.06 dB）
//
// 執行：l06_ex2_solution

#include <dawlab/Common.h>
#include <dawlab/dsp/Saturation.h>

#include <cstdio>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr double kF0 = 100.0;

double meanOf(const float* x, int n) {
    double s = 0.0;
    for (int i = 0; i < n; ++i)
        s += x[i];
    return s / n;
}

// 把振幅 A 的正弦波丟進 f()，回傳處理後的 buffer
template <typename Fn>
dawlab::AudioBuffer shaped(float amplitude, Fn&& f) {
    dawlab::AudioBuffer b = dawlab::makeSine(kF0, kSampleRate, 1.0, amplitude, 1);
    float* d = b.getWritePointer(0);
    for (int i = 0; i < b.getNumSamples(); ++i)
        d[i] = f(d[i]);
    return b;
}

double amp(const dawlab::AudioBuffer& b, double freq) {
    return dawlab::goertzelAmplitude(b.getReadPointer(0), b.getNumSamples(), freq, kSampleRate);
}

} // namespace

int main() {
    // ---- x² -------------------------------------------------------------------------------
    {
        const float A = 0.8f;
        const auto b = shaped(A, [](float x) { return x * x; });
        const double dc = meanOf(b.getReadPointer(0), b.getNumSamples());
        std::printf("x²，A = %.1f：DC %.4f（理論 %.4f）、H1 %.5f（理論 0）、H2 %.4f（理論 %.4f）\n", A, dc,
                    A * A / 2.0, amp(b, kF0), amp(b, 2 * kF0), A * A / 2.0);
        DAWLAB_CHECK_NEAR(dc, A * A / 2.0, 1e-3);
        DAWLAB_CHECK_NEAR(amp(b, 2 * kF0), A * A / 2.0, 2e-3);
        DAWLAB_CHECK(amp(b, kF0) < 1e-3);
        DAWLAB_CHECK(amp(b, 3 * kF0) < 1e-3);
    }
    // ---- x³ -------------------------------------------------------------------------------
    for (float A : {0.5f, 1.0f}) {
        const auto b = shaped(A, [](float x) { return x * x * x; });
        const double h1 = amp(b, kF0), h3 = amp(b, 3 * kF0);
        std::printf("x³，A = %.1f：H1 %.5f（理論 %.5f）、H3 %.5f（理論 %.5f）、H3/H1 = %.2f dB（理論 -9.54）\n", A, h1,
                    0.75 * A * A * A, h3, 0.25 * A * A * A, dawlab::gainToDb(h3 / h1));
        DAWLAB_CHECK_NEAR(h1, 0.75 * A * A * A, 2e-3);
        DAWLAB_CHECK_NEAR(h3, 0.25 * A * A * A, 2e-3);
        DAWLAB_CHECK_NEAR(dawlab::gainToDb(h3 / h1), -9.542, 0.05);
        DAWLAB_CHECK(amp(b, 2 * kF0) < 1e-3); // 奇函式 → 沒有偶次
        DAWLAB_CHECK(amp(b, 5 * kF0) < 1e-3); // 3 次多項式最多只到 3 次諧波
    }
    // ---- cubic soft clip（用 Saturation::shape）--------------------------------------------
    for (float A : {0.5f, 1.0f}) {
        const auto b = shaped(A, [](float x) {
            return dawlab::dsp::Saturation::shape(dawlab::dsp::SaturationCurve::Cubic, x);
        });
        const double theoryH1 = A - A * A * A / 4.0, theoryH3 = A * A * A / 12.0;
        const double thd = dawlab::thdPercent(b.getReadPointer(0), b.getNumSamples(), kF0, kSampleRate, 10);
        std::printf("cubic，A = %.1f：H3/H1 = %.2f dB（理論 %.2f）、THD %.3f %%（理論 %.3f %%）\n", A,
                    dawlab::gainToDb(amp(b, 3 * kF0) / amp(b, kF0)), dawlab::gainToDb(theoryH3 / theoryH1), thd,
                    100.0 * theoryH3 / theoryH1);
        DAWLAB_CHECK_NEAR(amp(b, kF0), theoryH1, 2e-3);
        DAWLAB_CHECK_NEAR(dawlab::gainToDb(amp(b, 3 * kF0) / amp(b, kF0)), dawlab::gainToDb(theoryH3 / theoryH1), 0.05);
        DAWLAB_CHECK_NEAR(thd, 100.0 * theoryH3 / theoryH1, 0.02);
    }
    // ---- x + k x² --------------------------------------------------------------------------
    {
        const float A = 0.5f, k = 0.5f;
        const auto b = shaped(A, [k](float x) { return x + k * x * x; });
        const double ratioDb = dawlab::gainToDb(amp(b, 2 * kF0) / amp(b, kF0));
        std::printf("x + 0.5x²，A = 0.5：H2/H1 = %.2f dB（理論 %.2f）、DC %.4f（理論 %.4f）\n", ratioDb,
                    dawlab::gainToDb(k * A / 2.0), meanOf(b.getReadPointer(0), b.getNumSamples()), k * A * A / 2.0);
        DAWLAB_CHECK_NEAR(ratioDb, dawlab::gainToDb(k * A / 2.0), 0.05);
        DAWLAB_CHECK_NEAR(meanOf(b.getReadPointer(0), b.getNumSamples()), k * A * A / 2.0, 1e-3);
    }
    return dawlab::test::summary("l06_ex2");
}
