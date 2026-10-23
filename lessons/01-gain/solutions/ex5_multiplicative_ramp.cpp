// 練習 5 解答：multiplicative（exponential）ramp —— 在 dB 上走直線的 fader automation
//
// 本章的 dsp::SmoothedValue 是 linear ramp：每個 sample 加同一個 step，所以「倍率」對時間是直線。
// 但耳朵聽的是 dB。從 1.0 線性走到 0.001（−60 dB）時，ramp 走到一半倍率還有 0.5（−6 dB），
// 90% 的時間都在 −20 dB 以上，最後幾十個 sample 才一口氣掉下去——聽起來是「一直很大聲，最後突然消失」。
//
// Multiplicative ramp 改成每個 sample「乘」同一個倍率 r：
//     r = (target / current)^(1/N)        → g[n] = current · r^n
//     dB[n] = dB[0] + n · 20·log10(r)     → 在 dB 上是一條直線，每個 sample 前進同樣的 dB
// 例：100 ms（4800 samples @ 48 kHz）從 0 dB 到 −60 dB，每個 sample −0.0125 dB，一半的時間剛好 −30 dB。
// （JUCE 的 SmoothedValue<float, ValueSmoothingTypes::Multiplicative> 就是這個做法。）
//
// 唯一的麻煩是 0：0 dB 乘任何倍率都到不了 0，0 也乘不出任何東西。
// 所以把 0 視為一個「地板」kFloorGain = −100 dB（= kMinusInfinityDb），ramp 在地板與目標之間走，
// 最後一個 sample 再「snap」成精確的目標值（mute 時就是精確的 0）。
//
// 執行：l01_ex5_solution

#include <dawlab/Common.h>
#include <dawlab/dsp/SmoothedValue.h>

#include <cmath>
#include <cstdio>
#include <vector>

using namespace dawlab;

class MultiplicativeRamp {
public:
    static constexpr double kFloorGain = 1.0e-5; // −100 dB：0 的替身（dbToGain(kMinusInfinityDb) 也是 0）

    void reset(double sampleRate, double rampSeconds) noexcept {
        rampSamples_ = std::max(0, static_cast<int>(std::lround(rampSeconds * sampleRate)));
        setCurrentAndTargetValue(target_);
    }

    void setCurrentAndTargetValue(float value) noexcept {
        target_ = std::max(0.0f, value);
        current_ = target_;
        countdown_ = 0;
    }

    void setTargetValue(float value) noexcept {
        value = std::max(0.0f, value); // 只處理非負的 gain（倍率的 ratio 不能跨過 0）
        if (value == target_)
            return;
        target_ = value;
        if (rampSamples_ == 0) {
            setCurrentAndTargetValue(value);
            return;
        }
        // 從「目前的值」出發（ramp 中途改目標也不會跳），0 換成地板
        const double from = std::max(current_, kFloorGain);
        const double to = std::max(static_cast<double>(value), kFloorGain);
        current_ = from;
        multiplier_ = std::pow(to / from, 1.0 / static_cast<double>(rampSamples_));
        countdown_ = rampSamples_;
    }

    float getNextValue() noexcept {
        if (countdown_ <= 0)
            return target_;
        if (--countdown_ == 0) {
            current_ = target_; // 最後一步 snap：避免累乘的捨入誤差，也讓 mute 停在精確的 0
            return target_;
        }
        current_ *= multiplier_; // 狀態用 double：4800 次累乘後的誤差 ~1e-13，float 會到 ~1e-4
        return static_cast<float>(current_);
    }

    bool isSmoothing() const noexcept { return countdown_ > 0; }
    float getTargetValue() const noexcept { return target_; }
    double getMultiplier() const noexcept { return multiplier_; }

private:
    double current_ = 1.0;
    double multiplier_ = 1.0;
    float target_ = 1.0f;
    int rampSamples_ = 0;
    int countdown_ = 0;
};

