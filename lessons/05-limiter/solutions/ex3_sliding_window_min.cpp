// 練習 3 解答：O(1) amortized 的 sliding window minimum（monotonic deque）vs 暴力法
//
// 暴力法：每個 sample 回頭掃 W 個值 → O(W) / sample。lookahead 5 ms @ 48 kHz 是 W = 241，
// 每個 sample 要做 241 次比較；10 ms @ 96 kHz 就是 961 次——這在 audio thread 上很浪費。
// Monotonic deque：每個值最多 push 一次、pop 一次，所以 N 個 sample 總共 O(N)。
//
// 執行：l05_ex3_solution → 比對兩種作法在隨機資料上的結果完全相同，並印出耗時。

#include <dawlab/Common.h>
#include <dawlab/dsp/Limiter.h> // 只拿來對照 dawlab::dsp::SlidingWindowMin，自己的版本寫在下面

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

// ---- 暴力法：環形緩衝存最近 W 個值，每次掃一遍 ----
class BruteForceWindowMin {
public:
    void prepare(int window) {
        window_ = window;
        ring_.assign(static_cast<size_t>(window), 0.0f);
        reset();
    }
    void reset() {
        index_ = 0;
        filled_ = 0;
    }
    float push(float v) {
        ring_[static_cast<size_t>(index_)] = v;
        index_ = (index_ + 1) % window_;
        filled_ = std::min(filled_ + 1, window_);
        float m = v;
        for (int i = 0; i < filled_; ++i) // 還沒填滿時只看已經有的值
            m = std::min(m, ring_[static_cast<size_t>(i)]);
        return m;
    }

private:
    int window_ = 1, index_ = 0, filled_ = 0;
    std::vector<float> ring_;
};

// ---- Monotonic deque：只保留「還有機會成為最小值」的 (value, index) ----
//
//   視窗 = 最近 W 個 index：(n - W, n]
//   不變量：deque 由前到後 index 遞增、value 嚴格遞增；最前面就是視窗最小值。
//
//   push(v)：
//     1. 尾端所有 value ≥ v 的元素丟掉（它們比 v 早過期又不比 v 小 → 永遠不會再是最小值）
//     2. 前端 index ≤ n - W 的元素丟掉（已離開視窗）
//     3. (v, n) 放到尾端
//     4. 回傳前端的 value
//
class MonotonicWindowMin {
public:
    void prepare(int window) {
        window_ = window;
        values_.assign(static_cast<size_t>(window), 0.0f);
        indices_.assign(static_cast<size_t>(window), 0);
        reset();
    }
    void reset() {
        head_ = 0;
        count_ = 0;
        n_ = 0;
    }
    float push(float v) {
        const int64_t n = n_++;
        while (count_ > 0 && values_[static_cast<size_t>(back())] >= v)
            --count_;
        while (count_ > 0 && indices_[static_cast<size_t>(head_)] <= n - window_) {
            head_ = (head_ + 1) % window_;
            --count_;
        }
        const int slot = (head_ + count_) % window_; // 此時 count_ ≤ window_ - 1，放得下
        values_[static_cast<size_t>(slot)] = v;
        indices_[static_cast<size_t>(slot)] = n;
        ++count_;
        return values_[static_cast<size_t>(head_)];
    }

private:
    int back() const { return (head_ + count_ - 1) % window_; }
    int window_ = 1, head_ = 0, count_ = 0;
    int64_t n_ = 0;
    std::vector<float> values_;
    std::vector<int64_t> indices_;
};

template <typename Algo>
double timeIt(Algo& algo, const std::vector<float>& data, float& checksum) {
    const auto t0 = std::chrono::steady_clock::now();
    float sum = 0.0f;
    for (float v : data)
        sum += algo.push(v);
    checksum = sum;
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

} // namespace

int main() {
    // ---- 正確性：各種視窗長度、隨機資料（含很多重複值，測試 >= 的處理）----
    dawlab::NoiseGenerator rng(7);
    const int n = 20000;
    std::vector<float> data(static_cast<size_t>(n));
    for (float& v : data)
        v = std::round(rng.next() * 8.0f) / 8.0f; // 量化成少數幾個值，讓相等的情況常常發生

    for (int window : {1, 2, 3, 7, 64, 241, 961, 5000, n}) {
        BruteForceWindowMin brute;
        MonotonicWindowMin mono;
        dawlab::dsp::SlidingWindowMin lib;
        brute.prepare(window);
        mono.prepare(window);
        lib.prepare(window);
        int mismatches = 0;
        for (float v : data) {
            const float a = brute.push(v);
            const float b = mono.push(v);
            const float c = lib.push(v);
            if (a != b || a != c)
                ++mismatches;
        }
        std::printf("window %5d：mismatches = %d\n", window, mismatches);
        DAWLAB_CHECK(mismatches == 0);
    }

    // 階梯訊號：先遞減再遞增，deque 內容會從「只剩一個」到「塞滿」
    {
        const int window = 50;
        BruteForceWindowMin brute;
        MonotonicWindowMin mono;
        brute.prepare(window);
        mono.prepare(window);
        int mismatches = 0;
        for (int i = 0; i < 1000; ++i) {
            const float v = static_cast<float>((i / 100) % 2 == 0 ? 100 - i % 100 : i % 100);
            if (brute.push(v) != mono.push(v))
                ++mismatches;
        }
        DAWLAB_CHECK(mismatches == 0);
    }

    // ---- 效能：1 秒 @ 48 kHz 的 noise，視窗 241（5 ms）與 961（10 ms @ 96 kHz）----
    std::vector<float> noise(48000 * 10);
    for (float& v : noise)
        v = rng.next();
    std::printf("\n%8s %14s %14s %10s\n", "window", "brute (ms)", "deque (ms)", "speedup");
    for (int window : {241, 961}) {
        BruteForceWindowMin brute;
        MonotonicWindowMin mono;
        brute.prepare(window);
        mono.prepare(window);
        float c1 = 0.0f, c2 = 0.0f;
        const double tb = timeIt(brute, noise, c1);
        const double tm = timeIt(mono, noise, c2);
        std::printf("%8d %14.2f %14.2f %9.1fx\n", window, tb, tm, tb / std::max(tm, 1.0e-6));
        DAWLAB_CHECK(c1 == c2); // 同樣的輸入 → 同樣的輸出總和
    }
    std::printf("（Debug 建置的絕對時間沒有意義，看倍率即可；Release 下差距會更大）\n");

    return dawlab::test::summary("l05_ex3");
}
