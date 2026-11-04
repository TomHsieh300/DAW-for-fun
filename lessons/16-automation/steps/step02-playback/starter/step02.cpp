// Step 2 (starter): the curve on the audio thread.
//
// Build and run:  l16_step02_starter   (prints [FAIL] until all four TODOs are done)
// Reference:      ../solution/step02.cpp, docs/step-02-playback.md ("Your turn")
//
// AutomationCurve (Step 1, solved), CurveCursor, MiniTransport and AutomationLane are in ../minidaw.h.

#include "../minidaw.h"

namespace minidaw {

int CurveCursor::locate(const AutomationCurve& curve, std::int64_t position) noexcept {
    // TODO 1: `segment` is right if it is a possible index for THIS curve (-1 .. size() - 1), point
    //         `segment` is at or before the position (or segment is -1), and point `segment` + 1 is
    //         after it (or segment is the last point). Then return it. Otherwise ++searches, store
    //         curve.segmentAt(position) in segment and return that.
    (void)curve;
    (void)position;
    return -1;
}

void AutomationCurve::render(std::int64_t start, float* out, int n, CurveCursor& cursor) const noexcept {
    // TODO 2: Step 1's render() (see its solution): n <= 0 writes nothing, an empty curve writes
    //         default_. But i = cursor.locate(*this, start) in place of the search, and after the
    //         loop cursor.segment = i, for the next block.
    (void)start;
    (void)out;
    (void)n;
    (void)cursor;
}

void AutomationLane::render(const TimelineSegment* segments, int numSegments, std::int64_t playhead, float* out,
                            int n) noexcept {
    // TODO 3: n <= 0: nothing. Read the curve ONCE (getCurve()).
    //         No curve: fill out with the knob's value.
    //         No segments (stopped): fill out with curve->valueAt(playhead).
    //         Otherwise, for each segment: curve->render(its timelineStart, out + its blockOffset,
    //         its numSamples, cursor_).
    //         Then limitSlew(out, n).
    (void)segments;
    (void)numSegments;
    (void)playhead;
    (void)out;
    (void)n;
}

void AutomationLane::limitSlew(float* out, int n) noexcept {
    // TODO 4: n <= 0: nothing. Start from last_ (if hasLast_; otherwise out[0], so the first sample
    //         stays as it is). For each sample: more than maxStep_ above the value: value += maxStep_;
    //         more than maxStep_ below it: value -= maxStep_; otherwise value = out[i] EXACTLY.
    //         out[i] = value. At the end store last_ and set hasLast_.
    (void)out;
    (void)n;
}

} // namespace minidaw

#include "../check.h"
