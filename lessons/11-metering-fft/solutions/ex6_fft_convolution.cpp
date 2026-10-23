// Exercise 6 solution: fast convolution with overlap-add using the FFT (paving the way for the chapter 12 convolution reverb)
//
// Direct convolution: y[n] = Σ_{k=0}^{L-1} h[k] · x[n-k]
//   Each output sample takes L multiply-adds. With L = 48000 (a 1 second IR) that is 48000 × 48000 ≈ 2.3 billion
//   multiply-adds per second -- far too slow.
//
// Convolution theorem: convolution in the time domain = multiplication in the frequency domain
//   y = x * h   <=>   Y[k] = X[k] · H[k]
// But a product of DFTs corresponds to "circular convolution": whatever extends beyond length N wraps around to the
// beginning and is added there.
// The fix: zero-pad a segment of x (length B) and h (length L) to N >= B + L − 1, and then circular convolution equals
// linear convolution (nothing wraps around).
//
// Overlap-add: cut the input into blocks of length B and, for each block separately, do "zero-pad -> FFT -> multiply by H
// -> IFFT", which yields a result of length B + L − 1. The first B samples plus "the tail left by the previous block"
// are the output for this block, and the remaining L − 1 samples are stored (the tail) to be added onto later blocks.
//
//   block 0: [ x0 x0 x0 x0 | 0 0 0 0 0 0 ] ─FFT·H·IFFT─▶ [ y y y y | t t t t t t ]
//   block 1:               [ x1 x1 x1 x1 | 0 0 0 0 0 0 ] ─▶       [ y y y y | t t t t t t ]
//                                         ↑ the previous block's tail is added here (overlap-add)
//
// Cost per block: one forwardReal + one inverseReal (each O(N log N)) + N/2+1 complex multiplications.
// Per sample that is about O((N/B) log N), far faster than the O(L) of direct convolution when L is large.
//
// This single-partition version has a latency of 0 (one block is output for every block that comes in), but when the IR is
// long N is large and the work per block is very uneven (chapter 12's uniform partitioned convolution solves this by
// cutting the IR into many segments).
//
// Run: bin/Debug/l11_ex6_solution.exe (ctest label: solution)
// Output: dawlab_output/l11_ex6_convolved.wav (the pluck sequence through a synthetic 0.25 s "room" IR)

#include <dawlab/Common.h>
#include <dawlab/dsp/Fft.h>

#include <algorithm>
#include <chrono>
#include <complex>
#include <cstdio>
#include <vector>

using namespace dawlab;
using dawlab::dsp::Fft;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

class OverlapAddConvolver {
public:
    // Allocates (not for the audio thread): decides the FFT size and precomputes the IR spectrum
    void prepare(const std::vector<float>& impulseResponse, int maxBlockSize) {
        irLength_ = static_cast<int>(impulseResponse.size());
        blockSize_ = maxBlockSize;
        // N = the power of 2 that is >= B + L − 1
        int order = 1;
        while ((1 << order) < blockSize_ + irLength_ - 1)
            ++order;
        fft_.setOrder(order);
        const int n = fft_.getSize();
        const int bins = fft_.getNumRealBins();

        std::vector<float> padded(static_cast<size_t>(n), 0.0f);
        std::copy(impulseResponse.begin(), impulseResponse.end(), padded.begin());
        irSpectrum_.assign(static_cast<size_t>(bins), {0.0f, 0.0f});
        fft_.forwardReal(padded.data(), irSpectrum_.data()); // H[k]: computed only once

        timeBuffer_.assign(static_cast<size_t>(n), 0.0f);
        spectrum_.assign(static_cast<size_t>(bins), {0.0f, 0.0f});
        overlap_.assign(static_cast<size_t>(n), 0.0f);
    }

    void reset() noexcept { std::fill(overlap_.begin(), overlap_.end(), 0.0f); }

    int getFftSize() const noexcept { return fft_.getSize(); }

