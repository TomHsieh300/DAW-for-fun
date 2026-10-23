#pragma once
// 第 04 章：Compressor（feedforward、log-domain 平滑）。
//
// 訊號流程（每個 sample）：
//
//   sidechain x ──► detector ──► 20·log10 ──► gain computer ──► attack/release ──► 10^(-GR/20) ──┐
//                 (|x| 或 RMS)    (dB)        (static curve)     (平滑 GR, dB)     (線性 gain)     │
//   main x ───────────────────────────────────────────────────────────────────────────────────► × ──► × makeup ──► y
//
// 設計依據：Giannoulis, Massberg & Reiss, "Digital Dynamic Range Compressor Design—A Tutorial
// and Analysis", JAES 60(6), 2012。重點：
//   - gain computer 的輸入是「瞬間」level（Peak 模式）或 RMS level 的 dB 值；
//   - attack / release 平滑放在 gain computer 之後、在 log(dB) domain 做（論文 Fig. 7 的建議結構），
//     好處是 attack / release 時間與 threshold、ratio 無關，而且 release 曲線在 dB 上是指數衰減、
//     聽起來最自然；
//   - 預設用 Decoupled 平滑（見 EnvelopeFollower.h），對週期訊號能精確追到 peak。
//
// 只依賴 common/ 與本章的 EnvelopeFollower.h。

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>
#include <dawlab/dsp/EnvelopeFollower.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

namespace dawlab::dsp {

class Compressor {
public:
    Compressor() = default;

    // std::atomic 成員讓 class 預設不能複製；手動定義，才能放進 std::vector / std::array（第 14 章 8 軌會用到）
    Compressor(const Compressor& other) { copyFrom(other); }
    Compressor& operator=(const Compressor& other) {
        if (this != &other)
            copyFrom(other);
        return *this;
    }

    // ---- 標準介面 ----------------------------------------------------------
    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        sampleRate_ = sampleRate;
        maxBlockSize_ = maxBlockSize;
        channels_.resize(static_cast<size_t>(std::max(1, numChannels)));
        for (auto& ch : channels_) {
            ch.rmsDetector.prepare(sampleRate);
            ch.rmsDetector.setMode(DetectorMode::Rms);
            ch.gainSmoother.prepare(sampleRate);
        }
        // makeup gain 用約 10 ms 的 one-pole 平滑，避免轉旋鈕時出現 zipper noise
        makeupSmoothCoeff_ = 1.0f - attackReleaseCoefficient(10.0, sampleRate);
        updateTimeConstants();
        reset();
    }

    void reset() noexcept {
        for (auto& ch : channels_) {
            ch.rmsDetector.reset();
            ch.gainSmoother.reset();
        }
        makeupGainSmoothed_ = dbToGain(makeupGainDb_);
        gainReductionDb_.store(0.0f, std::memory_order_relaxed);
    }

    // ---- 參數 --------------------------------------------------------------
    void setThresholdDb(float db) noexcept { thresholdDb_ = db; }
    void setRatio(float ratio) noexcept { ratio_ = std::max(1.0f, ratio); } // 1:1 = 不壓縮
    void setKneeDb(float kneeDb) noexcept { kneeDb_ = std::max(0.0f, kneeDb); } // 0 = hard knee
    void setAttackMs(float ms) noexcept {
        attackMs_ = std::max(0.0f, ms);
        updateTimeConstants();
    }
    void setReleaseMs(float ms) noexcept {
        releaseMs_ = std::max(0.0f, ms);
        updateTimeConstants();
    }
    void setMakeupGainDb(float db) noexcept { makeupGainDb_ = db; }
    // 切換 link 是安全的（不會 click）：linked 模式下每個 channel 仍然各自跑自己的 GR smoother，
    // 只是大家吃「同一個」GR 目標值。所以切換瞬間每個 channel 都從自己目前的 GR 出發，
    // 以 attack / release 的速度平滑地走向新的目標（見 process() 的註解）。
    void setStereoLink(bool linked) noexcept { stereoLink_ = linked; }
    void setDetectorMode(DetectorMode mode) noexcept { detectorMode_ = mode; }
    // RMS 模式的平均視窗（attack = release = window）
    void setRmsWindowMs(float ms) noexcept {
        rmsWindowMs_ = std::max(0.0f, ms);
        updateTimeConstants();
    }
    void setSmoothingMode(SmoothingMode mode) noexcept {
        smoothingMode_ = mode;
        for (auto& ch : channels_)
            ch.gainSmoother.setMode(mode);
    }

