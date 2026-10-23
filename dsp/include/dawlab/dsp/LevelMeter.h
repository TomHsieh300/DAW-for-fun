#pragma once
// 第 11 章：LevelMeter —— sample peak（含 hold 與 dB/s decay）與滑動視窗 RMS。
//
// 一個 meter 服務多個 channel（每個 channel 獨立狀態）。process() 在 audio thread 跑，
// 量測結果寫進 std::atomic<float>（relaxed），UI thread 隨時可以用 getXxxDb() 讀取。
//
// 三個讀數（都以 dBFS 表示，靜音 = kMinusInfinityDb = -100）：
// - peak：      瞬間攻擊（新的 |x| 比目前大就立刻跳上去），之後以固定 dB/s 速度下降（ballistics）。
// - peakHold：  在 hold 時間內停在最近的最大值，時間到才以同樣速度下降（永遠不低於 peak）。
// - rms：       長度 rmsWindowMs 的滑動視窗 RMS = sqrt( (1/W) Σ x[n]^2 )。
//
// dB/s decay 在線性域就是每個 sample 乘一個固定係數：
//     g = 10^( -decayDbPerSecond / (20 · fs) )
// 因為 20·log10(g^fs) = -decayDbPerSecond。

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <memory>
#include <vector>

namespace dawlab::dsp {

class LevelMeter {
public:
    static constexpr float kMaxRmsWindowMs = 3000.0f; // prepare 時依此配置 ring buffer

    // 唯一可以 allocate 的地方
    void prepare(double sampleRate, int numChannels) {
        assert(sampleRate > 0.0 && numChannels > 0);
        sampleRate_ = sampleRate;
        numChannels_ = numChannels;
        maxRmsSamples_ = std::max(1, msToSamples(kMaxRmsWindowMs, sampleRate));
        channels_.assign(static_cast<size_t>(numChannels), ChannelState{});
        for (ChannelState& c : channels_)
            c.rmsRing.assign(static_cast<size_t>(maxRmsSamples_), 0.0f);
        peakDb_ = std::make_unique<std::atomic<float>[]>(static_cast<size_t>(numChannels));
        holdDb_ = std::make_unique<std::atomic<float>[]>(static_cast<size_t>(numChannels));
        rmsDb_ = std::make_unique<std::atomic<float>[]>(static_cast<size_t>(numChannels));
        updateCoefficients();
        reset();
    }

    void reset() noexcept {
        for (ChannelState& c : channels_) {
            c.peak = 0.0f;
            c.hold = 0.0f;
            c.holdCounter = 0;
            c.sumSquares = 0.0;
            c.ringPos = 0;
            std::fill(c.rmsRing.begin(), c.rmsRing.end(), 0.0f);
        }
        for (size_t ch = 0; ch < static_cast<size_t>(numChannels_); ++ch) {
            peakDb_[ch].store(kMinusInfinityDb, std::memory_order_relaxed);
            holdDb_[ch].store(kMinusInfinityDb, std::memory_order_relaxed);
            rmsDb_[ch].store(kMinusInfinityDb, std::memory_order_relaxed);
        }
        rmsWindowChanged_.store(false, std::memory_order_relaxed);
        rmsSamples_ = pendingRmsSamples_.load(std::memory_order_relaxed);
    }

    // hold 時間；< 0 代表無限 hold（直到 resetPeakHold()）。預設 1000 ms。
    void setPeakHoldMs(float ms) noexcept {
        peakHoldMs_ = ms;
        updateCoefficients();
    }

    // peak / hold 的下降速度，預設 24 dB/s
    void setDecayDbPerSecond(float dbPerSecond) noexcept {
        decayDbPerSecond_ = std::max(0.0f, dbPerSecond);
        updateCoefficients();
    }

    // RMS 滑動視窗長度（上限 kMaxRmsWindowMs），預設 300 ms。改變時視窗內容會清空重新累積。
    void setRmsWindowMs(float ms) noexcept {
        rmsWindowMs_ = std::clamp(ms, 1.0f, kMaxRmsWindowMs);
        updateCoefficients();
    }

    int getNumChannels() const noexcept { return numChannels_; }
    float getPeakHoldMs() const noexcept { return peakHoldMs_; }
    float getDecayDbPerSecond() const noexcept { return decayDbPerSecond_; }
    float getRmsWindowMs() const noexcept { return rmsWindowMs_; }

