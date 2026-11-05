// Step 2 (solution): where the take belongs.
//
// Build and run:  l17_step02_solution   (ctest, label "solution")
// Your version:   starter/step02.cpp -> l17_step02_starter

#include "../minidaw.h"

#include <cmath>

namespace minidaw {

Placement place(std::int64_t stampedPosition, const Latencies& latencies, std::int64_t numFrames) noexcept {
    const std::int64_t start = stampedPosition - latencies.total();
    // Frames from before the timeline's zero are not part of any song.
    const std::int64_t skip = std::min(start < 0 ? -start : 0, numFrames);
    return {start < 0 ? 0 : start, skip, numFrames - skip};
}

std::int64_t measureRoundTrip(const float* take, std::int64_t numFrames, std::int64_t stampedPosition,
                              std::int64_t clickPosition, float minPeak) noexcept {
    float peak = 0.0f;
    for (std::int64_t i = 0; i < numFrames; ++i)
        peak = std::max(peak, std::abs(take[i]));
    if (peak < minPeak)
        return -1;
    for (std::int64_t i = 0; i < numFrames; ++i) {
        if (std::abs(take[i]) >= 0.5f * peak) {
            const std::int64_t roundTrip = stampedPosition + i - clickPosition;
            return roundTrip >= 0 ? roundTrip : -1;
        }
    }
    return -1;
}

bool Recorder::start(const Latencies& device, int numChannels, int ringFrames) {
    if (!isIdle())
        return false;
    // Idle: the audio thread stays away from everything below until it sees Armed.
    ring_.prepare(numChannels, ringFrames);
    latencies_ = device;
    started_ = false;
    state_.store(State::Armed, std::memory_order_release);
    return true;
}

void Recorder::stop() noexcept {
    // The audio thread may be changing the state at this moment: Armed to Recording, or Recording to
    // Idle. In that order the two tries cannot miss a state that still needs stopping.
    State expected = State::Armed;
    if (!state_.compare_exchange_strong(expected, State::Stopping, std::memory_order_acq_rel)) {
        expected = State::Recording;
        state_.compare_exchange_strong(expected, State::Stopping, std::memory_order_acq_rel);
    }
}

void Recorder::process(const float* const* input, int numInputChannels, int n, std::int64_t position, bool playing,
                       int mixLatency) noexcept {
    State state = state_.load(std::memory_order_acquire);
    if (state == State::Idle)
        return;
    if (state == State::Stopping) {
        state_.store(State::Idle, std::memory_order_release);
        return;
    }
    if (state == State::Armed) {
        if (!playing)
            return;
        if (!state_.compare_exchange_strong(state, State::Recording, std::memory_order_acq_rel))
            return; // stop() came first; the next block confirms it
        latencies_.mix = mixLatency;
        stampedPosition_ = position;
        nextPosition_ = position;
        started_ = true;
    } else if (!playing || position != nextPosition_) {
        // A take is one straight line: a stop or a seek ends it. If stop() made it Stopping in the
        // meantime, this store does what the next block would have done.
        state_.store(State::Idle, std::memory_order_release);
        return;
    }
    ring_.push(input, numInputChannels, n);
    nextPosition_ += n;
}

} // namespace minidaw

#include "../check.h"
