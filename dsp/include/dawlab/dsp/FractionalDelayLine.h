#pragma once
// FractionalDelayLine：支援「非整數 sample」延遲的 circular buffer（第 09 章）。
//
// 設計重點：
// - buffer 長度取 2 的次方，讀寫指標的 wrap 用 bitwise AND（index & mask）而不是 %，
//   在 audio thread 上更便宜，也不用擔心負數取餘數的問題。
// - 每個 channel 一段連續記憶體、各自一個 write index，所以 push(ch, x) 可以逐 channel 呼叫。
// - 慣例：push() 之後 readXxx(ch, 0) 回傳剛剛 push 進去的那個 sample；
//   readXxx(ch, D) 回傳 D 次 push 以前的 sample（D 可以是小數）。
// - 三種讀法：
//     readInteger : 整數延遲，直接取值（最便宜，也最準，但延遲只能是整數）
//     readLinear  : 兩點線性插值。簡單，但高頻會衰減（Δ = 0.5 時 |H| = |cos(πf/fs)|）
//     readCubic   : 四點 cubic Hermite（Catmull-Rom）插值。高頻保留得比 linear 好很多
//   readLinear / readCubic 另有 Split 版本（延遲拆成 int + 小數），給長延遲、需要 sub-sample 精度的呼叫端用。
//     readLagrange: 四點 3 階 Lagrange 插值（教學比較用；在 Δ = 0.5 時與 Hermite 完全相同）
// - prepare() 是唯一會 allocate 的地方；其餘 method 都是 noexcept、不 allocate。

#include <algorithm>
#include <cassert>
#include <cmath>
#include <vector>

namespace dawlab::dsp {

class FractionalDelayLine {
public:
    // maxDelaySamples：之後 read 時允許的最大延遲（以 sample 計，含小數部分進位後的值）。
    // 內部會多配 4 個 sample 的餘裕，讓 cubic 插值需要的 d+2 不會越界。
    void prepare(int maxDelaySamples, int numChannels) {
        maxDelay_ = std::max(1, maxDelaySamples);
        numChannels_ = std::max(1, numChannels);
        // cubic 讀取需要 d-1 .. d+2，d 最大 = maxDelay_，所以至少要 maxDelay_ + 3 個 sample；
        // 再往上取到 2 的次方，讓 wrap 可以用 & mask 完成。
        size_ = nextPowerOfTwo(maxDelay_ + 4);
        mask_ = size_ - 1;
        buffer_.assign(static_cast<size_t>(size_) * static_cast<size_t>(numChannels_), 0.0f);
        writeIndex_.assign(static_cast<size_t>(numChannels_), 0);
    }

    void reset() noexcept {
        std::fill(buffer_.begin(), buffer_.end(), 0.0f);
        std::fill(writeIndex_.begin(), writeIndex_.end(), 0);
    }

    int getMaxDelaySamples() const noexcept { return maxDelay_; }
    int getNumChannels() const noexcept { return numChannels_; }
    int getBufferSize() const noexcept { return size_; } // 2 的次方（教學用，可印出來看）

    // 寫入一個 sample，write index 前進一格並 wrap。
    void push(int ch, float x) noexcept {
        assert(ch >= 0 && ch < numChannels_);
        float* b = channelData(ch);
        int& w = writeIndex_[static_cast<size_t>(ch)];
        b[w] = x;
        w = (w + 1) & mask_; // 等價於 (w + 1) % size_，但只需要一個 AND
    }

    // 整數延遲：delaySamples = 0 代表最後一次 push 的 sample。
    float readInteger(int ch, int delaySamples) const noexcept {
        return at(ch, std::clamp(delaySamples, 0, maxDelay_));
    }

    // 線性插值：y = (1-Δ)·x[n-d] + Δ·x[n-d-1]，d = floor(delay)，Δ = delay - d。
    float readLinear(int ch, float delaySamples) const noexcept {
        const float delay = std::clamp(delaySamples, 0.0f, static_cast<float>(maxDelay_));
        const int d = static_cast<int>(delay); // floor（delay ≥ 0）
        return readLinearSplit(ch, d, delay - static_cast<float>(d));
    }

    // 同上，但延遲已經拆成「整數部分 d」與「小數部分 frac ∈ [0, 1)」。
    // 為什麼需要它：float 只有 24 bit 精度，延遲 65536 sample 以上時 float 的最小間距（ulp）
    // 就是 1/128 sample，720000 sample（15 s @ 48 kHz）時更到 1/16 sample——讀取位置會被量化成階梯。
    // 呼叫端用 double 算位置、自己拆成 int + 小數，小數部分永遠在 [0, 1)，float 精度綽綽有餘。
    float readLinearSplit(int ch, int d, float frac) const noexcept {
        clampSplit(d, frac, 0);
        const float a = at(ch, d);
        const float b = at(ch, d + 1);
        return a + frac * (b - a);
    }

