#pragma once
// Delay: a stereo echo / delay effect (chapter 09).
//
// Signal path (per channel, normal mode):
//
//   x ──┬──────────────────────────────(1-mix)──┐
//       │                                       (+)──► y
//       │   ┌─────────────────┐                 │
//       └──►(+)──► delay line ├──┬────(mix)──────┘
//           ▲    └─────────────────┘  │
//           │                         │
//           └── fb × HP × LP ◄────────┘   (filtering inside the feedback loop: the tape-echo character)
//
// Ping-pong mode (stereo): the input is first mixed down to mono into the left delay line,
// the left output feeds the right delay line, and the right output goes back to the left
// multiplied by feedback:
//
//   (L+R)/2 ──►(+)──► delay L ──┬──► wet L
//               ▲               │
//               │        fb×filt│
//               │               ▼
//          fb×filt ◄── delay R ◄┘
//                        │
//                        └──► wet R
//
// Key points:
// - The delay time is smoothed linearly (default 50 ms ramp), so changing it does not click;
//   instead there is a brief pitch change (Doppler). This is the "smooth transition"
//   strategy; the crossfade strategy is left to exercise 6.
//   The ramp is computed by hand in double (DelayRamp) rather than with the float
//   SmoothedValue: the delay time in samples can reach hundreds of thousands, and above 65536
//   float's spacing (ulp) is 1/128 sample, so adding a small step every sample gets rounded
//   and the error accumulates; when the ramp ends the read position "jumps back" to the
//   target -> click. See the comment on DelayRamp.
// - It uses the cubic interpolation of FractionalDelayLine, so the delay time can be any
//   number of milliseconds and can change continuously. When reading, the double position is
//   split into "integer + fraction" before being handed to readCubicSplit, so long delays
//   keep sub-sample precision too.
// - Feedback is clamped to [0, 0.99]: |feedback| < 1 is a necessary condition for stability.
// - Only prepare() allocates; process() does not allocate, lock or do I/O.
// - Values written into the delay line are denormal-flushed (see flushDenormal), so the CPU
//   load does not suddenly jump after a long stretch of silence.
// - With mono (numChannels = 1), setPingPong(true) has no effect and behaves like a normal echo.
// - Every setter ignores NaN (keeping the last valid value), and +-inf is clamped to the valid range.
// - It does not report latency: the delay is the effect itself, and the host should not compensate for it.
// - Calling reset() / process() before prepare() is safe (does nothing).

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>
#include <dawlab/dsp/FractionalDelayLine.h>
#include <dawlab/dsp/OnePole.h>
#include <dawlab/dsp/SmoothedValue.h>

#include <algorithm>
#include <cmath>

namespace dawlab::dsp {

class Delay {
public:
    enum class Interpolation { Linear, Cubic };

    static constexpr int kMaxChannels = 2;
    static constexpr float kMaxFeedback = 0.99f;

    // ---- Configuration (before prepare) --------------------------------------
    // Maximum delay time (determines the buffer length allocated in prepare). Default 2000 ms.
    // NaN is ignored (std::clamp / std::max pass NaN straight through, and the read position
    // computed from it later would be garbage).
    void setMaxDelayMs(float ms) noexcept {
        if (!std::isnan(ms))
            maxDelayMs_ = std::clamp(ms, 1.0f, 60000.0f);
    }

    // ---- Standard interface ---------------------------------------------------
    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        (void)maxBlockSize; // processed sample by sample, so no scratch block is needed
        sampleRate_ = sampleRate;
        numChannels_ = std::clamp(numChannels, 1, kMaxChannels);
        maxDelaySamples_ = msToSamples(static_cast<double>(maxDelayMs_), sampleRate) + 2;
        line_.prepare(maxDelaySamples_, numChannels_);

        lowpass_.setType(OnePole::Type::Lowpass);
        lowpass_.prepare(sampleRate, maxBlockSize, numChannels_);
        lowpass_.setCutoff(lowpassHz_);
        highpass_.setType(OnePole::Type::Highpass);
        highpass_.prepare(sampleRate, maxBlockSize, numChannels_);
        highpass_.setCutoff(highpassHz_);

