// Step 4 (starter): punch and loop.
//
// Build and run:  l17_step04_starter   (prints [FAIL] until all four TODOs are done)
// Reference:      ../solution/step04.cpp, docs/step-04-punch-and-loop.md ("Your turn")
//
// RecordRange, TimeWindow, the Recorder's other functions and everything of Steps 1 to 3 (solved) are
// in ../minidaw.h.

#include "../minidaw.h"

namespace minidaw {

TimeWindow visibleWindow(const RecordRange& range, std::int64_t stampedPosition) noexcept {
    // TODO 1: A default TimeWindow is [0, kForever). If range.loopsFrom(stampedPosition), narrow it to
    //         the loop. If range.hasPunch(), narrow it to the punch.
    (void)range;
    (void)stampedPosition;
    return {};
}

std::vector<Placement> takesOf(std::int64_t stampedPosition, const Latencies& latencies, const RecordRange& range,
                               std::int64_t numFrames, std::int64_t minFrames) {
    // TODO 2: The window from visibleWindow().
    //         heardAtZero is stampedPosition - latencies.total(). loopLength is 0 without a loop.
    //         For pass = 0, 1, 2, ..., with shift = pass * loopLength:
    //           frame i is in this pass if window.from <= heardAtZero + i - shift < window.to, so
    //             first = window.from + shift - heardAtZero, but not below 0
    //             last = window.to + shift - heardAtZero, but not above numFrames
    //             (kForever has no room for sums: then last is simply numFrames)
    //           If there are frames, and at least minFrames: { first + heardAtZero - shift, first, last - first }.
    //           Done when there is no loop, or when this pass's window reaches the end of the frames.
    (void)stampedPosition;
    (void)latencies;
    (void)range;
    (void)numFrames;
    (void)minFrames;
    return {};
}

int bestTake(const std::vector<Placement>& takes) noexcept {
    // TODO 3: The index of the longest take, the later one if two are equal. -1 if there are none.
    (void)takes;
    return -1;
}

void Recorder::process(const float* const* input, int numInputChannels, const TimelineSegment* segments, int numSegments,
                       int mixLatency) noexcept {
    // TODO 4: Load the state (acquire).
    //         Idle: return. Stopping: store Idle (release) and return.
    //         Armed: no segments: return. compare_exchange_strong Armed -> Recording; if that fails,
    //         stop() came first: return. Then set latencies_.mix, stampedPosition_ and nextPosition_ from
    //         the first segment, and started_.
    //         Recording: no segments: store Idle (release) and return.
    //         Then go through the segments. Each one has to start at nextPosition_; but if the transport
    //         loops (range_.loopsFrom(stampedPosition_)) and nextPosition_ is the loop's end, it has to
    //         start at the loop's start instead. As long as it does: add its frames, and nextPosition_ is
    //         where it ends. At the first one that does not, stop looking.
    //         Push the good frames to the ring (they are the first ones of the block), add them to
    //         streamFrames_. If a segment was wrong, store Idle (release).
    (void)input;
    (void)numInputChannels;
    (void)segments;
    (void)numSegments;
    (void)mixLatency;
}

} // namespace minidaw

#include "../check.h"
