// Exercise 6 starter: switching the delay time with a crossfade
//
// Goal: two read heads, A and B. When a new delay time arrives, put the "head not in use" at the
// new position, use a raised cosine over crossfadeMs to fade the output out of the old head and
// into the new one, and swap roles when it finishes. Switching this way has neither a click
// (the problem with jumping directly) nor a Doppler pitch shift (the problem with a smooth ramp).
//
//   x ──►(+)──► delay line ──┬── head A (D_old) ──×gA──┐
//         ▲                  │                          (+)──► wet
//         │                  └── head B (D_new) ──×gB──┘
//         └──────────── ×fb ◄───────────────────────────┘
//
// After completing the TODOs in setDelayMs() / process(), run l09_ex6_starter; all the checks should pass.
// The starter code as given "jumps straight to the new value", so it will measure a click.

#include <dawlab/Common.h>
#include <dawlab/dsp/FractionalDelayLine.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

using dawlab::dsp::FractionalDelayLine;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr double kToneHz = 440.0;

class CrossfadeDelay {
public:
    static constexpr int kMaxChannels = 2;

    void setMaxDelayMs(float ms) noexcept { maxDelayMs_ = std::max(1.0f, ms); }

    void prepare(double sampleRate, int /*maxBlockSize*/, int numChannels) {
        sampleRate_ = sampleRate;
        numChannels_ = std::clamp(numChannels, 1, kMaxChannels);
        maxDelaySamples_ = dawlab::msToSamples(maxDelayMs_, sampleRate) + 2;
        line_.prepare(maxDelaySamples_, numChannels_);
        prepared_ = true;
        setCrossfadeMs(crossfadeMs_);
        reset();
    }

    void reset() noexcept {
        line_.reset();
        // A reset does not need a crossfade (the buffer has been cleared): both read heads are
        // aligned straight to the current target. Otherwise the order prepare → setDelayMs →
        // reset would throw away a fade that has not finished and leave us stuck at the old delay.
        if (prepared_)
            headDelay_[0] = headDelay_[1] = toSamples(delayMs_);
        active_ = 0;
        fadePos_ = -1;
        pending_ = false;
    }

    void setDelayMs(float ms) noexcept {
        delayMs_ = std::clamp(ms, 0.0f, maxDelayMs_);
        if (!prepared_)
            return;
        // TODO: if a fade is in progress (fadePos_ >= 0), set pending_ = true and wait for it to finish;
        //       otherwise set headDelay_[1 - active_] to toSamples(delayMs_) and fadePos_ = 0 to start the crossfade.
        headDelay_[active_] = toSamples(delayMs_); // for now: jump directly (will click)
    }

    void setCrossfadeMs(float ms) noexcept {
        crossfadeMs_ = std::max(0.0f, ms);
        if (prepared_)
            fadeLength_ = std::max(1, dawlab::msToSamples(crossfadeMs_, sampleRate_));
    }
    void setFeedback(float fb) noexcept { feedback_ = std::clamp(fb, 0.0f, 0.99f); }
    void setMix(float mix) noexcept { mix_ = std::clamp(mix, 0.0f, 1.0f); }
    bool isFading() const noexcept { return fadePos_ >= 0; }

    void process(dawlab::AudioBlock block) noexcept {
        const int n = block.getNumSamples();
        const int nch = std::min(block.getNumChannels(), numChannels_);
        for (int i = 0; i < n; ++i) {
            // TODO: if fadePos_ >= 0: t = fadePos_ / fadeLength_, gB = ½(1 − cos πt), gA = 1 − gB,
            //       wet = gA · head[active_] + gB · head[1 − active_];
            //       when fadePos_ reaches fadeLength_, swap active_, set fadePos_ = −1, and handle pending_.
            for (int ch = 0; ch < nch; ++ch) {
                float* data = block.getChannelPointer(ch);
                const float x = data[i];
                const float wet = line_.readCubic(ch, headDelay_[active_] - 1.0f);
                line_.push(ch, x + feedback_ * wet);
                data[i] = (1.0f - mix_) * x + mix_ * wet;
            }
        }
    }

private:
    float toSamples(float ms) const noexcept {
        return std::clamp(static_cast<float>(ms * 0.001 * sampleRate_), 2.0f, static_cast<float>(maxDelaySamples_));
    }

    FractionalDelayLine line_;
    double sampleRate_ = 48000.0;
    int numChannels_ = 2;
    int maxDelaySamples_ = 0;
    float maxDelayMs_ = 2000.0f;
    float delayMs_ = 250.0f;
    float crossfadeMs_ = 20.0f;
    float feedback_ = 0.0f;
    float mix_ = 1.0f;
    float headDelay_[2] = {2.0f, 2.0f};
    int active_ = 0;
    int fadePos_ = -1;
    int fadeLength_ = 1;
    bool pending_ = false;
    bool prepared_ = false;
};

