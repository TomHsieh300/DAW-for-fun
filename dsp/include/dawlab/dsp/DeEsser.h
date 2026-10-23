#pragma once
// DeEsser.h —— de-esser（第 08 章）
//
// 人聲的齒音（sibilance：s、sh、ch、t）能量集中在 5–10 kHz，聽起來刺耳。
// de-esser = 「只聽高頻的 compressor」：
//
//   x ──┬──► bandpass(f, Q=1.5) ──► EnvelopeFollower ──► GR = clamp(level − T, 0, range) ──► g
//       │                                                                                   │
//       ├──(wideband)  y = x · g                                                            ◄┘
//       └──(split-band) 以 Linkwitz-Riley 4 階分頻：y = LP(x) + HP(x) · g（只壓高頻段）
//
// - 偵測用 bandpass 濾波後的訊號（中心 f、Q = 1.5）。analog prototype 的 −3 dB 頻寬是 0.72 f ~ 1.39 f
//   （f = 7 kHz 時 5.05–9.71 kHz）；數位 RBJ bandpass 的頻寬會被 bilinear transform 壓窄
//   （1/Q = 2 sinh(ln2/2 · BW · ω0/sin ω0)），48 kHz 時實際約 5.22–9.17 kHz（見 README 2.5.2）。
// - wideband：整段訊號一起壓，簡單但齒音出現時母音也會被壓小（可能聽到「吸氣」感）。
// - split-band：分頻點 = 0.72 f（analog prototype 的下 −3 dB 點），只壓高頻段，母音不受影響。
//   LR4 的 LP + HP 相加為 allpass（振幅平坦、相位改變），gain = 1 時聲音不變（除了相位）。
// - listen：輸出偵測訊號（bandpass 後的 x），用來調 frequency / threshold。
// - range：最大衰減（正值，例如 12 dB）——不要把齒音壓到完全消失，會變「大舌頭」。
//
// 這裡的 gain computer 用 ∞:1（超過 threshold 幾 dB 就壓幾 dB，上限 range），對 de-esser 來說最直覺。
// threshold 與 range 直接出現在 GR 公式裡，所以兩者都經過 SmoothedValue（kParameterRampMs）平滑，
// 在 block 之間改它們不會讓 gain 瞬間跳變；reset() 之後、處理第一個 sample 之前的 setter 直接生效。
// 輸入的 NaN / Inf 在進入任何 biquad 之前就換成 0：TDF-II 的狀態一旦變成 NaN 就再也回不來。

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>
#include <dawlab/dsp/Biquad.h>
#include <dawlab/dsp/EnvelopeFollower.h>
#include <dawlab/dsp/SmoothedValue.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

namespace dawlab::dsp {

class DeEsser {
public:
    static constexpr float kDetectorQ = 1.5f;
    // 分頻點 / 偵測中心頻率 = (sqrt(1 + 4Q²) − 1) / (2Q)，Q = 1.5 → 0.7208（analog prototype bandpass 的下 −3 dB 點；
    // 數位 bandpass 的實際下緣稍高，48 kHz、f = 7 kHz 時約 0.745 f）
    static constexpr float kCrossoverRatio = 0.7208f;
    // Linkwitz-Riley 4 階 = 兩個串聯的 2 階 Butterworth（Q = 1/√2）
    static constexpr double kButterworthQ = 0.70710678118654752;
    // threshold / range 改變時的線性 ramp 長度
    static constexpr float kParameterRampMs = 20.0f;

    DeEsser() = default;
    // std::atomic 成員讓 class 預設不能複製；手動定義，才能放進 std::vector（第 14 章的 8 軌 DAW）
    DeEsser(const DeEsser& other) { copyFrom(other); }
    DeEsser& operator=(const DeEsser& other) {
        if (this != &other)
            copyFrom(other);
        return *this;
    }

