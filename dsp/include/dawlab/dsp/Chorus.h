#pragma once
// Chorus: a short delay (5-30 ms) modulated by an LFO and added to the dry signal, giving the
// thickness of "several people playing together, each slightly out of tune". Chapter 10.
//
// Signal flow (one path per voice):
//
//   x ──┬──────────────────────────────────────────(1-mix)──┐
//       │                                                     ├─► y
//       └─► FractionalDelayLine ─ read(centre + depth·lfo_v) ─(mix/N)─┘   (N voices summed)
//
// - The LFO slowly lengthens / shortens the delay time -> the read speed differs from the write
//   speed -> the pitch drifts slightly up and down (the Doppler effect).
//   d(t) = d0 + D·sin(2π r t)  =>  pitch ratio = 1 - d'(t) = 1 - 2π r D cos(2π r t)
// - The LFO start phases of the voices are spread evenly around one cycle (voice v's phase = v / N),
//   so their detuning directions differ.
// - stereo spread: when the right channel reads the same voice's LFO, the phase gets an extra
//   spread × 0.25 cycle (up to 90° apart), so the left and right detuning are out of sync ->
//   a wider stereo image (lower left/right correlation).
//
// Dependencies: Chapter 09 FractionalDelayLine (readCubic), and this chapter's Lfo.

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>
#include <dawlab/dsp/FractionalDelayLine.h>
#include <dawlab/dsp/Lfo.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace dawlab::dsp {

class Chorus {
public:
    static constexpr int kMaxVoices = 4;
    static constexpr float kMaxCentreDelayMs = 50.0f;
    static constexpr float kMaxDepthMs = 30.0f;

    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        (void)maxBlockSize; // processed sample by sample; no block scratch buffer needed
        sampleRate_ = sampleRate;
        numChannels_ = std::max(1, numChannels);

        // The only place allowed to allocate: longest delay = centre limit + depth limit, plus a few samples for interpolation
        const int maxDelay = msToSamples(kMaxCentreDelayMs + kMaxDepthMs, sampleRate) + 8;
        delay_.prepare(maxDelay, numChannels_);

        for (auto& lfo : lfos_) {
            lfo.prepare(sampleRate);
            lfo.setRateHz(rateHz_); // a setRateHz call made before prepare is not lost
        }

        centreSm_.prepare(sampleRate, 20.0);
        depthSm_.prepare(sampleRate, 20.0);
        mixSm_.prepare(sampleRate, 20.0);
        spreadSm_.prepare(sampleRate, 20.0);
        reset();
    }

    void reset() noexcept {
        delay_.reset();
        spreadVoicePhases(0.0f); // back to the initial state: voice v's start phase = v / N
        for (auto& lfo : lfos_)
            lfo.reset();
        centreSm_.setCurrentAndTarget(centreSm_.getTarget());
        depthSm_.setCurrentAndTarget(depthSm_.getTarget());
        mixSm_.setCurrentAndTarget(mixSm_.getTarget());
        spreadSm_.setCurrentAndTarget(spreadSm_.getTarget());
    }

    // ---- Parameters ----------------------------------------------------------
    void setRateHz(float hz) noexcept {
        rateHz_ = std::clamp(hz, 0.0f, 20.0f);
        for (auto& lfo : lfos_)
            lfo.setRateHz(rateHz_);
    }
    // Maximum deviation of the delay from centre (±depth).
    void setDepthMs(float ms) noexcept { depthSm_.setTarget(std::clamp(ms, 0.0f, kMaxDepthMs)); }
    void setCentreDelayMs(float ms) noexcept { centreSm_.setTarget(std::clamp(ms, 1.0f, kMaxCentreDelayMs)); }
    // A plugin / DAW often pushes every parameter once per block, so when the value has not changed
    // this must do nothing -- otherwise every block would pull the LFOs back to their start phase,
    // the delay would periodically jump back, and that becomes a nasty pitch glitch.
    // When the value really changes: voice 0 keeps running from its current phase, and only the
    // other voices are re-spread behind it (v / N cycle).
    // The other voices' delays still jump once, so this is a "set-up time" operation (make it a
    // discrete option in the UI).
    void setVoices(int voices) noexcept {
        voices = std::clamp(voices, 1, kMaxVoices);
        if (voices == voices_)
            return;
        voices_ = voices;
        spreadVoicePhases(lfos_[0].getPhase());
    }
    void setMix(float mix) noexcept { mixSm_.setTarget(std::clamp(mix, 0.0f, 1.0f)); }
    // 0 = left and right identical; 1 = the right channel's LFO is 90° off the left (phase +0.25 cycle)
    void setStereoSpread(float spread) noexcept { spreadSm_.setTarget(std::clamp(spread, 0.0f, 1.0f)); }
    // For demonstration: turn cubic off and use linear interpolation, to hear / measure the high-frequency loss
    void setUseCubicInterpolation(bool cubic) noexcept { useCubic_ = cubic; }

    float getRateHz() const noexcept { return rateHz_; }
    float getDepthMs() const noexcept { return depthSm_.getTarget(); }
    float getCentreDelayMs() const noexcept { return centreSm_.getTarget(); }
    int getVoices() const noexcept { return voices_; }
    float getMix() const noexcept { return mixSm_.getTarget(); }
    float getStereoSpread() const noexcept { return spreadSm_.getTarget(); }

    // ---- Processing ----------------------------------------------------------
    // Per sample: at each point in time the calls must go channel 0 first, then channel 1, ...
    // (the LFOs and parameter smoothers advance one step only on channel 0; the other channels
    // share the same values)
    float processSample(int channel, float x) noexcept {
        if (channel == 0)
            advanceModulation();
        return renderSample(channel, x);
    }

    void process(AudioBlock block) noexcept {
        const int n = block.getNumSamples();
        const int nch = std::min(block.getNumChannels(), numChannels_);
        for (int i = 0; i < n; ++i) {
            advanceModulation();
            for (int ch = 0; ch < nch; ++ch) {
                float* d = block.getChannelPointer(ch);
                d[i] = renderSample(ch, d[i]);
            }
        }
    }

