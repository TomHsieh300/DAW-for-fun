// Exercise 4 solution: tape-style delay -- lowpass + highpass + mild saturation inside the feedback loop
//
// On every trip around the loop, a tape echo (Echoplex, Space Echo) makes the sound:
//   1. darker (high-frequency loss of the tape / head)       -> a one-pole lowpass in the feedback loop
//   2. a little thinner (transformers, coupling capacitors)  -> a one-pole highpass in the feedback loop
//   3. gently compressed / saturated by the tape             -> a tanh in the feedback loop
//
//   x ──►(+)──► delay line ──┬──► wet ──(mix)──► y
//         ▲                  │
//         └── ×fb ◄── tanh ◄── LP ◄── HP ◄┘
//
// Point 3 is the soul of a dub delay: when feedback is turned above 1, a linear delay diverges
// to infinity, but the tanh "squashes" the gain of every trip back down, and the result is a
// self-oscillation of controlled amplitude that sings by itself.
// This solution verifies two things numerically: (a) the second echo has much less high
// frequency content than the first; (b) with feedback = 1.1 the linear version diverges while
// the tape version settles at a fixed amplitude (which can be predicted with a describing function).
//
// Run: l09_ex4_solution. Writes dawlab_output/l09_ex4_tape_dry.wav, l09_ex4_tape_wet.wav, l09_ex4_dub.wav.

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
    // Deliberately allowed up to 1.5 here: feedback > 1 is the dub technique, and the saturation keeps it in its cage
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
    // drive: the gain applied to the tanh input. The output is divided by drive again, so the
    // small-signal gain stays 1 and only large signals are squashed.
    void setDrive(float drive) noexcept { drive_ = std::max(0.1f, drive); }
    void setSaturationEnabled(bool on) noexcept { saturate_ = on; }

    float getLoopGainAt(double freq) const {
        // For the README / self-checks: the linear gain of one trip around the loop, fb·|HP(f)|·|LP(f)|
        // (excluding the tanh; the HP is always in the loop)
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
                const float wet = line_.readCubic(ch, d - 1.0f); // read first, push afterwards, hence the −1
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

    // Feedback path: HP → LP → tanh → ×fb
    float feedbackPath(int ch, float wet) noexcept {
        float v = highpass_.processSample(ch, wet);
        v = lowpass_.processSample(ch, v);
        if (saturate_)
            v = std::tanh(drive_ * v) / drive_; // ≈ v for small signals; squashed to ±1/drive when |v| is large
        return feedback_ * v;
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
    std::printf("=== Chapter 09, Exercise 4: tape-style delay ===\n\n");

    // ---- (a) Darker on every trip: compare the spectra of echo 1 and echo 2 ---------------
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
        tape.setDrive(0.5f); // small drive: this part is about the effect of the filters
        tape.setMix(1.0f);
        tape.reset();

        dawlab::AudioBuffer ir = dawlab::makeImpulse(dawlab::secondsToSamples(1.0, kSampleRate), 1);
        processInBlocks(tape, ir);
        const float* y = ir.getReadPointer(0);
        // Echo k appears at n = kD. goertzelAmplitude applies a Hann window internally (zero at
        // both ends), so centre the analysis window on the echo: [kD − D/2, kD + D/2)
        const double lo1 = dawlab::goertzelAmplitude(y + D / 2, D, 500.0, kSampleRate);
        const double lo2 = dawlab::goertzelAmplitude(y + 3 * D / 2, D, 500.0, kSampleRate);
        const double hi1 = dawlab::goertzelAmplitude(y + D / 2, D, 8000.0, kSampleRate);
        const double hi2 = dawlab::goertzelAmplitude(y + 3 * D / 2, D, 8000.0, kSampleRate);
        const double rLo = lo2 / lo1, rHi = hi2 / hi1;
        std::printf("impulse -> echo 1 vs echo 2 (delay %.0f ms, fb 0.7, LP 2 kHz, HP 100 Hz):\n", delayMs);
        std::printf("   500 Hz: echo2/echo1 = %.3f (theory fb·|HP|·|LP| ~ %.3f)\n", rLo, tape.getLoopGainAt(500.0));
        std::printf("  8000 Hz: echo2/echo1 = %.3f (theory ~ %.3f)\n", rHi, tape.getLoopGainAt(8000.0));
        std::printf("  The first echo peaks at %.3f at n = %d\n", y[D], D);
        DAWLAB_CHECK_NEAR(y[D], 1.0, 1.0e-3);          // the first echo is the original, unfiltered impulse
        DAWLAB_CHECK_NEAR(rLo, tape.getLoopGainAt(500.0), 0.05);
        DAWLAB_CHECK_NEAR(rHi, tape.getLoopGainAt(8000.0), 0.05);
        DAWLAB_CHECK(rHi < 0.5 * rLo); // highs fall away much faster than lows on every trip
        std::printf("  -> only %.0f%% of the highs remain per trip, versus %.0f%% of the lows: each echo is darker than the last.\n\n", 100.0 * rHi, 100.0 * rLo);
    }

    // ---- (b) feedback > 1: the linear version diverges, the tanh version settles at a fixed amplitude ----
    {
        const float fb = 1.1f, drive = 2.0f;
        const double toneHz = 200.0;
        auto makeInput = [&] {
            // A 200 Hz sine sounds for only 0.3 s; the remaining 2.7 s let the loop run by itself
            dawlab::AudioBuffer b(1, dawlab::secondsToSamples(3.0, kSampleRate));
            const int burst = dawlab::secondsToSamples(0.3, kSampleRate);
            for (int i = 0; i < burst; ++i)
                b.setSample(0, i, static_cast<float>(0.5 * std::sin(dawlab::kTwoPi * toneHz * i / kSampleRate)));
            return b;
        };
        auto run = [&](bool saturate, float driveValue) {
            TapeDelay tape;
            tape.setMaxDelayMs(500.0f);
            tape.prepare(kSampleRate, kBlockSize, 1);
            tape.setDelayMs(100.0f);
            tape.setFeedback(fb);
            tape.setLowpassHz(2000.0f);
            tape.setHighpassHz(20.0f); // HP at its minimum of 20 Hz (still in the loop; |HP| ~ 0.995 at 200 Hz, so the effect is tiny)
            tape.setDrive(driveValue);
            tape.setSaturationEnabled(saturate);
            tape.setMix(1.0f);
            tape.reset();
            dawlab::AudioBuffer b = makeInput();
            processInBlocks(tape, b);
            return std::make_pair(b, tape.getLoopGainAt(toneHz));
        };
        auto [linear, loopGain] = run(false, drive);
        auto [tape, loopGain2] = run(true, drive);
        auto [tape1, loopGain3] = run(true, 1.0f); // drive 1: the amplitude should be twice that of drive 2
        (void)loopGain2;
        (void)loopGain3;

        const int tailStart = dawlab::secondsToSamples(2.5, kSampleRate);
        const int tailLen = dawlab::secondsToSamples(0.5, kSampleRate);
        const float linearTailPeak = dawlab::peakAbs(linear.getReadPointer(0) + tailStart, tailLen);
        const float tapeTailPeak = dawlab::peakAbs(tape.getReadPointer(0) + tailStart, tailLen);
        const double predicted = selfOscillationPeak(fb, loopGain, drive);
        std::printf("feedback = %.1f (linear loop gain %.3f > 1), 200 Hz sine sounding for only 0.3 s:\n", fb, loopGain);
        std::printf("  Linear version: overall peak %.2f, peak of the 2.5-3.0 s tail %.2f (multiplied by %.3f every 100 ms, still growing)\n",
                    dawlab::peakAbs(linear), linearTailPeak, loopGain);
        std::printf("  tanh version: overall peak %.2f, peak of the 2.5-3.0 s tail %.3f (describing function predicts %.3f)\n",
                    dawlab::peakAbs(tape), tapeTailPeak, predicted);
        DAWLAB_CHECK(loopGain > 1.0f);
        DAWLAB_CHECK(linearTailPeak > 3.0f);                 // linear: diverges
        DAWLAB_CHECK(dawlab::peakAbs(tape) < 1.2f);          // tanh: bounded
        DAWLAB_CHECK_NEAR(tapeTailPeak, predicted, 0.03);    // and lands on the amplitude the describing function predicts
        DAWLAB_CHECK(!dawlab::hasNanOrInf(tape));
        DAWLAB_CHECK(!dawlab::hasNanOrInf(linear));

        // Effect of drive on the self-oscillation amplitude: let u = drive·v, and the loop becomes
        // u <- G·tanh(u), independent of drive, so the tanh input amplitude X is determined by G
        // alone, while wet = fb·tanh(X)/drive is proportional to 1/drive.
        const float tape1TailPeak = dawlab::peakAbs(tape1.getReadPointer(0) + tailStart, tailLen);
        const double predicted1 = selfOscillationPeak(fb, loopGain, 1.0);
        std::printf("  tanh version with drive 1: tail peak %.3f (predicted %.3f); drive 1 / drive 2 = %.2f (theory 2)\n",
                    tape1TailPeak, predicted1, tape1TailPeak / tapeTailPeak);
        DAWLAB_CHECK_NEAR(tape1TailPeak, predicted1, 0.06);
        DAWLAB_CHECK_NEAR(tape1TailPeak / tapeTailPeak, 2.0, 0.1);
        tape.applyGain(0.8f);
        dawlab::writeWav(dawlab::outputPath("l09_ex4_dub.wav"), tape, kSampleRate);
        std::printf("  -> Linear feedback >= 1 always blows up; the tanh turns it into a fixed-amplitude self-oscillation (dub delay).\n");
        std::printf("Wrote dawlab_output/l09_ex4_dub.wav\n\n");
    }

    // ---- (c) Musical material -------------------------------------------------------
    {
        dawlab::AudioBuffer seq = dawlab::makePluckSequence(kSampleRate, 4.0, 0.4f);
        dawlab::AudioBuffer dry(2, dawlab::secondsToSamples(8.0, kSampleRate));
        for (int ch = 0; ch < 2; ++ch)
            dry.copyFrom(ch, 0, seq, ch, 0, seq.getNumSamples());
        dawlab::AudioBuffer wet = dry;
        TapeDelay tape;
        tape.setMaxDelayMs(2000.0f);
        tape.prepare(kSampleRate, kBlockSize, 2);
        tape.setDelayMs(375.0f); // dotted eighth at 120 BPM
        tape.setFeedback(0.6f);
        tape.setLowpassHz(2500.0f);
        tape.setHighpassHz(120.0f);
        tape.setDrive(1.5f);
        tape.setMix(0.35f);
        tape.reset();
        processInBlocks(tape, wet);
        dawlab::writeWav(dawlab::outputPath("l09_ex4_tape_dry.wav"), dry, kSampleRate);
        dawlab::writeWav(dawlab::outputPath("l09_ex4_tape_wet.wav"), wet, kSampleRate);
        std::printf("Plucked arpeggio: peak dry %.3f / tape %.3f\n", dawlab::peakAbs(dry), dawlab::peakAbs(wet));
        DAWLAB_CHECK(!dawlab::hasNanOrInf(wet));
        DAWLAB_CHECK(dawlab::peakAbs(wet) < 1.0f);
        std::printf("Wrote dawlab_output/l09_ex4_tape_dry.wav, l09_ex4_tape_wet.wav\n\n");
    }

    return dawlab::test::summary("l09_ex4");
}
