#pragma once
// Exercise 4 - the self-check (main). Included at the end of the starter and of the solution.

#include "ex4_trim.h"

#include <dawlab/Check.h>

#include <cmath>
#include <cstdio>

namespace minidaw::ex4_check {

inline AutomationCurve ride() {
    AutomationCurve c(0.5f);
    c.add({0, 0.2f, Shape::Linear});
    c.add({48000, 0.6f, Shape::Hold});
    c.add({96000, 0.3f, Shape::Linear});
    c.add({96000, 0.5f, Shape::Linear}); // a jump
    c.add({144000, 0.7f, Shape::Linear});
    return c;
}

// The largest difference between `trimmed` and what it should be, inside the pass and outside it.
struct Deviation {
    double inside = 0.0, outside = 0.0;
};
inline Deviation compare(const AutomationCurve& old, const AutomationCurve& trimmed, const std::vector<TrimPoint>& pass) {
    Deviation d;
    const std::int64_t first = pass.front().position, last = pass.back().position;
    auto look = [&](std::int64_t p) {
        const bool in = p >= first && p < last;
        const double want = in ? AutomationCurve::clamp01(old.valueAt(p) + offsetAt(pass, p)) : old.valueAt(p);
        double& worst = in ? d.inside : d.outside;
        worst = std::max(worst, std::abs(static_cast<double>(trimmed.valueAt(p)) - want));
    };
    for (std::int64_t p = -50; p < 200000; p += 13)
        look(p);
    for (const AutomationPoint& q : old.points())
        for (const std::int64_t p : {q.position - 1, q.position, q.position + 1})
            look(p);
    for (const TrimPoint& q : pass)
        for (const std::int64_t p : {q.position - 1, q.position, q.position + 1})
            look(p);
    return d;
}

inline void checkCurve() {
    const AutomationCurve old = ride();
    // +0.2 over the hold and the jump, then down to -0.1.
    const std::vector<TrimPoint> pass = {{24000, 0.0f}, {60000, 0.2f}, {120000, 0.2f}, {130000, -0.1f}};
    const AutomationCurve trimmed = applyTrim(old, pass);
    const Deviation d = compare(old, trimmed, pass);
    std::printf("  a trim over a hold and a jump: off by %.1e inside, %.1e outside\n", d.inside, d.outside);
    DAWLAB_CHECK(d.inside < 1e-6 && d.outside < 1e-6);
    // The shape of the ride stays: the hold is still flat where the offset is, the jump is still there.
    DAWLAB_CHECK(std::abs(trimmed.valueAt(70000) - 0.8f) < 1e-6f && std::abs(trimmed.valueAt(95999) - 0.8f) < 1e-6f);
    DAWLAB_CHECK(std::abs(trimmed.valueAt(96000) - 0.7f) < 1e-6f);
    DAWLAB_CHECK(std::abs(trimmed.valueAt(54000) - (0.6f + 0.2f * 30000.0f / 36000.0f)) < 1e-6f); // the offset rises over the hold

    // Starting and ending on points of the old curve, and on its jump.
    for (const std::vector<TrimPoint>& p : {std::vector<TrimPoint>{{48000, 0.1f}, {96000, 0.1f}},
                                            std::vector<TrimPoint>{{0, -0.1f}, {144000, 0.05f}},
                                            std::vector<TrimPoint>{{96000, 0.1f}, {100000, 0.1f}, {200000, 0.0f}}}) {
        const Deviation e = compare(old, applyTrim(old, p), p);
        DAWLAB_CHECK(e.inside < 1e-6 && e.outside < 1e-6);
    }

    // Ending inside the hold: the hold goes on after the trim.
    const std::vector<TrimPoint> inHold = {{30000, 0.1f}, {70000, 0.1f}};
    const Deviation h = compare(old, applyTrim(old, inHold), inHold);
    DAWLAB_CHECK(h.inside < 1e-6 && h.outside < 1e-6);
    // Starting on a jump in a rising line: up to it, the old line, not one heading for the jump's second value.
    AutomationCurve rise(0.5f);
    rise.add({0, 0.2f, Shape::Linear});
    rise.add({50000, 0.6f, Shape::Linear});
    rise.add({50000, 0.3f, Shape::Linear});
    rise.add({100000, 0.5f, Shape::Linear});
    const std::vector<TrimPoint> onJump = {{50000, 0.1f}, {80000, 0.1f}};
    const Deviation j = compare(rise, applyTrim(rise, onJump), onJump);
    DAWLAB_CHECK(j.inside < 1e-6 && j.outside < 1e-6);

    // A trim of nothing changes nothing.
    const Deviation zero = compare(old, applyTrim(old, {{10000, 0.0f}, {150000, 0.0f}}), {{10000, 0.0f}, {150000, 0.0f}});
    DAWLAB_CHECK(zero.inside < 1e-6 && zero.outside < 1e-6);

    // Too far: the points are clamped, and the curve never leaves 0..1.
    const std::vector<TrimPoint> loud = {{24000, 0.5f}, {130000, 0.5f}};
    const AutomationCurve clipped = applyTrim(old, loud);
    bool inRange = true;
    for (std::int64_t p = 0; p < 200000; p += 11)
        inRange = inRange && clipped.valueAt(p) >= 0.0f && clipped.valueAt(p) <= 1.0f;
    DAWLAB_CHECK(inRange);
    DAWLAB_CHECK(clipped.valueAt(70000) == 1.0f);
    DAWLAB_CHECK(std::abs(clipped.valueAt(24000) - 0.9f) < 1e-6f); // 0.4 + 0.5, not clamped yet
}

inline void checkUndo() {
    Project p;
    p.nextTrackId = 2;
    p.tracks.push_back({TrackId{1}, "Vocal", {}});
    const AutomationTarget gain{StripRef::ofTrack(TrackId{1}), EffectId{}, "Gain"};
    insertLane(p, {gain, AutomationMode::Latch, ride()});
    const Project before = p;
    History h;
    const std::vector<TrimPoint> pass = {{24000, 0.0f}, {60000, 0.2f}, {130000, 0.2f}};
    const std::optional<Change> change = trimChange(p, gain, pass);
    DAWLAB_CHECK(change.has_value() && p == before);
    DAWLAB_CHECK(change.has_value() && h.perform(p, *change) && h.undoCount() == 1);
    const AutomationLaneState* lane = findLane(p, gain);
    DAWLAB_CHECK(lane != nullptr && lane->mode == AutomationMode::Latch);
    DAWLAB_CHECK(lane != nullptr && std::abs(lane->curve.valueAt(100000) - (0.5f + 0.2f * 4000.0f / 48000.0f + 0.2f)) < 1e-6f);
    DAWLAB_CHECK(h.undo(p) && p == before);
    // Nothing to trim.
    DAWLAB_CHECK(!trimChange(p, {StripRef::ofTrack(TrackId{1}), EffectId{}, "Pan"}, pass).has_value());
    DAWLAB_CHECK(!trimChange(p, gain, {{0, 0.1f}}).has_value());
}

} // namespace minidaw::ex4_check

int main() {
    std::printf("Exercise 4 - trim\n");
    minidaw::ex4_check::checkCurve();
    minidaw::ex4_check::checkUndo();
    return dawlab::test::summary("l16_ex4");
}
