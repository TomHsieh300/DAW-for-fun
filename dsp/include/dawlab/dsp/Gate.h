#pragma once
// Gate.h —— noise gate（第 08 章）
//
// 一個 gate 做的事：偵測訊號的大小（envelope），高於門檻就「開門」（gain = 0 dB），
// 低於門檻就「關門」（gain = range，例如 -80 dB）。為了不讓門在門檻附近抖動（chattering），
// 開門與關門用不同門檻（hysteresis），關門前先等一段時間（hold），開關本身是斜坡（attack / release）。
//
//   偵測訊號 ──► EnvelopeFollower ──► dB ──► 狀態機 ──► gain(dB) ──► 乘到所有 channel
//                (peak, 0.1 ms / 20 ms)        Closed / Attack / Open / Hold / Release
//
// 狀態機（每個 sample 走一步）：
//   Closed  ─ env ≥ T_open ─────────────► Attack
//   Attack  ─ gainDb 升到 0 ─────────────► Open
//   Open    ─ env < T_close ────────────► Hold（hold = 0 時直接 Release）
//   Hold    ─ env ≥ T_open ─────────────► Open
//           ─ 計數器歸零 ────────────────► Release
//   Release ─ env ≥ T_open ─────────────► Attack（從目前的 gain 往上爬，不會跳）
//           ─ gainDb 降到 range ────────► Closed
//   Closed 狀態下若 range 被改變（例如自動化），gain 以斜坡滑到新的 range，不會產生 step。
//
// gain 在 dB 域做「等速」斜坡：attack 時間 = 從 range 爬到 0 dB 的時間；release 時間 = 從 0 dB 降到 range 的時間。
// dB 域等速 = 線性域指數變化，聽起來是自然的 fade。
//
// 多 channel 一律 stereo-link：偵測用所有 channel 的 max|x|，同一個 gain 乘到每個 channel，
// 否則左右聲道會各自開關、聲像亂跳。

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>
#include <dawlab/dsp/EnvelopeFollower.h>

#include <algorithm>
#include <atomic>
#include <cmath>

namespace dawlab::dsp {

enum class GateState { Closed, Attack, Open, Hold, Release };

inline const char* gateStateName(GateState s) noexcept {
    switch (s) {
    case GateState::Closed: return "Closed";
    case GateState::Attack: return "Attack";
    case GateState::Open: return "Open";
    case GateState::Hold: return "Hold";
    case GateState::Release: return "Release";
    }
    return "?";
}

class Gate {
public:
    // 偵測器（peak）的 attack：0.1 ms，要能抓到 transient 的第一個波峰
    static constexpr float kDetectorAttackMs = 0.1f;
    // Closed 狀態下 range 改變時的最小滑動速度（dB/sample）：0.01 dB/sample = 480 dB/s @ 48 kHz
    static constexpr float kMinRangeGlideDbPerSample = 0.01f;

    Gate() = default;
    // std::atomic 成員讓 class 預設不能複製；手動定義，才能放進 std::vector（第 14 章的 8 軌 DAW）
    Gate(const Gate& other) { copyFrom(other); }
    Gate& operator=(const Gate& other) {
        if (this != &other)
            copyFrom(other);
        return *this;
    }

    // ---- 生命週期 -------------------------------------------------------
    void prepare(double sampleRate, int /*maxBlockSize*/, int numChannels) {
        sampleRate_ = sampleRate;
        numChannels_ = numChannels;
        detector_.prepare(sampleRate);
        detector_.setMode(DetectorMode::Peak);
        detector_.setAttackMs(kDetectorAttackMs);
        detector_.setReleaseMs(detectorReleaseMs_);
        updateTimes();
        reset();
    }

    void reset() noexcept {
        detector_.reset();
        state_.store(GateState::Closed, std::memory_order_relaxed);
        gainDb_ = rangeDb_;
        gain_ = rangeGain_;
        holdCounter_ = 0;
        gainReductionDb_.store(-rangeDb_, std::memory_order_relaxed);
    }

