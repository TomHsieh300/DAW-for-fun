// 練習 3 解答：one-pole 指數平滑器（exponential smoother）與時間常數量測
//
//   y[n] = y[n-1] + a · (target − y[n-1])，   a = 1 − exp(−1 / (τ · fs))
//
// 這是最簡單的一階 IIR 低通濾波器（第 02 章會正式介紹）。對一個階梯輸入，
// 輸出是 y[n] = target · (1 − (1−a)^n) = target · (1 − e^{−n/(τ·fs)})：
//   n = τ·fs      → 63.2%
//   n = 4.605·τ·fs → 99%   （ln 100 = 4.605）
//   n = 6.908·τ·fs → 99.9% （ln 1000 = 6.908，也就是剩餘誤差 −60 dB）
//
// 與線性 ramp 的差別：線性 ramp 固定時間走完、斜率固定；指數平滑永遠不會「真的」到達目標，
// 但一開始走得快、接近目標時走得慢，聽起來更像類比推桿 / 電容充電。
//
// 精度陷阱（實作時真的會踩到）：如果 current_ 用 float 存，當剩餘距離 d = target − current 小到
// a·d 比 current 的半個 ulp 還小時，current_ += a·d 等於沒加——狀態會「卡」住。
// current 略小於 1（落在 [0.5, 1)）時 ulp = 2^-24 ≈ 6e-8、半個 ulp ≈ 3e-8，所以卡在
// d ≈ 3e-8 / a = ε/(4a) 的地方（ε = 2^-23 ≈ 1.19e-7 是 1.0 的 ulp；τ = 10 ms 時 a ≈ 0.0021
// → 卡在 ≈ 1.4e-5），永遠到不了 1e-6 的 snap 門檻，isSmoothing() 就永遠是 true。解法：狀態用 double（ε ≈ 2.2e-16），回傳時再轉 float。
// 第 02 章的 one-pole filter 會再遇到同一件事。

#include <dawlab/Common.h>
#include <dawlab/dsp/SmoothedValue.h>

#include <cmath>
#include <cstdio>

using namespace dawlab;

class ExpSmoother {
public:
    // τ = timeConstantSeconds：輸出走到 63.2% 所需的時間
    void reset(double sampleRate, double timeConstantSeconds) noexcept {
        const double tauSamples = timeConstantSeconds * sampleRate;
        // a = 1 − e^{−1/τfs}；τ = 0 時 a = 1（直接跳到目標）
        coeff_ = tauSamples > 0.0 ? 1.0 - std::exp(-1.0 / tauSamples) : 1.0;
        current_ = target_;
    }

    void setCurrentAndTargetValue(float v) noexcept { current_ = target_ = v; }
    void setTargetValue(float v) noexcept { target_ = v; }

    float getNextValue() noexcept {
        // 差分方程式：每個 sample 走「剩餘距離」的 a 倍
        current_ += coeff_ * (target_ - current_);
        // 剩餘誤差小到聽不見（且避免 denormal）時直接對齊目標
        if (std::abs(target_ - current_) < kSnapThreshold)
            current_ = target_;
        return static_cast<float>(current_);
    }

    float getCurrentValue() const noexcept { return static_cast<float>(current_); }
    float getTargetValue() const noexcept { return static_cast<float>(target_); }
    bool isSmoothing() const noexcept { return current_ != target_; }
    float getCoefficient() const noexcept { return static_cast<float>(coeff_); }

private:
    static constexpr double kSnapThreshold = 1.0e-6; // 低於 −120 dB 的差距視為已到達
    double coeff_ = 1.0;
    double current_ = 0.0;
    double target_ = 0.0;
};

// 對階梯 0 → 1 量測：輸出第一次 ≥ fraction 的 sample index（從 1 起算）
int samplesToReach(ExpSmoother& s, float fraction, int maxSamples) {
    s.setCurrentAndTargetValue(0.0f);
    s.setTargetValue(1.0f);
    for (int n = 1; n <= maxSamples; ++n)
        if (s.getNextValue() >= fraction)
            return n;
    return -1;
}

