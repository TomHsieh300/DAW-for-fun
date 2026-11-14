// Exercise 3 solution: Vibrato -- a modulated delay, but without mixing in the dry signal
//
// The only difference from the chorus is one thing: mix = 1 (the output is entirely the delayed
// signal, with no dry signal). Without the dry signal there is no comb filter (no peaks and
// valleys); all that remains is the pitch drifting up and down from the Doppler effect = vibrato.
//
// Delay d(t) = centre + depth·sin(2πrt)
// Pitch ratio = 1 − d'(t) = 1 − 2πr·depth·cos(2πrt)   (depth in seconds)
// Maximum deviation = ±2πr·depth; in cents: up 1200·log2(1 + 2πr·depth), down 1200·log2(1 − 2πr·depth).
//
// self-check:
//   1. 1 kHz sine, rate 5 Hz, depth 1 ms -> 2π·5·0.001 = 0.0314 -> instantaneous frequency 968.6 .. 1031.4 Hz (+53.6 / −55.3 cents)
//   2. The mean frequency is still 1000 Hz (the pitch just drifts back and forth; it does not become higher or lower overall)
//   3. The output RMS equals the input RMS (±0.3 dB): with mix = 1 there are no comb peaks and valleys, so the amplitude is unchanged
//   4. 1 kHz sine, depth 0 -> the output is just the delayed input, and the instantaneous frequency stays at 1000 Hz
//   5. centre 1 ms, depth 10 ms -> the depth in use is limited to centre − the minimum delay, so the delay never rests
//      on its clamp (without the limit it would sit there for 47% of each cycle, and the pitch sweep would go flat)
// Output: dawlab_output/l10_ex3_vibrato.wav (plucked arpeggio with vibrato, rate 5.5 Hz, depth 0.6 ms)

#include <dawlab/Common.h>
#include <dawlab/dsp/FractionalDelayLine.h>
#include <dawlab/dsp/Lfo.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

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
    static constexpr float kMinDelaySamples = 2.0f; // readCubic needs one newer sample, plus one for margin

    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        (void)maxBlockSize;
        sampleRate_ = sampleRate;
        numChannels_ = std::max(1, numChannels);
        minDelayMs_ = static_cast<float>(dawlab::samplesToMs(kMinDelaySamples, sampleRate));
        delay_.prepare(dawlab::msToSamples(kMaxCentreMs + kMaxDepthMs, sampleRate) + 8, numChannels_);
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
    // The depth in use is at most centre − the minimum delay (advanceModulation), so the delay never rests on its clamp.
    // The centre itself is just a fixed latency the ear cannot hear.
    void setCentreDelayMs(float ms) noexcept { centreMs_ = std::clamp(ms, 1.0f, kMaxCentreMs); }
    void setShape(LfoShape shape) noexcept { lfo_.setShape(shape); }

    float processSample(int channel, float x) noexcept {
        if (channel == 0)
            advanceModulation();
        delay_.push(channel, x);
        return delay_.readCubic(channel, currentDelaySamples_); // no dry signal: this is the whole difference between vibrato and chorus
    }

    void process(dawlab::AudioBlock block) noexcept {
        const int n = block.getNumSamples();
        const int nch = std::min(block.getNumChannels(), numChannels_);
        for (int i = 0; i < n; ++i) {
            advanceModulation();
            for (int ch = 0; ch < nch; ++ch) {
                float* d = block.getChannelPointer(ch);
                delay_.push(ch, d[i]);
                d[i] = delay_.readCubic(ch, currentDelaySamples_);
            }
        }
    }

private:
    void advanceModulation() noexcept {
        // The sweep stays above the minimum delay: depth is limited to centre − minimum after the smoother. Otherwise the
        // delay would rest on the clamp for part of each cycle, and the pitch sweep would go flat there.
        const float depth = std::min(depthSm_.next(), std::max(0.0f, centreMs_ - minDelayMs_));
        const float ms = centreMs_ + depth * lfo_.getNextValue();
        const float maxRead = static_cast<float>(delay_.getMaxDelaySamples() - 3);
        currentDelaySamples_ = std::clamp(static_cast<float>(ms * sampleRate_ * 0.001), kMinDelaySamples, maxRead); // a rail only
    }

    double sampleRate_ = 48000.0;
    int numChannels_ = 2;
    float centreMs_ = 5.0f;
    float minDelayMs_ = kMinDelaySamples * 1000.0f / 48000.0f;
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

