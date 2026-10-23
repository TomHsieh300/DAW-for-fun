#pragma once
// Limiter：lookahead brickwall limiter（第 05 章）。
//
// 保證：任何輸入下，輸出的 sample peak ≤ ceiling（線性值 = dbToGain(ceilingDb)）。
//
// 演算法（每個 sample，stereo-linked；L = lookahead 的 sample 數）：
//
//   1. 輸入增益        xs[n]  = x[n] * inputGain
//   2. 需要的 gain     g[n]   = min(1, ceiling / max_ch |xs_ch[n]|)
//   3. release         gr[n]  = min(g[n], gr[n-1] + (1 - gr[n-1]) * cRel)   （只讓 gain「慢慢回到 1」）
//   4. 視窗最小值      m[n]   = min(gr[n-L], ..., gr[n])                       （長度 L+1 的 sliding window min）
//   5. 移動平均        s[n]   = (m[n-L] + ... + m[n]) / (L + 1)                 （長度 L+1 的 box filter = 線性 attack ramp）
//   6. 輸出            y[n]   = xs[n-L] * s[n]                                  （音訊延遲 L 個 sample）
//
// 為什麼不會超過 ceiling（完整推導見 README 2.5 節）：
//   第 5 步平均的每一項 m[n-k]（k = 0..L）都是「包含 index n-L」的視窗最小值，
//   所以 m[n-k] ≤ gr[n-L] ≤ g[n-L]；平均之後 s[n] ≤ g[n-L]，
//   於是 |y[n]| = |xs[n-L]| · s[n] ≤ |xs[n-L]| · g[n-L] ≤ ceiling。
//
// 使用前一定要 prepare()；process() 收到的 block 的 channel 數不可超過 prepare() 時的 numChannels
// （Debug 建置會 assert；Release 建置把多出來的 channel 清成 0——寧可靜音，也不讓沒被限制的訊號通過）。
//
// 只依賴 common/ 與 DelayLine.h；不依賴 JUCE。

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>
#include <dawlab/dsp/DelayLine.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

namespace dawlab::dsp {

// ---------------------------------------------------------------------------
// SlidingWindowMin：固定長度視窗的最小值，用 monotonic deque 做到 O(1) amortized。
//
// 想法：deque 裡只保留「還有機會成為最小值」的元素——由舊到新、值嚴格遞增。
// 新值 v 進來時，比 v 大（或相等）的舊元素永遠不可能再贏過 v（它們比 v 更早過期），直接丟掉。
// 最前面的元素就是目前視窗的最小值；它離開視窗時才 pop 掉。
// 每個元素最多 push 一次、pop 一次，所以 N 個 sample 總共 O(N)，平均每個 sample O(1)。
//
// deque 用預先配置的環形陣列實作：push() 內不 allocate。
// ---------------------------------------------------------------------------
class SlidingWindowMin {
public:
    // 會 allocate；maxWindowLength ≥ 1
    void prepare(int maxWindowLength) {
        assert(maxWindowLength >= 1);
        capacity_ = maxWindowLength;
        values_.assign(static_cast<size_t>(capacity_), 0.0f);
        indices_.assign(static_cast<size_t>(capacity_), 0);
        window_ = capacity_;
        reset();
    }

    // 視窗長度（1..maxWindowLength）。改變長度時整個清空，重新開始累積。
    void setWindowLength(int length) noexcept {
        window_ = std::clamp(length, 1, capacity_);
        reset();
    }

    int getWindowLength() const noexcept { return window_; }

    void reset() noexcept {
        head_ = 0;
        count_ = 0;
        sampleIndex_ = 0;
    }

    // 推入新值，回傳「最近 window 個值」的最小值（含這一個）。
    float push(float v) noexcept {
        const int64_t n = sampleIndex_++;

        // (a) 從尾端丟掉所有 ≥ v 的元素：它們比 v 早過期、又不比 v 小，不可能再當最小值
        while (count_ > 0 && values_[static_cast<size_t>(back())] >= v)
            --count_;

        // (b) 前端若已離開視窗 [n - window + 1, n] 就丟掉
        while (count_ > 0 && indices_[static_cast<size_t>(head_)] <= n - window_) {
            head_ = (head_ + 1) % capacity_;
            --count_;
        }

        // (c) 把 v 放到尾端。此時 deque 內元素數 ≤ window - 1，所以加上 v 不會超過 capacity
        const int slot = (head_ + count_) % capacity_;
        values_[static_cast<size_t>(slot)] = v;
        indices_[static_cast<size_t>(slot)] = n;
        ++count_;

        return values_[static_cast<size_t>(head_)];
    }

private:
    int back() const noexcept { return (head_ + count_ - 1) % capacity_; }

