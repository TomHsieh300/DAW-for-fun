#pragma once
// Oversampler: 2x / 4x / 8x oversampling, implemented as a polyphase FIR halfband filter.
//
// Why do we need it? Nonlinear processing (saturation, distortion, hard clipping, ...) creates new
// harmonics, and the ones above Nyquist (fs/2) "fold back" (alias) into the audible band as noise that
// is inharmonic with the original sound. The fix is to first raise the signal to L times the sample rate
// (Nyquist becomes L*fs/2), apply the nonlinearity at the high rate, then lowpass away the harmonics
// above fs/2 and drop back to the original sample rate.
//
//   x[n] ──> upsample(xL) ──> nonlinearity f() ──> downsample(/L) ──> y[n]
//            (zero-stuff + lowpass)                (lowpass + discard)
//
// Every stage is a "x2" or "/2" halfband stage (HalfbandStage); 4x = two stages in series, 8x = three.
//
// Properties of the halfband FIR (derived in section 2.6 of the Chapter 06 README):
//   - The cutoff is exactly 1/4 of the new sample rate (i.e. the Nyquist of the old sample rate).
//   - The coefficients h[n] are all 0 for even n (the center h[0] = 1/2 is the only exception), and for
//     odd n, h[n] = sin(pi*n/2)/(pi*n).
//   - So, written in polyphase form:
//       upsampling    even output = FIR over the odd coefficients (2K multiplications), odd output = pure delay (0 multiplications)
//       downsampling  output = odd-coefficient FIR (applied to the even inputs) + 1/2 x the delayed odd inputs
//     Each "original-rate sample" needs only 2K multiply-adds, instead of running the whole FIR twice at
//     the high rate (about 8K).
//   - The price: a linear-phase FIR always has delay (latency). Each stage's upsample + downsample costs
//     2M samples "at that stage's high rate" (M = 2K - 1 is the FIR's group delay, i.e. its half-length;
//     note this is a different thing from the factor L above).
//     getLatencySamples() converts the delay of all stages back to the original rate and guarantees an
//     integer (when downsampling we may take either the even or the odd samples of the filter output; the
//     odd ones have 1 fewer high-rate sample of delay, and this decimation phase is used to absorb the
//     fractional remainder).
//
// Usage:
//   dawlab::dsp::Oversampler os(2);              // 2^2 = 4x
//   os.prepare(48000.0, 512, 2);
//   ...
//   dawlab::AudioBlock up = os.upsample(block);   // up has 4x as many samples as block (a view of an internal buffer)
//   for (each sample) up[i] = std::tanh(up[i]);   // apply the nonlinearity at 192 kHz
//   os.downsample(block);                         // writes back into block (block must have the same length as at upsample)
//
// Depends only on common/ (AudioBuffer, DspMath); upsample / downsample do not allocate.

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <vector>

namespace dawlab::dsp {

namespace detail {

// Zeroth-order modified Bessel function I0(x), used by the Kaiser window. Series: I0(x) = sum_k ((x/2)^k / k!)^2
inline double besselI0(double x) noexcept {
    const double y = 0.25 * x * x;
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 500; ++k) {
        term *= y / (static_cast<double>(k) * static_cast<double>(k));
        sum += term;
        if (term < 1.0e-14 * sum)
            break;
    }
    return sum;
}

} // namespace detail

// One halfband stage: handles "x2 upsampling" and "/2 downsampling". Oversampler chains 1 to 3 stages.
// Upsampling and downsampling each keep their own FIR history (within one block a stage upsamples first
// and downsamples later, and the two FIRs see different signals).
class HalfbandStage {
public:
    // Designs the full coefficient set of the halfband FIR (length 2*numOddTaps - 1 = 4K - 1; numOddTaps = 2K must be even).
    // Ideal halfband: h[0] = 1/2, h[n] = sin(pi*n/2)/(pi*n) (n odd), h[n] = 0 (n even and non-zero),
    // multiplied by a Kaiser window (larger beta: deeper stopband attenuation but a wider transition band).
    // In the returned h, index 0 = the leftmost tap (h[-M]) and the center is at index M = 2K - 1. sum(h) ~ 1 (DC gain ~ 1).
    static std::vector<double> designHalfband(int numOddTaps, double beta) {
        assert(numOddTaps >= 2 && numOddTaps % 2 == 0);
        const int K = numOddTaps / 2;
        const int M = 2 * K - 1;             // half-length; full length 2M + 1 = 4K - 1
        std::vector<double> h(static_cast<size_t>(2 * M + 1), 0.0);
        const double i0beta = detail::besselI0(beta);
        for (int n = -M; n <= M; ++n) {
            double ideal;
            if (n == 0)
                ideal = 0.5;
            else if (n % 2 == 0)
                ideal = 0.0;                 // the key to a halfband: all even taps are 0
            else
                ideal = std::sin(0.5 * kPi * n) / (kPi * n);
            const double r = static_cast<double>(n) / static_cast<double>(M);
            const double window = detail::besselI0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0beta;
            h[static_cast<size_t>(n + M)] = ideal * window;
        }
        return h;
    }

