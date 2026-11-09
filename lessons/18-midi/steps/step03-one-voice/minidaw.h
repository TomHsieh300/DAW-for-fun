#pragma once
// minidaw.h - Step 3 snapshot of chapter 18: one voice.
//
// Steps 1 and 2 delivered note ons and note offs at the right samples. Now something has to sound. A voice
// of a subtractive synthesizer - the classic design since the 1970s - is three parts:
//
//   oscillator (a sawtooth: all harmonics)  ->  filter (takes the top off)  ->  amplifier (an envelope)
//
// Each part has its own way of going wrong, and each is a lesson:
//
//   - A sawtooth made by `2 * phase - 1` has a jump that is infinitely sharp. Its harmonics go on forever,
//     those above half the sample rate fold back as *aliases*: inharmonic tones that are not in the note.
//     PolyBLEP rounds the jump off with a tiny polynomial, only in the two samples next to it.
//   - An amplifier that jumps from 0 to full, or from full to 0, clicks. The ADSR envelope never jumps:
//     a new note starts from where the level is, a release starts from where it is, and no stage is
//     shorter than 1 ms.
//   - The audio thread renders it: no allocation, nothing that depends on the block size.
//
// The filter is the dsp library's StateVariableFilter (chapter 02).
//
// YOUR TURN (Step 3): noteFrequency(), polyBlep(), Saw::next(), Adsr::noteOn(), noteOff(), next(), and
// Voice::start(), release(), render() in starter/step03.cpp.

#include <dawlab/dsp/StateVariableFilter.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace minidaw {

// ---- YOUR TURN (starter/step03.cpp) ---------------------------------------------------------------------
// Equal temperament, A4 = MIDI note 69 = 440 Hz, plus a bend in semitones (fractions too).
double noteFrequency(int pitch, double bendSemitones = 0.0) noexcept;

// The PolyBLEP residual: the difference between an ideal (band-limited) step and a naive one, for a phase
// `t` in [0, 1) and a phase increment `dt` (frequency / sample rate). Not 0 only within dt of the jump:
//     t < dt:        x = t / dt         ->  2x - x^2 - 1
//     t > 1 - dt:    x = (t - 1) / dt   ->  x^2 + 2x + 1
//     otherwise      0
double polyBlep(double t, double dt) noexcept;

// A sawtooth from -1 to 1, rising, that jumps down when the phase wraps.
class Saw {
public:
    void setFrequency(double hz, double sampleRate) noexcept { dt_ = std::clamp(hz / sampleRate, 0.0, 0.5); }
    void reset(double phase = 0.0) noexcept { phase_ = phase; }
    double phase() const noexcept { return phase_; }

    // ---- YOUR TURN ----------------------------------------------------------------------------------------
    // The value at the current phase - the naive saw 2 * phase - 1, minus polyBlep(phase, dt) - then the
    // phase moves on by dt and wraps to [0, 1).
    float next() noexcept;

    // ---- provided: the naive one, for comparison ------------------------------------------------------------
    float nextNaive() noexcept {
        const auto v = static_cast<float>(2.0 * phase_ - 1.0);
        phase_ += dt_;
        if (phase_ >= 1.0)
            phase_ -= 1.0;
        return v;
    }

private:
    double phase_ = 0.0;
    double dt_ = 0.0;
};

// Attack, decay, sustain, release. Linear stages, so that the check can say exactly where they are.
class Adsr {
public:
    enum class Stage { Idle, Attack, Decay, Sustain, Release };
    static constexpr double kMinSeconds = 0.001; // no stage is shorter: shorter would click

    // Times in seconds (shorter than kMinSeconds count as kMinSeconds), sustain 0..1.
    void set(double sampleRate, double attack, double decay, double sustain, double release) noexcept {
        auto samples = [sampleRate](double s) { return std::max(s, kMinSeconds) * sampleRate; };
        attackSamples_ = samples(attack);
        decaySamples_ = samples(decay);
        sustain_ = std::clamp(sustain, 0.0, 1.0);
        releaseSamples_ = samples(release);
    }

    // ---- YOUR TURN ------------------------------------------------------------------------------------------
    // Attack from the level it is at now (a note played again while it still sounds does not start from 0).
    void noteOn() noexcept;
    // Release from the level it is at now, in any stage but Idle: it reaches 0 after the release time,
    // whatever that level was (the slope is level / releaseSamples).
    void noteOff() noexcept;
    // The level for this sample, then one step on:
    //   Attack    up by 1 / attackSamples; at 1: Decay
    //   Decay     down by (1 - sustain) / decaySamples; at sustain: Sustain
    //   Sustain   stays
    //   Release   down by the slope noteOff() worked out; at 0: Idle
    //   Idle      0
    // A stage never overshoots: the level is clamped to the stage's target when it gets there.
    float next() noexcept;

    // ---- provided -------------------------------------------------------------------------------------------
    Stage stage() const noexcept { return stage_; }
    double level() const noexcept { return level_; }
    bool isActive() const noexcept { return stage_ != Stage::Idle; }

private:
    double attackSamples_ = 480.0, decaySamples_ = 4800.0, sustain_ = 0.7, releaseSamples_ = 9600.0;
    double level_ = 0.0;
    double releaseStep_ = 0.0;
    Stage stage_ = Stage::Idle;
};

// How loud a velocity is. A square law: velocity 64 is about a quarter of 127 (-12 dB), which is how most
// synths feel.
inline float velocityGain(int velocity) noexcept {
    const float v = static_cast<float>(std::clamp(velocity, 0, 127)) / 127.0f;
    return v * v;
}

// One voice: Saw -> lowpass -> Adsr x velocity.
class Voice {
public:
    struct Settings {
        double cutoffHz = 4000.0; // the filter, at most 0.45 x the sample rate
        double resonance = 0.9;   // the filter's Q
        double attack = 0.005, decay = 0.2, sustain = 0.6, release = 0.3;
    };

    void prepare(double sampleRate, const Settings& settings) {
        sampleRate_ = sampleRate;
        settings_ = settings;
        filter_.prepare(sampleRate, 512, 1);
        filter_.setType(dawlab::dsp::StateVariableFilter::Type::Lowpass);
        filter_.setCutoff(static_cast<float>(std::min(settings.cutoffHz, 0.45 * sampleRate)));
        filter_.setQ(static_cast<float>(settings.resonance));
        env_.set(sampleRate, settings.attack, settings.decay, settings.sustain, settings.release);
    }

    // ---- YOUR TURN ------------------------------------------------------------------------------------------
    // A note: the saw at the note's frequency, the gain of the velocity, the envelope's attack. A voice that
    // was silent (Idle) starts its saw at phase 0 and its filter from rest; a voice that still sounds keeps
    // both, so the waveform does not jump.
    void start(int pitch, int velocity) noexcept;
    // The note off: the envelope's release. The voice goes on sounding until the release has ended.
    void release() noexcept;
    // ADDS numSamples to out (a voice is mixed with others). An Idle voice adds nothing at all - and costs
    // nothing.
    void render(float* out, int numSamples) noexcept;

    // ---- provided -------------------------------------------------------------------------------------------
    bool isActive() const noexcept { return env_.isActive(); }
    int pitch() const noexcept { return pitch_; }
    const Adsr& envelope() const noexcept { return env_; }

private:
    double sampleRate_ = 48000.0;
    Settings settings_;
    Saw saw_;
    dawlab::dsp::StateVariableFilter filter_;
    Adsr env_;
    float gain_ = 0.0f;
    int pitch_ = -1;
};

} // namespace minidaw
