#pragma once
// model.h - the model of Step 4, solved, for Exercises 3 and 6.
//
// Step 4's minidaw.h (the ring, the take file, placement, punch and loop) with visibleWindow(),
// takesOf(), bestTake() and Recorder::process() defined here, exactly as in
// steps/step04-punch-and-loop/solution/step04.cpp. Include it in ONE source file per program.

#include "../steps/step04-punch-and-loop/minidaw.h"

namespace minidaw {

TimeWindow visibleWindow(const RecordRange& range, std::int64_t stampedPosition) noexcept {
    TimeWindow window;
    if (range.loopsFrom(stampedPosition)) {
        window.from = std::max(window.from, range.loopStart);
        window.to = std::min(window.to, range.loopEnd);
    }
    if (range.hasPunch()) {
        window.from = std::max(window.from, range.punchIn);
        window.to = std::min(window.to, range.punchOut);
    }
    return window;
}

std::vector<Placement> takesOf(std::int64_t stampedPosition, const Latencies& latencies, const RecordRange& range,
                               std::int64_t numFrames, std::int64_t minFrames) {
    std::vector<Placement> takes;
    const TimeWindow window = visibleWindow(range, stampedPosition);
    const std::int64_t heardAtZero = stampedPosition - latencies.total();
    const std::int64_t loopLength = range.loopsFrom(stampedPosition) ? range.loopEnd - range.loopStart : 0;

    for (std::int64_t pass = 0;; ++pass) {
        const std::int64_t shift = pass * loopLength;
        // The frames i with window.from <= heard(i) - shift < window.to.
        const std::int64_t first = std::max<std::int64_t>(window.from + shift - heardAtZero, 0);
        const std::int64_t last =
            window.to == kForever ? numFrames : std::min(window.to + shift - heardAtZero, numFrames);
        if (last > first && last - first >= minFrames)
            takes.push_back({first + heardAtZero - shift, first, last - first});
        if (loopLength == 0 || window.to + shift - heardAtZero >= numFrames)
            return takes;
    }
}

int bestTake(const std::vector<Placement>& takes) noexcept {
    int best = -1;
    for (int i = 0; i < static_cast<int>(takes.size()); ++i)
        if (best < 0 || takes[static_cast<std::size_t>(i)].numFrames >= takes[static_cast<std::size_t>(best)].numFrames)
            best = i;
    return best;
}

void Recorder::process(const float* const* input, int numInputChannels, const TimelineSegment* segments, int numSegments,
                       int mixLatency) noexcept {
    State state = state_.load(std::memory_order_acquire);
    if (state == State::Idle)
        return;
    if (state == State::Stopping) {
        state_.store(State::Idle, std::memory_order_release);
        return;
    }
    if (state == State::Armed) {
        if (numSegments == 0)
            return;
        if (!state_.compare_exchange_strong(state, State::Recording, std::memory_order_acq_rel))
            return; // stop() came first; the next block confirms it
        latencies_.mix = mixLatency;
        stampedPosition_ = segments[0].timelineStart;
        nextPosition_ = segments[0].timelineStart;
        started_ = true;
    } else if (numSegments == 0) {
        state_.store(State::Idle, std::memory_order_release);
        return;
    }

    // A wrap of the loop continues the stream. Anything else that is not the next position ends it,
    // and so does running past the loop's end: takesOf() would take those frames for a next pass.
    const bool loops = range_.loopsFrom(stampedPosition_);
    int frames = 0;
    bool unbroken = true;
    for (int s = 0; s < numSegments && unbroken; ++s) {
        const std::int64_t expected = loops && nextPosition_ == range_.loopEnd ? range_.loopStart : nextPosition_;
        unbroken = segments[s].timelineStart == expected;
        if (unbroken) {
            frames += segments[s].numSamples;
            nextPosition_ = segments[s].timelineStart + segments[s].numSamples;
        }
    }
    ring_.push(input, numInputChannels, frames);
    streamFrames_ += frames;
    if (!unbroken)
        state_.store(State::Idle, std::memory_order_release);
}

} // namespace minidaw
