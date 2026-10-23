#pragma once
// Expander.h —— downward expander（第 08 章）
//
// compressor 把「高於 threshold」的部分壓小；downward expander 把「低於 threshold」的部分壓得更小，
// 讓安靜的地方更安靜（雜訊、串音、殘響尾巴），是 gate 的「溫和版」。
//
// 靜態曲線（gain computer，單位 dB，R = ratio ≥ 1，T = threshold，W = knee 寬度）：
//
//   y = x                                        x ≥ T + W/2       （不動）
//   y = x + (1 − R)(x − T − W/2)² / (2W)        T − W/2 ≤ x < T + W/2（soft knee，二次曲線）
//   y = T + R (x − T)                            x < T − W/2       （每低 1 dB 變成低 R dB）
//
// 這和第 04 章 compressor 的公式是「上下對稱」的：把 (1/R − 1) 換成 (1 − R)、把 +W/2 換成 −W/2。
// range：gain 的下限（例如 -60 dB），避免把訊號壓到完全消失（那就變 gate 了）。
//
// 時間行為：用第 04 章的 EnvelopeFollower 做「有 attack / release 的 level detector」，
// 再把平滑後的 level 丟進靜態曲線。訊號上升 → envelope 用 attack 追 → gain 很快回到 0 dB（門打開）；
// 訊號下降 → envelope 用 release 慢慢掉 → gain reduction 慢慢加深（門慢慢關）。
// 這就是 expander / gate 對 attack、release 的命名慣例：attack = 開門速度、release = 關門速度。
//
// 參數平滑：和 gate 不同，expander 的 threshold、ratio、knee、range 直接出現在 gain 公式裡
// （g = max(range, (R − 1)(x − T))），在 block 之間改它們會讓 gain 在一個 sample 內整段跳到新值（click）。
// 所以這四個參數各自經過一個 SmoothedValue（第 01 章），在 kParameterRampMs 內線性走到新值。
// 例外：reset() 之後、處理第一個 sample 之前呼叫的 setter 直接生效（不 ramp），
// 這樣「prepare → 設定參數 → 開始處理」的初始化流程不會從預設值滑過來。

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>
#include <dawlab/dsp/EnvelopeFollower.h>
#include <dawlab/dsp/SmoothedValue.h>

#include <algorithm>
#include <atomic>
#include <cmath>

namespace dawlab::dsp {

class Expander {
public:
    // threshold / ratio / knee / range 改變時的線性 ramp 長度
    static constexpr float kParameterRampMs = 20.0f;

    Expander() = default;
    // std::atomic 成員讓 class 預設不能複製；手動定義，才能放進 std::vector（第 14 章的 8 軌 DAW）
    Expander(const Expander& other) { copyFrom(other); }
    Expander& operator=(const Expander& other) {
        if (this != &other)
            copyFrom(other);
        return *this;
    }

    // ---- 靜態 gain computer（純函式，練習與表格直接用）-------------------------
    // 回傳輸出位準（dB）。ratio ≥ 1；ratio = 1 表示不處理。kneeDb ≤ 0 視為 hard knee。
    static float computeOutputDb(float inputDb, float thresholdDb, float ratio, float kneeDb) noexcept {
        const float r = std::max(1.0f, ratio);
        const float w = std::max(0.0f, kneeDb);
        const float halfW = 0.5f * w;
        if (inputDb >= thresholdDb + halfW)
            return inputDb;
        if (w > 0.0f && inputDb >= thresholdDb - halfW) {
            const float d = inputDb - thresholdDb - halfW; // ≤ 0
            return inputDb + (1.0f - r) * d * d / (2.0f * w);
        }
        return thresholdDb + r * (inputDb - thresholdDb);
    }

    // 回傳 gain（dB，≤ 0）= 輸出 − 輸入
    static float computeGainDb(float inputDb, float thresholdDb, float ratio, float kneeDb) noexcept {
        return computeOutputDb(inputDb, thresholdDb, ratio, kneeDb) - inputDb;
    }

