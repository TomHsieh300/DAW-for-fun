// Exercise 3 starter: Vibrato -- a modulated delay, but without mixing in the dry signal
//
// Goal: write a Vibrato class that uses FractionalDelayLine + Lfo to get a "pitch drifts up and
// down, amplitude unchanged" effect. The only difference from the chorus: the output is entirely
// the delayed signal (mix = 1) with no dry signal -> no comb peaks and valleys.
//
// Delay d(t) = centre + depth·lfo(t)
// Pitch ratio = 1 − d'(t), maximum deviation = ±2πr·depth (depth in seconds)
//
// This program compiles; when run, the self-check prints FAIL because the TODOs are not done yet
// (this is expected).
// When finished you should see: 1 kHz sine, rate 5 Hz, depth 1 ms -> instantaneous frequency
// 968.6 .. 1031.4 Hz, RMS unchanged.

#include <dawlab/Common.h>
#include <dawlab/dsp/FractionalDelayLine.h>
#include <dawlab/dsp/Lfo.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

using dawlab::dsp::FractionalDelayLine;
using dawlab::dsp::Lfo;
using dawlab::dsp::LfoShape;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

class Vibrato {
public:
    static constexpr float kMaxCentreMs = 20.0f;
    static constexpr float kMaxDepthMs = 10.0f;

    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        (void)maxBlockSize;
        sampleRate_ = sampleRate;
        numChannels_ = std::max(1, numChannels);
        // TODO 1: delay_.prepare(...) -- the longest delay = kMaxCentreMs + kMaxDepthMs (converted to samples), plus a few spare samples for interpolation
        lfo_.prepare(sampleRate);
        depthSm_.prepare(sampleRate, 20.0);
        reset();
    }

    void reset() noexcept {
        delay_.reset();
        lfo_.reset();
        depthSm_.setCurrentAndTarget(depthSm_.getTarget());
    }

    void setRateHz(float hz) noexcept { lfo_.setRateHz(std::clamp(hz, 0.0f, 20.0f)); }
    void setDepthMs(float ms) noexcept { depthSm_.setTarget(std::clamp(ms, 0.0f, kMaxDepthMs)); }
    void setCentreDelayMs(float ms) noexcept { centreMs_ = std::clamp(ms, 1.0f, kMaxCentreMs); }
    void setShape(LfoShape shape) noexcept { lfo_.setShape(shape); }

    float processSample(int channel, float x) noexcept {
        if (channel == 0)
            advanceModulation();
        // TODO 3: push into the delay line, then return the readCubic read at currentDelaySamples_ (do not mix in x!)
        return x;
    }

    void process(dawlab::AudioBlock block) noexcept {
        const int n = block.getNumSamples();
        const int nch = std::min(block.getNumChannels(), numChannels_);
        for (int i = 0; i < n; ++i) {
            advanceModulation();
            for (int ch = 0; ch < nch; ++ch) {
                float* d = block.getChannelPointer(ch);
                // TODO 3: same as above
                (void)ch;
                (void)d;
            }
        }
    }

private:
    void advanceModulation() noexcept {
        const float depth = depthSm_.next();
        // TODO 2: ms = centreMs_ + depth · lfo_.getNextValue(); convert to samples, clamp to [2, maxDelay − 3], and store in currentDelaySamples_
        (void)depth;
    }

    double sampleRate_ = 48000.0;
    int numChannels_ = 2;
    float centreMs_ = 5.0f;
    float currentDelaySamples_ = 240.0f;
    FractionalDelayLine delay_;
    Lfo lfo_;
    dawlab::dsp::detail::ModParamSmoother depthSm_{1.0f};
};

template <typename Effect>
void processInBlocks(Effect& fx, dawlab::AudioBuffer& buffer) {
    dawlab::AudioBlock whole(buffer);
    for (int start = 0; start < buffer.getNumSamples(); start += kBlockSize) {
        const int n = std::min(kBlockSize, buffer.getNumSamples() - start);
        fx.process(whole.getSubBlock(start, n));
    }
}

