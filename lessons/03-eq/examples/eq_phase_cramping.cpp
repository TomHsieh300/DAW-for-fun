// 範例 4：相位、群延遲、Nyquist cramping、以及「直接插值係數」會發生什麼事
//
// 示範什麼：
//   A. 一顆 +6 dB@1 kHz Q=1 的 peak：印出 magnitude / phase / group delay，看 IIR EQ 的 minimum-phase 特性
//      （boost 的地方相位先正後負、群延遲在中心最大）。
//   B. Nyquist cramping：44.1 kHz 下 +6 dB@10 kHz Q=1 的 peak，比較類比原型 H(s) 與 bilinear 數位版 H(z)。
//      數位版在 Nyquist（22.05 kHz）被硬壓回 0 dB，鐘形變得不對稱。
//   C. 係數插值：把「100 Hz peak」與「10 kHz peak」的係數線性插值到一半，結果既不是 1 kHz 的 peak，
//      增益與 Q 也都變了——這就是為什麼我們平滑「參數」而不是平滑「係數」。
//   D. 係數插值什麼時候會不穩定：
//      D1. 把 24 dB/oct high cut（4 階）乘成一個 4 階多項式再插值 → 中點的 pole 跑出單位圓。
//      D2. 每個 sample 在兩組「各自都穩定」的係數之間交替 → time-varying 的 filter 照樣發散。
// 怎麼執行：l03_eq_phase_cramping（不需參數）
// 輸出：只有 console 文字。

#include <dawlab/Common.h>
#include <dawlab/dsp/ParametricEq.h>

#include <array>
#include <complex>
#include <cstdio>

using namespace dawlab;
using namespace dawlab::dsp;

namespace {

// RBJ peak 的類比原型：H(s) = (s² + s·A/Q + 1) / (s² + s/(A·Q) + 1)，s = jΩ，Ω = f / f0
double analogPeakDb(double f, double f0, double q, double gainDb) {
    const double A = std::pow(10.0, gainDb / 40.0);
    const std::complex<double> s(0.0, f / f0);
    const std::complex<double> h = (s * s + s * (A / q) + 1.0) / (s * s + s / (A * q) + 1.0);
    return gainToDb(std::abs(h));
}

// ---- D 段用的小工具 ----

// 4 階 IIR：b[0..4]、a[0..4]（a[0] = 1），也就是兩顆 biquad 的多項式乘開
struct Poly4 {
    std::array<double, 5> b{}, a{};
};

Poly4 cascade(const BiquadCoefficients& x, const BiquadCoefficients& y) {
    const double xb[3] = {x.b0, x.b1, x.b2}, xa[3] = {1.0, x.a1, x.a2};
    const double yb[3] = {y.b0, y.b1, y.b2}, ya[3] = {1.0, y.a1, y.a2};
    Poly4 p;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            p.b[static_cast<size_t>(i + j)] += xb[i] * yb[j];
            p.a[static_cast<size_t>(i + j)] += xa[i] * ya[j];
        }
    return p;
}

// 1 + a1 z^-1 + … + a4 z^-4 = 0 的根（pole）的最大絕對值。
// 用 Durand–Kerner 迭代同時求 4 個根：z_i ← z_i − p(z_i) / Π_{j≠i}(z_i − z_j)
double maxPoleMagnitude(const std::array<double, 5>& a) {
    using cd = std::complex<double>;
    std::array<cd, 4> r;
    for (size_t i = 0; i < 4; ++i)
        r[i] = std::pow(cd(0.4, 0.9), static_cast<double>(i)); // 慣用的起始點（不在實軸上、互不相同）
    for (int iter = 0; iter < 500; ++iter)
        for (size_t i = 0; i < 4; ++i) {
            cd p = 1.0; // z^4 + a1 z^3 + a2 z^2 + a3 z + a4（Horner）
            for (size_t k = 1; k < 5; ++k)
                p = p * r[i] + a[k];
            cd den = 1.0;
            for (size_t j = 0; j < 4; ++j)
                if (j != i)
                    den *= r[i] - r[j];
            r[i] -= p / den;
        }
    double m = 0.0;
    for (const cd& z : r)
        m = std::max(m, std::abs(z));
    return m;
}

