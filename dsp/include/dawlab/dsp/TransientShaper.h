#pragma once
// TransientShaper.h —— transient shaper（第 08 章）
//
// 不看絕對音量、只看「音量變化的快慢」的動態處理器：把每個音符拆成 attack（起音）與 sustain（延音），
// 各自加強或減弱。
//
// 偵測：同一個訊號餵給兩個 EnvelopeFollower
//   fast：attack 0.5 ms / release 30 ms   → 幾乎貼著訊號的 peak 走
//   slow：attack 10 ms  / release 150 ms  → 慢半拍的「平均音量」
//   d = 20·log10(fast / slow)
//     起音瞬間：fast 已經衝到 peak、slow 還在爬 → d > 0
//     延音 / 衰減：fast 跟著訊號往下掉、slow 還撐在高處 → d < 0
//
// 增益映射（dB）：
//   d > 0：gainDb = attackDb  · min(d / 6 dB, 1)
//   d < 0：gainDb = sustainDb · min(−d / 6 dB, 1)
//   （d 超過 ±6 dB 就套用完整的 attackDb / sustainDb；slow envelope 低於 −80 dBFS 視為靜音、不處理）
// 最後把 gainDb 用 0.5 ms 的 one-pole 平滑，避免 envelope 漣漪造成 zipper noise。
//
// attack +12 dB 讓鼓更「打」、crest factor 變大；attack −12 dB 讓鼓變圓、變遠；
// sustain −12 dB 縮短尾音（像把房間縮小）、sustain +12 dB 拉長尾音。

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>
#include <dawlab/dsp/EnvelopeFollower.h>

#include <algorithm>
#include <atomic>
#include <cmath>

namespace dawlab::dsp {

class TransientShaper {
public:
    static constexpr float kFastAttackMs = 0.5f;
    static constexpr float kFastReleaseMs = 30.0f;
    static constexpr float kSlowAttackMs = 10.0f;
    static constexpr float kSlowReleaseMs = 150.0f;
    static constexpr float kFullScaleDb = 6.0f;  // |d| 達到這個值就套用 100% 的 attackDb / sustainDb
    static constexpr float kFloorDb = -80.0f;    // slow envelope 低於此視為靜音
    static constexpr float kGainSmoothMs = 0.5f;

    TransientShaper() = default;
    // std::atomic 成員讓 class 預設不能複製；手動定義，才能放進 std::vector（第 14 章的 8 軌 DAW）
    TransientShaper(const TransientShaper& other) { copyFrom(other); }
    TransientShaper& operator=(const TransientShaper& other) {
        if (this != &other)
            copyFrom(other);
        return *this;
    }

    // ---- 生命週期 -------------------------------------------------------
    void prepare(double sampleRate, int /*maxBlockSize*/, int numChannels) {
        sampleRate_ = sampleRate;
        numChannels_ = numChannels;
        fast_.prepare(sampleRate);
        fast_.setMode(DetectorMode::Peak);
        fast_.setAttackMs(kFastAttackMs);
        fast_.setReleaseMs(kFastReleaseMs);
        slow_.prepare(sampleRate);
        slow_.setMode(DetectorMode::Peak);
        slow_.setAttackMs(kSlowAttackMs);
        slow_.setReleaseMs(kSlowReleaseMs);
        smoothCoef_ = static_cast<float>(std::exp(-1.0 / (kGainSmoothMs * 0.001 * sampleRate)));
        reset();
    }

    void reset() noexcept {
        fast_.reset();
        slow_.reset();
        gainDb_ = 0.0f;
        gain_ = 1.0f;
        lastGainDb_.store(0.0f, std::memory_order_relaxed);
    }

    // ---- 參數 -----------------------------------------------------------
    // -24..+24 dB：正值加強 transient、負值減弱
    void setAttackDb(float db) noexcept { attackDb_ = std::clamp(db, -24.0f, 24.0f); }
    void setSustainDb(float db) noexcept { sustainDb_ = std::clamp(db, -24.0f, 24.0f); }
    float getAttackDb() const noexcept { return attackDb_; }
    float getSustainDb() const noexcept { return sustainDb_; }
    int getLatencySamples() const noexcept { return 0; }

    // 目前套用的 gain（dB，可正可負；block 結束時更新；thread-safe）
    float getLastGainDb() const noexcept { return lastGainDb_.load(std::memory_order_relaxed); }

    // ---- 處理 -----------------------------------------------------------
    // 核心：餵入偵測用 sample（max|x| across channels），回傳線性 gain
    float nextGain(float detectorInput) noexcept {
        const float fast = fast_.processSample(detectorInput);
        const float slow = slow_.processSample(detectorInput);
        const float slowDb = gainToDb(slow);
        float target = 0.0f;
        if (slowDb > kFloorDb) {
            const float d = gainToDb(fast) - slowDb; // 快慢 envelope 的差（dB）
            if (d > 0.0f)
                target = attackDb_ * std::min(d / kFullScaleDb, 1.0f);
            else
                target = sustainDb_ * std::min(-d / kFullScaleDb, 1.0f);
        }
        gainDb_ = target + smoothCoef_ * (gainDb_ - target);
        gain_ = dbToGain(gainDb_);
        return gain_;
    }

    void process(AudioBlock block) noexcept {
        const int n = block.getNumSamples();
        const int nch = block.getNumChannels();
        for (int i = 0; i < n; ++i) {
            float det = 0.0f;
            for (int ch = 0; ch < nch; ++ch)
                det = std::max(det, std::abs(block.getChannelPointer(ch)[i]));
            const float g = nextGain(det);
            for (int ch = 0; ch < nch; ++ch)
                block.getChannelPointer(ch)[i] *= g;
        }
        lastGainDb_.store(gainDb_, std::memory_order_relaxed);
    }

private:
    void copyFrom(const TransientShaper& o) {
        sampleRate_ = o.sampleRate_;
        numChannels_ = o.numChannels_;
        attackDb_ = o.attackDb_;
        sustainDb_ = o.sustainDb_;
        fast_ = o.fast_;
        slow_ = o.slow_;
        smoothCoef_ = o.smoothCoef_;
        gainDb_ = o.gainDb_;
        gain_ = o.gain_;
        lastGainDb_.store(o.lastGainDb_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }

    double sampleRate_ = 48000.0;
    int numChannels_ = 2;
    float attackDb_ = 0.0f;
    float sustainDb_ = 0.0f;

    EnvelopeFollower fast_, slow_;
    float smoothCoef_ = 0.0f;
    float gainDb_ = 0.0f;
    float gain_ = 1.0f;
    std::atomic<float> lastGainDb_{0.0f};
};

} // namespace dawlab::dsp
