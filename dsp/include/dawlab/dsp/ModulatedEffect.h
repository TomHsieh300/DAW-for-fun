#pragma once
// ModulatedEffect: the sample and block entry points that Chorus, Flanger and Phaser share.
//
// All three advance their modulation (LFOs and parameter smoothers) once per time step and then
// render each channel with the same modulation values. They differ only in those two steps, so
// each effect supplies them and inherits processSample() / process() from here (CRTP):
//
//     class Chorus : public ModulatedEffect<Chorus> {
//         friend class ModulatedEffect<Chorus>;
//         void advanceModulation() noexcept;               // once per time step
//         float renderSample(int channel, float x) noexcept;
//         int numChannels_;  bool prepared_;
//     };

#include <dawlab/AudioBuffer.h>

#include <algorithm>

namespace dawlab::dsp {

template <class Effect>
class ModulatedEffect {
public:
    // Per sample: at each point in time the calls must go channel 0 first, then channel 1, ...
    // (the LFOs and parameter smoothers advance one step only on channel 0; the other channels
    // share the same values)
    float processSample(int channel, float x) noexcept {
        Effect& e = self();
        if (!e.prepared_)
            return x;
        if (channel == 0)
            e.advanceModulation();
        return e.renderSample(channel, x);
    }

    void process(AudioBlock block) noexcept {
        Effect& e = self();
        const int n = block.getNumSamples();
        const int nch = std::min(block.getNumChannels(), e.numChannels_);
        if (!e.prepared_) // before prepare() the block passes through as it is: its buffers do not exist yet
            return;
        for (int i = 0; i < n; ++i) {
            e.advanceModulation();
            for (int ch = 0; ch < nch; ++ch) {
                float* d = block.getChannelPointer(ch);
                d[i] = e.renderSample(ch, d[i]);
            }
        }
    }

protected:
    ModulatedEffect() = default;

private:
    Effect& self() noexcept { return static_cast<Effect&>(*this); }
};

} // namespace dawlab::dsp