// The share of one LFO cycle (rate 1 Hz) the delay spends within 0.05 sample of its shortest value, wherever a clamp
// put it. A ramp x[n] = slope·n goes in: cubic interpolation reproduces a straight line, so the delay in use is
// (x − y) / slope. A sweep that turns round at its trough spends about 1.5% of the cycle there; one that rests on a
// clamp spends far more.
double shareAtClamp(float centreMs, float depthMs) {
    Vibrato v;
    v.prepare(kSampleRate, kBlockSize, 1);
    v.setRateHz(1.0f);
    v.setCentreDelayMs(centreMs);
    v.setDepthMs(depthMs);
    v.reset();
    constexpr double slope = 1.0e-5;
    const long settle = 4000, cycle = static_cast<long>(kSampleRate);
    std::vector<double> delay;
    for (long i = 0; i < settle + cycle; ++i) {
        const float x = static_cast<float>(slope * static_cast<double>(i));
        const float y = v.processSample(0, x);
        if (i >= settle)
            delay.push_back((static_cast<double>(x) - static_cast<double>(y)) / slope);
    }
    const double shortest = *std::min_element(delay.begin(), delay.end());
    const auto atFloor = std::count_if(delay.begin(), delay.end(), [shortest](double d) { return d < shortest + 0.05; });
    return static_cast<double>(atFloor) / static_cast<double>(cycle);
}

} // namespace

int main() {
    std::printf("=== Chapter 10 Exercise 3: Vibrato ===\n\n");

    const float rate = 5.0f, depthMs = 1.0f;
    const double dev = dawlab::kTwoPi * rate * depthMs * 0.001;

    // ---- 1 + 2 + 3. 1 kHz sine -------------------------------------------------
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
        std::printf("  Theory: 1000 x (1 +/- %.4f) = %.1f .. %.1f Hz (%+.1f / %+.1f cents)\n", dev, 1000.0 * (1.0 - dev),
                    1000.0 * (1.0 + dev), 1200.0 * std::log2(1.0 + dev), 1200.0 * std::log2(1.0 - dev));
        std::printf("  Measured: %.1f .. %.1f Hz, mean %.2f Hz; RMS relative to input %+.2f dB\n", fMin, fMax, fMean,
                    dawlab::gainToDb(outRms / inRms));
        DAWLAB_CHECK_NEAR(fMax, 1000.0 * (1.0 + dev), 2.0);
        DAWLAB_CHECK_NEAR(fMin, 1000.0 * (1.0 - dev), 2.0);
        DAWLAB_CHECK_NEAR(fMean, 1000.0, 1.0);
        DAWLAB_CHECK_NEAR(dawlab::gainToDb(outRms / inRms), 0.0, 0.3);
    }

    // ---- 4. depth 0: just a fixed delay ----------------------------------------
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

    // ---- Listening ---------------------------------------------------------------
    {
        dawlab::AudioBuffer pluck = dawlab::makePluckSequence(kSampleRate, 4.0, 0.4f, 2);
        Vibrato v;
        v.prepare(kSampleRate, kBlockSize, 2);
        v.setRateHz(5.5f);
        v.setDepthMs(0.6f);
        v.setCentreDelayMs(5.0f);
        v.reset();
        processInBlocks(v, pluck);
        dawlab::writeWav(dawlab::outputPath("l10_ex3_vibrato.wav"), pluck, kSampleRate);
        std::printf("Output l10_ex3_vibrato.wav (rate 5.5 Hz, depth 0.6 ms ~ %+.1f / %+.1f cents): peak %.3f\n",
                    1200.0 * std::log2(1.0 + dawlab::kTwoPi * 5.5 * 0.0006),
                    1200.0 * std::log2(1.0 - dawlab::kTwoPi * 5.5 * 0.0006), dawlab::peakAbs(pluck));
        DAWLAB_CHECK(!dawlab::hasNanOrInf(pluck));
    }

    // ---- 5. The sweep never rests on the clamp ------------------------------------
    {
        // centre 1 ms, depth 10 ms: without the depth limit the delay would sit at 2 samples for 47% of each cycle,
        // and the pitch sweep would go flat (no vibrato at all) for that part of the cycle
        const double share = shareAtClamp(1.0f, 10.0f);
        std::printf("centre 1 ms, depth 10 ms: delay at its shortest for %.2f%% of the cycle (must be < 5%%)\n", 100.0 * share);
        DAWLAB_CHECK(share < 0.05);
    }

    return dawlab::test::summary("l10_ex3");
}
