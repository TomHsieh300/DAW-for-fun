// Step 3 (solution): automation and the user.
//
// Build and run:  l16_step03_solution   (ctest, label "solution")
// Your version:   starter/step03.cpp -> l16_step03_starter

#include "../minidaw.h"

#include <cmath>
#include <cstddef>
#include <utility>

namespace minidaw {

bool AutomationLane::userWins(bool playing) noexcept {
    const bool touching = touching_.load(std::memory_order_relaxed);
    const unsigned touches = touchCount_.load(std::memory_order_relaxed);
    const bool touchedSinceLastBlock = touches != seenTouches_;
    seenTouches_ = touches;
    const AutomationMode mode = getMode();
    if (!playing || mode != AutomationMode::Latch)
        latched_ = false;
    if (!playing)
        return touching;
    switch (mode) {
    case AutomationMode::Read: return false;
    case AutomationMode::Touch: return touching;
    case AutomationMode::Latch:
        latched_ = latched_ || touching || touchedSinceLastBlock;
        return latched_;
    case AutomationMode::Write: return true;
    }
    return false;
}

void AutomationLane::render(const TimelineSegment* segments, int numSegments, std::int64_t playhead, float* out,
                            int n) noexcept {
    if (n <= 0)
        return;
    const bool playing = numSegments > 0;
    const bool user = userWins(playing);
    const float knob = getKnobValue();
    const AutomationCurve* curve = getCurve();
    if (user || curve == nullptr) {
        std::fill_n(out, n, knob);
    } else if (!playing) {
        std::fill_n(out, n, curve->valueAt(playhead));
    } else {
        for (int s = 0; s < numSegments; ++s) {
            const TimelineSegment& segment = segments[s];
            curve->render(segment.timelineStart, out + segment.blockOffset, segment.numSamples, cursor_);
        }
    }

    const bool capture = user && playing;
    if (capturing_ && !capture)
        endOwed_ = true;
    capturing_ = capture;
    if (endOwed_ && capture_.push({playhead, 0, knob})) // before this block's events, which may start a new pass
        endOwed_ = false;
    if (capture)
        for (int s = 0; s < numSegments; ++s)
            capture_.push({segments[s].timelineStart, segments[s].numSamples, knob});

    limitSlew(out, n);
}

std::vector<AutomationPoint> thin(const std::vector<AutomationPoint>& points, float tolerance) {
    const std::size_t count = points.size();
    if (count <= 2)
        return points;
    std::vector<char> keep(count, 0);
    keep[0] = keep[count - 1] = 1;
    // A list of ranges still to look at, not recursion: a long recording must not depend on the stack.
    std::vector<std::pair<std::size_t, std::size_t>> todo{{0, count - 1}};
    while (!todo.empty()) {
        const auto [first, last] = todo.back();
        todo.pop_back();
        const AutomationPoint& a = points[first];
        const AutomationPoint& b = points[last];
        double worst = tolerance;
        std::size_t worstAt = 0;
        for (std::size_t i = first + 1; i < last; ++i) {
            const double t = static_cast<double>(points[i].position - a.position) / static_cast<double>(b.position - a.position);
            const double onLine = a.value + (static_cast<double>(b.value) - a.value) * t;
            const double error = std::abs(points[i].value - onLine);
            if (error > worst) {
                worst = error;
                worstAt = i;
            }
        }
        if (worstAt != 0) {
            keep[worstAt] = 1;
            todo.push_back({first, worstAt});
            todo.push_back({worstAt, last});
        }
    }
    std::vector<AutomationPoint> kept;
    for (std::size_t i = 0; i < count; ++i)
        if (keep[i])
            kept.push_back(points[i]);
    return kept;
}

AutomationCurve punch(const AutomationCurve& old, const std::vector<AutomationPoint>& pass) {
    if (pass.size() < 2)
        return old;
    const std::int64_t from = pass.front().position;
    const std::int64_t to = pass.back().position;
    const auto& points = old.points();

    // Going in: the value the old curve arrives with. If it jumps right at `from`, that is the first
    // point there; otherwise the curve at `from`.
    const auto atFrom = std::lower_bound(points.begin(), points.end(), from,
                                         [](const AutomationPoint& a, std::int64_t p) { return a.position < p; });
    const float before = atFrom != points.end() && atFrom->position == from ? atFrom->value : old.valueAt(from);
    // Going out: the value the old curve goes on with, on the segment it goes on with.
    const int segment = old.segmentAt(to);
    const Shape shapeAfter = segment >= 0 ? points[static_cast<size_t>(segment)].shape : Shape::Linear;
    const float after = old.valueAt(to);

    AutomationCurve result(old.defaultValue());
    for (const AutomationPoint& p : points)
        if (p.position < from)
            result.add(p);
    result.add({from, before, Shape::Linear});
    for (const AutomationPoint& p : pass)
        result.add(p);
    result.add({to, after, shapeAfter});
    for (const AutomationPoint& p : points)
        if (p.position > to)
            result.add(p);
    return result;
}

bool Recorder::collect(CaptureQueue& queue, AutomationCurve& curve) {
    bool changed = false;
    auto finishPass = [&] {
        if (pass_.empty())
            return;
        pass_.push_back({passEnd_, lastValue_, Shape::Linear});
        curve = punch(curve, thin(pass_, tolerance_));
        pass_.clear();
        changed = true;
    };
    CaptureEvent event;
    while (queue.pop(event)) {
        if (event.numSamples == 0) {
            finishPass();
            continue;
        }
        if (!pass_.empty() && event.position != passEnd_)
            finishPass();
        pass_.push_back({event.position, event.value, Shape::Linear});
        passEnd_ = event.position + event.numSamples;
        lastValue_ = event.value;
    }
    return changed;
}

} // namespace minidaw

#include "../check.h"
