#pragma once
// minidaw.h - Step 4 snapshot of chapter 18: polyphony.
//
// Step 3 made one voice. A synth plays chords, so it has a fixed number of voices and a *voice allocator*
// that decides which voice plays which note:
//
//   - A note that is already sounding is played again on its own voice (two voices on one pitch sound like
//     one note too loud, with a phasing smear).
//   - Otherwise a free voice.
//   - Otherwise one is *stolen*: the one that will be missed least - a released note before a held one, and
//     among them the one that started (or was released) longest ago.
//
// A stolen voice cannot just jump to its new note: that clicks. It fades its old note out in 2 ms and then
// starts the new one (Voice::steal, provided). The new note is 2 ms late; nobody hears that, everybody hears
// a click.
//
// The synth also follows the sustain pedal (controller 64: a note off while it is down keeps the note until
// it comes up), pitch bend (Step 1's 14 bits, +-2 semitones), and "all notes off" (controller 123).
//
// YOUR TURN (Step 4): Synth::allocate(), handle() and render() in starter/step04.cpp.

#include <dawlab/dsp/StateVariableFilter.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace minidaw {

// ---- Step 1 (solved) ----------------------------------------------------------------------------------
enum class Kind { NoteOn, NoteOff, Controller, PitchBend, Other };

struct MidiMessage {
    std::uint8_t status = 0;
    std::uint8_t data1 = 0;
    std::uint8_t data2 = 0;

    static MidiMessage noteOn(int channel, int note, int velocity) noexcept { return make(0x90, channel, note, velocity); }
    static MidiMessage noteOff(int channel, int note, int velocity = 0) noexcept { return make(0x80, channel, note, velocity); }
    static MidiMessage controller(int channel, int number, int value) noexcept { return make(0xB0, channel, number, value); }
    static MidiMessage pitchBend(int channel, int value) noexcept {
        const int v = std::clamp(value, -8192, 8191) + 8192;
        return make(0xE0, channel, v & 0x7F, v >> 7);
    }

    int channel() const noexcept { return status & 0x0F; }
    int note() const noexcept { return data1; }
    int velocity() const noexcept { return data2; }
    Kind kind() const noexcept {
        switch (status & 0xF0) {
        case 0x80: return Kind::NoteOff;
        case 0x90: return data2 == 0 ? Kind::NoteOff : Kind::NoteOn;
        case 0xB0: return Kind::Controller;
        case 0xE0: return Kind::PitchBend;
        default: return Kind::Other;
        }
    }
    int pitchBendValue() const noexcept { return ((data2 << 7) | data1) - 8192; }

private:
    static MidiMessage make(int kind, int channel, int a, int b) noexcept {
        return {static_cast<std::uint8_t>(kind | (channel & 0x0F)), static_cast<std::uint8_t>(a & 0x7F), static_cast<std::uint8_t>(b & 0x7F)};
    }
};

struct BlockEvent {
    int offset = 0;
    MidiMessage message;
};

// ---- Step 3 (solved) ----------------------------------------------------------------------------------
inline double noteFrequency(int pitch, double bendSemitones = 0.0) noexcept {
    return 440.0 * std::pow(2.0, (pitch - 69 + bendSemitones) / 12.0);
}

inline double polyBlep(double t, double dt) noexcept {
    if (t < dt) {
        const double x = t / dt;
        return x + x - x * x - 1.0;
    }
    if (t > 1.0 - dt) {
        const double x = (t - 1.0) / dt;
        return x * x + x + x + 1.0;
    }
    return 0.0;
}

class Saw {
public:
    void setFrequency(double hz, double sampleRate) noexcept { dt_ = std::clamp(hz / sampleRate, 0.0, 0.5); }
    void reset(double phase = 0.0) noexcept { phase_ = phase; }
    float next() noexcept {
        const double v = 2.0 * phase_ - 1.0 - polyBlep(phase_, dt_);
        phase_ += dt_;
        if (phase_ >= 1.0)
            phase_ -= 1.0;
        return static_cast<float>(v);
    }

private:
    double phase_ = 0.0, dt_ = 0.0;
};

class Adsr {
public:
    enum class Stage { Idle, Attack, Decay, Sustain, Release };
    static constexpr double kMinSeconds = 0.001;

