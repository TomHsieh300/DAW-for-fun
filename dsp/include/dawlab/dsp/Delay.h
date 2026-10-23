#pragma once
// Delay：立體聲 echo / delay 效果器（第 09 章）。
//
// 訊號路徑（每個 channel，一般模式）：
//
//   x ──┬──────────────────────────────(1-mix)──┐
//       │                                       (+)──► y
//       │   ┌─────────────────┐                 │
//       └──►(+)──► delay line ├──┬────(mix)──────┘
//           ▲    └─────────────────┘  │
//           │                         │
//           └── fb × HP × LP ◄────────┘   (feedback 迴路內的濾波：tape echo 的質感)
//
// ping-pong 模式（立體聲）：輸入先混成 mono 進左邊的 delay line，
// 左邊的輸出餵右邊的 delay line，右邊的輸出再 × feedback 回到左邊：
//
//   (L+R)/2 ──►(+)──► delay L ──┬──► wet L
//               ▲               │
//               │        fb×filt│
//               │               ▼
//          fb×filt ◄── delay R ◄┘
//                        │
//                        └──► wet R
//
// 重點：
// - delay time 線性平滑（預設 50 ms ramp），改變時不會 click，
//   而是短暫的 pitch 變化（Doppler），這是「平滑過渡」策略；crossfade 策略留在練習 6。
//   這條 ramp 用 double 自己算（DelayRamp），不用 float 的 SmoothedValue：delay time 以 sample 計可以到
//   數十萬，float 在 65536 以上的間距（ulp）是 1/128 sample，每個 sample 累加一小步會被捨入、
//   誤差一路累積，ramp 結束時讀取位置會「跳回」目標值 → click。見 DelayRamp 的註解。
// - 使用 FractionalDelayLine 的 cubic 插值，所以 delay time 可以是任意毫秒數、也可以連續變化。
//   讀取時把 double 位置拆成「整數 + 小數」再交給 readCubicSplit，長 delay 也保有 sub-sample 精度。
// - feedback 會被 clamp 到 [0, 0.99]：|feedback| < 1 是穩定的必要條件。
// - 只有 prepare() 會 allocate；process() 不 allocate、不 lock、不 I/O。
// - 寫進 delay line 的值會 flush denormal（見 flushDenormal），長時間靜音後 CPU 不會突然變重。
// - mono（numChannels = 1）時 setPingPong(true) 沒有作用，等同一般 echo。
// - 所有 setter 都忽略 NaN（保留上一個有效值），±inf 會被 clamp 到合法範圍。
// - 不回報 latency：延遲是效果本身，host 不該補償。
// - prepare() 之前呼叫 reset() / process() 是安全的（什麼都不做）。

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>
#include <dawlab/dsp/FractionalDelayLine.h>
#include <dawlab/dsp/OnePole.h>
#include <dawlab/dsp/SmoothedValue.h>

#include <algorithm>
#include <cmath>

namespace dawlab::dsp {

class Delay {
public:
    enum class Interpolation { Linear, Cubic };

    static constexpr int kMaxChannels = 2;
    static constexpr float kMaxFeedback = 0.99f;

    // ---- 設定（prepare 之前）------------------------------------------------
    // 最大 delay time（決定 prepare 時配置的 buffer 長度）。預設 2000 ms。
    // NaN 會被忽略（std::clamp / std::max 遇到 NaN 會原樣傳下去，之後算出的讀取位置就是垃圾）。
    void setMaxDelayMs(float ms) noexcept {
        if (!std::isnan(ms))
            maxDelayMs_ = std::clamp(ms, 1.0f, 60000.0f);
    }

    // ---- 標準介面 -----------------------------------------------------------
    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        (void)maxBlockSize; // 逐 sample 處理，不需要暫存 block
        sampleRate_ = sampleRate;
        numChannels_ = std::clamp(numChannels, 1, kMaxChannels);
        maxDelaySamples_ = msToSamples(static_cast<double>(maxDelayMs_), sampleRate) + 2;
        line_.prepare(maxDelaySamples_, numChannels_);