    int capacity_ = 1;
    int window_ = 1;
    int head_ = 0;
    int count_ = 0;
    int64_t sampleIndex_ = 0;
    std::vector<float> values_;
    std::vector<int64_t> indices_;
};

// ---------------------------------------------------------------------------
// Limiter
// ---------------------------------------------------------------------------
class Limiter {
public:
    // lookahead 的上限（決定 prepare 要配置多少記憶體）。要在 prepare 之前呼叫，下一次 prepare 才生效；預設 20 ms。
    void setMaxLookaheadMs(float ms) noexcept { maxLookaheadMs_ = std::max(0.0f, ms); }

    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        assert(sampleRate > 0.0 && numChannels >= 1);
        (void)maxBlockSize; // 本元件逐 sample 處理，不需要依 block 大小配置暫存
        sampleRate_ = sampleRate;
        numChannels_ = numChannels;
        preparedMaxLookaheadMs_ = maxLookaheadMs_;
        maxLookaheadSamples_ = std::max(0, msToSamples(maxLookaheadMs_, sampleRate));

        delay_.prepare(maxLookaheadSamples_, numChannels);
        windowMin_.prepare(maxLookaheadSamples_ + 1);
        averageRing_.assign(static_cast<size_t>(maxLookaheadSamples_ + 1), 1.0f);

        updateDerived();
        applyLookahead();
        reset();
    }

    void reset() noexcept {
        delay_.reset();
        windowMin_.reset();
        std::fill(averageRing_.begin(), averageRing_.end(), 1.0f);
        averageSum_ = static_cast<double>(averageLength_);
        averageIndex_ = 0;
        releasedGain_ = 1.0f;
        currentInputGain_ = targetInputGain_;
        gainReductionDb_.store(0.0f, std::memory_order_relaxed);
        safetyClipCount_.store(0, std::memory_order_relaxed);
    }

    // ---- 參數（在 block 之間呼叫） ------------------------------------------------
    void setCeilingDb(float db) noexcept {
        ceilingDb_ = db;
        ceiling_ = dbToGain(db);
    }

    // 輸入增益用約 20 ms 的線性 ramp 走到新值，避免 zipper noise。
    // 值沒變就直接 return：host（例如第 13 章的 plugin）常常每個 block 都把參數再設一次，
    // 若每次都依「剩下的距離」重算 step，ramp 會越走越慢、永遠走不完。
    void setInputGainDb(float db) noexcept {
        if (db == inputGainDb_)
            return;
        inputGainDb_ = db;
        targetInputGain_ = dbToGain(db);
        const double rampSamples = std::max(1.0, 0.02 * sampleRate_);
        inputGainRampStep_ = static_cast<float>(std::abs(targetInputGain_ - currentInputGain_) / rampSamples);
    }

    void setReleaseMs(float ms) noexcept {
        ms = std::max(0.0f, ms);
        if (ms == releaseMs_)
            return; // 沒變就不必重算 exp()（host 可能每個 block 都呼叫）
        releaseMs_ = ms;
        updateDerived();
    }

    // 生效的 lookahead = min(ms, 上限)，負值當 0；實際生效的 sample 數看 getLatencySamples()。
    // 這裡只記住使用者要求的值，上限在用到時才套用，所以和 setMaxLookaheadMs() / prepare() 的呼叫順序無關
    // （例如先 setLookaheadMs(30) 再 setMaxLookaheadMs(50)，prepare 後就是 30 ms）。
    // 改變 lookahead 會重設 gain 平滑的狀態（視窗長度變了），建議在停止播放時改。
    void setLookaheadMs(float ms) noexcept {
        requestedLookaheadMs_ = std::max(0.0f, ms);
        if (sampleRate_ <= 0.0)
            return; // 還沒 prepare：先記住，prepare 時再套用
        updateDerived();
        applyLookahead();
    }

