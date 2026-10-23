// 練習 5 解答：從 biquad 係數算出 pole，判斷穩定性，並用 impulse 實際驗證。
//
// 分母 1 + a1 z^-1 + a2 z^-2 = 0  ⇔  z² + a1 z + a2 = 0
//     p = (-a1 ± sqrt(a1² - 4 a2)) / 2
// 穩定 ⇔ 所有 pole 都在單位圓內（|p| < 1）。
// 判別式 < 0 時是一對共軛複數 pole：半徑 r = sqrt(a2)、角度 θ = acos(-a1 / (2r))，
//   - θ 對應「共振頻率」f = θ fs / (2π)
//   - r 決定衰減速度：impulse response 的包絡 ∝ r^n，時間常數 τ = -1 / ln(r) samples
// 二階系統的穩定條件也可以寫成「三角形條件」：|a2| < 1 且 |a1| < 1 + a2。
#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>

#include <complex>
#include <cstdio>
#include <limits>
#include <utility>

using namespace dawlab;
using dawlab::dsp::Biquad;
using dawlab::dsp::BiquadCoefficients;

constexpr double kSampleRate = 48000.0;

using Poles = std::pair<std::complex<double>, std::complex<double>>;

// 自己用二次公式算 pole（不呼叫 BiquadCoefficients::poles()，最後再拿它對答案）
static Poles computePoles(double a1, double a2) {
    const std::complex<double> disc = std::sqrt(std::complex<double>(a1 * a1 - 4.0 * a2, 0.0));
    return {(-a1 + disc) * 0.5, (-a1 - disc) * 0.5};
}

static bool isStable(double a1, double a2) {
    const Poles p = computePoles(a1, a2);
    return std::abs(p.first) < 1.0 && std::abs(p.second) < 1.0;
}

// 把 impulse 送進 filter 跑 n 個 sample，回傳「最後 10% 的峰值 / 前 10% 的峰值」：
// 穩定 → 趨近 0；臨界 → 約 1；不穩定 → 遠大於 1（或 inf）
static double tailToHeadRatio(const BiquadCoefficients& c, int n) {
    Biquad f;
    f.prepare(kSampleRate, 512, 1);
    f.setCoefficients(c);
    double head = 0.0, tail = 0.0;
    for (int i = 0; i < n; ++i) {
        const double y = f.processSample(0, i == 0 ? 1.0f : 0.0f);
        if (!std::isfinite(y))
            return std::numeric_limits<double>::infinity();
        if (i < n / 10)
            head = std::max(head, std::abs(y));
        if (i >= n - n / 10)
            tail = std::max(tail, std::abs(y));
    }
    return tail / head;
}