    void set(double sampleRate, double attack, double decay, double sustain, double release) noexcept {
        auto samples = [sampleRate](double s) { return std::max(s, kMinSeconds) * sampleRate; };
        attackSamples_ = samples(attack);
        decaySamples_ = samples(decay);
        sustain_ = std::clamp(sustain, 0.0, 1.0);
        releaseSamples_ = samples(release);
    }
    void noteOn() noexcept { stage_ = Stage::Attack; }
    void noteOff() noexcept { releaseOver(releaseSamples_); }
    // New in Step 4: a release over a given number of samples (stealing uses a short one).
    void releaseOver(double samples) noexcept {
        if (stage_ == Stage::Idle)
            return;
        stage_ = Stage::Release;
        releaseStep_ = level_ / std::max(samples, 1.0);
    }
    float next() noexcept {
        const auto out = static_cast<float>(level_);
        switch (stage_) {
        case Stage::Attack:
            level_ += 1.0 / attackSamples_;
            if (level_ >= 1.0) {
                level_ = 1.0;
                stage_ = Stage::Decay;
            }
            break;
        case Stage::Decay:
            level_ -= (1.0 - sustain_) / decaySamples_;
            if (level_ <= sustain_) {
                level_ = sustain_;
                stage_ = Stage::Sustain;
            }
            break;
        case Stage::Release:
            level_ -= releaseStep_;
            if (level_ <= 0.0) {
                level_ = 0.0;
                stage_ = Stage::Idle;
            }
            break;
        case Stage::Sustain:
        case Stage::Idle: break;
        }
        return out;
    }
    Stage stage() const noexcept { return stage_; }
    double level() const noexcept { return level_; }
    bool isActive() const noexcept { return stage_ != Stage::Idle; }

private:
    double attackSamples_ = 480.0, decaySamples_ = 4800.0, sustain_ = 0.7, releaseSamples_ = 9600.0;
    double level_ = 0.0, releaseStep_ = 0.0;
    Stage stage_ = Stage::Idle;
};

inline float velocityGain(int velocity) noexcept {
    const float v = static_cast<float>(std::clamp(velocity, 0, 127)) / 127.0f;
    return v * v;
}

// Step 3's voice, with three additions for Step 4: a pitch bend, stealing, and what it is doing now.
class Voice {
public:
    struct Settings {
        double cutoffHz = 4000.0;
        double resonance = 0.9;
        double attack = 0.005, decay = 0.2, sustain = 0.6, release = 0.3;
    };
    static constexpr double kStealSeconds = 0.002;

    void prepare(double sampleRate, const Settings& settings) {
        sampleRate_ = sampleRate;
        filter_.prepare(sampleRate, 512, 1);
        filter_.setType(dawlab::dsp::StateVariableFilter::Type::Lowpass);
        filter_.setCutoff(static_cast<float>(std::min(settings.cutoffHz, 0.45 * sampleRate)));
        filter_.setQ(static_cast<float>(settings.resonance));
        env_.set(sampleRate, settings.attack, settings.decay, settings.sustain, settings.release);
    }

    void start(int pitch, int velocity) noexcept {
        stealing_ = false;
        if (!env_.isActive()) {
            saw_.reset(0.0);
            filter_.reset();
        }
        pitch_ = pitch;
        gain_ = velocityGain(velocity);
        saw_.setFrequency(noteFrequency(pitch, bend_), sampleRate_);
        env_.noteOn();
    }
    // A note off. If the voice was still fading out a stolen note, the new note never starts.
    void release() noexcept {
        stealing_ = false;
        env_.noteOff();
    }
    // Takes the voice for a new note: a silent voice starts it at once; a sounding one fades its note out over
    // kStealSeconds first, and starts the new one in the sample after that (inside render()).
    void steal(int pitch, int velocity) noexcept {
        if (!env_.isActive()) {
            start(pitch, velocity);
            return;
        }
        env_.releaseOver(kStealSeconds * sampleRate_);
        stealing_ = true;
        pendingVelocity_ = velocity;
        pitch_ = pitch; // the note it belongs to now
    }
    // In semitones; a sounding note follows at once.
    void setBend(double semitones) noexcept {
        bend_ = semitones;
        if (!stealing_)
            saw_.setFrequency(noteFrequency(pitch_, bend_), sampleRate_);
    }
    void render(float* out, int numSamples) noexcept {
        for (int i = 0; i < numSamples; ++i) {
            if (stealing_ && !env_.isActive())
                start(pitch_, pendingVelocity_);
            if (!env_.isActive())
                return;
            out[i] += filter_.processSample(0, saw_.next()) * env_.next() * gain_;
        }
    }

