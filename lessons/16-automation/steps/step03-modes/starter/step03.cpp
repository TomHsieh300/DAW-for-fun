// Step 3 (starter): automation and the user.
//
// Build and run:  l16_step03_starter   (prints [FAIL] until all five TODOs are done)
// Reference:      ../solution/step03.cpp, docs/step-03-modes.md ("Your turn")
//
// The curve, the cursor and the transport (Steps 1-2, solved), CaptureQueue and the rest of
// AutomationLane and Recorder are in ../minidaw.h.

#include "../minidaw.h"

#include <cmath>

namespace minidaw {

bool AutomationLane::userWins(bool playing) noexcept {
    // TODO 1: read touching_ and touchCount_; a touch was seen if the count differs from seenTouches_
    //         (then store it). Clear latched_ if stopped or if the mode is not Latch.
    //         Stopped: the user wins while touching, in every mode.
    //         Read: never. Touch: while touching. Write: always.
    //         Latch: latched_ becomes true on touching or on a touch seen, and stays; return latched_.
    (void)playing;
    return false;
}

void AutomationLane::render(const TimelineSegment* segments, int numSegments, std::int64_t playhead, float* out,
                            int n) noexcept {
    // TODO 2: n <= 0: nothing. playing = numSegments > 0; user = userWins(playing).
    //         Fill out as in Step 2, but with the knob's value if user or if there is no curve.
    //         capture = user && playing. If capturing_ was true and capture is false: endOwed_ = true.
    //         capturing_ = capture. If endOwed_: push {playhead, 0, knob}; on success endOwed_ = false.
    //         (Before this block's events: they may start a new pass.)
    //         If capture: push {timelineStart, numSamples, knob} for each segment.
    //         Then limitSlew(out, n).
    (void)segments;
    (void)numSegments;
    (void)playhead;
    (void)out;
    (void)n;
}

std::vector<AutomationPoint> thin(const std::vector<AutomationPoint>& points, float tolerance) {
    // TODO 3: Ramer-Douglas-Peucker. 2 points or fewer: return them. Mark the first and the last as
    //         kept. With a list of (first, last) ranges to do, starting with the whole: find the
    //         point between them furthest from the line first-last, measured in VALUE at its position.
    //         If that is more than tolerance: keep it and add the two ranges on either side of it.
    //         Return the kept points in order.
    (void)tolerance;
    return points;
}

AutomationCurve punch(const AutomationCurve& old, const std::vector<AutomationPoint>& pass) {
    // TODO 4: pass of fewer than 2 points: return old. from = first position, to = last position.
    //         before: the value of the FIRST old point at `from` if there is one, otherwise
    //         old.valueAt(from). after: old.valueAt(to); its shape: that of the point old.segmentAt(to)
    //         (Linear if there is none).
    //         The result, with old's default: the old points before `from`, {from, before, Linear}, all
    //         of pass, {to, after, its shape}, the old points after `to`.
    (void)pass;
    return old;
}

bool Recorder::collect(CaptureQueue& queue, AutomationCurve& curve) {
    // TODO 5: pop every event. To finish a pass (if pass_ is not empty): add {passEnd_, lastValue_},
    //         curve = punch(curve, thin(pass_, tolerance_)), clear pass_, remember that curve changed.
    //         End marker (numSamples == 0): finish the pass.
    //         Otherwise: if the pass is not empty and event.position != passEnd_, finish it. Then add
    //         {position, value} to pass_, passEnd_ = position + numSamples, lastValue_ = value.
    (void)queue;
    (void)curve;
    return false;
}

} // namespace minidaw

#include "../check.h"
