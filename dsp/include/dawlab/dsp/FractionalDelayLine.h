#pragma once
// FractionalDelayLine: a circular buffer that supports "non-integer sample" delays (chapter 09).
//
// Design notes:
// - The buffer length is a power of two, and the read/write indices wrap with a bitwise AND
//   (index & mask) instead of %, which is cheaper on the audio thread and avoids the
//   remainder-of-a-negative-number problem.
// - Each channel has its own contiguous block of memory and its own write index, so
//   push(ch, x) can be called channel by channel.
// - Convention: after push(), readXxx(ch, 0) returns the sample that was just pushed;
//   readXxx(ch, D) returns the sample from D pushes ago (D may be fractional).
// - Three ways to read:
//     readInteger : integer delay, plain lookup (cheapest and exact, but the delay must be an integer)
//     readLinear  : two-point linear interpolation. Simple, but attenuates high frequencies
//                   (|H| = |cos(πf/fs)| at Δ = 0.5)
//     readCubic   : four-point cubic Hermite (Catmull-Rom) interpolation. Keeps the high
//                   frequencies much better than linear.
//   readLinear / readCubic also have Split versions (the delay split into int + fraction) for
//   callers with long delays that need sub-sample precision.
//     readLagrange: four-point 3rd-order Lagrange interpolation (for teaching comparisons;
//                   identical to Hermite at Δ = 0.5)
// - prepare() is the only place that allocates; every other method is noexcept and does not allocate.

#include <algorithm>
#include <cassert>
#include <cmath>
#include <vector>

namespace dawlab::dsp {

class FractionalDelayLine {
public:
    // maxDelaySamples: the largest delay later reads may use (in samples, after rounding any
    // fractional part up). Internally 4 extra samples of headroom are allocated so that the
    // d+2 needed by cubic interpolation cannot run out of bounds.
    void prepare(int maxDelaySamples, int numChannels) {
        maxDelay_ = std::max(1, maxDelaySamples);
        numChannels_ = std::max(1, numChannels);
        // A cubic read needs d-1 .. d+2 with d at most maxDelay_, so we need at least
        // maxDelay_ + 3 samples; round that up to a power of two so the wrap can be done
        // with & mask.
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
    int getBufferSize() const noexcept { return size_; } // a power of two (for teaching: print it and look)

    // Write one sample; the write index advances by one and wraps.
    void push(int ch, float x) noexcept {
        assert(ch >= 0 && ch < numChannels_);
        float* b = channelData(ch);
        int& w = writeIndex_[static_cast<size_t>(ch)];
        b[w] = x;
        w = (w + 1) & mask_; // equivalent to (w + 1) % size_, but needs only one AND
    }

    // Integer delay: delaySamples = 0 means the sample that was pushed last.
    float readInteger(int ch, int delaySamples) const noexcept {
        return at(ch, std::clamp(delaySamples, 0, maxDelay_));
    }

    // Linear interpolation: y = (1-Δ)·x[n-d] + Δ·x[n-d-1], d = floor(delay), Δ = delay - d.
    float readLinear(int ch, float delaySamples) const noexcept {
        const float delay = std::clamp(delaySamples, 0.0f, static_cast<float>(maxDelay_));
        const int d = static_cast<int>(delay); // floor (delay >= 0)
        return readLinearSplit(ch, d, delay - static_cast<float>(d));
    }

    // Same as above, but the delay is already split into an "integer part d" and a
    // "fractional part frac in [0, 1)".
    // Why this exists: float has only 24 bits of precision, so for delays of 65536 samples or
    // more its smallest spacing (ulp) is 1/128 sample, and at 720000 samples (15 s @ 48 kHz)
    // it reaches 1/16 sample -- the read position would be quantised into steps. The caller
    // computes the position in double and splits it into int + fraction; the fractional part
    // is always in [0, 1), where float precision is more than enough.
    float readLinearSplit(int ch, int d, float frac) const noexcept {
        clampSplit(d, frac, 0);
        const float a = at(ch, d);
        const float b = at(ch, d + 1);
        return a + frac * (b - a);
    }

    // Four-point cubic Hermite (Catmull-Rom) interpolation. It needs the "newer" sample at d-1,
    // so the delay must be at least 1; anything smaller is clamped to 1.
    float readCubic(int ch, float delaySamples) const noexcept {
        const float delay = std::clamp(delaySamples, 1.0f, static_cast<float>(maxDelay_));
        const int d = static_cast<int>(delay);
        return readCubicSplit(ch, d, delay - static_cast<float>(d));
    }

    // The "integer + fraction" version of readCubic (see readLinearSplit for why). With d < 1,
    // d is clamped to 1 and t to 0.
    float readCubicSplit(int ch, int d, float t) const noexcept {
        clampSplit(d, t, 1);
        const float y0 = at(ch, d - 1); // one sample newer than the target
        const float y1 = at(ch, d);     // left of the target
        const float y2 = at(ch, d + 1); // right of the target (older)
        const float y3 = at(ch, d + 2);
        // Catmull-Rom: take the tangents as the central differences of the neighbouring
        // points, m1 = (y2 - y0)/2 and m2 = (y3 - y1)/2, then expand with the Hermite basis
        // into c0 + c1 t + c2 t² + c3 t³.
        const float c0 = y1;
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + c0;
    }

    // Four-point 3rd-order Lagrange interpolation: the value at t of the unique cubic
    // polynomial through the four points y0..y3.
    // h_k(t) is the Lagrange basis (k = -1, 0, 1, 2 correspond to y0..y3).
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
    // Limit (d, frac) to [minDelay, maxDelay_]: when out of range the fractional part becomes 0
    // (the same result as clamping a float delay).
    void clampSplit(int& d, float& frac, int minDelay) const noexcept {
        if (!(frac >= 0.0f)) // also catches NaN
            frac = 0.0f;
        if (frac >= 1.0f) { // when the caller computes 1.0 (rounding), carry into the next integer
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

    // Fetch the sample from "d pushes ago". The last push wrote at writeIndex - 1, so the
    // target position is writeIndex - 1 - d; adding size_ before & mask avoids negatives.
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