        delayRamp_.setRampSamples(rampSecondsToSamples(delayRampSeconds_));
        delayRamp_.setCurrentAndTarget(targetDelaySamples());
        feedback_.reset(sampleRate, kParamRampSeconds);
        feedback_.setCurrentAndTargetValue(feedbackTarget_);
        mix_.reset(sampleRate, kParamRampSeconds);
        mix_.setCurrentAndTargetValue(mixTarget_);
        prepared_ = true;
    }

    void reset() noexcept {
        line_.reset();
        lowpass_.reset();
        highpass_.reset();
        // Parameters jump straight to their targets: a reset usually happens when the
        // transport stops / restarts, so no smoothing is needed. Before prepare() the sample
        // rate and buffer length are not decided yet, so there is nothing to jump.
        if (!prepared_)
            return;
        delayRamp_.setCurrentAndTarget(targetDelaySamples());
        feedback_.setCurrentAndTargetValue(feedbackTarget_);
        mix_.setCurrentAndTargetValue(mixTarget_);
    }

    // ---- Parameters -----------------------------------------------------------
    // Delay time (milliseconds), smoothed linearly to the new value over setDelayRampSeconds.
    void setDelayMs(float ms) noexcept {
        if (std::isnan(ms))
            return;
        delayMs_ = std::clamp(ms, 0.0f, maxDelayMs_);
        if (prepared_)
            delayRamp_.setTarget(targetDelaySamples());
    }

    // Length (seconds) of the ramp used when the delay time changes. The longer it is, the
    // smaller the pitch shift but the slower it reaches the new value. Default 0.05 s.
    void setDelayRampSeconds(double seconds) noexcept {
        if (!std::isfinite(seconds))
            return;
        delayRampSeconds_ = std::clamp(seconds, 0.0, 10.0);
        if (prepared_)
            delayRamp_.setRampSamples(rampSecondsToSamples(delayRampSeconds_)); // if already ramping, restart from the current position with the new length
    }

    // Feedback 0..0.99 (the factor applied on every trip around the loop). >= 1 diverges, so it is clamped.
    void setFeedback(float feedback) noexcept {
        if (std::isnan(feedback))
            return;
        feedbackTarget_ = std::clamp(feedback, 0.0f, kMaxFeedback);
        if (prepared_)
            feedback_.setTargetValue(feedbackTarget_);
    }

    // Dry/wet: 0 = dry only, 1 = delay only. y = (1-mix)·dry + mix·wet (linear crossfade).
    void setMix(float mix) noexcept {
        if (std::isnan(mix))
            return;
        mixTarget_ = std::clamp(mix, 0.0f, 1.0f);
        if (prepared_)
            mix_.setTargetValue(mixTarget_);
    }

    void setPingPong(bool enabled) noexcept { pingPong_ = enabled; }

    // One-pole lowpass inside the feedback loop; >= 20000 Hz means off.
    void setLowpassHz(float hz) noexcept {
        if (std::isnan(hz))
            return;
        lowpassHz_ = std::clamp(hz, 20.0f, 20000.0f);
        lowpassEnabled_ = lowpassHz_ < 20000.0f;
        if (prepared_)
            lowpass_.setCutoff(lowpassHz_);
    }

    // One-pole highpass inside the feedback loop; <= 20 Hz means off.
    void setHighpassHz(float hz) noexcept {
        if (std::isnan(hz))
            return;
        highpassHz_ = std::clamp(hz, 20.0f, 20000.0f);
        highpassEnabled_ = highpassHz_ > 20.0f;
        if (prepared_)
            highpass_.setCutoff(highpassHz_);
    }

    void setInterpolation(Interpolation interpolation) noexcept { interpolation_ = interpolation; }

    float getDelayMs() const noexcept { return delayMs_; }
    float getMaxDelayMs() const noexcept { return maxDelayMs_; }
    float getFeedback() const noexcept { return feedbackTarget_; }
    float getMix() const noexcept { return mixTarget_; }
    bool isPingPong() const noexcept { return pingPong_; }
    int getMaxDelaySamples() const noexcept { return maxDelaySamples_; }

