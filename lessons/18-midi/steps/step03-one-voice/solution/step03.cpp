// Chapter 18, Step 3 (solution): one voice.
//
// Build and run:  l18_step03_solution   (ctest, label "solution")
// Your version:   starter/step03.cpp -> l18_step03_starter

#include "../minidaw.h"

namespace minidaw {

double noteFrequency(int pitch, double bendSemitones) noexcept {
    return 440.0 * std::pow(2.0, (pitch - 69 + bendSemitones) / 12.0);
}

double polyBlep(double t, double dt) noexcept {
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

float Saw::next() noexcept {
    const double v = 2.0 * phase_ - 1.0 - polyBlep(phase_, dt_);
    phase_ += dt_;
    if (phase_ >= 1.0)
        phase_ -= 1.0;
    return static_cast<float>(v);
}

void Adsr::noteOn() noexcept {
    stage_ = Stage::Attack; // from the level it is at
}

void Adsr::noteOff() noexcept {
    if (stage_ == Stage::Idle)
        return;
    stage_ = Stage::Release;
    releaseStep_ = level_ / releaseSamples_;
}

float Adsr::next() noexcept {
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

void Voice::start(int pitch, int velocity) noexcept {
    if (!env_.isActive()) {
        saw_.reset(0.0);
        filter_.reset();
    }
    pitch_ = pitch;
    gain_ = velocityGain(velocity);
    saw_.setFrequency(noteFrequency(pitch), sampleRate_);
    env_.noteOn();
}

void Voice::release() noexcept {
    env_.noteOff();
}

void Voice::render(float* out, int numSamples) noexcept {
    for (int i = 0; i < numSamples && env_.isActive(); ++i)
        out[i] += filter_.processSample(0, saw_.next()) * env_.next() * gain_;
}

} // namespace minidaw

#include "../check.h"