    // ---- 生命週期 -------------------------------------------------------
    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        sampleRate_ = sampleRate;
        numChannels_ = numChannels;
        detectorFilter_.prepare(sampleRate, maxBlockSize, numChannels);
        lowpass1_.prepare(sampleRate, maxBlockSize, numChannels);
        lowpass2_.prepare(sampleRate, maxBlockSize, numChannels);
        highpass1_.prepare(sampleRate, maxBlockSize, numChannels);
        highpass2_.prepare(sampleRate, maxBlockSize, numChannels);
        detectorOut_.assign(static_cast<size_t>(numChannels), 0.0f);
        follower_.prepare(sampleRate);
        follower_.setMode(DetectorMode::Peak);
        follower_.setAttackMs(attackMs_);
        follower_.setReleaseMs(releaseMs_);
        thresholdSmoothed_.reset(sampleRate, kParameterRampMs * 0.001);
        rangeSmoothed_.reset(sampleRate, kParameterRampMs * 0.001);
        updateFilters();
        reset();
    }

    void reset() noexcept {
        detectorFilter_.reset();
        lowpass1_.reset();
        lowpass2_.reset();
        highpass1_.reset();
        highpass2_.reset();
        follower_.reset();
        thresholdSmoothed_.setCurrentAndTargetValue(thresholdDb_);
        rangeSmoothed_.setCurrentAndTargetValue(rangeDb_);
        hasProcessed_ = false;
        gainReductionDbCurrent_ = 0.0f;
        gain_ = 1.0f;
        gainReductionDb_.store(0.0f, std::memory_order_relaxed);
    }

    // ---- 參數 -----------------------------------------------------------
    // 偵測中心頻率（Hz），預設 7000；分頻點會跟著 = 0.72 × f
    void setFrequency(float hz) noexcept {
        frequencyHz_ = std::clamp(hz, 1000.0f, static_cast<float>(sampleRate_ * 0.45));
        updateFilters();
    }
    void setThresholdDb(float db) noexcept {
        thresholdDb_ = db;
        setSmoothedTarget(thresholdSmoothed_, thresholdDb_);
    }
    // 最大衰減（正值，dB）；正負號一律視為 |db|
    void setRangeDb(float db) noexcept {
        rangeDb_ = std::abs(db);
        setSmoothedTarget(rangeSmoothed_, rangeDb_);
    }
    void setSplitBand(bool enabled) noexcept { splitBand_ = enabled; }
    void setListen(bool enabled) noexcept { listen_ = enabled; }
    // 額外：偵測器 ballistics（預設 attack 1 ms、release 40 ms）
    void setAttackMs(float ms) noexcept {
        attackMs_ = std::max(0.0f, ms);
        follower_.setAttackMs(attackMs_);
    }
    void setReleaseMs(float ms) noexcept {
        releaseMs_ = std::max(0.0f, ms);
        follower_.setReleaseMs(releaseMs_);
    }

    float getFrequency() const noexcept { return frequencyHz_; }
    float getCrossoverHz() const noexcept { return frequencyHz_ * kCrossoverRatio; }
    float getThresholdDb() const noexcept { return thresholdDb_; }
    float getRangeDb() const noexcept { return rangeDb_; }
    bool isSplitBand() const noexcept { return splitBand_; }
    bool isListen() const noexcept { return listen_; }
    float getAttackMs() const noexcept { return attackMs_; }
    float getReleaseMs() const noexcept { return releaseMs_; }
    int getLatencySamples() const noexcept { return 0; } // IIR 分頻：沒有整數 sample 的延遲

    // 正值：目前衰減多少 dB（block 結束時更新；thread-safe）
    float getGainReductionDb() const noexcept { return gainReductionDb_.load(std::memory_order_relaxed); }