        lowpass_.setType(OnePole::Type::Lowpass);
        lowpass_.prepare(sampleRate, maxBlockSize, numChannels_);
        lowpass_.setCutoff(lowpassHz_);
        highpass_.setType(OnePole::Type::Highpass);
        highpass_.prepare(sampleRate, maxBlockSize, numChannels_);
        highpass_.setCutoff(highpassHz_);

        delayRamp_.setRampSamples(rampSecondsToSamples(delayRampSeconds_));
        delayRamp_.setCurrentAndTarget(targetDelaySamples());
        feedback_.reset(sampleRate, kParamRampSeconds);
        feedback_.setCurrentAndTargetValue(feedbackTarget_);
        mix_.reset(sampleRate, kParamRampSeconds);
        mix_.setCurrentAndTargetValue(mixTarget_);
        prepared_ = true;
    }

    void reset() noexcept {
        line_.reset();
        lowpass_.reset();
        highpass_.reset();
        // 參數直接跳到目標值：reset 通常發生在 transport 停止 / 重新開始時，不需要平滑。
        // prepare() 之前 sample rate 與 buffer 長度都還沒決定，沒有東西可以跳。
        if (!prepared_)
            return;
        delayRamp_.setCurrentAndTarget(targetDelaySamples());
        feedback_.setCurrentAndTargetValue(feedbackTarget_);
        mix_.setCurrentAndTargetValue(mixTarget_);
    }

    // ---- 參數 ---------------------------------------------------------------
    // delay time（毫秒），會以 setDelayRampSeconds 的時間線性平滑到新值。
    void setDelayMs(float ms) noexcept {
        if (std::isnan(ms))
            return;
        delayMs_ = std::clamp(ms, 0.0f, maxDelayMs_);
        if (prepared_)
            delayRamp_.setTarget(targetDelaySamples());
    }

    // delay time 變化的 ramp 長度（秒）。越長 pitch 偏移越小、但到達新值越慢。預設 0.05 s。
    void setDelayRampSeconds(double seconds) noexcept {
        if (!std::isfinite(seconds))
            return;
        delayRampSeconds_ = std::clamp(seconds, 0.0, 10.0);
        if (prepared_)
            delayRamp_.setRampSamples(rampSecondsToSamples(delayRampSeconds_)); // 正在 ramp 時從目前位置以新長度重新出發
    }

    // feedback 0..0.99（每一次回圈乘上的倍率）。≥ 1 會發散，所以直接 clamp。
    void setFeedback(float feedback) noexcept {
        if (std::isnan(feedback))
            return;
        feedbackTarget_ = std::clamp(feedback, 0.0f, kMaxFeedback);
        if (prepared_)
            feedback_.setTargetValue(feedbackTarget_);
    }

    // dry/wet：0 = 只有原音，1 = 只有 delay。y = (1-mix)·dry + mix·wet（線性 crossfade）。
    void setMix(float mix) noexcept {
        if (std::isnan(mix))
            return;
        mixTarget_ = std::clamp(mix, 0.0f, 1.0f);
        if (prepared_)
            mix_.setTargetValue(mixTarget_);
    }

    void setPingPong(bool enabled) noexcept { pingPong_ = enabled; }

    // feedback 迴路內的 one-pole lowpass；≥ 20000 Hz 視為關閉。
    void setLowpassHz(float hz) noexcept {
        if (std::isnan(hz))
            return;
        lowpassHz_ = std::clamp(hz, 20.0f, 20000.0f);
        lowpassEnabled_ = lowpassHz_ < 20000.0f;
        if (prepared_)
            lowpass_.setCutoff(lowpassHz_);
    }

    // feedback 迴路內的 one-pole highpass；≤ 20 Hz 視為關閉。
    void setHighpassHz(float hz) noexcept {
        if (std::isnan(hz))
            return;
        highpassHz_ = std::clamp(hz, 20.0f, 20000.0f);
        highpassEnabled_ = highpassHz_ > 20.0f;
        if (prepared_)
            highpass_.setCutoff(highpassHz_);
    }

    void setInterpolation(Interpolation interpolation) noexcept { interpolation_ = interpolation; }