    // ---- 參數（可在 block 之間呼叫）----------------------------------------
    // 開門門檻（dBFS，以 peak envelope 比較）
    void setThresholdDb(float db) noexcept {
        thresholdDb_ = db;
        closeDb_ = thresholdDb_ - hysteresisDb_;
    }
    // 關門門檻 = threshold − hysteresis（hysteresis ≥ 0）
    void setHysteresisDb(float db) noexcept {
        hysteresisDb_ = std::max(0.0f, db);
        closeDb_ = thresholdDb_ - hysteresisDb_;
    }
    // 關門時的衰減量。合約規定負值（例如 -80）；為了防呆，正負號一律視為「衰減 |db| dB」。
    // 低於 kMinusInfinityDb（-100 dB）時 dbToGain 會直接回傳 0，等於完全靜音。
    // 門關著時改 range 不會瞬間跳變：nextGain() 的 Closed 狀態會用斜坡把 gain 滑到新的 range。
    void setRangeDb(float db) noexcept {
        rangeDb_ = -std::abs(db);
        rangeGain_ = dbToGain(rangeDb_);
        updateTimes();
    }
    void setAttackMs(float ms) noexcept {
        attackMs_ = std::max(0.0f, ms);
        updateTimes();
    }
    void setHoldMs(float ms) noexcept {
        holdMs_ = std::max(0.0f, ms);
        updateTimes();
    }
    void setReleaseMs(float ms) noexcept {
        releaseMs_ = std::max(0.0f, ms);
        updateTimes();
    }
    // 額外：偵測器的 release（預設 20 ms）。越短越靈敏、但低頻訊號的 envelope 漣漪越大（見 README 5.3）。
    void setDetectorReleaseMs(float ms) noexcept {
        detectorReleaseMs_ = std::max(0.1f, ms);
        detector_.setReleaseMs(detectorReleaseMs_);
    }

    float getThresholdDb() const noexcept { return thresholdDb_; }
    float getHysteresisDb() const noexcept { return hysteresisDb_; }
    float getCloseThresholdDb() const noexcept { return closeDb_; }
    float getRangeDb() const noexcept { return rangeDb_; }
    float getAttackMs() const noexcept { return attackMs_; }
    float getHoldMs() const noexcept { return holdMs_; }
    float getReleaseMs() const noexcept { return releaseMs_; }
    float getDetectorReleaseMs() const noexcept { return detectorReleaseMs_; }
    int getLatencySamples() const noexcept { return 0; } // 沒有 lookahead → 零延遲（練習 6 會加上）

    // ---- 量測（thread-safe，給 UI）---------------------------------------
    // 正值：目前衰減多少 dB（block 結束時更新一次）
    float getGainReductionDb() const noexcept { return gainReductionDb_.load(std::memory_order_relaxed); }
    // 門目前是否「開著或正在開」（Attack / Open / Hold）。Release 期間視為關門中。
    // state 存在 std::atomic（relaxed），UI thread 可以隨時讀（讀到的是「最近某個 sample」的狀態）。
    bool isOpen() const noexcept {
        const GateState s = state_.load(std::memory_order_relaxed);
        return s == GateState::Attack || s == GateState::Open || s == GateState::Hold;
    }
    GateState getState() const noexcept { return state_.load(std::memory_order_relaxed); }

    // ---- 處理 -----------------------------------------------------------
    // 核心：餵入一個「偵測用」sample（通常是 max|x| across channels），狀態機走一步，回傳要乘上的線性 gain。
    // process() 就是對每個 sample 呼叫它；練習（lookahead、sidechain）也可以直接用它。
    float nextGain(float detectorInput) noexcept {
        const float env = detector_.processSample(detectorInput);
        const float envDb = gainToDb(env);
        const bool aboveOpen = envDb >= thresholdDb_;
        const bool belowClose = envDb < closeDb_;
        // 狀態先讀進區域變數，最後只寫回一次 atomic（relaxed 的 load / store 在 x86 上就是普通的 mov）
        GateState state = state_.load(std::memory_order_relaxed);

        switch (state) {
        case GateState::Closed:
            if (gainDb_ == rangeDb_) {
                gain_ = rangeGain_;
            } else {
                // range 在門關著時被改了：往上用 attack 斜率、往下用 release 斜率滑過去（至少 0.01 dB/sample）
                if (gainDb_ < rangeDb_)
                    gainDb_ = std::min(rangeDb_, gainDb_ + std::max(attackStepDb_, kMinRangeGlideDbPerSample));
                else
                    gainDb_ = std::max(rangeDb_, gainDb_ - std::max(releaseStepDb_, kMinRangeGlideDbPerSample));
                gain_ = gainDb_ == rangeDb_ ? rangeGain_ : dbToGain(gainDb_);
            }
            if (aboveOpen)
                state = GateState::Attack;
            break;

        case GateState::Attack:
            // 一旦觸發就一路開到底（確保 transient 完整通過），開到 0 dB 才看門檻
            gainDb_ += attackStepDb_;
            if (gainDb_ >= 0.0f) {
                gainDb_ = 0.0f;
                gain_ = 1.0f;
                state = GateState::Open;
            } else {
                gain_ = dbToGain(gainDb_);
            }
            break;

        case GateState::Open:
            gainDb_ = 0.0f;
            gain_ = 1.0f;
            if (belowClose) {
                if (holdSamples_ > 0) {
                    holdCounter_ = holdSamples_;
                    state = GateState::Hold;
                } else {
                    state = GateState::Release;
                }
            }
            break;

        case GateState::Hold:
            // hold 期間 gain 維持 0 dB；訊號若回到門檻以上就回到 Open（計數器作廢）
            if (aboveOpen)
                state = GateState::Open;
            else if (--holdCounter_ <= 0)
                state = GateState::Release;
            break;

        case GateState::Release:
            if (aboveOpen) {
                state = GateState::Attack; // 從目前的 gain 往上爬，不跳
            } else if (gainDb_ <= rangeDb_) {
                // 只會在 release 途中 range 被調高（衰減變少）時發生：交給 Closed 狀態慢慢滑上去，不跳
                state = GateState::Closed;
            } else {
                gainDb_ = std::max(rangeDb_, gainDb_ - releaseStepDb_);
                if (gainDb_ <= rangeDb_) {
                    gain_ = rangeGain_;
                    state = GateState::Closed;
                } else {
                    gain_ = dbToGain(gainDb_);
                }
            }
            break;
        }
        state_.store(state, std::memory_order_relaxed);
        return gain_;
    }