    // numOddTaps = 2K (the number of odd coefficients, which is also the number of multiplications per input sample); beta = Kaiser beta;
    // decimationPhase (0 or 1): whether downsampling keeps the even or the odd samples of the filter output
    // (phase 1 has 1 fewer high-rate sample of delay; Oversampler uses it to make the total latency an integer).
    void prepare(int numOddTaps, double beta, int numChannels, int decimationPhase = 0) {
        K_ = numOddTaps / 2;
        phase_ = decimationPhase;
        const std::vector<double> h = designHalfband(numOddTaps, beta);
        const int M = 2 * K_ - 1;
        // Polyphase odd branch: g[j] = 2 h[2(j - K) + 1], j = 0 .. 2K-1 (the factor 2 compensates for upsampling
        // (zero-stuffing); downsampling multiplies by 1/2 again)
        g_.resize(static_cast<size_t>(2 * K_));
        for (int j = 0; j < 2 * K_; ++j)
            g_[static_cast<size_t>(j)] = static_cast<float>(2.0 * h[static_cast<size_t>(2 * (j - K_) + 1 + M)]);
        state_.assign(static_cast<size_t>(numChannels), ChannelState{});
        for (auto& s : state_) {
            s.upHist.assign(static_cast<size_t>(4 * K_), 0.0f);        // 2N (N = 2K) mirrored ring buffer
            s.downHist.assign(static_cast<size_t>(4 * K_), 0.0f);
            s.delayHist.assign(static_cast<size_t>(2 * (K_ + 1)), 0.0f);
        }
        reset();
    }

    void reset() noexcept {
        for (auto& s : state_) {
            std::fill(s.upHist.begin(), s.upHist.end(), 0.0f);
            std::fill(s.downHist.begin(), s.downHist.end(), 0.0f);
            std::fill(s.delayHist.begin(), s.delayHist.end(), 0.0f);
            s.upPos = 0;
            s.downPos = 0;
            s.delayPos = 0;
        }
    }

    int getNumOddTaps() const noexcept { return 2 * K_; }
    int getFilterLength() const noexcept { return 4 * K_ - 1; }
    // FIR group delay M = 2K - 1 (half the FIR length), counted at "this stage's high sample rate"
    int getFilterDelay() const noexcept { return 2 * K_ - 1; }
    int getDecimationPhase() const noexcept { return phase_; }
    // Total upsample + downsample delay of this stage, counted at "this stage's high sample rate": 2M - phase
    int getRoundTripDelay() const noexcept { return 2 * getFilterDelay() - phase_; }
    const std::vector<float>& getPolyphaseCoefficients() const noexcept { return g_; }

    // Upsampling: n inputs -> 2n outputs.
    //   out[2i]   = sum_j g[j] x[i - j]      (the interpolated "in-between point")
    //   out[2i+1] = x[i - (K - 1)]            (the original sample itself, a pure delay)
    // Overall this is equivalent to "zero-stuffing + a lowpass 2*h", delayed by M high-rate samples.
    void upsample(int channel, const float* in, float* out, int n) noexcept {
        ChannelState& s = state_[static_cast<size_t>(channel)];
        const int N = 2 * K_;
        const float* g = g_.data();
        for (int i = 0; i < n; ++i) {
            pushMirror(s.upHist, s.upPos, N, in[i]);
            const float* hist = s.upHist.data() + s.upPos; // hist[j] = x[i - j], contiguous memory
            float acc = 0.0f;
            for (int j = 0; j < N; ++j)
                acc += g[j] * hist[j];
            out[2 * i] = acc;
            out[2 * i + 1] = hist[K_ - 1];
        }
    }

