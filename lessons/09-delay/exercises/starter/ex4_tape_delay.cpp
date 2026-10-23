// Exercise 4 starter: tape-style delay
//
// Goal: put highpass -> lowpass -> tanh saturation into the feedback loop:
//
//   x ──►(+)──► delay line ──┬──► wet ──(mix)──► y
//         ▲                  │
//         └── ×fb ◄── tanh ◄── LP ◄── HP ◄┘
//
// After completing the TODO in feedbackPath(), run l09_ex4_starter; all the checks should pass:
//   (a) the 8 kHz content of the second echo is much smaller than the first (the lowpass is in the loop);
//   (b) with feedback = 1.1 the tanh version's output stays bounded, and its amplitude lands
//       near the describing-function prediction (see selfOscillationPeak).

#include <dawlab/Common.h>
#include <dawlab/dsp/FractionalDelayLine.h>
#include <dawlab/dsp/OnePole.h>
#include <dawlab/dsp/SmoothedValue.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

using dawlab::dsp::FractionalDelayLine;
using dawlab::dsp::OnePole;
using dawlab::dsp::SmoothedValue;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

class TapeDelay {
public:
    static constexpr int kMaxChannels = 2;

    void setMaxDelayMs(float ms) noexcept { maxDelayMs_ = std::max(1.0f, ms); }

    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        sampleRate_ = sampleRate;
        numChannels_ = std::clamp(numChannels, 1, kMaxChannels);
        maxDelaySamples_ = dawlab::msToSamples(maxDelayMs_, sampleRate) + 2;
        line_.prepare(maxDelaySamples_, numChannels_);
        lowpass_.setType(OnePole::Type::Lowpass);
        lowpass_.prepare(sampleRate, maxBlockSize, numChannels_);
        highpass_.setType(OnePole::Type::Highpass);
        highpass_.prepare(sampleRate, maxBlockSize, numChannels_);
        delaySamples_.reset(sampleRate, 0.05);
        prepared_ = true;
        setDelayMs(delayMs_);
        setLowpassHz(lowpassHz_);
        setHighpassHz(highpassHz_);
        reset();
    }

    void reset() noexcept {
        line_.reset();
        lowpass_.reset();
        highpass_.reset();
        delaySamples_.setCurrentAndTargetValue(targetDelaySamples());
    }

    void setDelayMs(float ms) noexcept {
        delayMs_ = std::clamp(ms, 0.0f, maxDelayMs_);
        if (prepared_)
            delaySamples_.setTargetValue(targetDelaySamples());
    }
    void setFeedback(float fb) noexcept { feedback_ = std::clamp(fb, 0.0f, 1.5f); }
    void setMix(float mix) noexcept { mix_ = std::clamp(mix, 0.0f, 1.0f); }
    void setLowpassHz(float hz) noexcept {
        lowpassHz_ = std::clamp(hz, 20.0f, 20000.0f);
        if (prepared_)
            lowpass_.setCutoff(lowpassHz_);
    }
    void setHighpassHz(float hz) noexcept {
        highpassHz_ = std::clamp(hz, 20.0f, 20000.0f);
        if (prepared_)
            highpass_.setCutoff(highpassHz_);
    }
    void setDrive(float drive) noexcept { drive_ = std::max(0.1f, drive); }
    void setSaturationEnabled(bool on) noexcept { saturate_ = on; }

    float getLoopGainAt(double freq) const {
        return feedback_ * onePoleMag(freq, lowpassHz_, false) * onePoleMag(freq, highpassHz_, true);
    }

    void process(dawlab::AudioBlock block) noexcept {
        const int n = block.getNumSamples();
        const int nch = std::min(block.getNumChannels(), numChannels_);
        for (int i = 0; i < n; ++i) {
            const float d = delaySamples_.getNextValue();
            for (int ch = 0; ch < nch; ++ch) {
                float* data = block.getChannelPointer(ch);
                const float x = data[i];
                const float wet = line_.readCubic(ch, d - 1.0f);
                line_.push(ch, x + feedbackPath(ch, wet));
                data[i] = (1.0f - mix_) * x + mix_ * wet;
            }
        }
    }

