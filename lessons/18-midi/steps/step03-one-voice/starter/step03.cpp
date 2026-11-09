// Step 3 (starter): one voice.
//
// Build and run:  l18_step03_starter   (prints [FAIL] until all nine TODOs are done)
// Reference:      ../solution/step03.cpp, docs/step-03-one-voice.md ("Your turn")
//
// Saw, Adsr, velocityGain and Voice are in ../minidaw.h.

#include "../minidaw.h"

namespace minidaw {

double noteFrequency(int pitch, double bendSemitones) noexcept {
    // TODO 1: 440 x 2^((pitch - 69 + bend) / 12).
    (void)pitch;
    (void)bendSemitones;
    return 0.0;
}

double polyBlep(double t, double dt) noexcept {
    // TODO 2: the two polynomials of the header, near the jump; 0 elsewhere.
    (void)t;
    (void)dt;
    return 0.0;
}

float Saw::next() noexcept {
    // TODO 3: 2 x phase - 1 - polyBlep(phase, dt), then advance and wrap the phase (as nextNaive() does).
    return nextNaive();
}

void Adsr::noteOn() noexcept {
    // TODO 4: The attack, from the level the envelope has now.
}

void Adsr::noteOff() noexcept {
    // TODO 5: Unless Idle: the release, with a step of level / releaseSamples_.
}

float Adsr::next() noexcept {
    // TODO 6: Return the level, then move it one step in the current stage, clamped at the stage's target,
    //         and change the stage there.
    (void)attackSamples_;
    (void)decaySamples_;
    (void)sustain_;
    (void)releaseStep_;
    return static_cast<float>(level_);
}

void Voice::start(int pitch, int velocity) noexcept {
    // TODO 7: If the voice is silent: reset the saw (phase 0) and the filter. Then the pitch, the gain
    //         (velocityGain), the saw's frequency, the envelope's note on.
    (void)pitch;
    (void)velocity;
}

void Voice::release() noexcept {
    // TODO 8: The envelope's note off.
}

void Voice::render(float* out, int numSamples) noexcept {
    // TODO 9: While the envelope is active: out[i] += filter(saw) x envelope x gain.
    (void)out;
    (void)numSamples;
    (void)sampleRate_;
    (void)gain_;
}

} // namespace minidaw

#include "../check.h"
