#pragma once
// Lfo：低頻振盪器（Low-Frequency Oscillator）。第 10 章。
//
// LFO 不是拿來「聽」的訊號，而是拿來「控制」別的參數（delay 時間、gain、filter 頻率……）。
// 它的輸出固定在 -1..+1，由各效果器自己決定要把這個值對應到什麼範圍。
//
// 設計重點：
// - 內部只有一個 phase 累加器（0..1），每個 sample 前進 rate / sampleRate。
//   所有波形都是「phase 的函式」，所以 rate 改變時 phase 連續、不會跳。
// - Square / Saw / Sample&Hold 有瞬間跳變。LFO 本身的頻率太低，aliasing 不是問題，
//   但「被它控制的 gain 或 delay」瞬間跳變會在音訊裡產生 click，所以這幾種波形
//   預設經過一個 1 ms 的 one-pole 平滑（setSmoothingMs 可調，0 = 關閉）。
// - Sine / Triangle 本身連續，不平滑，才能保持精確的波形（例如 chorus 要算 pitch 偏移）。

#include <dawlab/DspMath.h>
#include <dawlab/Signals.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace dawlab::dsp {

enum class LfoShape { Sine, Triangle, Saw, Square, SampleAndHold };

namespace detail {

// 第 10 章效果器共用的一階（one-pole）參數平滑器：
//   y[n] = y[n-1] + k (target - y[n-1])，  k = 1 - exp(-1 / (τ · fs))
// τ 是時間常數：經過 τ 之後走完 63%，約 5τ 之後視為到達。
class ModParamSmoother {
public:
    explicit ModParamSmoother(float initialValue = 0.0f) noexcept : current_(initialValue), target_(initialValue) {}

    void prepare(double sampleRate, double timeConstantMs) noexcept {
        const double tauSamples = std::max(1.0, timeConstantMs * 0.001 * sampleRate);
        coeff_ = static_cast<float>(1.0 - std::exp(-1.0 / tauSamples));
    }
    void setCurrentAndTarget(float v) noexcept { current_ = target_ = v; }
    void setTarget(float v) noexcept { target_ = v; }
    float getTarget() const noexcept { return target_; }
    float getCurrent() const noexcept { return current_; }

    float next() noexcept {
        current_ += coeff_ * (target_ - current_);
        if (std::abs(target_ - current_) < 1e-6f) // 收斂後直接貼齊目標，避免 denormal 與永遠差一點點
            current_ = target_;
        return current_;
    }

private:
    float current_ = 0.0f;
    float target_ = 0.0f;
    float coeff_ = 1.0f;
};

// 給 UI thread 讀的量測值（例如目前的 gain、延遲、中心頻率）：audio thread 寫、UI thread 讀，
// 必須是 std::atomic，否則是 data race（authoring guide 4.1）。只需要「讀到某個最近的值」，
// 不需要和其他變數同步，所以 memory_order_relaxed 就夠了（x86 / ARM 上就是一般的 load / store）。
// std::atomic 本身不能複製；包一層讓擁有它的效果器 class 仍然可以被複製（例如以值回傳）。
class RelaxedAtomicFloat {
public:
    explicit RelaxedAtomicFloat(float v = 0.0f) noexcept : value_(v) {}
    RelaxedAtomicFloat(const RelaxedAtomicFloat& other) noexcept : value_(other.load()) {}
    RelaxedAtomicFloat& operator=(const RelaxedAtomicFloat& other) noexcept {
        store(other.load());
        return *this;
    }
    void store(float v) noexcept { value_.store(v, std::memory_order_relaxed); }
    float load() const noexcept { return value_.load(std::memory_order_relaxed); }

private:
    std::atomic<float> value_;
};

} // namespace detail