private:
    // The actual magnitude response of the digital OnePole (chapter 02), not the analog
    // one-pole 1/√(1+(f/fc)²):
    //   a = exp(−2π·fc/fs), ω = 2π·f/fs, denominator |1 − a·e^{−jω}| = √(1 − 2a·cosω + a²)
    //   lowpass : H = (1−a) / (1 − a z⁻¹)               → |H| = (1−a) / denominator
    //   highpass: H = ½(1+a)(1 − z⁻¹) / (1 − a z⁻¹)     → |H| = ½(1+a)·√(2 − 2cosω) / denominator
    // The analog formula is nearly the same at low frequencies, but differs by several
    // percent around 8 kHz (fs/6).
    float onePoleMag(double f, float cutoff, bool highpass) const noexcept {
        const double fc = std::clamp(static_cast<double>(cutoff), 1.0e-3, sampleRate_ * 0.49); // same clamp as OnePole
        const double a = dawlab::dsp::OnePole::coefficientForCutoff(fc, sampleRate_);
        const double c = std::cos(dawlab::kTwoPi * f / sampleRate_);
        const double den = std::sqrt(1.0 - 2.0 * a * c + a * a);
        const double mag = highpass ? 0.5 * (1.0 + a) * std::sqrt(2.0 - 2.0 * c) / den : (1.0 - a) / den;
        return static_cast<float>(mag);
    }

    float targetDelaySamples() const noexcept {
        return std::clamp(static_cast<float>(delayMs_ * 0.001 * sampleRate_), 2.0f,
                          static_cast<float>(maxDelaySamples_));
    }

    float feedbackPath(int ch, float wet) noexcept {
        // TODO: wet → highpass_.processSample(ch, ·) → lowpass_.processSample(ch, ·)
        //       → (if saturate_) tanh(drive_ · v) / drive_ → × feedback_
        (void)ch;
        return feedback_ * wet; // for now: no filtering and no saturation (the checks will FAIL)
    }

    FractionalDelayLine line_;
    OnePole lowpass_, highpass_;
    SmoothedValue delaySamples_;
    double sampleRate_ = 48000.0;
    int numChannels_ = 2;
    int maxDelaySamples_ = 0;
    float maxDelayMs_ = 2000.0f;
    float delayMs_ = 375.0f;
    float feedback_ = 0.5f;
    float mix_ = 0.35f;
    float lowpassHz_ = 2500.0f;
    float highpassHz_ = 120.0f;
    float drive_ = 1.5f;
    bool saturate_ = true;
    bool prepared_ = false;
};

void processInBlocks(TapeDelay& d, dawlab::AudioBuffer& b) {
    dawlab::AudioBlock whole(b);
    for (int start = 0; start < b.getNumSamples(); start += kBlockSize)
        d.process(whole.getSubBlock(start, std::min(kBlockSize, b.getNumSamples() - start)));
}

// Use a describing function to predict the self-oscillation amplitude.
// The signal going around the loop is approximated by a sine. If the tanh input is X·sin θ,
// the amplitude of its fundamental output is
//     F1(X) = (2/π) ∫₀^π tanh(X sin θ) sin θ dθ        (F1 ≈ X for small signals, → 4/π for large ones)
// The "fundamental" gain of one trip around the loop = G · F1(X)/X, where G = fb·|HP|·|LP| is the
// linear gain of the loop (drive is multiplied before the tanh and divided after, so it cancels).
// Steady oscillation <=> this gain is exactly 1: F1(X)/X = 1/G. F1(X)/X decreases monotonically
// from 1, so we solve for X by bisection.
// The loop signal (= wet) is the output of fb·tanh(·)/drive, so its peak = fb·tanh(X)/drive.
double fundamentalOfTanh(double X) {
    const int steps = 2000;
    double sum = 0.0;
    for (int k = 0; k < steps; ++k) {
        const double th = dawlab::kPi * (k + 0.5) / steps; // midpoint rule
        sum += std::tanh(X * std::sin(th)) * std::sin(th);
    }
    return 2.0 / dawlab::kPi * sum * (dawlab::kPi / steps);
}