namespace {

constexpr double kSr = 48000.0;
constexpr int kRamp = 4800; // 100 ms

// 跑完整條 ramp（含最後 snap 的那一步），回傳 kRamp 個值
template <typename Ramp>
std::vector<float> runRamp(Ramp& r, float from, float to) {
    r.setCurrentAndTargetValue(from);
    r.setTargetValue(to);
    std::vector<float> v(static_cast<size_t>(kRamp));
    for (auto& x : v)
        x = r.getNextValue();
    return v;
}

// 相鄰兩個 sample 之間最大的 |ΔdB|（從起點值開始算）
double maxDbStep(const std::vector<float>& v, float from) {
    double prev = gainToDb(static_cast<double>(from)), m = 0.0;
    for (size_t i = 0; i + 1 < v.size(); ++i) { // 最後一個（snap）不算：mute 時它是 −100 → 0
        const double d = gainToDb(static_cast<double>(v[i]));
        m = std::max(m, std::abs(d - prev));
        prev = d;
    }
    return m;
}

// ramp 期間有幾成時間在 thresholdDb 以上
double fractionAbove(const std::vector<float>& v, double thresholdDb) {
    int count = 0;
    for (float x : v)
        if (gainToDb(static_cast<double>(x)) > thresholdDb)
            ++count;
    return static_cast<double>(count) / static_cast<double>(v.size());
}

} // namespace

