#pragma once
// Chapter 11: SpectrumAnalyzer -- the audio thread pushes samples, the UI thread computes the spectrum,
// and a lock-free FIFO sits in between.
//
// Architecture (single-producer / single-consumer):
//
//   audio thread                      ring buffer (power of 2, = 4N)                UI thread
//   pushSamples() ──store samples──▶ [ . . . . x x x x x x x x . . . ] ──copy latest N──▶ computeSpectrum()
//     1. writeEnd_ = pos + n (announce "how far I will write") + release fence   1. writePos_ (acquire) -> decide start
//     2. write the samples (relaxed)                                             2. copy [start, start + N)
//     3. writePos_ = pos + n (release, announce "done writing")                  3. acquire fence -> read writeEnd_
//
// - The producer only writes the ring, writeEnd_ and writePos_; the consumer only reads them: no mutex is needed.
// - The consumer always takes "the latest N samples" instead of consuming in order, so a slow UI does not fall
//   further and further behind; it simply skips the intermediate frames.
// - Overwrite detection: after copying, the consumer looks at writeEnd_. If writeEnd_ - start > capacity, the
//   producer (possibly in a push that has not finished yet) has started writing far enough to wrap around onto
//   [start, start + N), and this frame is discarded. Looking at writePos_ alone is not enough: a push in
//   progress may already have overwritten part of the region while writePos_ still holds the old value.
//   When can this happen? When one push of length n plus N exceeds the capacity 4N (e.g. N = 64, block 512),
//   or when the UI thread is suspended by the OS for too long in the middle of copying.
// - The ring elements are std::atomic<float> (relaxed): in the C++ memory model two threads touching the same
//   memory at once must use atomics to avoid a data race; on x86 / ARM a relaxed float access is just an
//   ordinary load / store.
//
// Post-processing in computeSpectrum():
//   1. Multiply by the window (Hann by default)
//   2. forwardReal -> N/2+1 bins
//   3. Normalize: a sinusoid of amplitude A (exactly on a bin centre) -> |X[k]| = A · N · CG / 2 (CG = coherent gain)
//      so A = 2|X[k]| / (N·CG); DC and Nyquist have no mirror bin, so their factor is 1 instead of 2.
//      This makes a full-scale sinusoid read 0 dBFS.
//   4. Exponential averaging (linear domain): s[k] = α·s[k] + (1-α)·m[k], α = smoothing
//   5. Convert to dB

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>
#include <dawlab/dsp/Fft.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <complex>
#include <cstdint>
#include <memory>
#include <vector>

namespace dawlab::dsp {

class SpectrumAnalyzer {
public:
    // Allocates. fftOrder = 11 -> N = 2048, a bin resolution of 23.4 Hz at 48 kHz.
    void prepare(double sampleRate, int fftOrder = 11) {
        assert(sampleRate > 0.0 && fftOrder >= 4 && fftOrder <= 16);
        sampleRate_ = sampleRate;
        fft_.setOrder(fftOrder);
        fftSize_ = fft_.getSize();
        ringCapacity_ = fftSize_ * 4;
        ringMask_ = static_cast<uint64_t>(ringCapacity_ - 1);
        ring_ = std::make_unique<std::atomic<float>[]>(static_cast<size_t>(ringCapacity_));
        for (int i = 0; i < ringCapacity_; ++i)
            ring_[static_cast<size_t>(i)].store(0.0f, std::memory_order_relaxed);
        frame_.assign(static_cast<size_t>(fftSize_), 0.0f);
        spectrum_.assign(static_cast<size_t>(fft_.getNumRealBins()), std::complex<float>(0.0f, 0.0f));
        smoothed_.assign(static_cast<size_t>(fft_.getNumRealBins()), 0.0f);
        rebuildWindow();
        writePos_.store(0, std::memory_order_relaxed);
        writeEnd_.store(0, std::memory_order_relaxed);
        lastFramePos_ = 0;
        primed_ = false;
        updateHop();
    }