    // 四點 cubic Hermite（Catmull-Rom）插值。需要 d-1 這個「比較新」的 sample，
    // 所以延遲至少要 1；小於 1 會被 clamp 到 1。
    float readCubic(int ch, float delaySamples) const noexcept {
        const float delay = std::clamp(delaySamples, 1.0f, static_cast<float>(maxDelay_));
        const int d = static_cast<int>(delay);
        return readCubicSplit(ch, d, delay - static_cast<float>(d));
    }

    // readCubic 的「整數 + 小數」版本（理由見 readLinearSplit）。d < 1 時 clamp 到 1、t = 0。
    float readCubicSplit(int ch, int d, float t) const noexcept {
        clampSplit(d, t, 1);
        const float y0 = at(ch, d - 1); // 比目標新一個 sample
        const float y1 = at(ch, d);     // 目標的左邊
        const float y2 = at(ch, d + 1); // 目標的右邊（更舊）
        const float y3 = at(ch, d + 2);
        // Catmull-Rom：切線取相鄰兩點的中央差分 m1 = (y2 - y0)/2、m2 = (y3 - y1)/2，
        // 再用 Hermite 基底展開成 c0 + c1 t + c2 t² + c3 t³。
        const float c0 = y1;
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + c0;
    }

    // 四點 3 階 Lagrange 插值：通過 y0..y3 四點的唯一三次多項式在 t 的值。
    // h_k(t) 是 Lagrange 基底（k = -1, 0, 1, 2 對應 y0..y3）。
    float readLagrange(int ch, float delaySamples) const noexcept {
        const float delay = std::clamp(delaySamples, 1.0f, static_cast<float>(maxDelay_));
        const int d = static_cast<int>(delay);
        const float t = delay - static_cast<float>(d);
        const float y0 = at(ch, d - 1);
        const float y1 = at(ch, d);
        const float y2 = at(ch, d + 1);
        const float y3 = at(ch, d + 2);
        const float hm1 = -t * (t - 1.0f) * (t - 2.0f) / 6.0f;
        const float h0 = (t + 1.0f) * (t - 1.0f) * (t - 2.0f) / 2.0f;
        const float h1 = -(t + 1.0f) * t * (t - 2.0f) / 2.0f;
        const float h2 = (t + 1.0f) * t * (t - 1.0f) / 6.0f;
        return hm1 * y0 + h0 * y1 + h1 * y2 + h2 * y3;
    }

    static int nextPowerOfTwo(int n) noexcept {
        int p = 1;
        while (p < n)
            p <<= 1;
        return p;
    }

private:
    // 把 (d, frac) 限制在 [minDelay, maxDelay_]：超出範圍時小數部分歸 0（和 clamp 一個 float 延遲的結果相同）。
    void clampSplit(int& d, float& frac, int minDelay) const noexcept {
        if (!(frac >= 0.0f)) // 也擋掉 NaN
            frac = 0.0f;
        if (frac >= 1.0f) { // 呼叫端算出 1.0（捨入）時進位到下一個整數
            d += 1;
            frac = 0.0f;
        }
        if (d < minDelay) {
            d = minDelay;
            frac = 0.0f;
        } else if (d >= maxDelay_) {
            d = maxDelay_;
            frac = 0.0f;
        }
    }

    float* channelData(int ch) noexcept {
        return buffer_.data() + static_cast<size_t>(ch) * static_cast<size_t>(size_);
    }
    const float* channelData(int ch) const noexcept {
        return buffer_.data() + static_cast<size_t>(ch) * static_cast<size_t>(size_);
    }

    // 取「d 次 push 以前」的 sample。最後一次 push 寫在 writeIndex - 1，
    // 所以目標位置是 writeIndex - 1 - d；先加 size_ 再 & mask 避免負數。
    float at(int ch, int d) const noexcept {
        assert(ch >= 0 && ch < numChannels_);
        assert(d >= 0 && d <= maxDelay_ + 2);
        const int w = writeIndex_[static_cast<size_t>(ch)];
        return channelData(ch)[(w - 1 - d + size_) & mask_];
    }

    std::vector<float> buffer_;
    std::vector<int> writeIndex_;
    int numChannels_ = 0;
    int maxDelay_ = 0;
    int size_ = 0;
    int mask_ = 0;
};

} // namespace dawlab::dsp