    // ---- 生命週期 -------------------------------------------------------
    void prepare(double sampleRate, int /*maxBlockSize*/, int numChannels) {
        sampleRate_ = sampleRate;
        numChannels_ = numChannels;
        detector_.prepare(sampleRate);
        detector_.setMode(mode_);
        // Decoupled（先 peak-hold 再 attack 平滑）：穩態正弦波的 level = 真正的 peak。
        // Branching 在 release 期間會讓 envelope 隨波形起伏而系統性低估 peak（1 kHz、attack 5 ms 約低 1 dB），
        // 而 expander 會把這個誤差再乘上 (R − 1) 放大到輸出。
        detector_.setSmoothingMode(SmoothingMode::Decoupled);
        detector_.setAttackMs(attackMs_);
        detector_.setReleaseMs(releaseMs_);
        const double rampSeconds = kParameterRampMs * 0.001;
        thresholdSmoothed_.reset(sampleRate, rampSeconds);
        ratioSmoothed_.reset(sampleRate, rampSeconds);
        kneeSmoothed_.reset(sampleRate, rampSeconds);
        rangeSmoothed_.reset(sampleRate, rampSeconds);
        reset();
    }

    void reset() noexcept {
        detector_.reset();
        // 參數直接對齊目標值（不 ramp）；之後到第一個 sample 之前的 setter 也直接生效
        thresholdSmoothed_.setCurrentAndTargetValue(thresholdDb_);
        ratioSmoothed_.setCurrentAndTargetValue(ratio_);
        kneeSmoothed_.setCurrentAndTargetValue(kneeDb_);
        rangeSmoothed_.setCurrentAndTargetValue(rangeDb_);
        hasProcessed_ = false;
        gainDb_ = rangeDb_;
        gain_ = dbToGain(rangeDb_);
        gainReductionDb_.store(-rangeDb_, std::memory_order_relaxed);
    }

    // ---- 參數（可在 block 之間呼叫；threshold / ratio / knee / range 會以 kParameterRampMs 平滑）--------
    void setThresholdDb(float db) noexcept {
        thresholdDb_ = db;
        setSmoothedTarget(thresholdSmoothed_, thresholdDb_);
    }
    // 2 代表低於 threshold 的每 1 dB 變成 2 dB
    void setRatio(float ratio) noexcept {
        ratio_ = std::max(1.0f, ratio);
        setSmoothedTarget(ratioSmoothed_, ratio_);
    }
    void setKneeDb(float db) noexcept {
        kneeDb_ = std::max(0.0f, db);
        setSmoothedTarget(kneeSmoothed_, kneeDb_);
    }
    // gain 的下限（dB）。合約慣例為負值（例如 -60）；正負號一律視為「最多衰減 |db| dB」。
    void setRangeDb(float db) noexcept {
        rangeDb_ = -std::abs(db);
        setSmoothedTarget(rangeSmoothed_, rangeDb_);
    }
    void setAttackMs(float ms) noexcept {
        attackMs_ = std::max(0.0f, ms);
        detector_.setAttackMs(attackMs_);
    }
    void setReleaseMs(float ms) noexcept {
        releaseMs_ = std::max(0.0f, ms);
        detector_.setReleaseMs(releaseMs_);
    }
    // 額外：偵測模式（預設 Peak；Rms 對人聲、合奏比較「音樂性」，但對 transient 反應慢）
    void setDetectorMode(DetectorMode mode) noexcept {
        mode_ = mode;
        detector_.setMode(mode);
    }

    float getThresholdDb() const noexcept { return thresholdDb_; }
    float getRatio() const noexcept { return ratio_; }
    float getKneeDb() const noexcept { return kneeDb_; }
    float getRangeDb() const noexcept { return rangeDb_; }
    float getAttackMs() const noexcept { return attackMs_; }
    float getReleaseMs() const noexcept { return releaseMs_; }
    DetectorMode getDetectorMode() const noexcept { return mode_; }
    int getLatencySamples() const noexcept { return 0; }

    // 正值：目前衰減多少 dB（block 結束時更新；thread-safe）
    float getGainReductionDb() const noexcept { return gainReductionDb_.load(std::memory_order_relaxed); }