    // Audio thread: process one block in place (length <= maxBlockSize), no allocation
    void process(float* data, int numSamples) noexcept {
        const int n = fft_.getSize();
        // 1. Zero-pad: the first numSamples are the input, the rest are 0
        std::copy_n(data, numSamples, timeBuffer_.begin());
        std::fill(timeBuffer_.begin() + numSamples, timeBuffer_.end(), 0.0f);
        // 2. FFT -> multiply bin by bin, Y[k] = X[k]·H[k] -> IFFT (inverseReal includes 1/N)
        fft_.forwardReal(timeBuffer_.data(), spectrum_.data());
        for (size_t k = 0; k < spectrum_.size(); ++k)
            spectrum_[k] *= irSpectrum_[k];
        fft_.inverseReal(spectrum_.data(), timeBuffer_.data());
        // 3. Overlap-add: add this result onto the tail accumulated earlier
        for (int i = 0; i < n; ++i)
            overlap_[static_cast<size_t>(i)] += timeBuffer_[static_cast<size_t>(i)];
        // 4. The first numSamples samples are now complete -> output them
        std::copy_n(overlap_.begin(), numSamples, data);
        // 5. Shift the remaining tail forward and zero the vacated positions
        std::copy(overlap_.begin() + numSamples, overlap_.end(), overlap_.begin());
        std::fill(overlap_.end() - numSamples, overlap_.end(), 0.0f);
    }

private:
    int irLength_ = 0;
    int blockSize_ = 0;
    Fft fft_;
    std::vector<std::complex<float>> irSpectrum_, spectrum_;
    std::vector<float> timeBuffer_, overlap_;
};

// Direct convolution (the ground truth): y[n] = Σ h[k] x[n−k], output length equal to the input length (tail truncated)
std::vector<float> directConvolve(const float* x, int n, const std::vector<float>& h) {
    std::vector<float> y(static_cast<size_t>(n), 0.0f);
    const int l = static_cast<int>(h.size());
    for (int i = 0; i < n; ++i) {
        double acc = 0.0;
        const int kMax = std::min(l - 1, i);
        for (int k = 0; k <= kMax; ++k)
            acc += static_cast<double>(h[static_cast<size_t>(k)]) * x[i - k];
        y[static_cast<size_t>(i)] = static_cast<float>(acc);
    }
    return y;
}

// Synthetic IR: exponentially decaying white noise (like a very small room), energy normalized to 1
std::vector<float> makeNoiseIr(int length, double rt60Seconds, uint32_t seed) {
    std::vector<float> h(static_cast<size_t>(length));
    NoiseGenerator noise(seed);
    // RT60: the time to decay by 60 dB -> the amplitude is multiplied by 10^(−3/rt60) every second
    const double decayPerSample = std::pow(10.0, -3.0 / (rt60Seconds * kSampleRate));
    double g = 1.0, energy = 0.0;
    for (float& v : h) {
        v = static_cast<float>(noise.next() * g);
        g *= decayPerSample;
        energy += static_cast<double>(v) * v;
    }
    const auto norm = static_cast<float>(1.0 / std::sqrt(energy));
    for (float& v : h)
        v *= norm;
    return h;
}

float maxAbsDiff(const std::vector<float>& a, const float* b, int n) {
    float e = 0.0f;
    for (int i = 0; i < n; ++i)
        e = std::max(e, std::abs(a[static_cast<size_t>(i)] - b[i]));
    return e;
}

} // namespace

