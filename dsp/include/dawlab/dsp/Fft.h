#pragma once
// Chapter 11: radix-2 Cooley-Tukey FFT (hand-written, no external library) and window functions.
//
// Design notes:
// - Fft computes the twiddle factors and the bit-reversal table up front and allocates them in the
//   constructor / setOrder(); after that, forward() / inverse() never allocate memory, so they are
//   safe to use on the audio thread.
// - forward() / inverse() are both in-place: the input array is overwritten with the output.
//   inverse() includes the 1/N normalization, so inverse(forward(x)) == x.
// - forwardReal() / inverseReal() handle real-valued signals: the spectrum of a real signal is
//   conjugate-symmetric, X[N-k] = conj(X[k]), so only N/2+1 bins need to be kept.
// - makeWindow() generates symmetric windows: w[n] is symmetric about n = (N-1)/2.
//
// Math: the DFT definition
//     X[k] = Σ_{n=0}^{N-1} x[n] · e^{-j 2π k n / N},   k = 0..N-1
// Split n into even and odd indices (Cooley-Tukey decimation in time):
//     X[k]       = E[k] + W_N^k · O[k]
//     X[k + N/2] = E[k] − W_N^k · O[k],   W_N = e^{-j 2π / N}
// E and O are the N/2-point DFTs of the even and odd samples; recursing gives O(N log N).
// This implementation is iterative: do a bit-reversal permutation first, then run log2(N) layers of
// butterflies from the smallest size up.

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

// Naive DFT for teaching / verification: computed straight from the definition, O(N^2). It accumulates
// in double and serves as the "ground truth" for the FFT. With inverse = true it computes the IDFT
// (including 1/N).
inline void naiveDft(const std::complex<float>* input, std::complex<float>* output, int n,
                     bool inverse = false) {
    const double sign = inverse ? 1.0 : -1.0;
    for (int k = 0; k < n; ++k) {
        double re = 0.0, im = 0.0;
        for (int i = 0; i < n; ++i) {
            // e^{±j 2π k i / N}: correlate the signal with the sinusoid at the k-th frequency
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

    // size = 2^order. Allocates (twiddles, bit-reversal table, scratch for the real transforms), so call
    // it only during the prepare phase.
    void setOrder(int order) {
        assert(order >= 0 && order <= 24);
        order_ = order;
        size_ = 1 << order;
        twiddles_.resize(static_cast<size_t>(size_ / 2 + 1)); // +1 so there is still one element when size_ = 1
        for (int k = 0; k < size_ / 2; ++k) {
            // W_N^k = e^{-j 2π k / N}. Computed in double and then converted to float so that angle error
            // does not accumulate for large N
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

    // In-place complex FFT: data must have getSize() elements
    void forward(std::complex<float>* data) const noexcept { transform(data, false); }

    // In-place complex IFFT including 1/N: inverse(forward(x)) == x
    void inverse(std::complex<float>* data) const noexcept {
        transform(data, true);
        const float scale = 1.0f / static_cast<float>(size_);
        for (int i = 0; i < size_; ++i)
            data[i] *= scale;
    }

    // Real input (length N) -> N/2+1 output bins (bin 0 = DC, bin N/2 = Nyquist).
    // Note: this uses an internal scratch buffer, so one Fft object must not be used by two threads at
    // the same time.
    void forwardReal(const float* input, std::complex<float>* output) const noexcept {
        for (int i = 0; i < size_; ++i)
            scratch_[static_cast<size_t>(i)] = std::complex<float>(input[i], 0.0f);
        transform(scratch_.data(), false);
        const int bins = getNumRealBins();
        for (int k = 0; k < bins; ++k)
            output[k] = scratch_[static_cast<size_t>(k)];
    }

    // N/2+1 bins -> real output (length N), including 1/N. Uses conjugate symmetry to fill in the other
    // half before running the IFFT.
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

    // Symmetric window of length size. w[n] is symmetric about n = (size-1)/2.
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
            case WindowType::BlackmanHarris: // 4-term, side lobes ≈ -92 dB
                w = 0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2.0 * t) - 0.01168 * std::cos(3.0 * t);
                break;
            }
            window[static_cast<size_t>(n)] = static_cast<float>(w);
        }
    }

    // coherent gain = Σ w[n] / N: the amplitude attenuation the window causes for a sinusoid that falls
    // exactly on a bin center. Dividing |X[k]| by (N · coherentGain / 2) recovers the sinusoid's amplitude.
    static double coherentGain(const float* window, int size) noexcept {
        if (size <= 0)
            return 1.0;
        double sum = 0.0;
        for (int i = 0; i < size; ++i)
            sum += window[i];
        return sum / size;
    }

    // Theoretical values (N -> ∞): Rectangular 1.0, Hann 0.5, Hamming 0.54, Blackman-Harris 0.35875
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
        // 1. Bit-reversal permutation: reorder the input into the order produced by recursing all the way down
        for (int i = 0; i < n; ++i) {
            const int j = bitReverse_[static_cast<size_t>(i)];
            if (j > i)
                std::swap(data[i], data[j]);
        }
        // 2. Butterflies from small to large: len = 2, 4, 8, ..., N
        for (int len = 2; len <= n; len <<= 1) {
            const int half = len / 2;
            const int stride = n / len; // the twiddle this layer needs is W_len^k = W_N^{k·stride}
            for (int start = 0; start < n; start += len) {
                for (int k = 0; k < half; ++k) {
                    std::complex<float> w = twiddles_[static_cast<size_t>(k * stride)];
                    if (inverseDirection)
                        w = std::conj(w); // the IDFT differs only in the opposite rotation direction (and the final 1/N)
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
