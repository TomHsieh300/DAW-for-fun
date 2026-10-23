// Exercise 3 solution: O(1) amortized sliding window minimum (monotonic deque) vs brute force
//
// Brute force: every sample scans back over W values -> O(W) per sample. A 5 ms lookahead @ 48 kHz is W = 241,
// so each sample costs 241 comparisons; 10 ms @ 96 kHz is 961 -- a waste on the audio thread.
// Monotonic deque: each value is pushed at most once and popped at most once, so N samples cost O(N) in total.
//
// Run: l05_ex3_solution -> checks that both approaches give identical results on random data, and prints the timings.

#include <dawlab/Common.h>
#include <dawlab/dsp/Limiter.h> // only used to compare against dawlab::dsp::SlidingWindowMin; our own version is written below

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

// ---- Brute force: a circular buffer holds the last W values and each push scans them all ----
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
        for (int i = 0; i < filled_; ++i) // until it is full, look only at the values we already have
            m = std::min(m, ring_[static_cast<size_t>(i)]);
        return m;
    }

private:
    int window_ = 1, index_ = 0, filled_ = 0;
    std::vector<float> ring_;
};

// ---- Monotonic deque: keep only the (value, index) pairs that still have a chance to become the minimum ----
//
//   Window = the most recent W indices: (n - W, n]
//   Invariant: from front to back the deque has increasing index and strictly increasing value; the front is the window minimum.
//
//   push(v):
//     1. Drop every element at the back with value >= v (it expires earlier than v and is no smaller -> it can never be the minimum again)
//     2. Drop every element at the front with index <= n - W (it has left the window)
//     3. Put (v, n) at the back
//     4. Return the value at the front
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
        const int slot = (head_ + count_) % window_; // count_ <= window_ - 1 at this point, so there is room
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
    // ---- Correctness: various window lengths, random data (with many repeated values, to test the handling of >=) ----
    dawlab::NoiseGenerator rng(7);
    const int n = 20000;
    std::vector<float> data(static_cast<size_t>(n));
    for (float& v : data)
        v = std::round(rng.next() * 8.0f) / 8.0f; // quantized to just a few values so ties happen often

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
        std::printf("window %5d: mismatches = %d\n", window, mismatches);
        DAWLAB_CHECK(mismatches == 0);
    }

    // Staircase signal: first decreasing then increasing, so the deque goes from "just one element" to "completely full"
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

    // ---- Performance: 10 seconds @ 48 kHz of noise, windows of 241 (5 ms) and 961 (10 ms @ 96 kHz) ----
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
        DAWLAB_CHECK(c1 == c2); // same input -> same output sum
    }
    std::printf("(Absolute times in a Debug build are meaningless, look only at the ratio; the gap is larger in Release)\n");

    return dawlab::test::summary("l05_ex3");
}