class Lfo {
public:
    void prepare(double sampleRate) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        updateIncrement();
        updateSmoothing();
        reset();
    }

    // 回到初始狀態：相位回到起始相位（setPhase 設定的值），Sample & Hold 的亂數序列也從種子重新開始。
    // 所以每次 reset() 之後，同樣的輸入一定得到同樣的輸出（離線 bounce 可以重現）。
    void reset() noexcept {
        phase_ = startPhase_;
        noise_ = NoiseGenerator(seed_);
        hold_ = noise_.next();
        smoothed_ = rawValue();
    }

    void setShape(LfoShape shape) noexcept { shape_ = shape; }
    LfoShape getShape() const noexcept { return shape_; }

    // 0 Hz 是合法的：LFO 停在目前相位（量測頻率響應時很好用）
    void setRateHz(float hz) noexcept {
        rateHz_ = std::max(0.0f, hz);
        updateIncrement();
    }
    float getRateHz() const noexcept { return rateHz_; }

    // 設定「目前」相位（0..1 = 一個週期），同時記住它作為 reset() 的起始相位。
    // chorus 的多個 voice、autopan 的左右聲道就是靠不同的起始相位錯開。
    void setPhase(float phase01) noexcept {
        startPhase_ = wrap(phase01);
        phase_ = startPhase_;
    }
    float getPhase() const noexcept { return static_cast<float>(phase_); }

    // Square / Saw / S&H 邊緣的平滑時間常數（ms）。0 = 不平滑（會有 click，示範用）
    void setSmoothingMs(float ms) noexcept {
        smoothingMs_ = std::max(0.0f, ms);
        updateSmoothing();
    }
    float getSmoothingMs() const noexcept { return smoothingMs_; }

    // Sample & Hold 用的亂數種子
    // 種子先經過 hash 打散：xorshift 從「很小的種子」起跑時，前幾個輸出的分布很差
    // （例如全部落在同一側），S&H 聽起來就不夠隨機。
    // 新的種子在下一次 reset()（或 prepare()）時生效。
    void setSeed(uint32_t seed) noexcept { seed_ = mixSeed(seed); }

    // 取出下一個值（-1..1）並讓相位前進一個 sample
    float getNextValue() noexcept {
        const float raw = rawValue();

        phase_ += increment_;
        if (phase_ >= 1.0) {
            phase_ -= 1.0;
            hold_ = noise_.next(); // S&H：每個週期開頭抽一個新的亂數
        }

        if (needsSmoothing() && smoothCoeff_ < 1.0f) {
            smoothed_ += smoothCoeff_ * (raw - smoothed_);
            return smoothed_;
        }
        smoothed_ = raw;
        return raw;
    }

    // 無狀態的波形函式：給定相位（0..1）回傳 -1..1。
    // chorus 用它從同一個相位推出「不同聲道錯開 90°」的值。S&H 需要狀態，這裡回傳 0。
    static float shapeValue(LfoShape shape, float phase01) noexcept {
        const float p = wrap(phase01);
        switch (shape) {
        case LfoShape::Sine:
            return static_cast<float>(std::sin(kTwoPi * p));
        case LfoShape::Triangle: // 0 → +1（p=0.25）→ -1（p=0.75）→ 0，和 sine 同相
            if (p < 0.25f)
                return 4.0f * p;
            if (p < 0.75f)
                return 2.0f - 4.0f * p;
            return 4.0f * p - 4.0f;
        case LfoShape::Saw: // -1 一路爬到 +1，週期結尾瞬間掉回 -1
            return 2.0f * p - 1.0f;
        case LfoShape::Square:
            return p < 0.5f ? 1.0f : -1.0f;
        case LfoShape::SampleAndHold:
        default:
            return 0.0f;
        }
    }

    // tempo sync：beats = 一個 LFO 週期有幾拍（1 = 四分音符、0.5 = 八分音符、0.75 = 附點八分、
    // 1/3 = 八分音符三連音的一個；三個八分三連音佔一拍）。rate = bpm / 60 / beats
    static float tempoToRateHz(double bpm, double beats) noexcept {
        if (bpm <= 0.0 || beats <= 0.0)
            return 0.0f;
        return static_cast<float>(bpm / 60.0 / beats);
    }

private:
    // 32-bit 整數 hash（lowbias32）：把相近的種子打散成看起來無關的值
    static uint32_t mixSeed(uint32_t s) noexcept {
        s ^= s >> 16;
        s *= 0x7feb352du;
        s ^= s >> 15;
        s *= 0x846ca68bu;
        s ^= s >> 16;
        return s != 0u ? s : 1u;
    }

    static float wrap(float p) noexcept {
        p -= std::floor(p);
        return p < 0.0f ? 0.0f : (p >= 1.0f ? 0.0f : p);
    }

    bool needsSmoothing() const noexcept {
        return shape_ == LfoShape::Square || shape_ == LfoShape::Saw || shape_ == LfoShape::SampleAndHold;
    }

    float rawValue() const noexcept {
        return shape_ == LfoShape::SampleAndHold ? hold_ : shapeValue(shape_, static_cast<float>(phase_));
    }

    void updateIncrement() noexcept { increment_ = rateHz_ / sampleRate_; }

    void updateSmoothing() noexcept {
        if (smoothingMs_ <= 0.0f) {
            smoothCoeff_ = 1.0f;
            return;
        }
        const double tau = smoothingMs_ * 0.001 * sampleRate_;
        smoothCoeff_ = static_cast<float>(1.0 - std::exp(-1.0 / std::max(1.0, tau)));
    }

    double sampleRate_ = 48000.0;
    LfoShape shape_ = LfoShape::Sine;
    float rateHz_ = 1.0f;
    double increment_ = 1.0 / 48000.0; // 相位用 double：rate 很低時 float 的累加誤差會讓頻率偏掉好幾 %
    double phase_ = 0.0;
    float startPhase_ = 0.0f;
    float hold_ = 0.0f;
    float smoothed_ = 0.0f;
    float smoothingMs_ = 1.0f;
    float smoothCoeff_ = 1.0f;
    uint32_t seed_ = mixSeed(20231u);
    NoiseGenerator noise_{seed_};
};

} // namespace dawlab::dsp