double selfOscillationPeak(double fb, double loopGain, double drive) {
    const double target = 1.0 / loopGain; // must be < 1 (the loop self-oscillates only if its linear gain is > 1)
    double lo = 1.0e-6, hi = 50.0;
    for (int i = 0; i < 60; ++i) {
        const double mid = 0.5 * (lo + hi);
        if (fundamentalOfTanh(mid) / mid > target)
            lo = mid; // gain still too high -> the amplitude must be larger
        else
            hi = mid;
    }
    const double X = 0.5 * (lo + hi);
    return fb * std::tanh(X) / drive;
}

} // namespace

int main() {
    std::printf("=== Chapter 09, Exercise 4 (starter): tape-style delay ===\n\n");

    // (a) The highs of echo 2 should be much smaller than those of echo 1
    {
        const float delayMs = 200.0f;
        const int D = dawlab::msToSamples(delayMs, kSampleRate);
        TapeDelay tape;
        tape.setMaxDelayMs(500.0f);
        tape.prepare(kSampleRate, kBlockSize, 1);
        tape.setDelayMs(delayMs);
        tape.setFeedback(0.7f);
        tape.setLowpassHz(2000.0f);
        tape.setHighpassHz(100.0f);
        tape.setDrive(0.5f);
        tape.setMix(1.0f);
        tape.reset();
        dawlab::AudioBuffer ir = dawlab::makeImpulse(dawlab::secondsToSamples(1.0, kSampleRate), 1);
        processInBlocks(tape, ir);
        const float* y = ir.getReadPointer(0);
        // Centre the analysis window on the echo (goertzelAmplitude applies a Hann window, zero at both ends)
        const double rLo = dawlab::goertzelAmplitude(y + 3 * D / 2, D, 500.0, kSampleRate) /
                           dawlab::goertzelAmplitude(y + D / 2, D, 500.0, kSampleRate);
        const double rHi = dawlab::goertzelAmplitude(y + 3 * D / 2, D, 8000.0, kSampleRate) /
                           dawlab::goertzelAmplitude(y + D / 2, D, 8000.0, kSampleRate);
        std::printf("echo2/echo1: 500 Hz %.3f (theory %.3f), 8 kHz %.3f (theory %.3f)\n", rLo,
                    tape.getLoopGainAt(500.0), rHi, tape.getLoopGainAt(8000.0));
        DAWLAB_CHECK_NEAR(rLo, tape.getLoopGainAt(500.0), 0.05);
        DAWLAB_CHECK_NEAR(rHi, tape.getLoopGainAt(8000.0), 0.05);
        DAWLAB_CHECK(rHi < 0.5 * rLo);
    }

    // (b) Feedback 1.1: the tanh version is bounded and its amplitude matches the prediction
    {
        const float fb = 1.1f, drive = 2.0f;
        TapeDelay tape;
        tape.setMaxDelayMs(500.0f);
        tape.prepare(kSampleRate, kBlockSize, 1);
        tape.setDelayMs(100.0f);
        tape.setFeedback(fb);
        tape.setLowpassHz(2000.0f);
        tape.setHighpassHz(20.0f);
        tape.setDrive(drive);
        tape.setMix(1.0f);
        tape.reset();
        dawlab::AudioBuffer b(1, dawlab::secondsToSamples(3.0, kSampleRate));
        for (int i = 0; i < dawlab::secondsToSamples(0.3, kSampleRate); ++i)
            b.setSample(0, i, static_cast<float>(0.5 * std::sin(dawlab::kTwoPi * 200.0 * i / kSampleRate)));
        processInBlocks(tape, b);
        const int tailStart = dawlab::secondsToSamples(2.5, kSampleRate);
        const float tailPeak = dawlab::peakAbs(b.getReadPointer(0) + tailStart, b.getNumSamples() - tailStart);
        const double predicted = selfOscillationPeak(fb, tape.getLoopGainAt(200.0), drive);
        std::printf("feedback %.1f: overall peak %.3f, tail peak %.3f (predicted %.3f)\n", fb, dawlab::peakAbs(b), tailPeak,
                    predicted);
        DAWLAB_CHECK(dawlab::peakAbs(b) < 1.2f);
        DAWLAB_CHECK_NEAR(tailPeak, predicted, 0.03);
        DAWLAB_CHECK(!dawlab::hasNanOrInf(b));
    }
    return dawlab::test::summary("l09_ex4_starter");
}