int main() {
    constexpr double sr = 48000.0;
    const double tauMs = 10.0;
    const int tauSamples = msToSamples(tauMs, sr); // 480

    ExpSmoother s;
    s.reset(sr, tauMs * 0.001);
    std::printf("τ = %.1f ms = %d samples，a = %.6f（理論 1 − e^(−1/480) = %.6f）\n", tauMs, tauSamples,
                s.getCoefficient(), 1.0 - std::exp(-1.0 / tauSamples));
    DAWLAB_CHECK_NEAR(s.getCoefficient(), 1.0 - std::exp(-1.0 / tauSamples), 1e-6);

    // 時間常數量測：63.2% 應該在 ≈ τ，99% 在 ≈ 4.605 τ，99.9% 在 ≈ 6.908 τ
    const int n632 = samplesToReach(s, 0.632f, 100000);
    const int n99 = samplesToReach(s, 0.99f, 100000);
    const int n999 = samplesToReach(s, 0.999f, 100000);
    std::printf("到達 63.2%%：%d samples（%.2f τ，期望 1.00 τ）\n", n632, double(n632) / tauSamples);
    std::printf("到達 99%%  ：%d samples（%.2f τ，期望 ln(100) = %.3f τ）\n", n99, double(n99) / tauSamples, std::log(100.0));
    std::printf("到達 99.9%%：%d samples（%.2f τ，期望 ln(1000) = %.3f τ）\n", n999, double(n999) / tauSamples,
                std::log(1000.0));
    DAWLAB_CHECK_NEAR(n632, tauSamples, 3);
    DAWLAB_CHECK_NEAR(n99, std::log(100.0) * tauSamples, 0.01 * tauSamples * std::log(100.0));
    DAWLAB_CHECK_NEAR(n999, std::log(1000.0) * tauSamples, 0.01 * tauSamples * std::log(1000.0));

    // 解析解對照：y[n] = 1 − (1−a)^n
    s.setCurrentAndTargetValue(0.0f);
    s.setTargetValue(1.0f);
    float maxErr = 0.0f;
    bool monotonic = true;
    float prev = 0.0f;
    for (int n = 1; n <= 5 * tauSamples; ++n) {
        const float y = s.getNextValue();
        const double expected = 1.0 - std::pow(1.0 - double(s.getCoefficient()), n);
        maxErr = std::max(maxErr, static_cast<float>(std::abs(y - expected)));
        monotonic = monotonic && (y >= prev);
        prev = y;
    }
    std::printf("與解析解 1 − (1−a)^n 的最大誤差：%.2e；單調遞增：%s；沒有超過目標：%s\n", maxErr, monotonic ? "yes" : "no",
                prev <= 1.0f ? "yes" : "no");
    DAWLAB_CHECK(maxErr < 1e-4f);
    DAWLAB_CHECK(monotonic);
    DAWLAB_CHECK(prev <= 1.0f);

    // snap：夠久之後必須「真的」到達目標並停止平滑（否則永遠 isSmoothing）
    for (int n = 0; n < 20 * tauSamples; ++n)
        s.getNextValue();
    DAWLAB_CHECK(!s.isSmoothing());
    DAWLAB_CHECK(s.getCurrentValue() == 1.0f);

    // 與線性 ramp 比較：同樣「50 ms」，線性剛好在 2400 sample 走完；指數 τ = 50 ms 時只走到 63%
    dsp::SmoothedValue lin;
    lin.reset(sr, 0.05);
    lin.setCurrentAndTargetValue(0.0f);
    lin.setTargetValue(1.0f);
    ExpSmoother e;
    e.reset(sr, 0.05);
    e.setCurrentAndTargetValue(0.0f);
    e.setTargetValue(1.0f);
    float linAt = 0.0f, expAt = 0.0f;
    for (int n = 0; n < msToSamples(50.0, sr); ++n) {
        linAt = lin.getNextValue();
        expAt = e.getNextValue();
    }
    std::printf("50 ms 後：線性 ramp = %.3f，指數（τ = 50 ms）= %.3f\n", linAt, expAt);
    DAWLAB_CHECK_NEAR(linAt, 1.0, 1e-6);
    DAWLAB_CHECK_NEAR(expAt, 1.0 - std::exp(-1.0), 2e-3);

    return dawlab::test::summary("l01_ex3");
}
