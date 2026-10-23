#pragma once
// 第 04 章：EnvelopeFollower（envelope 偵測器）與 AttackReleaseFilter（attack / release 平滑器）。
//
// 這兩個 class 是所有 dynamics 處理器（compressor、limiter、gate、expander、de-esser）的共同地基：
//
//   AttackReleaseFilter  一個「上升用 attack 係數、下降用 release 係數」的 one-pole 平滑器。
//                        輸入可以是任何量（線性振幅、x²、或 dB 值），所以 compressor 可以拿它在
//                        log(dB) domain 平滑 gain reduction，而 EnvelopeFollower 拿它在 linear domain
//                        平滑 |x|。
//   EnvelopeFollower     吃原始 sample，回傳線性振幅 envelope（Peak 或 RMS 模式）。
//                        第 08 章的 gate / expander / de-esser 會直接重用它。
//
// 核心數學（one-pole 平滑）：
//   y[n] = α·y[n-1] + (1-α)·x[n]，   α = exp(-1 / (τ·fs))
//   τ 是時間常數（秒）：對一個 step 輸入，y 在 t = τ 時到達目標的 63.2%（1 - 1/e），
//   t = 2.3τ 到達 90%。詳細推導見 lessons/04-compressor/README.md。
//
// 只依賴 common/（dawlab/DspMath.h）與標準函式庫，不依賴其他章節。

#include <dawlab/DspMath.h>

#include <algorithm>
#include <cmath>

namespace dawlab::dsp {

// 偵測器模式：Peak = 看瞬間絕對值 |x|；Rms = 看 x² 的移動平均再開根號
enum class DetectorMode { Peak, Rms };

// 平滑器的兩種結構（Giannoulis, Massberg & Reiss 2012，eq. 16 與 eq. 17）：
//   Branching：依「目標值比目前狀態高或低」切換 attack / release 係數（最直覺的寫法）
//   Decoupled：先做一個「瞬間上升、以 release 衰減」的 peak-hold，再用 attack 係數平滑；
//              對正弦波這種週期訊號不會因 ripple 而低估 peak，attack 曲線也精確是 1 - e^(-t/τ)
enum class SmoothingMode { Branching, Decoupled };

// α = exp(-1 / (τ·fs))，τ 以毫秒給。τ <= 0 代表「瞬間」（α = 0，輸出直接等於輸入）。
inline float attackReleaseCoefficient(double timeMs, double sampleRate) noexcept {
    if (timeMs <= 0.0 || sampleRate <= 0.0)
        return 0.0f;
    return static_cast<float>(std::exp(-1.0 / (timeMs * 0.001 * sampleRate)));
}

// 反向換算：已知 α 與 fs，求 τ（毫秒）。τ = -1 / (fs · ln α)
inline double coefficientToTimeMs(double alpha, double sampleRate) noexcept {
    if (alpha <= 0.0 || alpha >= 1.0 || sampleRate <= 0.0)
        return 0.0;
    return -1000.0 / (sampleRate * std::log(alpha));
}

class AttackReleaseFilter {
public:
    void prepare(double sampleRate) {
        sampleRate_ = sampleRate;
        updateCoefficients();
        reset();
    }

    void reset() noexcept {
        y_ = 0.0f;
        peakHold_ = 0.0f;
    }

    void setAttackMs(float ms) noexcept {
        attackMs_ = std::max(0.0f, ms);
        updateCoefficients();
    }

    void setReleaseMs(float ms) noexcept {
        releaseMs_ = std::max(0.0f, ms);
        updateCoefficients();
    }

    void setMode(SmoothingMode mode) noexcept { mode_ = mode; }

    float getAttackMs() const noexcept { return attackMs_; }
    float getReleaseMs() const noexcept { return releaseMs_; }
    float getAttackCoefficient() const noexcept { return attackCoeff_; }
    float getReleaseCoefficient() const noexcept { return releaseCoeff_; }
    SmoothingMode getMode() const noexcept { return mode_; }
    float getCurrentValue() const noexcept { return y_; }

    // 直接把狀態設成某個值（例如 compressor 想從「不壓縮」開始）
    void setCurrentValue(float value) noexcept {
        y_ = value;
        peakHold_ = value;
    }