    // The current (mid-ramp) delay time in milliseconds; for a UI or examples that want to watch the ramp.
    float getCurrentDelayMs() const noexcept {
        return prepared_ ? static_cast<float>(delayRamp_.getCurrent() * 1000.0 / sampleRate_) : delayMs_;
    }

    // Tempo sync: the length of `beats` beats in milliseconds. 1 beat = a quarter note.
    //   eighth = 0.5, dotted eighth = 0.75, eighth triplet = 1/3, sixteenth = 0.25.
    //   ms = beats × 60000 / bpm
    static float noteToMs(double bpm, double beats) noexcept {
        return bpm > 0.0 ? static_cast<float>(beats * 60000.0 / bpm) : 0.0f;
    }

    // ---- Processing -----------------------------------------------------------
    void process(AudioBlock block) noexcept {
        const int numSamples = block.getNumSamples();
        const int nch = std::min(block.getNumChannels(), numChannels_);
        if (!prepared_ || nch <= 0)
            return;

        if (pingPong_ && nch == 2)
            processPingPong(block, numSamples);
        else
            processIndependent(block, nch, numSamples);
    }

private:
    static constexpr double kParamRampSeconds = 0.02;

    // A linear ramp of the delay time (in samples), all in double.
    //
    // Why not the chapter 01 SmoothedValue (float, current += step on every sample)?
    // Example: 2000 ms buffer, 48 kHz, 1900 ms -> 1901 ms with a 50 ms ramp:
    //   The position is about 91200 samples, in [65536, 131072), where float's ulp = 2^-7 = 0.0078;
    //   step = 48 / 2400 = 0.02 = 2.56 ulp, and every addition is rounded to 3 ulp = 0.0234,
    //   so after 2399 steps it has travelled about 8.2 samples too far, and the last step
    //   then "jumps back" to the target -> the read position leaps 8 samples = click.
    //   At 15000 ms, ulp = 0.0625 > 2 x step, so current += step cannot even add: it stands
    //   still for 2400 samples and then jumps 48 samples in one go.
    // There are two fixes: (1) use double (ulp is 2^29 times smaller); (2) do not accumulate, but
    // compute current = target − step × countdown directly each time, so the error is only ever
    // one rounding and never builds up. We do both here.
    class DelayRamp {
    public:
        void setRampSamples(int numSamples) noexcept {
            rampSamples_ = std::max(0, numSamples);
            if (countdown_ > 0)
                start(); // already ramping: restart from the current position with the new length
        }
        void setCurrentAndTarget(double value) noexcept {
            current_ = target_ = value;
            step_ = 0.0;
            countdown_ = 0;
        }
        void setTarget(double value) noexcept {
            if (value == target_)
                return;
            target_ = value;
            start();
        }
        double getNext() noexcept {
            if (countdown_ > 0) {
                --countdown_;
                current_ = target_ - step_ * static_cast<double>(countdown_); // no accumulation: exactly = target when countdown = 0
            }
            return current_;
        }
        double getCurrent() const noexcept { return current_; }

    private:
        void start() noexcept {
            if (rampSamples_ <= 0) {
                setCurrentAndTarget(target_);
                return;
            }
            countdown_ = rampSamples_;
            step_ = (target_ - current_) / static_cast<double>(rampSamples_);
        }

        double current_ = 2.0;
        double target_ = 2.0;
        double step_ = 0.0;
        int rampSamples_ = 0;
        int countdown_ = 0;
    };

    int rampSecondsToSamples(double seconds) const noexcept {
        return static_cast<int>(std::lround(seconds * sampleRate_));
    }

    double targetDelaySamples() const noexcept {
        // At least 2 samples (a cubic read needs d-1 >= 1), at most the largest value the buffer allows
        const double s = static_cast<double>(delayMs_) * 0.001 * sampleRate_;
        return std::clamp(s, 2.0, std::max(2.0, static_cast<double>(maxDelaySamples_)));
    }

