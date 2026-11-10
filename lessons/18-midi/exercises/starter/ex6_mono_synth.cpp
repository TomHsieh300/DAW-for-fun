// Exercise 6 (starter): mono, legato and glide.
//
// Fill in the TODOs. Until then the self-check prints FAIL (expected). The solution is
// solutions/ex6_mono_synth.cpp. What the functions must do is in ../ex6_mono_synth.h.

#include "../ex6_mono_synth.h"

namespace ex6 {

void MonoSynth::sound(int pitch, int velocity, bool overlapping) noexcept {
    // TODO: the target and the gain; a slide over llround(glideSeconds x sampleRate) samples when `overlapping`
    //       (else the pitch jumps); the envelope: an attack from silence, or with legato off.
    (void)overlapping;
    target_ = pitch;
    pitch_ = pitch;
    gain_ = minidaw::velocityGain(velocity);
    env_.noteOn();
}

void MonoSynth::handle(const MidiMessage& message) noexcept {
    // TODO: note on - push the key (not twice, the oldest out when full) and sound it; note off - take the key
    //       out, then release if none is left, or sound the newest key left if it was the newest; controller 123.
    if (message.kind() == Kind::NoteOn)
        sound(message.note(), message.velocity(), false);
    else if (message.kind() == Kind::NoteOff)
        env_.noteOff();
}

void MonoSynth::render(float* out, int numSamples) noexcept {
    // TODO: per sample one step of the slide (exactly the target at its end), then the saw at the pitch's
    //       frequency, times the envelope and the gain, added to out[i].
    for (int i = 0; i < numSamples; ++i) {
        if (!env_.isActive())
            continue;
        saw_.setFrequency(440.0 * std::pow(2.0, (pitch_ - 69.0) / 12.0), sampleRate_);
        out[i] += saw_.next() * env_.next() * gain_;
    }
}

} // namespace ex6

#include "../ex6_check.h"