    // UI thread: clear the smoothing state (the ring is not cleared)
    void reset() noexcept {
        std::fill(smoothed_.begin(), smoothed_.end(), 0.0f);
        primed_ = false;
    }

    // The three setters below are called on the UI thread (the same thread as computeSpectrum)
    void setWindowType(WindowType type) {
        windowType_ = type;
        rebuildWindow();
    }
    // 0 = no smoothing; around 0.8 looks steady without being too slow. α is the weight of the old value.
    void setSmoothing(float alpha) noexcept { smoothing_ = std::clamp(alpha, 0.0f, 0.99f); }
    // Overlap fraction between adjacent frames (0 ~ 0.9); the default 0.5 -> the next frame is computed only after
    // another N/2 new samples have accumulated
    void setOverlap(float fraction) noexcept {
        overlap_ = std::clamp(fraction, 0.0f, 0.9f);
        updateHop();
    }

    // ---- audio thread (lock-free, no allocation) -----------------------------------------
    // May push any length at once (for very large n the ring keeps only the last 4N samples, which is exactly what
    // the consumer needs).
    void pushSamples(const float* x, int n) noexcept {
        if (!ring_ || n <= 0)
            return;
        const uint64_t pos = beginWrite(n);
        for (int i = 0; i < n; ++i)
            ring_[static_cast<size_t>((pos + static_cast<uint64_t>(i)) & ringMask_)].store(x[i], std::memory_order_relaxed);
        endWrite(pos, n);
    }

    // Multichannel block: average the channels down to mono, then push
    void pushBlock(const AudioBlock& block) noexcept {
        const int nch = block.getNumChannels();
        const int n = block.getNumSamples();
        if (!ring_ || nch <= 0 || n <= 0)
            return;
        const float scale = 1.0f / static_cast<float>(nch);
        const uint64_t pos = beginWrite(n);
        for (int i = 0; i < n; ++i) {
            float sum = 0.0f;
            for (int ch = 0; ch < nch; ++ch)
                sum += block.getSample(ch, i);
            ring_[static_cast<size_t>((pos + static_cast<uint64_t>(i)) & ringMask_)].store(sum * scale, std::memory_order_relaxed);
        }
        endWrite(pos, n);
    }

    // ---- UI thread ----------------------------------------------------------------------
    // Returns true and fills in N/2+1 bins of dBFS; returns false (magnitudesDb unchanged) when there is not
    // enough new data.
    bool computeSpectrum(std::vector<float>& magnitudesDb) {
        if (!ring_)
            return false;
        const uint64_t written = writePos_.load(std::memory_order_acquire);
        const uint64_t n = static_cast<uint64_t>(fftSize_);
        if (written < n)
            return false; // a complete frame has not accumulated yet
        if (written - lastFramePos_ < static_cast<uint64_t>(hop_))
            return false; // fewer than one hop of new samples, so no new frame

        // Copy the latest N samples and multiply by the window
        const uint64_t start = written - n;
        for (uint64_t i = 0; i < n; ++i)
            frame_[static_cast<size_t>(i)] =
                ring_[static_cast<size_t>((start + i) & ringMask_)].load(std::memory_order_relaxed) *
                window_[static_cast<size_t>(i)];

        // After reading, check whether the producer wrapped around and overwrote the region we just read.
        // What matters is writeEnd_ ("how far writing has started"), not writePos_ ("how far writing has finished"):
        // a pushSamples() still in progress may already have overwritten cells we just read, but its writePos_ is
        // only updated once the whole push ends.
        // Why is reading writeEnd_ guaranteed to be recent enough? If one of the relaxed loads above saw a new value
        // written by some push, that push's release fence (before it writes its samples) synchronizes with the
        // acquire fence below (C++ fence-fence synchronization), so that push's store to writeEnd_ is certain to be
        // visible (the standard seqlock pattern).
        std::atomic_thread_fence(std::memory_order_acquire);
        const uint64_t writeEnd = writeEnd_.load(std::memory_order_relaxed);
        if (writeEnd - start > static_cast<uint64_t>(ringCapacity_))
            return false; // the region may have been overwritten, so discard this frame (try again next time)

        lastFramePos_ = written;

        fft_.forwardReal(frame_.data(), spectrum_.data());
        const int bins = fft_.getNumRealBins();
        magnitudesDb.resize(static_cast<size_t>(bins)); // UI thread, so allocating is fine
        for (int k = 0; k < bins; ++k) {
            const float scale = (k == 0 || k == bins - 1) ? normalization_ : 2.0f * normalization_;
            const float magnitude = std::abs(spectrum_[static_cast<size_t>(k)]) * scale;
            float& s = smoothed_[static_cast<size_t>(k)];
            // Adopt the first frame as-is (otherwise the smoothing state starts at 0 and the first few frames read too low)
            s = primed_ ? smoothing_ * s + (1.0f - smoothing_) * magnitude : magnitude;
            magnitudesDb[static_cast<size_t>(k)] = gainToDb(s);
        }
        primed_ = true;
        return true;
    }