    // Read the value from "D samples ago". Because we read first and push afterwards (this
    // sample has not been written yet), readXxx(ch, k) actually corresponds to a delay of
    // k+1 samples, so we have to read D - 1.
    // The position is a double: split it into integer + fraction first (the fractional part
    // loses no precision when converted to float), then hand it to the Split version.
    float readDelayed(int ch, double delaySamples) const noexcept {
        const double idx = delaySamples - 1.0;
        const int d = static_cast<int>(idx); // floor (idx >= 1)
        const float frac = static_cast<float>(idx - static_cast<double>(d));
        return interpolation_ == Interpolation::Cubic ? line_.readCubicSplit(ch, d, frac)
                                                      : line_.readLinearSplit(ch, d, frac);
    }

    // The feedback loop is recursive: once the input goes silent, the echoes are multiplied by
    // fb on every trip and eventually fall into the denormal range (|x| < 1.2e-38, where the
    // CPU can be hundreds of times slower). Values below -300 dBFS are simply cleared to 0,
    // which makes no audible difference.
    static float flushDenormal(float x) noexcept { return std::abs(x) < 1.0e-15f ? 0.0f : x; }

    float feedbackFilter(int ch, float x) noexcept {
        if (highpassEnabled_)
            x = highpass_.processSample(ch, x);
        if (lowpassEnabled_)
            x = lowpass_.processSample(ch, x);
        return x;
    }

    void processIndependent(AudioBlock block, int nch, int numSamples) noexcept {
        for (int i = 0; i < numSamples; ++i) {
            const double d = delayRamp_.getNext();
            const float fb = feedback_.getNextValue();
            const float mix = mix_.getNextValue();
            for (int ch = 0; ch < nch; ++ch) {
                float* data = block.getChannelPointer(ch);
                const float x = data[i];
                const float wet = readDelayed(ch, d);
                line_.push(ch, flushDenormal(x + fb * feedbackFilter(ch, wet))); // feedback: the filtered delay output returns to the input
                data[i] = (1.0f - mix) * x + mix * wet;
            }
        }
    }

    void processPingPong(AudioBlock block, int numSamples) noexcept {
        float* left = block.getChannelPointer(0);
        float* right = block.getChannelPointer(1);
        for (int i = 0; i < numSamples; ++i) {
            const double d = delayRamp_.getNext();
            const float fb = feedback_.getNextValue();
            const float mix = mix_.getNextValue();
            const float xl = left[i];
            const float xr = right[i];
            const float mono = 0.5f * (xl + xr);
            const float wetL = readDelayed(0, d);
            const float wetR = readDelayed(1, d);
            line_.push(0, flushDenormal(mono + fb * feedbackFilter(0, wetR))); // the right output returns to the left
            line_.push(1, flushDenormal(fb * feedbackFilter(1, wetL)));        // the left output goes to the right
            left[i] = (1.0f - mix) * xl + mix * wetL;
            right[i] = (1.0f - mix) * xr + mix * wetR;
        }
    }

    FractionalDelayLine line_;
    OnePole lowpass_;
    OnePole highpass_;
    DelayRamp delayRamp_; // delay time (samples), a double-precision linear ramp
    SmoothedValue feedback_;
    SmoothedValue mix_;

    double sampleRate_ = 48000.0;
    double delayRampSeconds_ = 0.05;
    int numChannels_ = 2;
    int maxDelaySamples_ = 0;
    float maxDelayMs_ = 2000.0f;
    float delayMs_ = 375.0f;
    float feedbackTarget_ = 0.4f;
    float mixTarget_ = 0.3f;
    float lowpassHz_ = 20000.0f;
    float highpassHz_ = 20.0f;
    bool lowpassEnabled_ = false;
    bool highpassEnabled_ = false;
    bool pingPong_ = false;
    bool prepared_ = false;
    Interpolation interpolation_ = Interpolation::Cubic;
};

} // namespace dawlab::dsp
