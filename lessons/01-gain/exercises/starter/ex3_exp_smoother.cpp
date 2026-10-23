// 練習 3（起始碼）：one-pole 指數平滑器（exponential smoother）與時間常數量測
//
// 目標：實作 ExpSmoother，使得對階梯輸入 0 → 1：
//   y[n] = y[n-1] + a · (target − y[n-1])，a = 1 − exp(−1 / (τ · fs))
//   在 n = τ·fs 時到達 63.2%，n = 4.605·τ·fs 時到達 99%。
// 完成 TODO 之後，self-check 全部通過就算完成。起始碼執行時會印出 FAIL，這是預期的。

#include <dawlab/Common.h>
#include <dawlab/dsp/SmoothedValue.h>

#include <cmath>
#include <cstdio>

using namespace dawlab;

class ExpSmoother {
public:
    // τ = timeConstantSeconds：輸出走到 63.2% 所需的時間
    void reset(double sampleRate, double timeConstantSeconds) noexcept {
        (void)sampleRate;
        (void)timeConstantSeconds;
        // TODO: 由 τ 與 sample rate 算出 coeff_（τ = 0 時 coeff_ = 1），並把 current_ 對齊 target_
        coeff_ = 1.0;
        current_ = target_;
    }

    void setCurrentAndTargetValue(float v) noexcept { current_ = target_ = v; }
    void setTargetValue(float v) noexcept { target_ = v; }

    float getNextValue() noexcept {
        // TODO: 套用差分方程式；當 |target − current| 小於 kSnapThreshold 時直接對齊 target
        //       （否則永遠到不了目標，isSmoothing() 永遠是 true，而且會產生 denormal）
        return static_cast<float>(current_);
    }

    float getCurrentValue() const noexcept { return static_cast<float>(current_); }
    float getTargetValue() const noexcept { return static_cast<float>(target_); }
    bool isSmoothing() const noexcept { return current_ != target_; }
    float getCoefficient() const noexcept { return static_cast<float>(coeff_); }

private:
    // 狀態用 double：float 的話，接近目標時 a·(target − current) 會小於半個 ulp，狀態會卡住到不了目標
    static constexpr double kSnapThreshold = 1.0e-6;
    double coeff_ = 1.0;
    double current_ = 0.0;
    double target_ = 0.0;
};

// 對階梯 0 → 1 量測：輸出第一次 ≥ fraction 的 sample index（從 1 起算），找不到回傳 -1
int samplesToReach(ExpSmoother& s, float fraction, int maxSamples) {
    (void)s;
    (void)fraction;
    (void)maxSamples;
    // TODO: 把 s 設成 current = 0、target = 1，逐 sample 呼叫 getNextValue() 直到 ≥ fraction
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

    const int n632 = samplesToReach(s, 0.632f, 100000);
    const int n99 = samplesToReach(s, 0.99f, 100000);
    const int n999 = samplesToReach(s, 0.999f, 100000);
    std::printf("到達 63.2%%：%d samples（期望 ≈ %d）\n", n632, tauSamples);
    std::printf("到達 99%%  ：%d samples（期望 ≈ %.0f）\n", n99, std::log(100.0) * tauSamples);
    std::printf("到達 99.9%%：%d samples（期望 ≈ %.0f）\n", n999, std::log(1000.0) * tauSamples);
    DAWLAB_CHECK_NEAR(n632, tauSamples, 3);
    DAWLAB_CHECK_NEAR(n99, std::log(100.0) * tauSamples, 0.01 * tauSamples * std::log(100.0));
    DAWLAB_CHECK_NEAR(n999, std::log(1000.0) * tauSamples, 0.01 * tauSamples * std::log(1000.0));

    // 與解析解 y[n] = 1 − (1−a)^n 對照，且必須單調遞增、不超過目標
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
    DAWLAB_CHECK(maxErr < 1e-4f);
    DAWLAB_CHECK(monotonic);
    DAWLAB_CHECK(prev <= 1.0f);

    // 夠久之後必須真的到達目標並停止平滑
    for (int n = 0; n < 20 * tauSamples; ++n)
        s.getNextValue();
    DAWLAB_CHECK(!s.isSmoothing());
    DAWLAB_CHECK(s.getCurrentValue() == 1.0f);

    return dawlab::test::summary("l01_ex3_starter");
}