    float getCeilingDb() const noexcept { return ceilingDb_; }
    float getInputGainDb() const noexcept { return inputGainDb_; }
    float getReleaseMs() const noexcept { return releaseMs_; }
    // 目前生效的 lookahead（已夾在 [0, 上限]）；prepare 之後與 getLatencySamples() 一致
    float getLookaheadMs() const noexcept { return std::min(requestedLookaheadMs_, effectiveMaxLookaheadMs()); }
    // setMaxLookaheadMs() 設定的值（下一次 prepare 會用它配置記憶體）
    float getMaxLookaheadMs() const noexcept { return maxLookaheadMs_; }

    // 延遲 = lookahead 的 sample 數。Plugin host 會拿這個值做 plugin delay compensation。
    int getLatencySamples() const noexcept { return lookaheadSamples_; }

    // 上一個 block 內最大的 gain reduction（正值 dB；6 代表壓了 6 dB）。thread-safe。
    float getGainReductionDb() const noexcept { return gainReductionDb_.load(std::memory_order_relaxed); }

    // 最後一道 safety clamp 攔到「明顯超過 ceiling」（相對誤差 > 1e-6）的次數。
    // 理論上永遠是 0；只有在播放中改 ceiling / lookahead、或輸入含 NaN / Inf 時才會 > 0。
    // 拿來當除錯指標（例如 fuzz test）。
    uint64_t getSafetyClipCount() const noexcept { return safetyClipCount_.load(std::memory_order_relaxed); }

    // ---- 處理 -----------------------------------------------------------------
    void process(AudioBlock block) noexcept {
        assert(numChannels_ > 0 && "Limiter::prepare() not called");
        assert(block.getNumChannels() <= numChannels_ && "block has more channels than prepare()");
        const int numSamples = block.getNumSamples();
        const int numChannels = std::min(block.getNumChannels(), numChannels_);

        // 超出 prepare() 的 channel 沒有 delay line、也沒參與 peak 偵測，無法保證 ≤ ceiling：
        // 直接清成 0（沒呼叫 prepare() 時 numChannels_ = 0，等於整個 block 都清成 0）。
        if (block.getNumChannels() > numChannels)
            block.getChannelSubset(numChannels, block.getNumChannels() - numChannels).clear();
        if (numChannels <= 0)
            return;

        const int L = lookaheadSamples_;
        const float ceiling = ceiling_;
        const float ceilingTolerance = ceiling * (1.0f + 1.0e-6f); // 浮點捨入等級的超出不算違規
        const float cRel = releaseCoeff_;
        const double invLen = 1.0 / static_cast<double>(averageLength_);
        float minSmoothedGain = 1.0f;
        uint64_t clips = 0;

        for (int i = 0; i < numSamples; ++i) {
            // (1) 輸入增益（線性 ramp）→ 推進 delay line → 取所有 channel 的 |peak|（stereo link）
            if (currentInputGain_ != targetInputGain_) {
                if (std::abs(targetInputGain_ - currentInputGain_) <= inputGainRampStep_)
                    currentInputGain_ = targetInputGain_;
                else
                    currentInputGain_ += (targetInputGain_ > currentInputGain_) ? inputGainRampStep_ : -inputGainRampStep_;
            }
            float peak = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch) {
                const float xs = block.getSample(ch, i) * currentInputGain_;
                delay_.push(ch, xs);
                peak = std::max(peak, std::abs(xs)); // NaN 會被 max 忽略，之後由 safety clamp 處理
            }

            // (2) 這個 sample 需要的 gain：g = min(1, ceiling / peak)
            const float g = (peak > ceiling) ? ceiling / peak : 1.0f;

            // (3) release：gain 往上只能以 one-pole 的速度回到 1；往下則瞬間跟上
            float released = releasedGain_ + (1.0f - releasedGain_) * cRel;
            if (released > 1.0f - 1.0e-6f) // 收斂到夠接近 1 就直接設成 1，避免永遠差一點點
                released = 1.0f;
            releasedGain_ = std::min(g, released);

            // (4) lookahead 視窗（長度 L+1）內的最小值
            const float m = windowMin_.push(releasedGain_);

            // (5) 長度 L+1 的移動平均 → 線性 attack ramp，峰值抵達輸出時 gain 剛好降到位
            float& oldest = averageRing_[static_cast<size_t>(averageIndex_)];
            averageSum_ += static_cast<double>(m) - static_cast<double>(oldest);
            oldest = m;
            if (++averageIndex_ >= averageLength_)
                averageIndex_ = 0;
            // s = 平均本身（README 2.4 第 4 步）。平均的每一項都在 [0, 1]，所以 s 也在 [0, 1]；
            // clamp 只擋 running sum 的浮點捨入（double 累積，誤差遠小於 safety clamp 的 1e-6 容差）。
            // 注意：下限不能用 m——release 期間 gr 一路往上，m[n] = gr[n-L] 反而比平均大，
            // 用 m 當下限等於把 release 那一側的 box 平滑拿掉，release 很短時 gain 會一個 sample 跳回去（click）。
            const float s = std::clamp(static_cast<float>(averageSum_ * invLen), 0.0f, 1.0f);
            minSmoothedGain = std::min(minSmoothedGain, s);

            // (6) 輸出 = 延遲後的音訊 × 平滑後的 gain；最後用 safety clamp 兜底
            for (int ch = 0; ch < numChannels; ++ch) {
                float y = delay_.read(ch, L) * s;
                if (!(std::abs(y) <= ceiling)) { // 也會攔住 NaN（NaN 的比較一律 false）
                    if (!(std::abs(y) <= ceilingTolerance))
                        ++clips;
                    y = std::isnan(y) ? 0.0f : std::clamp(y, -ceiling, ceiling);
                }
                block.setSample(ch, i, y);
            }
        }

