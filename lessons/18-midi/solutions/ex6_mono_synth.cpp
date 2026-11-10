// Exercise 6 (solution): mono, legato and glide.
//
// Build and run:  l18_ex6_solution   (ctest, label "solution")
// Your version:   exercises/starter/ex6_mono_synth.cpp -> l18_ex6_starter

#include "../exercises/ex6_mono_synth.h"

namespace ex6 {

void MonoSynth::sound(int pitch, int velocity, bool overlapping) noexcept {
    target_ = pitch;
    gain_ = minidaw::velocityGain(velocity);
    const long glideSamples = std::lround(settings_.glideSeconds * sampleRate_);
    if (overlapping && glideSamples > 0) {
        glideLeft_ = glideSamples;
        step_ = (target_ - pitch_) / static_cast<double>(glideSamples);
    } else {
        glideLeft_ = 0;
        pitch_ = target_;
    }
    if (!overlapping) {
        if (!env_.isActive())
            saw_.reset(0.0);
        env_.noteOn();
    } else if (!settings_.legato) {
        env_.noteOn();
    }
}

void MonoSynth::handle(const MidiMessage& message) noexcept {
    switch (message.kind()) {
    case Kind::NoteOn: {
        for (int i = 0; i < numKeys_; ++i)
            if (keys_[static_cast<size_t>(i)].pitch == message.note())
                return; // already down
        const bool overlapping = numKeys_ > 0;
        if (numKeys_ == kMaxKeys) {
            for (int i = 1; i < numKeys_; ++i)
                keys_[static_cast<size_t>(i - 1)] = keys_[static_cast<size_t>(i)];
            --numKeys_;
        }
        keys_[static_cast<size_t>(numKeys_++)] = {message.note(), message.velocity()};
        sound(message.note(), message.velocity(), overlapping);
        break;
    }
    case Kind::NoteOff: {
        int at = -1;
        for (int i = 0; i < numKeys_ && at < 0; ++i)
            if (keys_[static_cast<size_t>(i)].pitch == message.note())
                at = i;
        if (at < 0)
            return;
        const bool wasNewest = at == numKeys_ - 1;
        for (int i = at + 1; i < numKeys_; ++i)
            keys_[static_cast<size_t>(i - 1)] = keys_[static_cast<size_t>(i)];
        --numKeys_;
        if (numKeys_ == 0) {
            env_.noteOff();
        } else if (wasNewest) {
            const Key& back = keys_[static_cast<size_t>(numKeys_ - 1)];
            sound(back.pitch, back.velocity, true);
        }
        break;
    }
    case Kind::Controller:
        if (message.data1 == 123) {
            numKeys_ = 0;
            env_.noteOff();
        }
        break;
    case Kind::PitchBend:
    case Kind::Other: break;
    }
}

void MonoSynth::render(float* out, int numSamples) noexcept {
    for (int i = 0; i < numSamples; ++i) {
        if (glideLeft_ > 0) {
            pitch_ += step_;
            if (--glideLeft_ == 0)
                pitch_ = target_; // exactly, whatever the steps added up to
        }
        if (!env_.isActive())
            continue;
        saw_.setFrequency(440.0 * std::pow(2.0, (pitch_ - 69.0) / 12.0), sampleRate_);
        out[i] += saw_.next() * env_.next() * gain_;
    }
}

} // namespace ex6

#include "../exercises/ex6_check.h"