private:
    // voice v's phase = base + v / N (Lfo::setPhase wraps it back into 0..1)
    void spreadVoicePhases(float base) noexcept {
        for (int v = 0; v < kMaxVoices; ++v)
            lfos_[static_cast<size_t>(v)].setPhase(base + static_cast<float>(v) / static_cast<float>(voices_));
    }

    // Done once per time step: smooth the parameters, grab each voice's phase, and advance the LFOs
    void advanceModulation() noexcept {
        centreMs_ = centreSm_.next();
        depthMs_ = depthSm_.next();
        mix_ = mixSm_.next();
        spread_ = spreadSm_.next();
        for (int v = 0; v < voices_; ++v) {
            auto& lfo = lfos_[static_cast<size_t>(v)];
            voicePhase_[v] = lfo.getPhase();
            lfo.getNextValue();
        }
    }

    float renderSample(int ch, float x) noexcept {
        delay_.push(ch, x); // Write first, so reading d samples back is exactly d (d = 0 reads the x just written)

        const float phaseOffset = 0.25f * spread_ * static_cast<float>(ch); // the right channel is at most 90° apart
        const float msToSamplesF = static_cast<float>(sampleRate_ * 0.001);
        const float maxRead = static_cast<float>(delay_.getMaxDelaySamples() - 3);

        float wet = 0.0f;
        for (int v = 0; v < voices_; ++v) {
            const float m = Lfo::shapeValue(LfoShape::Sine, voicePhase_[v] + phaseOffset);
            const float delayMs = centreMs_ + depthMs_ * m;
            const float delaySamples = std::clamp(delayMs * msToSamplesF, 2.0f, maxRead);
            wet += useCubic_ ? delay_.readCubic(ch, delaySamples) : delay_.readLinear(ch, delaySamples);
        }
        wet /= static_cast<float>(voices_);
        return (1.0f - mix_) * x + mix_ * wet;
    }

    double sampleRate_ = 48000.0;
    int numChannels_ = 2;
    int voices_ = 2;
    float rateHz_ = 0.8f;
    bool useCubic_ = true;

    FractionalDelayLine delay_;
    std::array<Lfo, kMaxVoices> lfos_{}; // fixed size: calling setters before prepare is also safe
    float voicePhase_[kMaxVoices] = {};

    // Defaults: centre 15 ms, depth 3 ms, mix 0.5, spread 1
    detail::ModParamSmoother centreSm_{15.0f}, depthSm_{3.0f}, mixSm_{0.5f}, spreadSm_{1.0f};
    float centreMs_ = 15.0f, depthMs_ = 3.0f, mix_ = 0.5f, spread_ = 1.0f;
};

} // namespace dawlab::dsp
