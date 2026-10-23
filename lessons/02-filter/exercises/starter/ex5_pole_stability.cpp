// 練習 5：從 biquad 係數算出 pole 並判斷穩定性（起始碼）。
//
// 完成 TODO：
//   1. computePoles()：解 z² + a1 z + a2 = 0（用 std::complex 的 sqrt，判別式為負時會得到共軛 pole）。
//   2. isStable()：所有 pole 的 |p| < 1。
//   3. tailToHeadRatio()：送 impulse 進 filter，回傳「最後 10% 的峰值 / 前 10% 的峰值」。
// 驗證：與 BiquadCoefficients::poles() / isStable() 一致；穩定的 filter impulse response 會衰減。
#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>

#include <complex>
#include <cstdio>
#include <utility>

using namespace dawlab;
using dawlab::dsp::Biquad;
using dawlab::dsp::BiquadCoefficients;

constexpr double kSampleRate = 48000.0;

using Poles = std::pair<std::complex<double>, std::complex<double>>;

static Poles computePoles(double a1, double a2) {
    // TODO: p = (-a1 ± sqrt(a1² - 4 a2)) / 2
    (void)a1;
    (void)a2;
    return {0.0, 0.0};
}

static bool isStable(double a1, double a2) {
    // TODO: 兩個 pole 的 |p| 都 < 1
    (void)a1;
    (void)a2;
    return false;
}

static double tailToHeadRatio(const BiquadCoefficients& c, int n) {
    // TODO: 用 Biquad 跑 impulse，比較最後 n/10 個與最前 n/10 個 sample 的峰值
    (void)c;
    (void)n;
    return 1.0;
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
        {"a1=-1.8  a2=0.81", raw(-1.8, 0.81), true},
        {"a1=-1.9  a2=1.0", raw(-1.9, 1.0), false},
        {"a1=-2.1  a2=1.1025", raw(-2.1, 1.1025), false},
    };
    for (const Case& cs : cases) {
        const Poles p = computePoles(cs.c.a1, cs.c.a2);
        const bool stable = isStable(cs.c.a1, cs.c.a2);
        const double ratio = tailToHeadRatio(cs.c, 20000);
        std::printf("%-22s p1 = %.4f%+.4fj  p2 = %.4f%+.4fj  stable=%s  tail/head=%.3g\n", cs.name, p.first.real(),
                    p.first.imag(), p.second.real(), p.second.imag(), stable ? "yes" : "NO", ratio);
        DAWLAB_CHECK(stable == cs.expectStable);
        const auto hp = cs.c.poles();
        const double d1 = std::abs(hp.first - p.first) + std::abs(hp.second - p.second);
        const double d2 = std::abs(hp.first - p.second) + std::abs(hp.second - p.first);
        DAWLAB_CHECK(std::min(d1, d2) < 1e-9);
        if (cs.expectStable)
            DAWLAB_CHECK(ratio < 1e-3);
        else
            DAWLAB_CHECK(ratio > 0.5);
    }
    return dawlab::test::summary("l02_ex5_starter");
}
