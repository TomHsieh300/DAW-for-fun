#pragma once
// SmoothedValue：把參數的「瞬間跳變」變成「線性斜坡」（linear ramp）。
//
// 為什麼需要它？任何會乘進音訊的參數（gain、pan、mix……）如果在兩個 sample 之間瞬間改變，
// 等於把訊號乘上一個階梯函數（step function）。階梯的頻譜是 1/f 的寬頻能量，
// 聽起來就是 click；推桿連續移動時每一小步都產生一次 click，就是所謂的 zipper noise。
// 解法：讓參數在 rampSeconds 內從目前值「走」到目標值，每個 sample 前進一小步。
//
// 用法（在 audio thread 上完全不 allocate）：
//
//   dawlab::dsp::SmoothedValue gain;
//   gain.reset(48000.0, 0.05);           // prepare：設定 ramp 長度 = 50 ms
//   gain.setCurrentAndTargetValue(1.0f); // 初始值（不要從 0 ramp 上來）
//   ...
//   gain.setTargetValue(0.5f);           // UI 改了參數：之後 2400 個 sample 會線性走到 0.5
//   for (int i = 0; i < n; ++i)
//       y[i] = x[i] * gain.getNextValue();
//
// 設計重點：
// - 每次 setTargetValue() 都從「目前值」重新出發，所以推桿連續移動時不會跳。
// - 目標值與目前值相同時不會啟動 ramp（isSmoothing() == false），process() 可走快速路徑。
// - 不用累加（current += step）：每一步都由 current = target − step·countdown 直接算出，
//   所以長 ramp、大數值（例如以 sample 為單位、上萬的 delay time）也不會因 float 累加誤差
//   超過目標再跳回來；走完 ramp 時 countdown = 0，current 剛好等於 target。
// - 不是 thread-safe：所有 method 都必須由同一個 thread（audio thread）呼叫，setter 在 block 之間呼叫。
//   成員都是普通的 float / int，跨 thread 讀寫是 data race（UB）；見第 13、14 章。
// - 這是「線性」平滑：固定時間內走完、斜率固定。one-pole 指數平滑（永遠走不到、但更像類比電路）
//   在本章練習 3 實作。

#include <dawlab/AudioBuffer.h>

#include <algorithm>
#include <cmath>

namespace dawlab::dsp {

class SmoothedValue {
public:
    SmoothedValue() = default;
    explicit SmoothedValue(float initialValue) noexcept : current_(initialValue), target_(initialValue) {}

    // 設定 sample rate 與 ramp 時間。會停止進行中的 ramp（current = target）。
    // 在 prepare() 階段呼叫；rampSeconds = 0 代表不平滑（直接跳到目標值）。
    void reset(double sampleRate, double rampSeconds) noexcept {
        rampSamples_ = std::max(0, static_cast<int>(std::lround(rampSeconds * sampleRate)));
        current_ = target_;
        countdown_ = 0;
        step_ = 0.0f;
    }

    // 只改 ramp 長度、不打斷目前的值；若正在 ramp，會以新的長度從目前值重新出發。
    void setRampLength(int numSamples) noexcept {
        rampSamples_ = std::max(0, numSamples);
        if (countdown_ > 0)
            startRamp();
    }

    // 同時設定目前值與目標值（例如初始化、或 reset() 時不想有 ramp）
    void setCurrentAndTargetValue(float value) noexcept {
        current_ = target_ = value;
        countdown_ = 0;
        step_ = 0.0f;
    }

    // 設定新的目標值：從「目前值」開始、用 rampSamples_ 個 sample 線性走到目標
    void setTargetValue(float value) noexcept {
        if (value == target_)
            return;
        target_ = value;
        startRamp();
    }

    // 取出下一個 sample 要用的值（每個 sample 呼叫一次）
    float getNextValue() noexcept {
        if (countdown_ <= 0)
            return target_;
        --countdown_;
        // 線性：current[n] = target − step·countdown[n]（等於 start + n·step，但不累加誤差）；
        // countdown 走到 0 時剛好是 target
        current_ = valueAtCountdown();
        return current_;
    }

    float getCurrentValue() const noexcept { return countdown_ > 0 ? current_ : target_; }
    float getTargetValue() const noexcept { return target_; }
    bool isSmoothing() const noexcept { return countdown_ > 0; }
    int getRampSamples() const noexcept { return rampSamples_; }
    int getRemainingSamples() const noexcept { return countdown_; }

    // 跳過 numSamples 個 sample（例如這一段 buffer 其實是靜音，不需要逐 sample 乘）
    void skip(int numSamples) noexcept {
        if (countdown_ <= 0 || numSamples <= 0)
            return;
        if (numSamples >= countdown_) {
            countdown_ = 0;
            current_ = target_;
            return;
        }
        countdown_ -= numSamples;
        current_ = valueAtCountdown();
    }

    // 便利函式：把連續 n 個平滑後的值乘進 data[0..n)
    void applyGain(float* data, int n) noexcept {
        if (!isSmoothing()) {
            const float g = target_;
            for (int i = 0; i < n; ++i)
                data[i] *= g;
            return;
        }
        for (int i = 0; i < n; ++i)
            data[i] *= getNextValue();
    }

    // 便利函式：整個 block、所有 channel 共用同一條 ramp（同一個 frame 的每個 channel 乘同一個值）
    void applyGain(AudioBlock block) noexcept {
        const int numChannels = block.getNumChannels();
        const int numSamples = block.getNumSamples();
        if (!isSmoothing()) {
            block.multiplyBy(target_);
            return;
        }
        for (int i = 0; i < numSamples; ++i) {
            const float g = getNextValue();
            for (int ch = 0; ch < numChannels; ++ch)
                block.getChannelPointer(ch)[i] *= g;
        }
    }

private:
    // ramp 上「還剩 countdown_ 步」的值；用 double 算乘法，大數值時也保留 float 的完整精度
    float valueAtCountdown() const noexcept {
        if (countdown_ <= 0)
            return target_;
        return static_cast<float>(static_cast<double>(target_)
                                  - static_cast<double>(step_) * static_cast<double>(countdown_));
    }

    void startRamp() noexcept {
        if (rampSamples_ <= 0) {
            current_ = target_;
            countdown_ = 0;
            step_ = 0.0f;
            return;
        }
        countdown_ = rampSamples_;
        step_ = (target_ - current_) / static_cast<float>(rampSamples_);
    }

    float current_ = 0.0f;
    float target_ = 0.0f;
    float step_ = 0.0f;
    int rampSamples_ = 0;
    int countdown_ = 0;
};

} // namespace dawlab::dsp