    float getThresholdDb() const noexcept { return thresholdDb_; }
    float getRatio() const noexcept { return ratio_; }
    float getKneeDb() const noexcept { return kneeDb_; }
    float getAttackMs() const noexcept { return attackMs_; }
    float getReleaseMs() const noexcept { return releaseMs_; }
    float getMakeupGainDb() const noexcept { return makeupGainDb_; }
    bool getStereoLink() const noexcept { return stereoLink_; }
    DetectorMode getDetectorMode() const noexcept { return detectorMode_; }
    float getRmsWindowMs() const noexcept { return rmsWindowMs_; }
    SmoothingMode getSmoothingMode() const noexcept { return smoothingMode_; }
    int getLatencySamples() const noexcept { return 0; } // feedforward、無 lookahead → 零延遲

    // 目前的 gain reduction（正值 dB，例如 6 代表正在壓 6 dB）。
    // 由 audio thread 寫入、UI thread 讀取：std::atomic + relaxed 就夠了，不需要 lock。
    float getGainReductionDb() const noexcept { return gainReductionDb_.load(std::memory_order_relaxed); }

    // ---- 靜態 gain computer（transfer curve）--------------------------------
    // 輸入 level x (dB)，回傳輸出 level y (dB)。Giannoulis et al. 2012, eq. (4)，W = knee 寬度：
    //
    //            x                                   if 2(x-T) < -W         （knee 以下：不動）
    //   y(x) =   x + (1/R - 1)·(x - T + W/2)² / (2W)  if 2|x-T| <= W         （knee 內：二次曲線）
    //            T + (x - T)/R                        if 2(x-T) > W          （knee 以上：斜率 1/R）
    //
    // W = 0 就退化成 hard knee：y = x (x <= T)，y = T + (x-T)/R (x > T)。
    static float computeOutputDb(float inputDb, float thresholdDb, float ratio, float kneeDb) noexcept {
        ratio = std::max(1.0f, ratio);
        kneeDb = std::max(0.0f, kneeDb);
        const float over = inputDb - thresholdDb; // x - T
        if (kneeDb <= 0.0f) // hard knee：明寫出來，下面的 /(2W) 就不會遇到 W = 0（也避免 MSVC 的 C4723）
            return over > 0.0f ? thresholdDb + over / ratio : inputDb;
        if (2.0f * over <= -kneeDb)
            return inputDb;
        if (2.0f * over >= kneeDb)
            return thresholdDb + over / ratio;
        const float t = over + 0.5f * kneeDb; // 0 .. W，從 knee 下緣算起
        return inputDb + (1.0f / ratio - 1.0f) * t * t / (2.0f * kneeDb);
    }

    // gain reduction = 輸入 dB - 輸出 dB（正值）
    static float computeGainReductionDb(float inputDb, float thresholdDb, float ratio, float kneeDb) noexcept {
        return inputDb - computeOutputDb(inputDb, thresholdDb, ratio, kneeDb);
    }

    // ---- 處理 --------------------------------------------------------------
    // 以自己為 sidechain（一般用法）
    void process(AudioBlock block) noexcept { process(block, block); }

    // 外部 sidechain：detector 看 sidechain，gain 套在 block 上。
    // sidechain 的 channel 數可以比 block 少（例如 mono sidechain 控 stereo），會重複使用最後一個 channel。
    void process(AudioBlock block, const AudioBlock& sidechain) noexcept {
        const int numCh = std::min(block.getNumChannels(), static_cast<int>(channels_.size()));
        const int numSamples = block.getNumSamples();
        const int scCh = sidechain.getNumChannels();
        if (numCh <= 0 || scCh <= 0)
            return;

        const float makeupTarget = dbToGain(makeupGainDb_);
        float blockMaxGr = 0.0f;

        for (int i = 0; i < numSamples; ++i) {
            // makeup gain 平滑：g += c·(target - g)
            makeupGainSmoothed_ += makeupSmoothCoeff_ * (makeupTarget - makeupGainSmoothed_);

            if (stereoLink_) {
                // Stereo link：所有 channel 共用「最大的 level」算出同一個 GR 目標值，立體聲像才不會左右漂移。
                // 每個 channel 的 smoother 都吃這個共同目標：穩態時大家的 GR 完全相同；
                // 而剛從 unlinked 切過來時，各 channel 從自己原本的 GR 平滑地收斂過去，不會瞬間跳變。
                // （如果只跑 channel 0 的 smoother，其他 channel 的 smoother 會凍結在舊值，
                //   之後切回 unlinked 時 gain 會突然跳回那個舊值 → click。）
                float level = 0.0f;
                for (int ch = 0; ch < numCh; ++ch) {
                    const float sc = sidechain.getSample(std::min(ch, scCh - 1), i);
                    level = std::max(level, detect(channels_[static_cast<size_t>(ch)], sc));
                }
                const float grTarget = computeGainReductionTarget(level);
                for (int ch = 0; ch < numCh; ++ch) {
                    const float gr = channels_[static_cast<size_t>(ch)].gainSmoother.process(grTarget);
                    blockMaxGr = std::max(blockMaxGr, gr);
                    block.getChannelPointer(ch)[i] *= dbToGain(-gr) * makeupGainSmoothed_;
                }
            } else {
                // 各 channel 獨立：各自有 detector 與 smoother
                for (int ch = 0; ch < numCh; ++ch) {
                    auto& state = channels_[static_cast<size_t>(ch)];
                    const float sc = sidechain.getSample(std::min(ch, scCh - 1), i);
                    const float gr = smoothGainReduction(state, detect(state, sc));
                    blockMaxGr = std::max(blockMaxGr, gr);
                    block.getChannelPointer(ch)[i] *= dbToGain(-gr) * makeupGainSmoothed_;
                }
            }
        }
        gainReductionDb_.store(blockMaxGr, std::memory_order_relaxed);
    }

private:
    struct ChannelState {
        EnvelopeFollower rmsDetector;      // 只在 Rms 模式使用
        AttackReleaseFilter gainSmoother;  // 在 dB domain 平滑 gain reduction
    };

