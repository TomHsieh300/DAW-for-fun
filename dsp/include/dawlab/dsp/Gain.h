#pragma once
// Gain：最基本的 DSP 元件——把訊號乘上一個倍率。
//
//   y[n] = g · x[n]，g = 10^(dB/20)（polarity invert 時 g 取負號）
//
// 看似只有一個乘法，但要做對有三件事：
// 1. 參數用 dB 設定（人耳對音量是對數感知），內部換成線性倍率才乘進訊號。
// 2. 倍率改變時用 SmoothedValue 線性平滑（預設 50 ms），避免 click / zipper noise。
// 3. polarity invert 也走同一條 ramp（+g → −g 會經過 0），所以切換時不會爆音。
//
// 用法（setter 與 process() 在同一個 thread、block 之間呼叫；見下方「參數」的說明）：
//   dawlab::dsp::Gain gain;
//   gain.prepare(48000.0, 512, 2);
//   gain.setGainDb(-6.0f);
//   gain.process(block);           // in-place，整個 block、所有 channel 共用同一條 ramp
//
// processSample() 的慣例：以「frame」為單位呼叫，channel 0 會推進 ramp，其他 channel 沿用
// 同一個值。也就是每個 frame 先呼叫 processSample(0, x0)，再呼叫 processSample(1, x1)。

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>
#include <dawlab/dsp/SmoothedValue.h>

#include <algorithm>
#include <cmath>

namespace dawlab::dsp {

class Gain {
public:
    // 唯一可以 allocate 的地方（這個 class 其實不需要 allocate，但介面統一）
    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        (void)maxBlockSize;
        (void)numChannels;
        sampleRate_ = sampleRate;
        smoother_.reset(sampleRate, rampSeconds_);
        smoother_.setCurrentAndTargetValue(targetLinear());
        lastValue_ = smoother_.getCurrentValue();
    }

    // 清除內部狀態：把 ramp 直接對齊目標值（沒有 filter memory 要清）
    void reset() noexcept {
        smoother_.setCurrentAndTargetValue(targetLinear());
        lastValue_ = smoother_.getCurrentValue();
    }

    // ---- 參數 ----
    // 使用規則：參數 setter 必須在 block 之間、由呼叫 process() 的同一個 thread 呼叫。
    // 本章元件不是 thread-safe：gainDb_ 與 SmoothedValue 的狀態都是普通的 float / int，
    // UI thread 寫、audio thread 讀在 C++ 記憶體模型下就是 data race（undefined behavior），
    // 「硬體上對齊的 float 寫入是 atomic」不能當作 thread safety 的依據。
    // 跨 thread 的參數傳遞（std::atomic、parameter 系統）見第 13、14 章。

    // 允許的最大 gain（+40 dB = 100 倍）。再大幾乎一定是 bug（例如把倍率當成 dB 傳進來）。
    static constexpr float kMaxGainDb = 40.0f;

    // db ≤ kMinusInfinityDb（含 -inf）= 靜音；NaN 會被忽略（保持原值）；超過 kMaxGainDb 會被夾住。
    void setGainDb(float db) noexcept {
        if (std::isnan(db))
            return; // NaN 不可流進音訊：保持原本的設定
        gainDb_ = std::min(db, kMaxGainDb);
        smoother_.setTargetValue(targetLinear());
    }

    // 以線性倍率設定（負值或 0 視為 -inf dB）
    void setGainLinear(float linear) noexcept { setGainDb(gainToDb(linear)); }

    float getGainDb() const noexcept { return gainDb_; }
    float getGainLinear() const noexcept { return dbToGain(gainDb_); }

    // polarity invert（Ø）：乘上 -1。走同一條 ramp，所以切換時從 +g 平滑經過 0 到 -g。
    void setPolarityInverted(bool inverted) noexcept {
        inverted_ = inverted;
        smoother_.setTargetValue(targetLinear());
    }
    bool isPolarityInverted() const noexcept { return inverted_; }

    // ramp 長度（預設 0.05 s = 50 ms）。prepare 之後呼叫也可以，不會打斷目前的值。
    void setRampSeconds(double seconds) noexcept {
        rampSeconds_ = std::max(0.0, seconds);
        if (sampleRate_ > 0.0)
            smoother_.setRampLength(static_cast<int>(std::lround(rampSeconds_ * sampleRate_)));
    }
    double getRampSeconds() const noexcept { return rampSeconds_; }

    // 目前實際乘進訊號的倍率（ramp 進行中會介於舊值與新值之間），給 UI 或測試用
    float getCurrentGainLinear() const noexcept { return smoother_.getCurrentValue(); }
    bool isSmoothing() const noexcept { return smoother_.isSmoothing(); }

    // ---- 處理 ----

    // 逐 sample：channel 0 推進 ramp，其他 channel 沿用同一個值（見檔頭說明）
    float processSample(int channel, float x) noexcept {
        if (channel == 0)
            lastValue_ = smoother_.getNextValue();
        return x * lastValue_;
    }

    // in-place 處理整個 block：同一個 frame 的所有 channel 乘同一個 gain 值
    void process(AudioBlock block) noexcept {
        smoother_.applyGain(block);
        lastValue_ = smoother_.getCurrentValue();
    }

private:
    float targetLinear() const noexcept {
        const float g = dbToGain(gainDb_);
        return inverted_ ? -g : g;
    }

    double sampleRate_ = 0.0;
    double rampSeconds_ = 0.05;
    float gainDb_ = 0.0f;
    bool inverted_ = false;
    float lastValue_ = 1.0f;
    SmoothedValue smoother_{1.0f};
};

} // namespace dawlab::dsp