int main() {
    struct Case {
        const char* name;
        BiquadCoefficients c;
        bool expectStable;
    };
    const auto raw = [](double a1, double a2) {
        BiquadCoefficients c;
        c.b0 = 1.0;
        c.a1 = a1;
        c.a2 = a2;
        return c;
    };
    const Case cases[] = {
        {"LPF 1 kHz Q=0.707", BiquadCoefficients::makeLowpass(kSampleRate, 1000.0, 0.7071), true},
        {"LPF 1 kHz Q=8", BiquadCoefficients::makeLowpass(kSampleRate, 1000.0, 8.0), true},
        {"a1=-1.8  a2=0.81 (double real pole 0.9)", raw(-1.8, 0.81), true},
        {"a1=-1.9  a2=1.0  (|p| = 1, marginal)", raw(-1.9, 1.0), false},
        {"a1=-2.1  a2=1.1025 (p = 1.05, unstable)", raw(-2.1, 1.1025), false},
        {"a1=0.5   a2=-0.6  (real poles 0.564, -1.064)", raw(0.5, -0.6), false},
    };

    std::printf("%-44s %22s %22s %8s %10s %10s\n", "case", "pole 1", "pole 2", "|p|max", "stable?", "tail/head");
    for (const Case& cs : cases) {
        const Poles p = computePoles(cs.c.a1, cs.c.a2);
        const double rmax = std::max(std::abs(p.first), std::abs(p.second));
        const bool stable = isStable(cs.c.a1, cs.c.a2);
        const double ratio = tailToHeadRatio(cs.c, 20000);
        std::printf("%-44s %10.4f%+10.4fj %10.4f%+10.4fj %8.4f %10s %10.3g\n", cs.name, p.first.real(),
                    p.first.imag(), p.second.real(), p.second.imag(), rmax, stable ? "yes" : "NO", ratio);
        DAWLAB_CHECK(stable == cs.expectStable);
        DAWLAB_CHECK(stable == cs.c.isStable()); // 與 header 的三角形條件一致
        // 與 header 的 poles() 對答案（順序可能相反，比較兩種配對）
        const auto hp = cs.c.poles();
        const double d1 = std::abs(hp.first - p.first) + std::abs(hp.second - p.second);
        const double d2 = std::abs(hp.first - p.second) + std::abs(hp.second - p.first);
        DAWLAB_CHECK(std::min(d1, d2) < 1e-9);
        // 用 impulse 驗證：穩定的要衰減，不穩定 / 臨界的不會
        if (cs.expectStable)
            DAWLAB_CHECK(ratio < 1e-3);
        else
            DAWLAB_CHECK(ratio > 0.5);
    }

    // 共軛 pole 的幾何意義：半徑與角度 → 衰減時間與共振頻率（以 Q=8 的 LPF 為例）
    {
        const BiquadCoefficients c = BiquadCoefficients::makeLowpass(kSampleRate, 1000.0, 8.0);
        const double r = std::sqrt(c.a2);
        const double theta = std::acos(-c.a1 / (2.0 * r));
        const double fRes = theta * kSampleRate / kTwoPi;
        const double tau = -1.0 / std::log(r);
        std::printf("\nLPF 1 kHz Q=8：pole 半徑 r = %.6f，角度 θ = %.5f rad → 共振頻率 %.1f Hz，τ = %.0f samples (%.1f ms)\n",
                    r, theta, fRes, tau, samplesToMs(tau, kSampleRate));
        // 角度對應的頻率接近 fc（高 Q 時 pole 角度 ≈ 2π fc / fs）
        DAWLAB_CHECK_NEAR(fRes, 1000.0, 10.0);
        // 實測 impulse response 的包絡：第 n 個峰 / 第 0 個峰 ≈ r^n → 看 1000 個 sample 後
        Biquad f;
        f.prepare(kSampleRate, 512, 1);
        f.setCoefficients(c);
        double early = 0.0, late = 0.0;
        for (int i = 0; i < 2000; ++i) {
            const double y = std::abs(f.processSample(0, i == 0 ? 1.0f : 0.0f));
            if (i < 100)
                early = std::max(early, y);
            if (i >= 1000 && i < 1100)
                late = std::max(late, y);
        }
        const double expectedDecayDb = 20.0 * std::log10(std::pow(r, 1000.0));
        std::printf("impulse response 包絡：1000 samples 後衰減 %.2f dB（理論 20 log10(r^1000) = %.2f dB）\n",
                    gainToDb(late / early), expectedDecayDb);
        DAWLAB_CHECK_NEAR(gainToDb(late / early), expectedDecayDb, 1.0);
    }

    // 三角形條件：在 (a1, a2) 平面上掃描，和「算 pole」的判斷完全一致
    int agree = 0, total = 0;
    for (int i = -50; i <= 50; ++i)
        for (int j = -30; j <= 30; ++j) {
            const double a1 = i * 0.05, a2 = j * 0.05;
            // 剛好落在三角形邊界上的點（|p| = 1）兩種判斷方法都會受浮點誤差影響，略過
            if (std::abs(std::abs(a2) - 1.0) < 1e-9 || std::abs(std::abs(a1) - (1.0 + a2)) < 1e-9)
                continue;
            const bool triangle = std::abs(a2) < 1.0 && std::abs(a1) < 1.0 + a2;
            ++total;
            if (triangle == isStable(a1, a2))
                ++agree;
        }
    std::printf("三角形條件 vs 直接算 pole：%d / %d 個 (a1, a2) 點結論一致\n", agree, total);
    DAWLAB_CHECK(agree == total);

    return dawlab::test::summary("l02_ex5");
}