    // ---- 處理 -----------------------------------------------------------
    void process(AudioBlock block) noexcept {
        const int n = block.getNumSamples();
        const int nch = std::min(block.getNumChannels(), numChannels_);
        for (int i = 0; i < n; ++i) {
            // 0. NaN / Inf 防護（與 EnvelopeFollower 相同）：先清理再送進 biquad，否則濾波器狀態會永久變成 NaN
            for (int ch = 0; ch < nch; ++ch) {
                float& x = block.getChannelPointer(ch)[i];
                if (!std::isfinite(x))
                    x = 0.0f;
            }
            // 1. 偵測：每個 channel 各自 bandpass，取 max|·| 做 stereo-link
            float det = 0.0f;
            for (int ch = 0; ch < nch; ++ch) {
                const float d = detectorFilter_.processSample(ch, block.getChannelPointer(ch)[i]);
                detectorOut_[static_cast<size_t>(ch)] = d;
                det = std::max(det, std::abs(d));
            }
            // 2. gain computer：∞:1，上限 range（threshold / range 用平滑後的值）
            const float levelDb = gainToDb(follower_.processSample(det));
            const float thresholdDb = thresholdSmoothed_.getNextValue();
            const float rangeDb = rangeSmoothed_.getNextValue();
            hasProcessed_ = true;
            const float gr = std::clamp(levelDb - thresholdDb, 0.0f, rangeDb);
            if (gr != gainReductionDbCurrent_) {
                gainReductionDbCurrent_ = gr;
                gain_ = dbToGain(-gr);
            }
            // 3. 套用
            for (int ch = 0; ch < nch; ++ch) {
                float* d = block.getChannelPointer(ch);
                const float x = d[i];
                // 分頻濾波器永遠跑著（即使 wideband 模式），切換模式時才不會因為濾波器狀態是空的而產生 click
                const float low = lowpass2_.processSample(ch, lowpass1_.processSample(ch, x));
                const float high = highpass2_.processSample(ch, highpass1_.processSample(ch, x));
                if (listen_)
                    d[i] = detectorOut_[static_cast<size_t>(ch)];
                else if (splitBand_)
                    d[i] = low + high * gain_;
                else
                    d[i] = x * gain_;
            }
        }
        gainReductionDb_.store(gainReductionDbCurrent_, std::memory_order_relaxed);
    }

private:
    // 還沒處理任何 sample（剛 prepare / reset）→ 直接生效；否則從目前值 ramp 過去
    void setSmoothedTarget(SmoothedValue& s, float value) noexcept {
        if (hasProcessed_)
            s.setTargetValue(value);
        else
            s.setCurrentAndTargetValue(value);
    }

    void copyFrom(const DeEsser& o) {
        sampleRate_ = o.sampleRate_;
        numChannels_ = o.numChannels_;
        frequencyHz_ = o.frequencyHz_;
        thresholdDb_ = o.thresholdDb_;
        rangeDb_ = o.rangeDb_;
        attackMs_ = o.attackMs_;
        releaseMs_ = o.releaseMs_;
        splitBand_ = o.splitBand_;
        listen_ = o.listen_;
        detectorFilter_ = o.detectorFilter_;
        lowpass1_ = o.lowpass1_;
        lowpass2_ = o.lowpass2_;
        highpass1_ = o.highpass1_;
        highpass2_ = o.highpass2_;
        detectorOut_ = o.detectorOut_;
        follower_ = o.follower_;
        thresholdSmoothed_ = o.thresholdSmoothed_;
        rangeSmoothed_ = o.rangeSmoothed_;
        hasProcessed_ = o.hasProcessed_;
        gainReductionDbCurrent_ = o.gainReductionDbCurrent_;
        gain_ = o.gain_;
        gainReductionDb_.store(o.gainReductionDb_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }

    void updateFilters() noexcept {
        const double f = frequencyHz_;
        const double fc = getCrossoverHz();
        detectorFilter_.setCoefficients(BiquadCoefficients::makeBandpass(sampleRate_, f, kDetectorQ));
        const auto lp = BiquadCoefficients::makeLowpass(sampleRate_, fc, kButterworthQ);
        const auto hp = BiquadCoefficients::makeHighpass(sampleRate_, fc, kButterworthQ);
        lowpass1_.setCoefficients(lp);
        lowpass2_.setCoefficients(lp);
        highpass1_.setCoefficients(hp);
        highpass2_.setCoefficients(hp);
    }

    double sampleRate_ = 48000.0;
    int numChannels_ = 0;

    float frequencyHz_ = 7000.0f;
    float thresholdDb_ = -30.0f;
    float rangeDb_ = 12.0f;
    float attackMs_ = 1.0f;
    float releaseMs_ = 40.0f;
    bool splitBand_ = false;
    bool listen_ = false;

    Biquad detectorFilter_;
    Biquad lowpass1_, lowpass2_, highpass1_, highpass2_;
    std::vector<float> detectorOut_;
    EnvelopeFollower follower_;
    SmoothedValue thresholdSmoothed_{-30.0f};
    SmoothedValue rangeSmoothed_{12.0f};
    bool hasProcessed_ = false;

    float gainReductionDbCurrent_ = 0.0f;
    float gain_ = 1.0f;
    std::atomic<float> gainReductionDb_{0.0f};
};

} // namespace dawlab::dsp