int main() {
    using Clock = std::chrono::steady_clock;

    // ---- 1. Correctness: a small IR, various block lengths (including a last block shorter than maxBlockSize) ----
    {
        const std::vector<float> h = {0.5f, -0.25f, 0.125f, 0.0f, 0.1f};
        AudioBuffer x = makeWhiteNoise(kSampleRate, 0.05, 0.5f, 1, 3);
        const int n = x.getNumSamples();
        const std::vector<float> reference = directConvolve(x.getReadPointer(0), n, h);
        OverlapAddConvolver conv;
        conv.prepare(h, 64);
        std::vector<float> y(x.getReadPointer(0), x.getReadPointer(0) + n);
        // Deliberately use blocks of varying length: 37, 64, 1, 64, ... (a host's callback length may vary anyway)
        const int pattern[] = {37, 64, 1, 64, 50};
        int pos = 0, p = 0;
        while (pos < n) {
            const int len = std::min(pattern[p++ % 5], n - pos);
            conv.process(y.data() + pos, len);
            pos += len;
        }
        const float err = maxAbsDiff(reference, y.data(), n);
        std::printf("5-tap IR, varying block lengths: FFT size = %d, max error vs direct convolution = %.3e\n", conv.getFftSize(), err);
        DAWLAB_CHECK(err < 1e-5f);
    }

    // ---- 2. The response to an impulse = the IR itself ---------------------------------------------
    {
        const std::vector<float> h = makeNoiseIr(1000, 0.05, 11);
        OverlapAddConvolver conv;
        conv.prepare(h, kBlockSize);
        AudioBuffer imp = makeImpulse(4096, 1, 0, 1.0f);
        for (int start = 0; start < imp.getNumSamples(); start += kBlockSize)
            conv.process(imp.getWritePointer(0) + start, std::min(kBlockSize, imp.getNumSamples() - start));
        float err = 0.0f;
        for (int i = 0; i < 4096; ++i) {
            const float expected = i < 1000 ? h[static_cast<size_t>(i)] : 0.0f;
            err = std::max(err, std::abs(imp.getSample(0, i) - expected));
        }
        std::printf("impulse -> output equals the IR: max error = %.3e\n", err);
        DAWLAB_CHECK(err < 1e-5f);
    }

    // ---- 3. Real material + timing: a 0.25 s IR (12000 taps) -------------------------------------------
    {
        const int irLen = 12000;
        const std::vector<float> h = makeNoiseIr(irLen, 0.2, 7);
        AudioBuffer dry = makePluckSequence(kSampleRate, 2.0, 0.4f, 1);
        const int n = dry.getNumSamples();

        auto t0 = Clock::now();
        const std::vector<float> reference = directConvolve(dry.getReadPointer(0), n, h);
        const double directMs = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();

        // Run the same IR with two block lengths:
        //   B = 512  : N = 16384, a 16384-point FFT + IFFT every 512 samples -> most of the work is wasted on the padded zeros
        //   B = 4096 : N is still 16384, but it runs only once every 4096 samples -> 8 times cheaper per sample
        // Conclusion: single-partition overlap-add pays off only when B and L are of the same order of magnitude; but the B of an audio
        // callback is small, and a large B means a large latency. Chapter 12's uniform partitioned convolution cuts the IR into many
        // short segments of length B, which solves both problems at once.
        AudioBuffer wet = dry;
        double fftMs[2] = {0.0, 0.0};
        float err[2] = {0.0f, 0.0f};
        int fftSize[2] = {0, 0};
        const int blockSizes[2] = {kBlockSize, 4096};
        for (int v = 0; v < 2; ++v) {
            OverlapAddConvolver conv;
            conv.prepare(h, blockSizes[v]);
            fftSize[v] = conv.getFftSize();
            wet = dry;
            t0 = Clock::now();
            for (int start = 0; start < n; start += blockSizes[v])
                conv.process(wet.getWritePointer(0) + start, std::min(blockSizes[v], n - start));
            fftMs[v] = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
            err[v] = maxAbsDiff(reference, wet.getReadPointer(0), n);
        }

        const float refPeak = peakAbs(reference.data(), n);
        std::printf("IR %d taps, %d samples (in Debug only the relative speed matters):\n", irLen, n);
        std::printf("  direct convolution              %8.1f ms\n", directMs);
        for (int v = 0; v < 2; ++v)
            std::printf("  overlap-add B = %4d, N = %5d   %8.1f ms (%.2f times the time of direct convolution), max error %.2e (%.0f dB)\n",
                        blockSizes[v], fftSize[v], fftMs[v], fftMs[v] / directMs, err[v],
                        gainToDb(err[v] / refPeak));
        DAWLAB_CHECK(err[0] / refPeak < 1e-4f); // float arithmetic: the error is more than 80 dB below the signal
        DAWLAB_CHECK(err[1] / refPeak < 1e-4f);
        DAWLAB_CHECK(fftMs[1] < directMs * 0.5); // with a large block it is clearly faster than direct convolution
        DAWLAB_CHECK(fftMs[1] < fftMs[0]);       // for the same N, the larger the block the cheaper each sample

        // Mix dry and wet for output; it sounds like plucking strings in a small room
        AudioBuffer out(2, n);
        for (int i = 0; i < n; ++i) {
            const float v = 0.7f * dry.getSample(0, i) + 0.5f * wet.getSample(0, i);
            out.setSample(0, i, v);
            out.setSample(1, i, v);
        }
        writeWav(outputPath("l11_ex6_convolved.wav"), out, kSampleRate);
        std::printf("-> dawlab_output/l11_ex6_convolved.wav\n");
    }

    return dawlab::test::summary("l11_ex6");
}
