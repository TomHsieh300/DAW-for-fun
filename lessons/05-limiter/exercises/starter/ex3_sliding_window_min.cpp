// Exercise 3 starter: O(1) amortized sliding window minimum (monotonic deque)
//
// Goal: complete MonotonicWindowMin::push() so that it returns the same values as the brute-force
// BruteForceWindowMin on every input, while doing only O(1) work per sample on average.
//
// Run: l05_ex3_starter -> it prints FAIL until you finish (expected).

#include <dawlab/Common.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

// ---- Brute force (already done): a circular buffer holds the last W values and each push scans them all -> O(W) per sample ----
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

// ---- Monotonic deque (complete push) ----
//
//   Window = the most recent W indices: (n - W, n]
//   Invariant: from front to back the deque has increasing index and strictly increasing value; the front is the window minimum.
//   The deque is implemented as a circular array of length W (head_ = front position, count_ = number of elements).
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
        // TODO 1: drop from the back every element with value >= v (hint: back() gives the back slot; dropping = --count_)
        // TODO 2: drop from the front every element with index <= n - window_ (hint: head_ = (head_ + 1) % window_; --count_)
        // TODO 3: put (v, n) into the back slot = (head_ + count_) % window_, then ++count_
        // TODO 4: return the value at the front
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
        v = std::round(rng.next() * 8.0f) / 8.0f; // lots of repeated values: tests the handling of >=

    for (int window : {1, 2, 3, 7, 64, 241, 961, 5000, n}) {
        BruteForceWindowMin brute;
        MonotonicWindowMin mono;
        brute.prepare(window);
        mono.prepare(window);
        int mismatches = 0;
        for (float v : data)
            if (brute.push(v) != mono.push(v))
                ++mismatches;
        std::printf("window %5d: mismatches = %d\n", window, mismatches);
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