int main() {
    MultiplicativeRamp mul;
    mul.reset(kSr, 0.1);
    dsp::SmoothedValue lin;
    lin.reset(kSr, 0.1);

    // ---- 1. 0 dB → −60 dB：multiplicative 在 dB 上是直線 ----------------------------------------
    const std::vector<float> m = runRamp(mul, 1.0f, 0.001f);
    const std::vector<float> l = runRamp(lin, 1.0f, 0.001f);
    const double stepDb = 20.0 * std::log10(mul.getMultiplier());
    std::printf("multiplier r = %.6f（每個 sample %.4f dB）\n", mul.getMultiplier(), stepDb);
    DAWLAB_CHECK_NEAR(stepDb, -60.0 / kRamp, 1e-6);

    const double mHalf = gainToDb(static_cast<double>(m[kRamp / 2 - 1])); // 第 2400 個 sample
    const double lHalf = gainToDb(static_cast<double>(l[kRamp / 2 - 1]));
    std::printf("走到一半（2400 samples）：multiplicative %.2f dB，linear %.2f dB\n", mHalf, lHalf);
    DAWLAB_CHECK_NEAR(mHalf, -30.0, 0.01);
    DAWLAB_CHECK_NEAR(lHalf, -6.01, 0.01);

    double maxDev = 0.0; // 每個 sample 與「dB 直線」的偏差
    for (int i = 0; i < kRamp; ++i)
        maxDev = std::max(maxDev, std::abs(gainToDb(static_cast<double>(m[static_cast<size_t>(i)]))
                                           - (-60.0 * (i + 1) / kRamp)));
    std::printf("與 dB 直線的最大偏差：%.5f dB\n", maxDev);
    DAWLAB_CHECK(maxDev < 1e-3);

    const double mAbove = fractionAbove(m, -20.0), lAbove = fractionAbove(l, -20.0);
    std::printf("ramp 期間在 -20 dB 以上的時間：multiplicative %.1f%%，linear %.1f%%\n", 100.0 * mAbove,
                100.0 * lAbove);
    DAWLAB_CHECK_NEAR(mAbove, 1.0 / 3.0, 0.001);
    DAWLAB_CHECK_NEAR(lAbove, 0.9 / 0.999, 0.001);

    const double mStep = maxDbStep(m, 1.0f), lStep = maxDbStep(l, 1.0f);
    std::printf("相鄰 sample 最大 |ΔdB|：multiplicative %.4f dB，linear %.3f dB（最後幾個 sample）\n", mStep, lStep);
    DAWLAB_CHECK_NEAR(mStep, 0.0125, 1e-4);
    DAWLAB_CHECK(lStep > 1.0);

    DAWLAB_CHECK(m.back() == 0.001f); // 最後一步 snap 成精確的目標
    DAWLAB_CHECK(!mul.isSmoothing());

    // ---- 2. mute：0 dB → 0（走到 −100 dB 地板，最後 snap 成精確的 0） -----------------------------
    const std::vector<float> mute = runRamp(mul, 1.0f, 0.0f);
    const double muteHalf = gainToDb(static_cast<double>(mute[kRamp / 2 - 1]));
    std::printf("mute：一半 %.2f dB，倒數第二個 %.2f dB，最後一個 = %g\n", muteHalf,
                gainToDb(static_cast<double>(mute[kRamp - 2])), static_cast<double>(mute.back()));
    DAWLAB_CHECK_NEAR(muteHalf, -50.0, 0.01);
    DAWLAB_CHECK(mute[kRamp - 2] > 0.0f && mute[kRamp - 2] < 1.1e-5f);
    DAWLAB_CHECK(mute.back() == 0.0f);

    // ---- 3. unmute：0 → 0 dB（從地板出發，第一個 sample 不是 0 也不會跳上去） ------------------------
    const std::vector<float> un = runRamp(mul, 0.0f, 1.0f);
    std::printf("unmute：第一個 sample %.3e（%.4f dB），最後一個 %.6f\n", static_cast<double>(un.front()),
                gainToDb(static_cast<double>(un.front())), static_cast<double>(un.back()));
    DAWLAB_CHECK(un.front() > 0.0f && un.front() < 2.0e-5f);
    DAWLAB_CHECK(un.back() == 1.0f);

    // ---- 4. ramp 中途改目標：從目前的值出發，不跳 -----------------------------------------------------
    mul.setCurrentAndTargetValue(1.0f);
    mul.setTargetValue(0.001f);
    float prev = 1.0f;
    for (int i = 0; i < kRamp / 4; ++i) // 走 1/4 → −15 dB
        prev = mul.getNextValue();
    mul.setTargetValue(1.0f);
    const float next = mul.getNextValue();
    const double jumpDb = gainToDb(static_cast<double>(next)) - gainToDb(static_cast<double>(prev));
    std::printf("走到 %.2f dB 時改回 0 dB：下一個 sample 只前進 %+.4f dB\n", gainToDb(static_cast<double>(prev)),
                jumpDb);
    DAWLAB_CHECK_NEAR(gainToDb(static_cast<double>(prev)), -15.0, 0.01);
    DAWLAB_CHECK(jumpDb > 0.0 && jumpDb < 0.01);

    // ---- 試聽：1 kHz 正弦波，前 2 秒 linear fade-out、後 2 秒 multiplicative fade-out（各 1.5 秒）----
    {
        MultiplicativeRamp fm;
        fm.reset(kSr, 1.5);
        dsp::SmoothedValue fl;
        fl.reset(kSr, 1.5);
        AudioBuffer demo = makeSine(1000.0, kSr, 4.0, 0.5f, 1);
        const int half = demo.getNumSamples() / 2;
        fl.setCurrentAndTargetValue(1.0f);
        fl.setTargetValue(0.0f);
        fm.setCurrentAndTargetValue(1.0f);
        fm.setTargetValue(0.0f);
        float* d = demo.getWritePointer(0);
        for (int i = 0; i < half; ++i)
            d[i] *= fl.getNextValue();
        for (int i = half; i < demo.getNumSamples(); ++i)
            d[i] *= fm.getNextValue();
        writeWav(outputPath("l01_ex5_multiplicative_ramp.wav"), demo, kSr);
        std::printf("輸出 dawlab_output/l01_ex5_multiplicative_ramp.wav（前半 linear、後半 multiplicative fade-out）\n");
    }

    return dawlab::test::summary("l01_ex5");
}