    float getDelayMs() const noexcept { return delayMs_; }
    float getMaxDelayMs() const noexcept { return maxDelayMs_; }
    float getFeedback() const noexcept { return feedbackTarget_; }
    float getMix() const noexcept { return mixTarget_; }
    bool isPingPong() const noexcept { return pingPong_; }
    int getMaxDelaySamples() const noexcept { return maxDelaySamples_; }

    // 目前（平滑中）的 delay time，單位毫秒；給 UI 或範例觀察 ramp 用。
    float getCurrentDelayMs() const noexcept {
        return prepared_ ? static_cast<float>(delayRamp_.getCurrent() * 1000.0 / sampleRate_) : delayMs_;
    }

    // tempo sync：beats 拍 的長度（毫秒）。1 拍 = 四分音符。
    //   八分音符 = 0.5、附點八分 = 0.75、三連音八分 = 1/3、十六分 = 0.25。
    //   ms = beats × 60000 / bpm
    static float noteToMs(double bpm, double beats) noexcept {
        return bpm > 0.0 ? static_cast<float>(beats * 60000.0 / bpm) : 0.0f;
    }

    // ---- 處理 ---------------------------------------------------------------
    void process(AudioBlock block) noexcept {
        const int numSamples = block.getNumSamples();
        const int nch = std::min(block.getNumChannels(), numChannels_);
        if (!prepared_ || nch <= 0)
            return;

        if (pingPong_ && nch == 2)
            processPingPong(block, numSamples);
        else
            processIndependent(block, nch, numSamples);
    }

private:
    static constexpr double kParamRampSeconds = 0.02;

    // delay time（以 sample 計）的線性 ramp，全部用 double。
    //
    // 為什麼不用第 01 章的 SmoothedValue（float、每個 sample current += step）？
    // 例：2000 ms buffer、48 kHz，1900 ms → 1901 ms，50 ms ramp：
    //   位置約 91200 sample，落在 [65536, 131072)，float 的 ulp = 2^-7 = 0.0078；
    //   step = 48 / 2400 = 0.02 = 2.56 ulp，每次相加都被捨入成 3 ulp = 0.0234，
    //   2399 步累積多走了約 8.2 sample，最後一步再「跳回」目標 → 讀取位置瞬間跳 8 個 sample = click。
    //   15000 ms 時 ulp = 0.0625 > 2 × step，current += step 根本加不上去：先靜止 2400 sample 再一次跳 48 sample。
    // 解法兩個：(1) 用 double（ulp 小 2^29 倍）；(2) 不累加，每次由 current = target − step × countdown 直接算，
    // 誤差永遠只有一次捨入、不會累積。這裡兩個都做。
    class DelayRamp {
    public:
        void setRampSamples(int numSamples) noexcept {
            rampSamples_ = std::max(0, numSamples);
            if (countdown_ > 0)
                start(); // 正在 ramp：從目前位置以新長度重新出發
        }
        void setCurrentAndTarget(double value) noexcept {
            current_ = target_ = value;
            step_ = 0.0;
            countdown_ = 0;
        }
        void setTarget(double value) noexcept {
            if (value == target_)
                return;
            target_ = value;
            start();
        }
        double getNext() noexcept {
            if (countdown_ > 0) {
                --countdown_;
                current_ = target_ - step_ * static_cast<double>(countdown_); // 不累加：countdown = 0 時剛好 = target
            }
            return current_;
        }
        double getCurrent() const noexcept { return current_; }

    private:
        void start() noexcept {
            if (rampSamples_ <= 0) {
                setCurrentAndTarget(target_);
                return;
            }
            countdown_ = rampSamples_;
            step_ = (target_ - current_) / static_cast<double>(rampSamples_);
        }

        double current_ = 2.0;
        double target_ = 2.0;
        double step_ = 0.0;
        int rampSamples_ = 0;
        int countdown_ = 0;
    };

    int rampSecondsToSamples(double seconds) const noexcept {
        return static_cast<int>(std::lround(seconds * sampleRate_));
    }

    double targetDelaySamples() const noexcept {
        // 至少 2 個 sample（cubic 讀取需要 d-1 ≥ 1）、至多 buffer 允許的最大值
        const double s = static_cast<double>(delayMs_) * 0.001 * sampleRate_;
        return std::clamp(s, 2.0, std::max(2.0, static_cast<double>(maxDelaySamples_)));
    }

