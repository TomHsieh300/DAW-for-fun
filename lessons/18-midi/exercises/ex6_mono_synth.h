#pragma once
// Exercise 6 - Mono, legato and glide.
//
// Step 4's synth is polyphonic: every key its own voice. A bass line or a lead is often played on ONE voice,
// as on the monophonic synthesizers of the 1970s, and that voice does three things a polyphonic one cannot:
//
//   Last-note priority  The keys that are down form a stack. The newest one sounds; when it goes up, the newest
//                       of the keys still down sounds again. Only when no key is down does the note end. A key
//                       that goes up while it is not the one sounding changes nothing.
//   Legato              A new note while another key is down does NOT restart the envelope: the sound goes on
//                       and only the pitch changes, the way a singer slurs two notes. With legato off, every new
//                       note restarts the attack (from the level it is at, as Step 3's Adsr does).
//   Glide               The pitch slides to the new note instead of jumping: in a straight line in semitones,
//                       over glideSeconds, whatever the interval. A new note during a slide starts a new slide from
//                       where the pitch is. Glide happens only between notes that overlap (another key was down);
//                       a note that starts from silence starts at its own pitch.
//
// The velocity of the note that sounds sets the gain (Step 3's velocityGain). Controller 123 (all notes off)
// forgets every key and releases the note. The keys are kept by pitch, at most kMaxKeys: a key already down is
// not added again, and one key too many pushes out the oldest.
//
// The voice is Step 3's: Saw and Adsr (the solved ones of steps/step04-polyphony/minidaw.h), without a filter.

#include "../steps/step04-polyphony/minidaw.h"

#include <array>
#include <cstdint>

namespace ex6 {

using minidaw::Adsr;
using minidaw::Kind;
using minidaw::MidiMessage;

struct MonoSettings {
    bool legato = true;
    double glideSeconds = 0.05;
    double attack = 0.005, decay = 0.2, sustain = 0.6, release = 0.3;
};

class MonoSynth {
public:
    static constexpr int kMaxKeys = 16;

    // ---- provided -----------------------------------------------------------------------------------------
    void prepare(double sampleRate, const MonoSettings& settings) noexcept {
        sampleRate_ = sampleRate;
        settings_ = settings;
        env_.set(sampleRate, settings.attack, settings.decay, settings.sustain, settings.release);
    }
    // The pitch now, in semitones (MIDI note numbers, with fractions while it glides).
    double pitch() const noexcept { return pitch_; }
    // The note the pitch goes to (or is at); -1 before the first note.
    int target() const noexcept { return target_; }
    float gain() const noexcept { return gain_; }
    const Adsr& envelope() const noexcept { return env_; }
    int numKeys() const noexcept { return numKeys_; }

    // ---- YOUR TURN (starter/ex6_mono_synth.cpp) -------------------------------------------------------------
    void handle(const MidiMessage& message) noexcept;
    // Adds numSamples to `out`: per sample, the pitch moves one step of its slide (and stops at the target),
    // the saw follows it, and the sample is saw x envelope x gain.
    void render(float* out, int numSamples) noexcept;

private:
    // The note `pitch` with `velocity` starts sounding, from silence (`overlapping` false) or out of another note.
    void sound(int pitch, int velocity, bool overlapping) noexcept;

    double sampleRate_ = 48000.0;
    MonoSettings settings_;
    minidaw::Saw saw_;
    Adsr env_;
    struct Key {
        int pitch = 0;
        int velocity = 0;
    };
    std::array<Key, kMaxKeys> keys_{}; // oldest first: the last one is the newest
    int numKeys_ = 0;
    double pitch_ = 0.0;
    int target_ = -1;
    double step_ = 0.0;      // semitones per sample while it glides
    long glideLeft_ = 0;     // samples of the slide still to go
    float gain_ = 0.0f;
};

} // namespace ex6