// Estimate the instantaneous frequency from the spacing between adjacent upward zero crossings: returns the minimum, maximum, and mean
void instantaneousFrequency(const float* x, int n, int skip, double& fMin, double& fMax, double& fMean) {
    fMin = 1.0e9;
    fMax = 0.0;
    double lastCross = -1.0, sum = 0.0;
    int count = 0;
    for (int i = std::max(1, skip); i < n; ++i) {
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) {
            const double cross = (i - 1) + double(-x[i - 1]) / (double(x[i]) - x[i - 1]);
            if (lastCross >= 0.0) {
                const double f = kSampleRate / (cross - lastCross);
                fMin = std::min(fMin, f);
                fMax = std::max(fMax, f);
                sum += f;
                ++count;
            }
            lastCross = cross;
        }
    }
    fMean = count > 0 ? sum / count : 0.0;
}

} // namespace

int main() {
    std::printf("=== Chapter 10 Exercise 3: Vibrato (starter) ===\n\n");

    const float rate = 5.0f, depthMs = 1.0f;
    const double dev = dawlab::kTwoPi * rate * depthMs * 0.001;

    {
        dawlab::AudioBuffer tone = dawlab::makeSine(1000.0, kSampleRate, 3.0, 0.5f, 1);
        const int skip = dawlab::secondsToSamples(0.5, kSampleRate);
        const float inRms = dawlab::rms(tone.getReadPointer(0) + skip, tone.getNumSamples() - skip);

        Vibrato v;
        v.prepare(kSampleRate, kBlockSize, 1);
        v.setRateHz(rate);
        v.setDepthMs(depthMs);
        v.setCentreDelayMs(5.0f);
        v.reset();
        processInBlocks(v, tone);

        double fMin, fMax, fMean;
        instantaneousFrequency(tone.getReadPointer(0), tone.getNumSamples(), skip, fMin, fMax, fMean);
        const float outRms = dawlab::rms(tone.getReadPointer(0) + skip, tone.getNumSamples() - skip);
        std::printf("1 kHz sine, rate %.1f Hz, depth %.1f ms:\n", rate, depthMs);
        std::printf("  Theory: %.1f .. %.1f Hz    Measured: %.1f .. %.1f Hz, mean %.2f Hz; RMS %+.2f dB\n",
                    1000.0 * (1.0 - dev), 1000.0 * (1.0 + dev), fMin, fMax, fMean, dawlab::gainToDb(outRms / inRms));
        DAWLAB_CHECK_NEAR(fMax, 1000.0 * (1.0 + dev), 2.0);
        DAWLAB_CHECK_NEAR(fMin, 1000.0 * (1.0 - dev), 2.0);
        DAWLAB_CHECK_NEAR(fMean, 1000.0, 1.0);
        DAWLAB_CHECK_NEAR(dawlab::gainToDb(outRms / inRms), 0.0, 0.3);
    }

    // depth 0: just a fixed delay, so the instantaneous frequency should stay at 1000 Hz
    {
        dawlab::AudioBuffer tone = dawlab::makeSine(1000.0, kSampleRate, 2.0, 0.5f, 1);
        Vibrato v;
        v.prepare(kSampleRate, kBlockSize, 1);
        v.setRateHz(rate);
        v.setDepthMs(0.0f);
        v.reset();
        processInBlocks(v, tone);
        double fMin, fMax, fMean;
        instantaneousFrequency(tone.getReadPointer(0), tone.getNumSamples(),
                               dawlab::secondsToSamples(0.5, kSampleRate), fMin, fMax, fMean);
        std::printf("depth 0: instantaneous frequency %.2f .. %.2f Hz (should stay at 1000)\n\n", fMin, fMax);
        DAWLAB_CHECK_NEAR(fMin, 1000.0, 0.5);
        DAWLAB_CHECK_NEAR(fMax, 1000.0, 0.5);
    }

    {
        dawlab::AudioBuffer pluck = dawlab::makePluckSequence(kSampleRate, 4.0, 0.4f, 2);
        Vibrato v;
        v.prepare(kSampleRate, kBlockSize, 2);
        v.setRateHz(5.5f);
        v.setDepthMs(0.6f);
        v.reset();
        processInBlocks(v, pluck);
        dawlab::writeWav(dawlab::outputPath("l10_ex3_vibrato.wav"), pluck, kSampleRate);
        DAWLAB_CHECK(!dawlab::hasNanOrInf(pluck));
    }

    return dawlab::test::summary("l10_ex3");
}
