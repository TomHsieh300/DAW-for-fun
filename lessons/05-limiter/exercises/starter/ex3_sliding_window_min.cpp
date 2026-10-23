// 練習 3 起始碼：O(1) amortized 的 sliding window minimum（monotonic deque）
//
// 目標：完成 MonotonicWindowMin::push()，讓它和暴力法 BruteForceWindowMin 在所有輸入上回傳一樣的值，
// 但每個 sample 平均只做 O(1) 的工作。
//
// 執行：l05_ex3_starter → 完成前會印出 FAIL（預期）。

#include <dawlab/Common.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

// ---- 暴力法（已完成）：環形緩衝存最近 W 個值，每次掃一遍 → O(W) / sample ----
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
        for (int i = 0; i < filled_; ++i)
            m = std::min(m, ring_[static_cast<size_t>(i)]);
        return m;
    }

private:
    int window_ = 1, index_ = 0, filled_ = 0;
    std::vector<float> ring_;
};

// ---- Monotonic deque（請完成 push）----
//
//   視窗 = 最近 W 個 index：(n - W, n]
//   不變量：deque 由前到後 index 遞增、value 嚴格遞增；最前面就是視窗最小值。
//   deque 用長度 W 的環形陣列實作（head_ = 前端位置，count_ = 元素數）。
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
        // TODO 1：從尾端丟掉所有 value >= v 的元素（提示：back() 給你尾端的 slot；丟掉 = --count_）
        // TODO 2：從前端丟掉所有 index <= n - window_ 的元素（提示：head_ = (head_ + 1) % window_; --count_）
        // TODO 3：把 (v, n) 放到尾端 slot = (head_ + count_) % window_，++count_
        // TODO 4：回傳前端的 value
        (void)v;
        (void)n;
        return 0.0f;
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
    dawlab::NoiseGenerator rng(7);
    const int n = 20000;
    std::vector<float> data(static_cast<size_t>(n));
    for (float& v : data)
        v = std::round(rng.next() * 8.0f) / 8.0f; // 很多重複值：測試 >= 的處理

    for (int window : {1, 2, 3, 7, 64, 241, 961, 5000, n}) {
        BruteForceWindowMin brute;
        MonotonicWindowMin mono;
        brute.prepare(window);
        mono.prepare(window);
        int mismatches = 0;
        for (float v : data)
            if (brute.push(v) != mono.push(v))
                ++mismatches;
        std::printf("window %5d：mismatches = %d\n", window, mismatches);
        DAWLAB_CHECK(mismatches == 0);
    }

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
        DAWLAB_CHECK(c1 == c2);
    }

    return dawlab::test::summary("l05_ex3");
}