    // detector：回傳線性 level（Peak = 瞬間 |x|；Rms = 移動平均 RMS）
    float detect(ChannelState& state, float x) noexcept {
        if (detectorMode_ == DetectorMode::Peak)
            return std::abs(x);
        return state.rmsDetector.processSample(x);
    }

    // level（線性）→ dB → 靜態 GR → attack/release 平滑後的 GR（dB，正值）
    float smoothGainReduction(ChannelState& state, float level) noexcept {
        return state.gainSmoother.process(computeGainReductionTarget(level));
    }

    // level（線性）→ dB → 靜態 GR 目標值（dB，正值，尚未平滑）
    float computeGainReductionTarget(float level) const noexcept {
        // non-finite 防護：level = Inf 時 gainToDb 回傳 Inf，靜態曲線會算出 Inf - Inf = NaN，
        // smoother 卡在 NaN、dbToGain(NaN) = 0 → 輸出永久靜音。壞掉的 level 當成 0（不壓）。
        if (!std::isfinite(level))
            level = 0.0f;
        const float levelDb = gainToDb(level); // 0 → -100 dB，GR 自然為 0
        return computeGainReductionDb(levelDb, thresholdDb_, ratio_, kneeDb_);
    }

    void copyFrom(const Compressor& o) {
        sampleRate_ = o.sampleRate_;
        maxBlockSize_ = o.maxBlockSize_;
        channels_ = o.channels_;
        thresholdDb_ = o.thresholdDb_;
        ratio_ = o.ratio_;
        kneeDb_ = o.kneeDb_;
        attackMs_ = o.attackMs_;
        releaseMs_ = o.releaseMs_;
        makeupGainDb_ = o.makeupGainDb_;
        stereoLink_ = o.stereoLink_;
        detectorMode_ = o.detectorMode_;
        rmsWindowMs_ = o.rmsWindowMs_;
        smoothingMode_ = o.smoothingMode_;
        makeupGainSmoothed_ = o.makeupGainSmoothed_;
        makeupSmoothCoeff_ = o.makeupSmoothCoeff_;
        gainReductionDb_.store(o.gainReductionDb_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }

    void updateTimeConstants() noexcept {
        for (auto& ch : channels_) {
            ch.gainSmoother.setAttackMs(attackMs_);
            ch.gainSmoother.setReleaseMs(releaseMs_);
            ch.gainSmoother.setMode(smoothingMode_);
            ch.rmsDetector.setAttackMs(rmsWindowMs_);
            ch.rmsDetector.setReleaseMs(rmsWindowMs_);
        }
    }

    double sampleRate_ = 48000.0;
    int maxBlockSize_ = 0;
    std::vector<ChannelState> channels_;

    float thresholdDb_ = -20.0f;
    float ratio_ = 4.0f;
    float kneeDb_ = 6.0f;
    float attackMs_ = 10.0f;
    float releaseMs_ = 100.0f;
    float makeupGainDb_ = 0.0f;
    bool stereoLink_ = true;
    DetectorMode detectorMode_ = DetectorMode::Peak;
    float rmsWindowMs_ = 10.0f;
    SmoothingMode smoothingMode_ = SmoothingMode::Decoupled;

    float makeupGainSmoothed_ = 1.0f;
    float makeupSmoothCoeff_ = 1.0f;
    std::atomic<float> gainReductionDb_{0.0f};
};

} // namespace dawlab::dsp