    bool isActive() const noexcept { return env_.isActive(); }
    bool isStealing() const noexcept { return stealing_; }
    // Released: the key is up (or the voice is being stolen), and it is fading.
    bool isReleasing() const noexcept { return env_.stage() == Adsr::Stage::Release; }
    int pitch() const noexcept { return pitch_; }
    double frequency() const noexcept { return noteFrequency(pitch_, bend_); }

private:
    double sampleRate_ = 48000.0;
    Saw saw_;
    dawlab::dsp::StateVariableFilter filter_;
    Adsr env_;
    float gain_ = 0.0f;
    int pitch_ = -1;
    double bend_ = 0.0;
    bool stealing_ = false;
    int pendingVelocity_ = 0;
};

// ---- Step 4: the synth ------------------------------------------------------------------------------------
class Synth {
public:
    static constexpr int kMaxVoices = 32;
    static constexpr double kBendRange = 2.0; // semitones at full bend
    static constexpr int kSustainPedal = 64;  // controller: >= 64 down, < 64 up
    static constexpr int kAllNotesOff = 123;  // controller

    void prepare(double sampleRate, int numVoices, const Voice::Settings& settings) {
        numVoices_ = std::clamp(numVoices, 1, kMaxVoices);
        for (Voice& v : voices_)
            v.prepare(sampleRate, settings);
    }

    // ---- YOUR TURN (starter/step04.cpp) ------------------------------------------------------------------
    // The voice for a new note on `pitch`:
    //   1. a voice whose note is `pitch` and that still sounds (played again: the same voice)
    //   2. else an idle voice (the first)
    //   3. else steal: among the voices that are released (fading, isReleasing(), with the key of their note
    //      up - a voice fading for a stolen note has the new note's key down), the one released longest ago;
    //      if none is, among all, the one whose note started longest ago.
    // Only the first numVoices voices count. Use the slots' `started` and `released` stamps (clock_).
    int allocate(int pitch) noexcept;
    // One MIDI message (all channels alike):
    //   note on       allocate(); a stolen voice (it was sounding, with another pitch) steal()s and counts in
    //                 stolen(); otherwise start(). The slot: key down, not sustained, started = ++clock_.
    //   note off      the sounding voice of that pitch whose key is down (a stolen note's voice has another
    //                 pitch now: nothing). Pedal down: sustained. Pedal up: release(), released = ++clock_.
    //   controller 64 down: remember it. Up: release every sustained voice (released = ++clock_).
    //   controller 123 release every voice, forget the pedal's notes and the pedal.
    //   pitch bend    value / 8192 x kBendRange semitones, to every voice (setBend), kept for new notes.
    void handle(const MidiMessage& message) noexcept;
    // Writes numSamples into out (cleared first, then every voice adds itself), cut at the events as in
    // Step 1: render all voices up to an event's offset, handle() it, go on.
    void render(const std::vector<BlockEvent>& events, float* out, int numSamples) noexcept;

    // ---- provided -----------------------------------------------------------------------------------------
    int numVoices() const noexcept { return numVoices_; }
    const Voice& voice(int i) const noexcept { return voices_[static_cast<size_t>(i)]; }
    int activeVoices() const noexcept {
        int n = 0;
        for (int i = 0; i < numVoices_; ++i)
            n += voices_[static_cast<size_t>(i)].isActive() ? 1 : 0;
        return n;
    }
    bool pedalDown() const noexcept { return pedal_; }
    double bendSemitones() const noexcept { return bend_; }
    long stolen() const noexcept { return stolen_; }

private:
    struct Slot {
        bool keyDown = false;   // its note's key is held
        bool sustained = false; // its key is up, but the pedal holds it
        long started = 0;       // clock_ when its note started
        long released = 0;      // clock_ when it was released
    };
    std::array<Voice, kMaxVoices> voices_{};
    std::array<Slot, kMaxVoices> slots_{};
    int numVoices_ = 8;
    long clock_ = 0;
    bool pedal_ = false;
    double bend_ = 0.0;
    long stolen_ = 0;
};

} // namespace minidaw