double estimateFrequency(const float* x, int n) {
    int first = -1, last = -1, count = 0;
    for (int i = 1; i < n; ++i) {
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) {
            if (first < 0)
                first = i;
            last = i;
            ++count;
        }
    }
    return count < 2 ? 0.0 : (count - 1) * kSampleRate / static_cast<double>(last - first);
}

float maxSampleStep(const float* x, int n) {
    float m = 0.0f;
    for (int i = 1; i < n; ++i)
        m = std::max(m, std::abs(x[i] - x[i - 1]));
    return m;
}

} // namespace

int main() {
    std::printf("=== Chapter 09, Exercise 6 (starter): switching the delay time with a crossfade ===\n\n");
    const int switchAt = dawlab::secondsToSamples(1.0, kSampleRate);
    const double sineMaxStep = 0.5 * dawlab::kTwoPi * kToneHz / kSampleRate;

    dawlab::AudioBuffer tone = dawlab::makeSine(kToneHz, kSampleRate, 3.0, 0.5f, 1);
    dawlab::AudioBuffer out = tone;
    CrossfadeDelay d;
    d.setMaxDelayMs(1000.0f);
    d.prepare(kSampleRate, kBlockSize, 1);
    d.setCrossfadeMs(20.0f);
    d.setDelayMs(10.0f);
    d.setMix(1.0f);
    d.reset();
    dawlab::AudioBlock whole(out);
    // 48000 is not a multiple of 512: split the block at the switch point, using the old value
    // for the first part and the new value for the second
    int start = 0;
    while (start < out.getNumSamples()) {
        if (start == switchAt)
            d.setDelayMs(250.0f);
        int n = std::min(kBlockSize, out.getNumSamples() - start);
        if (start < switchAt && switchAt < start + n)
            n = switchAt - start;
        d.process(whole.getSubBlock(start, n));
        start += n;
    }

    const float step = maxSampleStep(out.getReadPointer(0) + switchAt - 2, dawlab::secondsToSamples(0.1, kSampleRate));
    const double f = estimateFrequency(out.getReadPointer(0) + switchAt - 2400, 4800);
    std::printf("Largest adjacent-sample difference within 100 ms after the switch %.4f (the sine itself %.4f); measured %.1f Hz across the switch\n", step,
                sineMaxStep, f);
    DAWLAB_CHECK(step < 1.5 * sineMaxStep);
    DAWLAB_CHECK_NEAR(f, kToneHz, 6.0); // the two heads differ in phase, so the phase slips slightly during the crossfade (see the solution notes)

    float maxErr = 0.0f;
    for (int n = switchAt + 960 + 8; n < out.getNumSamples(); ++n)
        maxErr = std::max(maxErr, std::abs(out.getSample(0, n) - tone.getSample(0, n - 12000)));
    std::printf("Largest error between y[n] and x[n - 12000] after the fade ends: %.2e\n", maxErr);
    DAWLAB_CHECK(maxErr < 1.0e-5f);
    DAWLAB_CHECK(!dawlab::hasNanOrInf(out));

    // ---- 4. A second change arriving during the crossfade: it must be queued, not lost ----
    {
        CrossfadeDelay q;
        q.setMaxDelayMs(1000.0f);
        q.prepare(kSampleRate, kBlockSize, 1);
        q.setCrossfadeMs(20.0f);
        q.setDelayMs(10.0f);
        q.setMix(1.0f);
        q.reset();
        dawlab::AudioBuffer b = dawlab::makeSine(kToneHz, kSampleRate, 1.0, 0.5f, 1);
        dawlab::AudioBlock bw(b);
        q.process(bw.getSubBlock(0, 4800)); // 100 ms warm-up
        q.setDelayMs(100.0f);               // start the fade
        q.process(bw.getSubBlock(4800, 256));
        DAWLAB_CHECK(q.isFading());
        q.setDelayMs(300.0f); // change again mid-fade: queued
        q.process(bw.getSubBlock(5056, 4800 * 3));
        DAWLAB_CHECK(!q.isFading());
        // It should end up at 300 ms
        const int d300 = dawlab::msToSamples(300.0, kSampleRate);
        float err = 0.0f;
        for (int n = 4800 * 4; n < b.getNumSamples(); ++n) {
            const float expect = static_cast<float>(0.5 * std::sin(dawlab::kTwoPi * kToneHz * (n - d300) / kSampleRate));
            err = std::max(err, std::abs(b.getSample(0, n) - expect));
        }
        std::printf("Changing the delay again mid-fade (100 -> 300 ms): it should settle at 300 ms, error %.2e\n", err);
        DAWLAB_CHECK(err < 1.0e-4f);
        DAWLAB_CHECK(maxSampleStep(b.getReadPointer(0) + 4800, 4800 * 3) < 1.5 * sineMaxStep);
    }
    return dawlab::test::summary("l09_ex6_starter");
}
