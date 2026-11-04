// Step 2 (solution): the curve on the audio thread.
//
// Build and run:  l16_step02_solution   (ctest, label "solution")
// Your version:   starter/step02.cpp -> l16_step02_starter

#include "../minidaw.h"

namespace minidaw {

int CurveCursor::locate(const AutomationCurve& curve, std::int64_t position) noexcept {
    const auto& points = curve.points();
    const int last = curve.size() - 1;
    const int i = segment;
    // The hint is right exactly when point i is at or before the position and point i + 1 is after
    // it. Checked every time, so nothing has to tell the cursor about seeks, loops or a new curve.
    if (i >= -1 && i <= last && (i < 0 || points[static_cast<size_t>(i)].position <= position) &&
        (i == last || points[static_cast<size_t>(i) + 1].position > position))
        return i;
    ++searches;
    segment = curve.segmentAt(position);
    return segment;
}

void AutomationCurve::render(std::int64_t start, float* out, int n, CurveCursor& cursor) const noexcept {
    if (n <= 0)
        return;
    if (points_.empty()) {
        std::fill_n(out, n, default_);
        return;
    }
    int i = cursor.locate(*this, start);
    const int last = size() - 1;
    std::int64_t position = start;
    for (int k = 0; k < n; ++k, ++position) {
        while (i < last && points_[static_cast<size_t>(i) + 1].position <= position)
            ++i;
        out[k] = i < 0 ? points_.front().value : (i == last ? points_.back().value : between(i, position));
    }
    cursor.segment = i;
}

void AutomationLane::render(const TimelineSegment* segments, int numSegments, std::int64_t playhead, float* out,
                            int n) noexcept {
    if (n <= 0)
        return;
    const AutomationCurve* curve = getCurve();
    if (curve == nullptr) {
        std::fill_n(out, n, getKnobValue());
    } else if (numSegments == 0) {
        std::fill_n(out, n, curve->valueAt(playhead)); // stopped: the value where the playhead is
    } else {
        for (int s = 0; s < numSegments; ++s) {
            const TimelineSegment& segment = segments[s];
            curve->render(segment.timelineStart, out + segment.blockOffset, segment.numSamples, cursor_);
        }
    }
    limitSlew(out, n);
}

void AutomationLane::limitSlew(float* out, int n) noexcept {
    if (n <= 0)
        return;
    float value = hasLast_ ? last_ : out[0];
    for (int i = 0; i < n; ++i) {
        const float wanted = out[i];
        // Not value += clamp(wanted - value): that can round (rarely), and a curve must come through exactly.
        if (wanted - value > maxStep_)
            value += maxStep_;
        else if (value - wanted > maxStep_)
            value -= maxStep_;
        else
            value = wanted;
        out[i] = value;
    }
    last_ = value;
    hasLast_ = true;
}

} // namespace minidaw

#include "../check.h"