    // Downsampling: 2n inputs -> n outputs. Filter first, then keep every second sample (polyphase lets us
    // compute only the one we need):
    //   phase 0: out[i] = 1/2 ( sum_j g[j] u_even[i - j] + u_odd[i - K] )        = w[2i - M]
    //   phase 1: out[i] = 1/2 ( sum_j g[j] u_odd[i - j]  + u_even[i - (K - 1)] ) = w[2i - (M - 1)]
    // where u_even[i] = in[2i], u_odd[i] = in[2i+1], and w = the result of filtering with the full h (zero phase).
    void downsample(int channel, const float* in, float* out, int n) noexcept {
        ChannelState& s = state_[static_cast<size_t>(channel)];
        const int N = 2 * K_;
        const int D = K_ + 1;                      // length of delayHist
        const int centreDelay = (phase_ == 0) ? K_ : K_ - 1;
        const float* g = g_.data();
        for (int i = 0; i < n; ++i) {
            const float firIn = (phase_ == 0) ? in[2 * i] : in[2 * i + 1];
            const float delayIn = (phase_ == 0) ? in[2 * i + 1] : in[2 * i];
            pushMirror(s.downHist, s.downPos, N, firIn);
            pushMirror(s.delayHist, s.delayPos, D, delayIn);
            const float* hist = s.downHist.data() + s.downPos;
            float acc = 0.0f;
            for (int j = 0; j < N; ++j)
                acc += g[j] * hist[j];
            out[i] = 0.5f * (acc + s.delayHist[static_cast<size_t>(s.delayPos + centreDelay)]);
        }
    }

private:
    struct ChannelState {
        std::vector<float> upHist;    // history of the upsampling FIR, length 2N: every sample is written twice so the latest N samples are always contiguous
        std::vector<float> downHist;  // history of the downsampling FIR (same trick)
        std::vector<float> delayHist; // history of the downsampling "pure delay" branch, length 2D
        int upPos = 0;
        int downPos = 0;
        int delayPos = 0;
    };

    // "Mirrored ring buffer": the buffer has length 2N, the write position moves downward, and the same
    // value is written at both pos and pos + N. After that, buf[pos + j] (j = 0..N-1) is x[n - j], with no
    // modulo needed, so the FIR inner loop can run directly.
    static void pushMirror(std::vector<float>& buf, int& pos, int N, float x) noexcept {
        pos = (pos == 0) ? N - 1 : pos - 1;
        buf[static_cast<size_t>(pos)] = x;
        buf[static_cast<size_t>(pos + N)] = x;
    }

    int K_ = 0;
    int phase_ = 0;
    std::vector<float> g_;
    std::vector<ChannelState> state_;
};

class Oversampler {
public:
    // Default filter design (the numbers are in section 2.6 of the Chapter 06 README; the example
    // l06_oversampler_response measures them):
    //   Stage 1 (x2, at 2fs)  32 odd taps (63 taps), Kaiser beta = 8 -> stopband about -80 dB,
    //                         transition about 20.3 kHz -> 28 kHz (for fs = 48 kHz)
    //   Stage 2 (x4, at 4fs)  12 odd taps (23 taps): the transition band can be much wider, so the filter is much shorter
    //   Stage 3 (x8, at 8fs)   8 odd taps (15 taps)
    static constexpr int kMaxFactorLog2 = 3;
    static constexpr int kOddTapsStage1 = 32;
    static constexpr int kOddTapsStage2 = 12;
    static constexpr int kOddTapsStage3 = 8;
    static constexpr double kKaiserBeta = 8.0;

    // Factor = 2^factorLog2: 0 = no oversampling (1x, plain copy), 1 = 2x, 2 = 4x, 3 = 8x
    explicit Oversampler(int factorLog2 = 1) noexcept : factorLog2_(std::clamp(factorLog2, 0, kMaxFactorLog2)) {}

    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        sampleRate_ = sampleRate;
        maxBlockSize_ = std::max(1, maxBlockSize); // <= 0 is invalid input; allocate at least 1 to avoid a zero-sized buffer
        numChannels_ = numChannels;
        stages_.clear();
        stages_.resize(static_cast<size_t>(factorLog2_));

        // Total delay (in units of 1/2^S original samples) = sum_i (2 M_i - phi_i) * 2^(S-i)
        //   M_i: group delay of the stage-i FIR (once for upsampling, once for downsampling); phi_i in {0, 1}: the decimation phase of stage i.
        // First compute delayUnits with phi = 0; the remainder r = delayUnits mod 2^S is then subtracted by
        // the per-stage phi (bit (S-i) of r -> stage i), leaving an integer number of original samples.
        const int S = factorLog2_;
        int delayUnits = 0;
        for (int i = 1; i <= S; ++i) {
            const int K = oddTapsForStage(i) / 2;
            delayUnits += 2 * (2 * K - 1) * (1 << (S - i));
        }
        const int remainder = (S > 0) ? delayUnits % (1 << S) : 0;
        for (int i = 1; i <= S; ++i) {
            const int phase = (remainder >> (S - i)) & 1;
            stages_[static_cast<size_t>(i - 1)].prepare(oddTapsForStage(i), kKaiserBeta, numChannels, phase);
        }
        latencySamples_ = (S > 0) ? (delayUnits - remainder) >> S : 0;