    double getBinFrequency(int bin) const noexcept { return fftSize_ > 0 ? bin * sampleRate_ / fftSize_ : 0.0; }
    double getBinWidthHz() const noexcept { return fftSize_ > 0 ? sampleRate_ / fftSize_ : 0.0; }
    int getFftSize() const noexcept { return fftSize_; }
    int getNumBins() const noexcept { return fftSize_ / 2 + 1; }
    int getHopSize() const noexcept { return hop_; }
    double getSampleRate() const noexcept { return sampleRate_; }
    WindowType getWindowType() const noexcept { return windowType_; }
    float getSmoothing() const noexcept { return smoothing_; }

    // Number of new samples in the ring that no frame has used yet (for debugging / teaching)
    uint64_t getPendingSamples() const noexcept { return writePos_.load(std::memory_order_acquire) - lastFramePos_; }

private:
    // The producer's two-phase write (seqlock style):
    //   beginWrite: first announce "I am going to write up to pos + n" (writeEnd_), then place a release fence, and
    //               only then write the samples;
    //   endWrite  : once the samples are written, writePos_.store(release) announces "the data up to pos + n is now readable".
    // Only the audio thread calls these, so reading our own writePos_ with relaxed is enough.
    uint64_t beginWrite(int n) noexcept {
        const uint64_t pos = writePos_.load(std::memory_order_relaxed);
        writeEnd_.store(pos + static_cast<uint64_t>(n), std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release); // order writeEnd_ before all the sample writes that follow
        return pos;
    }
    void endWrite(uint64_t pos, int n) noexcept {
        writePos_.store(pos + static_cast<uint64_t>(n), std::memory_order_release); // the earlier sample writes become visible to the acquire side
    }

    void rebuildWindow() {
        Fft::makeWindow(window_, fftSize_, windowType_);
        const double cg = Fft::coherentGain(window_.data(), fftSize_);
        normalization_ = static_cast<float>(1.0 / (fftSize_ * cg)); // the factor of 2 is handled in computeSpectrum
    }

    void updateHop() noexcept {
        hop_ = std::max(1, static_cast<int>(std::lround(fftSize_ * (1.0 - static_cast<double>(overlap_)))));
    }

    double sampleRate_ = 0.0;
    int fftSize_ = 0;
    int ringCapacity_ = 0;
    uint64_t ringMask_ = 0;
    int hop_ = 1;
    float overlap_ = 0.5f;
    float smoothing_ = 0.8f;
    float normalization_ = 1.0f;
    WindowType windowType_ = WindowType::Hann;

    Fft fft_;
    std::unique_ptr<std::atomic<float>[]> ring_;
    std::atomic<uint64_t> writePos_{0}; // total number of samples fully written (readable)
    std::atomic<uint64_t> writeEnd_{0}; // position up to which writing has started (>= writePos_; > writePos_ while a push is in progress)
    uint64_t lastFramePos_ = 0;
    bool primed_ = false;
    std::vector<float> window_, frame_, smoothed_;
    std::vector<std::complex<float>> spectrum_;
};

} // namespace dawlab::dsp
