#pragma once
// 第 11 章：radix-2 Cooley-Tukey FFT（自己實作，不依賴外部函式庫）與 window function。
//
// 設計重點：
// - Fft 在建構 / setOrder() 時把 twiddle factor（旋轉因子）與 bit-reversal 表先算好並 allocate，
//   之後 forward() / inverse() 完全不配置記憶體，可在 audio thread 上使用。
// - forward() / inverse() 都是 in-place：輸入陣列直接被覆寫成輸出。inverse() 含 1/N 正規化，
//   所以 inverse(forward(x)) == x。
// - forwardReal() / inverseReal() 處理實數訊號：實數訊號的頻譜有共軛對稱性
//   X[N-k] = conj(X[k])，所以只要保留 N/2+1 個 bin。
// - makeWindow() 產生對稱（symmetric）window：w[n] 以 n = (N-1)/2 為中心對稱。
//
// 數學：DFT 定義
//     X[k] = Σ_{n=0}^{N-1} x[n] · e^{-j 2π k n / N},   k = 0..N-1
// 把 n 拆成偶數與奇數（Cooley-Tukey decimation-in-time）：
//     X[k]       = E[k] + W_N^k · O[k]
//     X[k + N/2] = E[k] − W_N^k · O[k],   W_N = e^{-j 2π / N}
// E、O 分別是偶數、奇數 sample 的 N/2 點 DFT，遞迴下去就是 O(N log N)。
// 這裡用迭代寫法：先做 bit-reversal 排列，再由小到大做 log2(N) 層 butterfly。

#include <dawlab/DspMath.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <complex>
#include <cstddef>
#include <utility>
#include <vector>

namespace dawlab::dsp {

enum class WindowType { Rectangular, Hann, Hamming, BlackmanHarris };

inline const char* windowTypeName(WindowType type) noexcept {
    switch (type) {
    case WindowType::Rectangular: return "Rectangular";
    case WindowType::Hann: return "Hann";
    case WindowType::Hamming: return "Hamming";
    case WindowType::BlackmanHarris: return "Blackman-Harris";
    }
    return "?";
}

// 教學 / 驗證用的 naive DFT：直接照定義算，O(N^2)。用 double 累加，當作 FFT 的「標準答案」。
// inverse = true 時算 IDFT（含 1/N）。
inline void naiveDft(const std::complex<float>* input, std::complex<float>* output, int n,
                     bool inverse = false) {
    const double sign = inverse ? 1.0 : -1.0;
    for (int k = 0; k < n; ++k) {
        double re = 0.0, im = 0.0;
        for (int i = 0; i < n; ++i) {
            // e^{±j 2π k i / N}：把「第 k 個頻率的正弦」和訊號做相關（correlation）
            const double angle = sign * kTwoPi * static_cast<double>(k) * static_cast<double>(i) / n;
            const double c = std::cos(angle), s = std::sin(angle);
            const double xr = input[i].real(), xi = input[i].imag();
            re += xr * c - xi * s;
            im += xr * s + xi * c;
        }
        if (inverse) {
            re /= n;
            im /= n;
        }
        output[k] = std::complex<float>(static_cast<float>(re), static_cast<float>(im));
    }
}

class Fft {
public:
    Fft() = default;
    explicit Fft(int order) { setOrder(order); }