        buffers_.clear();
        buffers_.resize(static_cast<size_t>(S + 1));
        for (int i = 0; i <= S; ++i)
            buffers_[static_cast<size_t>(i)].setSize(numChannels, maxBlockSize_ << i);
        currentNumSamples_ = 0;
    }

    void reset() noexcept {
        for (auto& s : stages_)
            s.reset();
        for (auto& b : buffers_)
            b.clear();
    }

    int getFactorLog2() const noexcept { return factorLog2_; }
    int getFactor() const noexcept { return 1 << factorLog2_; }
    double getOversampledRate() const noexcept { return sampleRate_ * getFactor(); }
    int getNumStages() const noexcept { return factorLog2_; }
    const HalfbandStage& getStage(int index) const noexcept { return stages_[static_cast<size_t>(index)]; }

    // Total round-trip (upsample + downsample) delay, in samples at the original rate (guaranteed to be an integer)
    int getLatencySamples() const noexcept { return latencySamples_; }

    // Upsample: returns a view of the internal highest-rate buffer, with length = input length x factor.
    // The caller applies the nonlinearity directly on this view and then calls downsample().
    AudioBlock upsample(const AudioBlock& input) noexcept {
        const int n = input.getNumSamples();
        const int numChannels = std::min(input.getNumChannels(), numChannels_);
        assert(n <= maxBlockSize_ && "block is larger than the maxBlockSize given to prepare");
        currentNumSamples_ = n;
        if (factorLog2_ == 0) {
            AudioBlock view = viewOf(0, n);
            view.copyFrom(input);
            return view;
        }
        for (int s = 1; s <= factorLog2_; ++s) {
            const int inLen = n << (s - 1);
            AudioBuffer& dst = buffers_[static_cast<size_t>(s)];
            for (int ch = 0; ch < numChannels; ++ch) {
                const float* src = (s == 1) ? input.getChannelPointer(ch)
                                            : buffers_[static_cast<size_t>(s - 1)].getReadPointer(ch);
                stages_[static_cast<size_t>(s - 1)].upsample(ch, src, dst.getWritePointer(ch), inLen);
            }
        }
        return viewOf(factorLog2_, n << factorLog2_);
    }

    // Downsample: filters the contents of the internal high-rate buffer, brings them back to the original rate and writes them into output (the length must equal that of the input given to upsample)
    void downsample(AudioBlock output) noexcept {
        const int n = output.getNumSamples();
        const int numChannels = std::min(output.getNumChannels(), numChannels_);
        assert(n == currentNumSamples_ && "downsample length must equal the length used at upsample");
        if (factorLog2_ == 0) {
            output.copyFrom(viewOf(0, n));
            return;
        }
        for (int s = factorLog2_; s >= 1; --s) {
            const int outLen = n << (s - 1);
            const AudioBuffer& src = buffers_[static_cast<size_t>(s)];
            for (int ch = 0; ch < numChannels; ++ch) {
                float* dst = (s == 1) ? output.getChannelPointer(ch)
                                      : buffers_[static_cast<size_t>(s - 1)].getWritePointer(ch);
                stages_[static_cast<size_t>(s - 1)].downsample(ch, src.getReadPointer(ch), dst, outLen);
            }
        }
    }

private:
    static int oddTapsForStage(int stage) noexcept {
        return stage == 1 ? kOddTapsStage1 : (stage == 2 ? kOddTapsStage2 : kOddTapsStage3);
    }

    AudioBlock viewOf(int level, int numSamples) noexcept {
        AudioBuffer& b = buffers_[static_cast<size_t>(level)];
        return AudioBlock(b.getArrayOfWritePointers(), b.getNumChannels(), numSamples);
    }

    int factorLog2_ = 1;
    double sampleRate_ = 48000.0;
    int maxBlockSize_ = 0;
    int numChannels_ = 0;
    int latencySamples_ = 0;
    int currentNumSamples_ = 0;
    std::vector<HalfbandStage> stages_;
    std::vector<AudioBuffer> buffers_; // the sample rate of buffers_[i] = fs x 2^i; [0] is used only at 1x
};

} // namespace dawlab::dsp
