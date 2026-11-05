// Step 2 (starter): where the take belongs.
//
// Build and run:  l17_step02_starter   (prints [FAIL] until all five TODOs are done)
// Reference:      ../solution/step02.cpp, docs/step-02-placement.md ("Your turn")
//
// Latencies, Placement, Recorder and the ring of Step 1 (solved) are in ../minidaw.h.

#include "../minidaw.h"

#include <cmath>

namespace minidaw {

Placement place(std::int64_t stampedPosition, const Latencies& latencies, std::int64_t numFrames) noexcept {
    // TODO 1: The take starts at stampedPosition - latencies.total(). If that is before 0, the clip
    //         starts at 0 and skips the frames before it, but never more frames than the take has.
    (void)stampedPosition;
    (void)latencies;
    (void)numFrames;
    return {};
}

std::int64_t measureRoundTrip(const float* take, std::int64_t numFrames, std::int64_t stampedPosition,
                              std::int64_t clickPosition, float minPeak) noexcept {
    // TODO 2: Find the peak of the take (absolute values). Below minPeak: -1.
    //         Frame i of the take came in while the transport was at stampedPosition + i. Find the
    //         first frame that reaches half of the peak: the round trip is that position minus
    //         clickPosition. If it is negative, this was not our click: -1.
    (void)take;
    (void)numFrames;
    (void)stampedPosition;
    (void)clickPosition;
    (void)minPeak;
    return -1;
}

bool Recorder::start(const Latencies& device, int numChannels, int ringFrames) {
    // TODO 3: Not idle: false. Otherwise prepare the ring, store the latencies, started_ = false, and
    //         only then store Armed in the state (release). Return true.
    (void)device;
    (void)numChannels;
    (void)ringFrames;
    return false;
}

void Recorder::stop() noexcept {
    // TODO 4: Armed -> Stopping; if that did not happen, Recording -> Stopping. Each one with
    //         compare_exchange_strong: the audio thread may be changing the state at this moment.
    //         Do not store Idle here: only the audio thread can know that it has left the take.
}

void Recorder::process(const float* const* input, int numInputChannels, int n, std::int64_t position, bool playing,
                       int mixLatency) noexcept {
    // TODO 5: Load the state (acquire).
    //         Idle: return.
    //         Stopping: store Idle (release) and return. This block no longer belongs to the take.
    //         Armed: not playing: return. Otherwise compare_exchange_strong Armed -> Recording; if
    //         that fails, stop() came first: return. Then set latencies_.mix, stampedPosition_,
    //         nextPosition_ and started_. (In this order: a stop that came first must find
    //         started_ still false.)
    //         Recording: not playing, or position != nextPosition_: store Idle (release) and return.
    //         When you get here: push the block to the ring, and nextPosition_ += n.
    (void)input;
    (void)numInputChannels;
    (void)n;
    (void)position;
    (void)playing;
    (void)mixLatency;
}

} // namespace minidaw

#include "../check.h"