    // 讀取「D 個 sample 以前」的值。因為我們是先讀再 push（這個 sample 還沒寫進去），
    // readXxx(ch, k) 實際對應 k+1 個 sample 的延遲，所以要讀 D - 1。
    // 位置是 double：先拆成整數 + 小數（小數部分轉成 float 也不會失去精度），再交給 Split 版本讀取。
    float readDelayed(int ch, double delaySamples) const noexcept {
        const double idx = delaySamples - 1.0;
        const int d = static_cast<int>(idx); // floor（idx ≥ 1）
        const float frac = static_cast<float>(idx - static_cast<double>(d));
        return interpolation_ == Interpolation::Cubic ? line_.readCubicSplit(ch, d, frac)
                                                      : line_.readLinearSplit(ch, d, frac);
    }

    // feedback 迴路是遞迴結構：輸入靜音後，回聲每圈乘 fb，最後會掉進 denormal 範圍
    // （|x| < 1.2e-38，CPU 處理這種數字可能慢上百倍）。小於 -300 dBFS 的值直接清成 0，聽不出差別。
    static float flushDenormal(float x) noexcept { return std::abs(x) < 1.0e-15f ? 0.0f : x; }

    float feedbackFilter(int ch, float x) noexcept {
        if (highpassEnabled_)
            x = highpass_.processSample(ch, x);
        if (lowpassEnabled_)
            x = lowpass_.processSample(ch, x);
        return x;
    }

    void processIndependent(AudioBlock block, int nch, int numSamples) noexcept {
        for (int i = 0; i < numSamples; ++i) {
            const double d = delayRamp_.getNext();
            const float fb = feedback_.getNextValue();
            const float mix = mix_.getNextValue();
            for (int ch = 0; ch < nch; ++ch) {
                float* data = block.getChannelPointer(ch);
                const float x = data[i];
                const float wet = readDelayed(ch, d);
                line_.push(ch, flushDenormal(x + fb * feedbackFilter(ch, wet))); // feedback：延遲輸出濾波後回到輸入
                data[i] = (1.0f - mix) * x + mix * wet;
            }
        }
    }

    void processPingPong(AudioBlock block, int numSamples) noexcept {
        float* left = block.getChannelPointer(0);
        float* right = block.getChannelPointer(1);
        for (int i = 0; i < numSamples; ++i) {
            const double d = delayRamp_.getNext();
            const float fb = feedback_.getNextValue();
            const float mix = mix_.getNextValue();
            const float xl = left[i];
            const float xr = right[i];
            const float mono = 0.5f * (xl + xr);
            const float wetL = readDelayed(0, d);
            const float wetR = readDelayed(1, d);
            line_.push(0, flushDenormal(mono + fb * feedbackFilter(0, wetR))); // 右邊的輸出回到左邊
            line_.push(1, flushDenormal(fb * feedbackFilter(1, wetL)));        // 左邊的輸出送到右邊
            left[i] = (1.0f - mix) * xl + mix * wetL;
            right[i] = (1.0f - mix) * xr + mix * wetR;
        }
    }

    FractionalDelayLine line_;
    OnePole lowpass_;
    OnePole highpass_;
    DelayRamp delayRamp_; // delay time（sample），double 精度的線性 ramp
    SmoothedValue feedback_;
    SmoothedValue mix_;

    double sampleRate_ = 48000.0;
    double delayRampSeconds_ = 0.05;
    int numChannels_ = 2;
    int maxDelaySamples_ = 0;
    float maxDelayMs_ = 2000.0f;
    float delayMs_ = 375.0f;
    float feedbackTarget_ = 0.4f;
    float mixTarget_ = 0.3f;
    float lowpassHz_ = 20000.0f;
    float highpassHz_ = 20.0f;
    bool lowpassEnabled_ = false;
    bool highpassEnabled_ = false;
    bool pingPong_ = false;
    bool prepared_ = false;
    Interpolation interpolation_ = Interpolation::Cubic;
};

} // namespace dawlab::dsp