    // 餵進一個「目標值」，回傳平滑後的值。
    float process(float target) noexcept {
        // non-finite 防護：遞迴濾波器只要吃進一個 Inf / NaN，狀態就會永久卡在 NaN
        // （NaN 參與的任何運算結果都是 NaN），之後再也回不來，只能 reset()。
        // 所以壞掉的輸入直接當成 0（忽略這個 sample），狀態維持有限值。
        if (!std::isfinite(target))
            target = 0.0f;
        if (mode_ == SmoothingMode::Branching) {
            // y[n] = α·y[n-1] + (1-α)·x[n]；x 比 y 高用 attack 係數，否則用 release 係數
            const float a = target > y_ ? attackCoeff_ : releaseCoeff_;
            y_ = target + a * (y_ - target); // 同一條式子的另一種寫法，少一次乘法
        } else {
            // Decoupled：y1[n] = max(x[n], αR·y1[n-1] + (1-αR)·x[n])   ← peak-hold，瞬間上升、release 衰減
            //            y[n]  = αA·y[n-1] + (1-αA)·y1[n]               ← attack 平滑
            peakHold_ = std::max(target, target + releaseCoeff_ * (peakHold_ - target));
            y_ = peakHold_ + attackCoeff_ * (y_ - peakHold_);
        }
        // denormal 防護：遞迴衰減到極小的值會變成 denormal，讓 CPU 變慢；直接 flush 成 0。
        // 同時再擋一次 non-finite（例如極大的有限輸入在運算中溢位成 Inf），確保狀態永遠可恢復。
        if (std::abs(y_) < kDenormalFlush || !std::isfinite(y_))
            y_ = 0.0f;
        if (std::abs(peakHold_) < kDenormalFlush || !std::isfinite(peakHold_))
            peakHold_ = 0.0f;
        return y_;
    }

private:
    static constexpr float kDenormalFlush = 1.0e-15f;

    void updateCoefficients() noexcept {
        attackCoeff_ = attackReleaseCoefficient(attackMs_, sampleRate_);
        releaseCoeff_ = attackReleaseCoefficient(releaseMs_, sampleRate_);
    }

    double sampleRate_ = 48000.0;
    float attackMs_ = 10.0f;
    float releaseMs_ = 100.0f;
    float attackCoeff_ = 0.0f;
    float releaseCoeff_ = 0.0f;
    SmoothingMode mode_ = SmoothingMode::Branching;
    float y_ = 0.0f;
    float peakHold_ = 0.0f;
};

// Envelope follower：輸入原始 sample，輸出線性振幅 envelope（永遠 >= 0）。
//
//   Peak 模式：env = smooth(|x|)
//   Rms  模式：env = sqrt(smooth(x²))   ← attack / release 作用在 x² 上，所以 x² 到 63% 時
//                                        振幅只到 sqrt(0.632) ≈ 79.5%；想用「RMS 視窗」的概念時
//                                        通常 attack = release = 視窗長度。
class EnvelopeFollower {
public:
    void prepare(double sampleRate) {
        filter_.prepare(sampleRate);
        reset();
    }

    void reset() noexcept {
        filter_.reset();
        envelope_ = 0.0f;
    }

    void setAttackMs(float ms) noexcept { filter_.setAttackMs(ms); }
    void setReleaseMs(float ms) noexcept { filter_.setReleaseMs(ms); }
    void setMode(DetectorMode mode) noexcept { mode_ = mode; }
    void setSmoothingMode(SmoothingMode mode) noexcept { filter_.setMode(mode); }

    float getAttackMs() const noexcept { return filter_.getAttackMs(); }
    float getReleaseMs() const noexcept { return filter_.getReleaseMs(); }
    DetectorMode getMode() const noexcept { return mode_; }
    SmoothingMode getSmoothingMode() const noexcept { return filter_.getMode(); }

    // 目前的 envelope（線性振幅），不改變狀態
    float getEnvelope() const noexcept { return envelope_; }

    float processSample(float x) noexcept {
        // 壞輸入防護：Inf / NaN 當成 0；極大值先夾在 ±kMaxInput，避免 x² 溢位成 Inf
        // （|x| 大於約 1.8e19 時 x² 就超過 float 上限）。正常音訊遠小於這個值，不受影響。
        if (!std::isfinite(x))
            x = 0.0f;
        x = std::clamp(x, -kMaxInput, kMaxInput);
        if (mode_ == DetectorMode::Peak) {
            envelope_ = filter_.process(std::abs(x));
        } else {
            const float meanSquare = filter_.process(x * x);
            envelope_ = std::sqrt(std::max(0.0f, meanSquare));
        }
        return envelope_;
    }

private:
    static constexpr float kMaxInput = 1.0e6f; // +120 dBFS，x² = 1e12，遠在 float 範圍內

    AttackReleaseFilter filter_;
    DetectorMode mode_ = DetectorMode::Peak;
    float envelope_ = 0.0f;
};

} // namespace dawlab::dsp