    // size = 2^order。會 allocate（twiddle、bit-reversal 表、實數轉換用的 scratch），只能在 prepare 階段呼叫。
    void setOrder(int order) {
        assert(order >= 0 && order <= 24);
        order_ = order;
        size_ = 1 << order;
        twiddles_.resize(static_cast<size_t>(size_ / 2 + 1)); // +1 讓 size_ = 1 時也有一個元素
        for (int k = 0; k < size_ / 2; ++k) {
            // W_N^k = e^{-j 2π k / N}。用 double 算完再轉 float，避免大 N 時角度誤差累積
            const double angle = -kTwoPi * static_cast<double>(k) / size_;
            twiddles_[static_cast<size_t>(k)] =
                std::complex<float>(static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle)));
        }
        bitReverse_.resize(static_cast<size_t>(size_));
        for (int i = 0; i < size_; ++i) {
            int reversed = 0;
            for (int bit = 0; bit < order_; ++bit)
                if (i & (1 << bit))
                    reversed |= 1 << (order_ - 1 - bit);
            bitReverse_[static_cast<size_t>(i)] = reversed;
        }
        scratch_.assign(static_cast<size_t>(size_), std::complex<float>(0.0f, 0.0f));
    }

    int getOrder() const noexcept { return order_; }
    int getSize() const noexcept { return size_; }
    int getNumRealBins() const noexcept { return size_ / 2 + 1; }

    // in-place 複數 FFT：data 長度必須是 getSize()
    void forward(std::complex<float>* data) const noexcept { transform(data, false); }

    // in-place 複數 IFFT，含 1/N：inverse(forward(x)) == x
    void inverse(std::complex<float>* data) const noexcept {
        transform(data, true);
        const float scale = 1.0f / static_cast<float>(size_);
        for (int i = 0; i < size_; ++i)
            data[i] *= scale;
    }

    // 實數輸入（長度 N）→ 輸出 N/2+1 個 bin（bin 0 = DC，bin N/2 = Nyquist）。
    // 注意：內部使用 scratch buffer，同一個 Fft 物件不可同時被兩個 thread 使用。
    void forwardReal(const float* input, std::complex<float>* output) const noexcept {
        for (int i = 0; i < size_; ++i)
            scratch_[static_cast<size_t>(i)] = std::complex<float>(input[i], 0.0f);
        transform(scratch_.data(), false);
        const int bins = getNumRealBins();
        for (int k = 0; k < bins; ++k)
            output[k] = scratch_[static_cast<size_t>(k)];
    }

    // N/2+1 個 bin → 實數輸出（長度 N），含 1/N。利用共軛對稱性把另一半補回來再做 IFFT。
    void inverseReal(const std::complex<float>* input, float* output) const noexcept {
        const int half = size_ / 2;
        for (int k = 0; k <= half; ++k)
            scratch_[static_cast<size_t>(k)] = input[k];
        for (int k = 1; k < half; ++k)
            scratch_[static_cast<size_t>(size_ - k)] = std::conj(input[k]); // X[N-k] = conj(X[k])
        transform(scratch_.data(), true);
        const float scale = 1.0f / static_cast<float>(size_);
        for (int i = 0; i < size_; ++i)
            output[i] = scratch_[static_cast<size_t>(i)].real() * scale;
    }

    // 對稱 window，長度 size。w[n] 對 n = (size-1)/2 對稱。
    static void makeWindow(std::vector<float>& window, int size, WindowType type) {
        window.assign(static_cast<size_t>(std::max(0, size)), 1.0f);
        if (size <= 1)
            return;
        const double denom = static_cast<double>(size - 1);
        for (int n = 0; n < size; ++n) {
            const double t = kTwoPi * n / denom; // 0 .. 2π
            double w = 1.0;
            switch (type) {
            case WindowType::Rectangular: w = 1.0; break;
            case WindowType::Hann: w = 0.5 - 0.5 * std::cos(t); break;
            case WindowType::Hamming: w = 0.54 - 0.46 * std::cos(t); break;
            case WindowType::BlackmanHarris: // 4-term，side lobe ≈ -92 dB
                w = 0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2.0 * t) - 0.01168 * std::cos(3.0 * t);
                break;
            }
            window[static_cast<size_t>(n)] = static_cast<float>(w);
        }
    }

    // coherent gain = Σ w[n] / N：window 對「正好落在 bin 中心的正弦」造成的振幅衰減，
    // 把 |X[k]| 除以 (N · coherentGain / 2) 就能還原正弦的振幅。
    static double coherentGain(const float* window, int size) noexcept {
        if (size <= 0)
            return 1.0;
        double sum = 0.0;
        for (int i = 0; i < size; ++i)
            sum += window[i];
        return sum / size;
    }

    // 理論值（N → ∞）：Rectangular 1.0、Hann 0.5、Hamming 0.54、Blackman-Harris 0.35875
    static double coherentGain(WindowType type) noexcept {
        switch (type) {
        case WindowType::Rectangular: return 1.0;
        case WindowType::Hann: return 0.5;
        case WindowType::Hamming: return 0.54;
        case WindowType::BlackmanHarris: return 0.35875;
        }
        return 1.0;
    }

private:
    void transform(std::complex<float>* data, bool inverseDirection) const noexcept {
        const int n = size_;
        if (n <= 1)
            return;
        // 1. bit-reversal 排列：把輸入排成「遞迴拆分到最底層」的順序
        for (int i = 0; i < n; ++i) {
            const int j = bitReverse_[static_cast<size_t>(i)];
            if (j > i)
                std::swap(data[i], data[j]);
        }
        // 2. 由小到大做 butterfly：len = 2, 4, 8, ..., N
        for (int len = 2; len <= n; len <<= 1) {
            const int half = len / 2;
            const int stride = n / len; // 這一層需要的 twiddle 是 W_len^k = W_N^{k·stride}
            for (int start = 0; start < n; start += len) {
                for (int k = 0; k < half; ++k) {
                    std::complex<float> w = twiddles_[static_cast<size_t>(k * stride)];
                    if (inverseDirection)
                        w = std::conj(w); // IDFT 只差旋轉方向相反（與最後的 1/N）
                    std::complex<float>& a = data[start + k];
                    std::complex<float>& b = data[start + k + half];
                    const std::complex<float> t = w * b;
                    b = a - t; // X[k + len/2] = E[k] − W·O[k]
                    a = a + t; // X[k]         = E[k] + W·O[k]
                }
            }
        }
    }

    int order_ = 0;
    int size_ = 0;
    std::vector<std::complex<float>> twiddles_;
    std::vector<int> bitReverse_;
    mutable std::vector<std::complex<float>> scratch_;
};

} // namespace dawlab::dsp