    // 以自己的訊號偵測（stereo-link：max|x| across channels）
    void process(AudioBlock block) noexcept { process(block, block); }

    // 以 sidechain 偵測（例如 kick 軌觸發 pad 軌）。sidechain 的 channel 數可以與 main 不同，
    // sample 數以兩者較小者為準。
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
    void copyFrom(const Gate& o) {
        sampleRate_ = o.sampleRate_;
        numChannels_ = o.numChannels_;
        thresholdDb_ = o.thresholdDb_;
        hysteresisDb_ = o.hysteresisDb_;
        closeDb_ = o.closeDb_;
        rangeDb_ = o.rangeDb_;
        rangeGain_ = o.rangeGain_;
        attackMs_ = o.attackMs_;
        holdMs_ = o.holdMs_;
        releaseMs_ = o.releaseMs_;
        detectorReleaseMs_ = o.detectorReleaseMs_;
        attackStepDb_ = o.attackStepDb_;
        releaseStepDb_ = o.releaseStepDb_;
        holdSamples_ = o.holdSamples_;
        holdCounter_ = o.holdCounter_;
        detector_ = o.detector_;
        state_.store(o.state_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        gainDb_ = o.gainDb_;
        gain_ = o.gain_;
        gainReductionDb_.store(o.gainReductionDb_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }

    void updateTimes() noexcept {
        const double attackSamples = attackMs_ * 0.001 * sampleRate_;
        const double releaseSamples = releaseMs_ * 0.001 * sampleRate_;
        holdSamples_ = msToSamples(holdMs_, sampleRate_);
        // 斜率（dB / sample）：attack 要在 attackMs 內從 range 爬到 0 dB。
        // 斜率的參考量至少取 1 dB：range = 0（等於關掉 gate）時斜率若是 0，正在 Attack 的 gain 會永遠爬不到 0 dB。
        const double spanDb = std::max(1.0, static_cast<double>(-rangeDb_));
        attackStepDb_ = attackSamples >= 1.0 ? static_cast<float>(spanDb / attackSamples) : 1.0e9f;
        releaseStepDb_ = releaseSamples >= 1.0 ? static_cast<float>(spanDb / releaseSamples) : 1.0e9f;
    }

    double sampleRate_ = 48000.0;
    int numChannels_ = 2;

    float thresholdDb_ = -40.0f;
    float hysteresisDb_ = 6.0f;
    float closeDb_ = -46.0f;
    float rangeDb_ = -80.0f;
    float rangeGain_ = 0.0001f; // dbToGain(-80)
    float attackMs_ = 1.0f;
    float holdMs_ = 50.0f;
    float releaseMs_ = 100.0f;
    float detectorReleaseMs_ = 20.0f;

    float attackStepDb_ = 0.0f;
    float releaseStepDb_ = 0.0f;
    int holdSamples_ = 0;
    int holdCounter_ = 0;

    EnvelopeFollower detector_;
    std::atomic<GateState> state_{GateState::Closed};
    float gainDb_ = -80.0f;
    float gain_ = 0.0f;
    std::atomic<float> gainReductionDb_{80.0f};
};

} // namespace dawlab::dsp
