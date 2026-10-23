#pragma once
// DelayLine：整數 sample 延遲線（circular buffer），第 05 章 Limiter 的 lookahead 用它把音訊延後。
//
// 觀念：
//   延遲 D 個 sample 的系統 y[n] = x[n - D]。要做到這件事，我們必須「記住」最近 D 個輸入，
//   所以需要一塊長度 ≥ D + 1 的記憶體。用 circular buffer（環形緩衝）的話，每次 push 只要
//   寫一格、把寫入指標往前推一格，讀取時往回數 D 格即可，不需要搬動資料。
//
//   write index  w  →  最新一個 sample 寫在 buffer[w-1]
//   read(ch, d)      =  buffer[(w - 1 - d) mod size]
//
//        ┌───┬───┬───┬───┬───┬───┬───┬───┐
//        │   │ x[n-5] x[n-4] x[n-3] x[n-2] x[n-1] x[n] │   │
//        └───┴───┴───┴───┴───┴───┴───┴───┘
//                                        ↑ w 下次寫這裡
//                      ↑ read(3)         ↑ read(0)
//
// 介面（API 合約，第 09 章的 FractionalDelayLine 會沿用同樣的設計）：
//   prepare(maxDelaySamples, numChannels)  配置記憶體（唯一會 allocate 的地方）
//   reset()                                清成 0
//   push(ch, x)                            把一個新 sample 推進第 ch 個 channel
//   read(ch, d)                            讀出「d 次 push 之前」的 sample；d = 0 是剛 push 的那個
//
// 每個 channel 有自己的 write index，所以 push 的順序與次數可以各 channel 獨立
// （例如只處理 mono 時只 push channel 0）。

#include <algorithm>
#include <cassert>
#include <vector>

namespace dawlab::dsp {

class DelayLine {
public:
    // maxDelaySamples：之後 read() 允許的最大 delay。會 allocate，不可在 audio thread 呼叫。
    void prepare(int maxDelaySamples, int numChannels) {
        assert(maxDelaySamples >= 0 && numChannels >= 0);
        maxDelay_ = maxDelaySamples;
        numChannels_ = numChannels;
        size_ = maxDelaySamples + 1; // 要能同時存「現在」與「maxDelay 之前」，所以長度 +1
        buffer_.assign(static_cast<size_t>(size_) * static_cast<size_t>(numChannels), 0.0f);
        writeIndex_.assign(static_cast<size_t>(numChannels), 0);
    }

    void reset() noexcept {
        std::fill(buffer_.begin(), buffer_.end(), 0.0f);
        std::fill(writeIndex_.begin(), writeIndex_.end(), 0);
    }

    int getMaxDelaySamples() const noexcept { return maxDelay_; }
    int getNumChannels() const noexcept { return numChannels_; }

    // 推進一個新 sample。O(1)，不 allocate。
    void push(int channel, float x) noexcept {
        assert(channel >= 0 && channel < numChannels_);
        int& w = writeIndex_[static_cast<size_t>(channel)];
        channelData(channel)[w] = x;
        if (++w >= size_) // 環形：到尾端就繞回開頭
            w = 0;
    }

    // 讀出 delaySamples 次 push 之前的 sample（0 = 最新的那個）。
    // delaySamples 超過 maxDelay 會被夾住（clamp），避免讀到不該讀的位置。
    float read(int channel, int delaySamples) const noexcept {
        assert(channel >= 0 && channel < numChannels_);
        const int d = std::clamp(delaySamples, 0, maxDelay_);
        int idx = writeIndex_[static_cast<size_t>(channel)] - 1 - d;
        if (idx < 0) // 往回數超過開頭就從尾端接回來（等價於 mod size，但避免負數取餘數的陷阱）
            idx += size_;
        return channelData(channel)[idx];
    }

private:
    float* channelData(int channel) noexcept {
        return buffer_.data() + static_cast<size_t>(channel) * static_cast<size_t>(size_);
    }
    const float* channelData(int channel) const noexcept {
        return buffer_.data() + static_cast<size_t>(channel) * static_cast<size_t>(size_);
    }

    int maxDelay_ = 0;
    int numChannels_ = 0;
    int size_ = 1;
    std::vector<float> buffer_;   // 所有 channel 連續排列：[ch0 的 size_ 格][ch1 的 size_ 格]...
    std::vector<int> writeIndex_; // 每個 channel 自己的寫入位置
};

} // namespace dawlab::dsp