        // gainToDb(1) = 0 → 取負會變成 -0.0，printf 會印出 "-0.00"；用 max 把它變回 +0.0
        gainReductionDb_.store(std::max(0.0f, -gainToDb(minSmoothedGain)), std::memory_order_relaxed);
        if (clips > 0)
            safetyClipCount_.fetch_add(clips, std::memory_order_relaxed);
    }

private:
    void updateDerived() noexcept {
        if (sampleRate_ <= 0.0)
            return;
        lookaheadSamples_ = std::clamp(msToSamples(getLookaheadMs(), sampleRate_), 0, maxLookaheadSamples_);
        averageLength_ = lookaheadSamples_ + 1;
        // one-pole release：每個 sample 走「剩餘距離」的 (1 - exp(-1/(τ fs)))，τ = releaseMs
        const double tauSamples = static_cast<double>(releaseMs_) * 0.001 * sampleRate_;
        releaseCoeff_ = tauSamples > 0.0 ? static_cast<float>(1.0 - std::exp(-1.0 / tauSamples)) : 1.0f;
    }

    // prepare 之後，上限以 prepare 當時配置的為準（之後再呼叫 setMaxLookaheadMs() 要等下一次 prepare 才生效）
    float effectiveMaxLookaheadMs() const noexcept {
        return sampleRate_ > 0.0 ? preparedMaxLookaheadMs_ : maxLookaheadMs_;
    }

    // 視窗長度改變：min 視窗與移動平均都從頭累積（假設之前的 gain 都是 1）
    void applyLookahead() noexcept {
        windowMin_.setWindowLength(averageLength_);
        std::fill(averageRing_.begin(), averageRing_.end(), 1.0f);
        averageSum_ = static_cast<double>(averageLength_);
        averageIndex_ = 0;
    }

    // 參數
    float maxLookaheadMs_ = 20.0f;         // setMaxLookaheadMs() 的值（下一次 prepare 用）
    float preparedMaxLookaheadMs_ = 20.0f; // 上一次 prepare 實際配置的上限
    float requestedLookaheadMs_ = 5.0f;    // setLookaheadMs() 要求的值（≥ 0，尚未套用上限）
    float ceilingDb_ = -1.0f;
    float ceiling_ = dbToGain(-1.0f);
    float inputGainDb_ = 0.0f;
    float targetInputGain_ = 1.0f;
    float releaseMs_ = 100.0f;

    // 由參數推導出的量
    double sampleRate_ = 0.0;
    int numChannels_ = 0;
    int maxLookaheadSamples_ = 0;
    int lookaheadSamples_ = 0;
    int averageLength_ = 1;
    float releaseCoeff_ = 1.0f;
    float inputGainRampStep_ = 1.0f;

    // 狀態
    DelayLine delay_;
    SlidingWindowMin windowMin_;
    std::vector<float> averageRing_;
    double averageSum_ = 1.0;
    int averageIndex_ = 0;
    float releasedGain_ = 1.0f;
    float currentInputGain_ = 1.0f;

    // 給 UI / 除錯讀的量
    std::atomic<float> gainReductionDb_{0.0f};
    std::atomic<uint64_t> safetyClipCount_{0};
};

} // namespace dawlab::dsp