    // ---- 處理 -----------------------------------------------------------
    // 核心：餵入偵測用 sample（max|x| across channels），回傳線性 gain
    float nextGain(float detectorInput) noexcept {
        const float env = detector_.processSample(detectorInput);
        const float levelDb = gainToDb(env);
        // 參數沒在變時 getNextValue() 直接回傳目標值（便宜的快速路徑）
        const float thresholdDb = thresholdSmoothed_.getNextValue();
        const float ratio = ratioSmoothed_.getNextValue();
        const float kneeDb = kneeSmoothed_.getNextValue();
        const float rangeDb = rangeSmoothed_.getNextValue();
        hasProcessed_ = true;
        const float g = computeGainDb(levelDb, thresholdDb, ratio, kneeDb);
        const float newGainDb = std::max(rangeDb, g);
        if (newGainDb != gainDb_) { // 只有 gain 改變時才算一次 pow
            gainDb_ = newGainDb;
            gain_ = dbToGain(gainDb_);
        }
        return gain_;
    }

    // 以自己的訊號偵測（stereo-link：max|x| across channels）
    void process(AudioBlock block) noexcept { process(block, block); }

    // 額外：以 sidechain 偵測（與 Gate 相同的介面）。sample 數以兩者較小者為準。
    void process(AudioBlock main, const AudioBlock& sidechain) noexcept {
        const int n = std::min(main.getNumSamples(), sidechain.getNumSamples());
        const int nMain = main.getNumChannels();
        const int nSc = sidechain.getNumChannels();
        for (int i = 0; i < n; ++i) {
            float det = 0.0f;
            for (int ch = 0; ch < nSc; ++ch)
                det = std::max(det, std::abs(sidechain.getChannelPointer(ch)[i]));
            const float g = nextGain(det);
            for (int ch = 0; ch < nMain; ++ch)
                main.getChannelPointer(ch)[i] *= g;
        }
        gainReductionDb_.store(-gainDb_, std::memory_order_relaxed);
    }

private:
    // 還沒處理任何 sample（剛 prepare / reset）→ 直接生效；否則從目前值 ramp 過去
    void setSmoothedTarget(SmoothedValue& s, float value) noexcept {
        if (hasProcessed_)
            s.setTargetValue(value);
        else
            s.setCurrentAndTargetValue(value);
    }

    void copyFrom(const Expander& o) {
        sampleRate_ = o.sampleRate_;
        numChannels_ = o.numChannels_;
        thresholdDb_ = o.thresholdDb_;
        ratio_ = o.ratio_;
        kneeDb_ = o.kneeDb_;
        rangeDb_ = o.rangeDb_;
        attackMs_ = o.attackMs_;
        releaseMs_ = o.releaseMs_;
        mode_ = o.mode_;
        detector_ = o.detector_;
        thresholdSmoothed_ = o.thresholdSmoothed_;
        ratioSmoothed_ = o.ratioSmoothed_;
        kneeSmoothed_ = o.kneeSmoothed_;
        rangeSmoothed_ = o.rangeSmoothed_;
        hasProcessed_ = o.hasProcessed_;
        gainDb_ = o.gainDb_;
        gain_ = o.gain_;
        gainReductionDb_.store(o.gainReductionDb_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }

    double sampleRate_ = 48000.0;
    int numChannels_ = 2;

    float thresholdDb_ = -40.0f;
    float ratio_ = 2.0f;
    float kneeDb_ = 0.0f;
    float rangeDb_ = -60.0f;
    float attackMs_ = 5.0f;
    float releaseMs_ = 100.0f;
    DetectorMode mode_ = DetectorMode::Peak;

    EnvelopeFollower detector_;
    SmoothedValue thresholdSmoothed_{-40.0f};
    SmoothedValue ratioSmoothed_{2.0f};
    SmoothedValue kneeSmoothed_{0.0f};
    SmoothedValue rangeSmoothed_{-60.0f};
    bool hasProcessed_ = false;
    float gainDb_ = 0.0f;
    float gain_ = 1.0f;
    std::atomic<float> gainReductionDb_{0.0f};
};

} // namespace dawlab::dsp
