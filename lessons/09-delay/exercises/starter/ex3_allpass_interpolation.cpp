// 練習 3 起始碼：allpass interpolation
//
// 目標：實作一階 allpass interpolator，讓 fractional delay 的幅度響應在所有頻率都是 0 dB，
// 並量測它的相位延遲與理論值比較。
//
//     H(z) = (η + z⁻¹) / (1 + η z⁻¹),   η = (1 − Δ) / (1 + Δ)
//     y[n] = η·(v[n] − y[n−1]) + v[n−1]      v[n] = x[n − d]
//
// 完成 TODO 之後執行 l09_ex3_starter，所有 check 應該通過。
// 提示：讓 Δ 落在 [0.5, 1.5)（d = floor(delay − 0.5)），η 就不會接近 1。

#include <dawlab/Common.h>
#include <dawlab/dsp/FractionalDelayLine.h>

#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

using dawlab::dsp::FractionalDelayLine;

namespace {

constexpr double kSampleRate = 48000.0;

class AllpassInterpolator {
public:
    void prepare(int maxDelaySamples) {
        line_.prepare(maxDelaySamples + 2, 1);
        reset();
    }

    void reset() noexcept {
        line_.reset();
        y1_ = 0.0f;
    }

    void setDelay(float delaySamples) noexcept {
        // TODO: 把 delaySamples 拆成整數 d_ 與小數 Δ（建議 Δ ∈ [0.5, 1.5)），
        //       再算 eta_ = (1 − Δ) / (1 + Δ)。
        d_ = static_cast<int>(delaySamples);
        eta_ = 0.0f;
    }

    float processSample(float x) noexcept {
        line_.push(0, x);
        // TODO: 讀出 v[n] = x[n−d] 與 v[n−1] = x[n−d−1]，套用
        //       y[n] = η·(v[n] − y[n−1]) + v[n−1]，更新 y1_，並 flush denormal。
        return line_.readInteger(0, d_); // 目前只有整數延遲：相位延遲的 check 會 FAIL
    }

    float getEta() const noexcept { return eta_; }
    int getIntegerDelay() const noexcept { return d_; }

private:
    FractionalDelayLine line_;
    int d_ = 0;
    float eta_ = 0.0f;
    float y1_ = 0.0f;
};

// allpass 的理論相位延遲（總延遲，單位 sample）
double allpassTheoryDelay(int d, double eta, double freq) {
    const double w = dawlab::kTwoPi * freq / kSampleRate;
    return d + 1.0 - (2.0 / w) * std::atan(eta * std::sin(w) / (1.0 + eta * std::cos(w)));
}

// 量測相位延遲（sample）：in / out 投影到 e^{−jωn} 取相位差，再用 expected 展開 2π 的模糊
template <typename Fn>
double measurePhaseDelaySamples(Fn&& fn, double freq, double expected) {
    const int skip = static_cast<int>(0.2 * kSampleRate);
    const int n = static_cast<int>(0.5 * kSampleRate);
    const double w = dawlab::kTwoPi * freq / kSampleRate;
    std::complex<double> sumIn(0.0, 0.0), sumOut(0.0, 0.0);
    for (int i = 0; i < skip + n; ++i) {
        const auto x = static_cast<float>(0.25 * std::sin(w * i));
        const float y = fn(x);
        if (i >= skip) {
            const int k = i - skip;
            const double win = 0.5 - 0.5 * std::cos(dawlab::kTwoPi * k / (n - 1));
            const std::complex<double> e(std::cos(w * i), -std::sin(w * i));
            sumIn += win * static_cast<double>(x) * e;
            sumOut += win * static_cast<double>(y) * e;
        }
    }
    const double phase = std::arg(sumOut * std::conj(sumIn));
    double delay = -phase / w;
    const double period = dawlab::kTwoPi / w;
    delay += period * std::round((expected - delay) / period);
    return delay;
}

double measureAllpassGainDb(float delay, double freq) {
    AllpassInterpolator ap;
    ap.prepare(64);
    ap.setDelay(delay);
    return dawlab::measureGainDb([&](float x) { return ap.processSample(x); }, freq, kSampleRate);
}

} // namespace

int main() {
    std::printf("=== 第 09 章 練習 3（起始碼）：allpass interpolation ===\n\n");
    const float delay = 10.5f;

    std::printf("幅度響應（dB）應該全部 ≈ 0：\n");
    for (double f : {100.0, 1000.0, 4000.0, 12000.0, 20000.0}) {
        const double g = measureAllpassGainDb(delay, f);
        std::printf("  %8.0f Hz  %+6.2f dB\n", f, g);
        DAWLAB_CHECK_NEAR(g, 0.0, 0.05);
    }

    std::printf("相位延遲（sample）應該符合理論：\n");
    for (double f : {100.0, 1000.0, 4000.0, 8000.0}) {
        AllpassInterpolator ap;
        ap.prepare(64);
        ap.setDelay(delay);
        const double t = measurePhaseDelaySamples([&](float x) { return ap.processSample(x); }, f, delay);
        const double theory = allpassTheoryDelay(ap.getIntegerDelay(), ap.getEta(), f);
        std::printf("  %8.0f Hz  量到 %7.3f  理論 %7.3f\n", f, t, theory);
        DAWLAB_CHECK_NEAR(t, theory, 0.02);
    }
    {
        AllpassInterpolator ap;
        ap.prepare(64);
        ap.setDelay(delay);
        DAWLAB_CHECK_NEAR(measurePhaseDelaySamples([&](float x) { return ap.processSample(x); }, 100.0, delay),
                          delay, 0.01);
    }
    for (float d : {10.1f, 10.3f, 10.7f, 10.9f}) {
        AllpassInterpolator ap;
        ap.prepare(64);
        ap.setDelay(d);
        DAWLAB_CHECK_NEAR(measurePhaseDelaySamples([&](float x) { return ap.processSample(x); }, 100.0, d), d, 0.01);
    }
    return dawlab::test::summary("l09_ex3_starter");
}