    // audio thread：不 allocate、不鎖
    void process(const AudioBlock& block) noexcept {
        applyPendingRequests();
        if (rmsWindowChanged_.exchange(false, std::memory_order_relaxed)) {
            // 視窗長度改了：清空 ring 重新累積（會短暫讀到偏低的 RMS）
            rmsSamples_ = pendingRmsSamples_.load(std::memory_order_relaxed);
            for (ChannelState& c : channels_) {
                std::fill(c.rmsRing.begin(), c.rmsRing.end(), 0.0f);
                c.sumSquares = 0.0;
                c.ringPos = 0;
            }
        }
        const int nch = std::min(numChannels_, block.getNumChannels());
        const int n = block.getNumSamples();
        for (int ch = 0; ch < nch; ++ch) {
            ChannelState& c = channels_[static_cast<size_t>(ch)];
            const float* x = block.getChannelPointer(ch);
            for (int i = 0; i < n; ++i) {
                const float a = std::abs(x[i]);

                // --- peak：瞬間 attack、固定 dB/s release ---
                if (a > c.peak)
                    c.peak = a;
                else
                    c.peak *= decayPerSample_;

                // --- peak hold ---
                if (a >= c.hold) {
                    c.hold = a;
                    c.holdCounter = holdSamples_;
                } else if (holdSamples_ < 0) {
                    // 無限 hold：什麼都不做
                } else if (c.holdCounter > 0) {
                    --c.holdCounter;
                } else {
                    c.hold = std::max(c.hold * decayPerSample_, c.peak);
                }

                // --- 滑動視窗 RMS：ring buffer 存 x^2，維護視窗內的總和 ---
                const float sq = a * a;
                float& slot = c.rmsRing[static_cast<size_t>(c.ringPos)];
                c.sumSquares += static_cast<double>(sq) - static_cast<double>(slot);
                slot = sq;
                if (++c.ringPos >= rmsSamples_) {
                    c.ringPos = 0;
                    // 每繞一圈就重新精確加總一次，消除浮點累積誤差（攤平後仍是 O(1) / sample）
                    double exact = 0.0;
                    for (int j = 0; j < rmsSamples_; ++j)
                        exact += c.rmsRing[static_cast<size_t>(j)];
                    c.sumSquares = exact;
                }
            }
            // 極小值 flush 成 0，避免 denormal 與永遠到不了 -inf
            if (c.peak < 1e-10f)
                c.peak = 0.0f;
            if (c.hold < 1e-10f)
                c.hold = 0.0f;
            if (c.sumSquares < 0.0)
                c.sumSquares = 0.0;

            const double meanSquare = c.sumSquares / static_cast<double>(rmsSamples_);
            peakDb_[static_cast<size_t>(ch)].store(gainToDb(c.peak), std::memory_order_relaxed);
            holdDb_[static_cast<size_t>(ch)].store(gainToDb(c.hold), std::memory_order_relaxed);
            rmsDb_[static_cast<size_t>(ch)].store(gainToDb(static_cast<float>(std::sqrt(meanSquare))),
                                                  std::memory_order_relaxed);
        }
    }

    // 以下 getter thread-safe（任何 thread 都可呼叫）
    float getPeakDb(int ch) const noexcept { return load(peakDb_.get(), ch); }
    float getRmsDb(int ch) const noexcept { return load(rmsDb_.get(), ch); }
    float getPeakHoldDb(int ch) const noexcept { return load(holdDb_.get(), ch); }

    // 把 hold 清掉（例如使用者點了 meter）。這通常從 UI thread 呼叫，所以不直接碰 audio thread 的狀態，
    // 而是設一個 atomic flag，讓 audio thread 在下一次 process() 自己清。
    void resetPeakHold() noexcept { resetHoldRequested_.store(true, std::memory_order_relaxed); }

    // 把 dB/s 換成每個 sample 的線性倍率（公開給練習與範例驗證用）
    static float decayCoefficient(float dbPerSecond, double sampleRate) noexcept {
        return static_cast<float>(std::pow(10.0, -static_cast<double>(dbPerSecond) / (20.0 * sampleRate)));
    }

private:
    struct ChannelState {
        float peak = 0.0f;
        float hold = 0.0f;
        int holdCounter = 0;
        double sumSquares = 0.0;
        int ringPos = 0;
        std::vector<float> rmsRing;
    };

    float load(const std::atomic<float>* v, int ch) const noexcept {
        if (v == nullptr || ch < 0 || ch >= numChannels_)
            return kMinusInfinityDb;
        return v[ch].load(std::memory_order_relaxed);
    }

    // audio thread：處理 UI thread 送來的請求
    void applyPendingRequests() noexcept {
        if (resetHoldRequested_.exchange(false, std::memory_order_relaxed))
            for (ChannelState& c : channels_) {
                c.hold = c.peak;
                c.holdCounter = 0;
            }
    }

    void updateCoefficients() noexcept {
        if (sampleRate_ <= 0.0)
            return;
        decayPerSample_ = decayCoefficient(decayDbPerSecond_, sampleRate_);
        holdSamples_ = peakHoldMs_ < 0.0f ? -1 : msToSamples(peakHoldMs_, sampleRate_);
        const int wanted = std::clamp(msToSamples(rmsWindowMs_, sampleRate_), 1, maxRmsSamples_);
        if (wanted != pendingRmsSamples_.load(std::memory_order_relaxed)) {
            pendingRmsSamples_.store(wanted, std::memory_order_relaxed);
            rmsWindowChanged_.store(true, std::memory_order_relaxed);
        }
    }

    double sampleRate_ = 0.0;
    int numChannels_ = 0;
    int maxRmsSamples_ = 1;
    float peakHoldMs_ = 1000.0f;
    float decayDbPerSecond_ = 24.0f;
    float rmsWindowMs_ = 300.0f;
    float decayPerSample_ = 1.0f;
    int holdSamples_ = 0;
    int rmsSamples_ = 1;
    std::atomic<int> pendingRmsSamples_{1};
    std::atomic<bool> rmsWindowChanged_{false};

    std::vector<ChannelState> channels_;
    std::unique_ptr<std::atomic<float>[]> peakDb_, holdDb_, rmsDb_;
    std::atomic<bool> resetHoldRequested_{false};
};

} // namespace dawlab::dsp