// 用 Direct Form I 跑 4 階 IIR 的 impulse response，回傳 h[n]
double impulseResponseAt(const Poly4& p, int n) {
    std::array<double, 5> x{}, y{};
    double h = 0.0;
    for (int i = 0; i <= n; ++i) {
        for (size_t k = 4; k > 0; --k) {
            x[k] = x[k - 1];
            y[k] = y[k - 1];
        }
        x[0] = (i == 0) ? 1.0 : 0.0;
        h = 0.0;
        for (size_t k = 0; k < 5; ++k)
            h += p.b[k] * x[k];
        for (size_t k = 1; k < 5; ++k)
            h -= p.a[k] * y[k];
        y[0] = h;
    }
    return h;
}

BiquadCoefficients lerpCoefficients(const BiquadCoefficients& x, const BiquadCoefficients& y, double t) {
    BiquadCoefficients c;
    c.b0 = x.b0 + t * (y.b0 - x.b0);
    c.b1 = x.b1 + t * (y.b1 - x.b1);
    c.b2 = x.b2 + t * (y.b2 - x.b2);
    c.a1 = x.a1 + t * (y.a1 - x.a1);
    c.a2 = x.a2 + t * (y.a2 - x.a2);
    return c;
}

} // namespace

int main() {
    std::printf("=== 第 03 章 範例 4：相位 / 群延遲 / Nyquist cramping / 係數插值 ===\n");

    // ---- A. minimum phase ----
    {
        constexpr double sr = 48000.0;
        ParametricEq eq;
        eq.prepare(sr, 512, 1);
        eq.setBand(0, {EqBandType::Peak, 1000.0f, 1.0f, 6.0f, true});
        eq.reset();
        std::printf("\n[A] +6 dB @ 1 kHz, Q = 1（48 kHz）\n");
        std::printf("   freq(Hz)   mag(dB)   phase(deg)   group delay(samples)  (ms)\n");
        for (double f : {100.0, 250.0, 500.0, 707.0, 1000.0, 1414.0, 2000.0, 4000.0, 10000.0}) {
            const double gd = eq.getGroupDelaySamples(f);
            std::printf("   %7.0f    %+6.2f     %+7.2f        %7.2f           %.3f\n", f, eq.getMagnitudeDb(f),
                        eq.getPhaseDegrees(f), gd, samplesToMs(gd, sr));
        }
        std::printf("   -> boost 的中心相位 = 0，低於中心相位為正（領先）、高於中心為負（落後）；\n");
        std::printf("      群延遲只有零點幾 ms，且只在 boost 附近——這是 minimum-phase 的特徵。\n");

        // 對照：同一顆改成 -6 dB cut，相位剛好反號
        eq.setBand(0, {EqBandType::Peak, 1000.0f, 1.0f, -6.0f, true});
        std::printf("   -6 dB cut 在 707 Hz 的相位 = %+.2f deg（boost 時為相反號）\n", eq.getPhaseDegrees(707.0));
    }

    // ---- B. Nyquist cramping ----
    {
        constexpr double sr = 44100.0;
        const double f0 = 10000.0, q = 1.0, g = 6.0;
        ParametricEq eq;
        eq.prepare(sr, 512, 1);
        eq.setBand(0, {EqBandType::Peak, static_cast<float>(f0), static_cast<float>(q), static_cast<float>(g), true});
        eq.reset();
        std::printf("\n[B] Nyquist cramping：+6 dB @ 10 kHz, Q = 1（44.1 kHz，Nyquist = 22.05 kHz）\n");
        std::printf("   freq(Hz)   analog(dB)   digital(dB)   diff\n");
        for (double f : {2500.0, 5000.0, 7000.0, 10000.0, 14000.0, 17000.0, 20000.0, 22000.0, 22050.0}) {
            const double a = analogPeakDb(f, f0, q, g), d = eq.getMagnitudeDb(f);
            std::printf("   %7.0f     %+6.2f       %+6.2f      %+6.2f\n", f, a, d, d - a);
        }
        std::printf("   -> 中心 10 kHz 兩者一樣（bilinear 有 pre-warp）；Nyquist 處數位版被壓回 0 dB，\n");
        std::printf("      類比版還有 %+.2f dB。鐘形右半邊被「擠扁」（cramped）。\n", analogPeakDb(sr / 2, f0, q, g));

        // 用 getMagnitudeDb 證明：RBJ peak 在 z = -1 的響應一定是 1
        const BiquadCoefficients c = ParametricEq::makeCoefficients(sr, eq.getBand(0));
        std::printf("   H(z=-1) = (b0-b1+b2)/(1-a1+a2) = %.6f（恆為 1）\n", (c.b0 - c.b1 + c.b2) / (1.0 - c.a1 + c.a2));
    }

    // ---- C. 係數插值 ----
    {
        constexpr double sr = 48000.0;
        const BiquadCoefficients lo = BiquadCoefficients::makePeak(sr, 100.0, 2.0, 12.0);
        const BiquadCoefficients hi = BiquadCoefficients::makePeak(sr, 10000.0, 2.0, 12.0);
        const BiquadCoefficients mid = BiquadCoefficients::makePeak(sr, 1000.0, 2.0, 12.0); // 參數插值到一半
        const BiquadCoefficients lerped = lerpCoefficients(lo, hi, 0.5); // 係數插值到一半

        std::printf("\n[C] 係數插值 vs 參數插值：+12 dB Q=2 的 peak，從 100 Hz 到 10 kHz 的「中點」\n");
        std::printf("   freq(Hz)   參數插值(1 kHz peak)   係數插值\n");
        double lerpedMax = -100.0, lerpedMaxFreq = 0.0;
        for (double f = 20.0; f <= 20000.0; f *= 1.02) {
            const double db = lerped.magnitudeDb(f, sr);
            if (db > lerpedMax) {
                lerpedMax = db;
                lerpedMaxFreq = f;
            }
        }
        for (double f : {100.0, 300.0, 1000.0, 3000.0, 10000.0})
            std::printf("   %7.0f        %+6.2f             %+6.2f\n", f, mid.magnitudeDb(f, sr), lerped.magnitudeDb(f, sr));
        std::printf("   係數插值的最大增益 %+.2f dB 出現在 %.0f Hz（不是 1 kHz，也不是 +12 dB）\n", lerpedMax, lerpedMaxFreq);
        std::printf("   穩定性（|a2| < 1 且 |a1| < 1 + a2）：%s —— 二階的穩定三角形是凸的，線性插值不會跑出去，\n",
                    lerped.isStable() ? "穩定" : "不穩定");
        std::printf("   但中間的響應沒有意義（頻率跑到 6 kHz 以上）。\n");
    }

    // ---- D. 係數插值什麼時候會「不穩定」 ----
    {
        constexpr double sr = 48000.0;
        std::printf("\n[D] 係數插值什麼時候會不穩定\n");

        // D1. 高階多項式的插值：24 dB/oct high cut（4 階 Butterworth）@ 100 Hz 與 @ 10 kHz。
        //     如果把兩顆 biquad 先乘成一個 4 階多項式再插值，中點的 pole 會跑出單位圓。
        const Poly4 lo = cascade(BiquadCoefficients::makeLowpass(sr, 100.0, butterworthQ(4, 0)),
                                 BiquadCoefficients::makeLowpass(sr, 100.0, butterworthQ(4, 1)));
        const Poly4 hi = cascade(BiquadCoefficients::makeLowpass(sr, 10000.0, butterworthQ(4, 0)),
                                 BiquadCoefficients::makeLowpass(sr, 10000.0, butterworthQ(4, 1)));
        Poly4 mid4;
        for (int k = 0; k < 5; ++k) {
            mid4.b[k] = 0.5 * (lo.b[k] + hi.b[k]);
            mid4.a[k] = 0.5 * (lo.a[k] + hi.a[k]);
        }
        std::printf("   D1. 4 階 Butterworth lowpass，100 Hz 與 10 kHz 的「4 階多項式係數」取中點：\n");
        std::printf("       max|pole|：100 Hz 版 %.5f、10 kHz 版 %.5f、插值中點 %.5f（> 1 = 不穩定）\n",
                    maxPoleMagnitude(lo.a), maxPoleMagnitude(hi.a), maxPoleMagnitude(mid4.a));
        std::printf("       中點的 impulse response：n = 100 時 |h| = %.3g，n = 1000 時 |h| = %.3g（發散）\n",
                    std::abs(impulseResponseAt(mid4, 100)), std::abs(impulseResponseAt(mid4, 1000)));
        // 對照：拆成兩顆 biquad、各自插值（二階穩定三角形是凸的）
        const BiquadCoefficients s1 = lerpCoefficients(BiquadCoefficients::makeLowpass(sr, 100.0, butterworthQ(4, 0)),
                                                       BiquadCoefficients::makeLowpass(sr, 10000.0, butterworthQ(4, 0)), 0.5);
        const BiquadCoefficients s2 = lerpCoefficients(BiquadCoefficients::makeLowpass(sr, 100.0, butterworthQ(4, 1)),
                                                       BiquadCoefficients::makeLowpass(sr, 10000.0, butterworthQ(4, 1)), 0.5);
        std::printf("       對照：拆成兩顆 biquad 各自插值 -> 兩顆都%s（每顆仍在凸的穩定三角形內）\n",
                    (s1.isStable() && s2.isStable()) ? "穩定" : "不穩定");

        // D2. 「每一瞬間都穩定」≠「time-varying 系統穩定」：
        //     每個 sample 在兩組都穩定的 lowpass 係數（50 Hz / 16 kHz，Q = 0.707）之間來回切換，
        //     用第 02 章真正的 Biquad（TDF-II）跑一個 impulse。
        const BiquadCoefficients c50 = BiquadCoefficients::makeLowpass(sr, 50.0, BiquadCoefficients::kButterworthQ);
        const BiquadCoefficients c16k = BiquadCoefficients::makeLowpass(sr, 16000.0, BiquadCoefficients::kButterworthQ);
        Biquad bq;
        bq.prepare(sr, 1, 1);
        std::printf("   D2. 每個 sample 在 lowpass 50 Hz 與 16 kHz（各自都穩定：%s / %s）之間交替，輸入 impulse：\n",
                    c50.isStable() ? "穩定" : "不穩定", c16k.isStable() ? "穩定" : "不穩定");
        std::printf("       ");
        for (int n = 0; n <= 80; ++n) {
            bq.setCoefficients((n % 2 == 0) ? c50 : c16k);
            const float y = bq.processSample(0, n == 0 ? 1.0f : 0.0f);
            if (n > 0 && n % 20 == 0)
                std::printf("n=%d |y|=%.3g   ", n, std::abs(static_cast<double>(y)));
        }
        std::printf("\n");
        // 對照：同樣在 50 Hz 與 16 kHz 之間來回，但每趟用 20 ms 在 log 頻率上平滑地走（ParametricEq 的做法）
        Biquad slow;
        slow.prepare(sr, 1, 1);
        NoiseGenerator noise(1);
        const int leg = msToSamples(20.0, sr);
        float slowPeak = 0.0f;
        for (int n = 0; n < secondsToSamples(1.0, sr); ++n) {
            const double phase = static_cast<double>(n % (2 * leg)) / leg; // 0..2：上去再下來
            const double t = phase < 1.0 ? phase : 2.0 - phase;
            const double f = 50.0 * std::pow(16000.0 / 50.0, t);
            slow.setCoefficients(BiquadCoefficients::makeLowpass(sr, f, BiquadCoefficients::kButterworthQ));
            slowPeak = std::max(slowPeak, std::abs(slow.processSample(0, 0.5f * noise.next())));
        }
        std::printf("       對照：每 20 ms 在 50 Hz <-> 16 kHz 之間平滑掃一趟（逐 sample 重算），1 秒白雜訊（peak 0.5）\n");
        std::printf("       輸出 peak = %.3f（有界）\n", slowPeak);
        std::printf("   -> 參數慢慢變（相對於 filter 的時間常數）時，time-varying 的效應很小；\n");
        std::printf("      係數「跳」或「快速來回」時，即使每組係數單獨看都穩定，整體也可能發散。\n");
    }
    return 0;
}
