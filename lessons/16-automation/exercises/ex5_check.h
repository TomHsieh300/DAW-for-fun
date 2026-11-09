#pragma once
// Exercise 5 - the self-check (main). Included at the end of the starter and of the solution.

#include "ex5_clipboard.h"

#include <dawlab/Check.h>

#include <cmath>
#include <cstdio>

namespace minidaw::ex5_check {

// Track 1 with a compressor (effect 7); its Gain lane rises, holds, jumps and rises again.
inline Project example() {
    Project p;
    p.nextTrackId = 2;
    p.nextEffectId = 8;
    p.tracks.push_back({TrackId{1}, "Synth", {{EffectId{7}, "Compressor"}}});
    AutomationCurve gain(0.5f);
    gain.add({0, 0.2f, Shape::Linear});
    gain.add({48000, 0.8f, Shape::Hold});
    gain.add({96000, 0.5f, Shape::Linear});
    gain.add({96000, 0.1f, Shape::Linear}); // a jump
    gain.add({144000, 0.9f, Shape::Linear});
    insertLane(p, {{StripRef::ofTrack(TrackId{1}), EffectId{}, "Gain"}, AutomationMode::Touch, gain});
    return p;
}

// How far `after` is from what it should be: the old curve outside [at, at + len), the source inside.
struct Deviation {
    double outside = 0.0, inside = 0.0;
};
inline Deviation compare(const AutomationCurve& before, const AutomationCurve& after, const AutomationCurve& source,
                         std::int64_t start, std::int64_t at, std::int64_t len, std::int64_t until) {
    Deviation d;
    for (std::int64_t p = 0; p < until; p += 7) {
        const bool in = p >= at && p < at + len;
        const double want = in ? source.valueAt(start + p - at) : before.valueAt(p);
        double& worst = in ? d.inside : d.outside;
        worst = std::max(worst, std::abs(static_cast<double>(after.valueAt(p)) - want));
    }
    // The edges themselves.
    for (const std::int64_t p : {at - 1, at, at + len - 1, at + len}) {
        const bool in = p >= at && p < at + len;
        const double want = in ? source.valueAt(start + p - at) : before.valueAt(p);
        double& worst = in ? d.inside : d.outside;
        worst = std::max(worst, std::abs(static_cast<double>(after.valueAt(p)) - want));
    }
    return d;
}

inline void run() {
    const Project start = example();
    Project p = start;
    History h;
    const AutomationTarget gain{StripRef::ofTrack(TrackId{1}), EffectId{}, "Gain"};
    const AutomationCurve source = findLane(p, gain)->curve;

    // ---- copy -----------------------------------------------------------------------------------
    const CurveClip clip = copyRange(source, 24000, 120000); // half way up, the hold, the jump, half way up again
    DAWLAB_CHECK(clip.length == 96000);
    DAWLAB_CHECK(clip.defaultValue == 0.5f);
    DAWLAB_CHECK(clip.points.size() == 5);
    if (clip.points.size() == 5) {
        DAWLAB_CHECK(clip.points[0].position == 0 && std::abs(clip.points[0].value - 0.5f) < 1e-6f);
        DAWLAB_CHECK(clip.points[1].position == 24000 && clip.points[1].shape == Shape::Hold);
        DAWLAB_CHECK(clip.points[2].position == 72000 && clip.points[3].position == 72000); // the jump
        DAWLAB_CHECK(clip.points[4].position == 96000 && std::abs(clip.points[4].value - 0.5f) < 1e-6f);
    }
    // Starting inside a hold: the copy holds too.
    const CurveClip held = copyRange(source, 60000, 90000);
    DAWLAB_CHECK(held.points.size() == 2 && held.points[0].shape == Shape::Hold && held.points[0].value == 0.8f);

    // ---- paste after the end, in the middle, onto another parameter ----------------------------------
    struct Case {
        const char* what;
        AutomationTarget target;
        std::int64_t at;
    };
    const AutomationTarget threshold{StripRef::ofTrack(TrackId{1}), EffectId{7}, "Threshold"};
    const Case cases[] = {{"after the end", gain, 200000}, {"over the middle", gain, 60000}, {"at 0", gain, 0},
                          {"onto the compressor's threshold", threshold, 30000}};
    for (const Case& c : cases) {
        const AutomationLaneState* lane = findLane(p, c.target); // only until the paste: it moves the lanes
        const AutomationCurve before = lane != nullptr ? lane->curve : AutomationCurve(clip.defaultValue);
        const AutomationMode mode = lane != nullptr ? lane->mode : AutomationMode::Read;
        const std::size_t entries = h.undoCount();
        const Project old = p;
        const std::optional<Change> change = pasteChange(p, c.target, clip, c.at);
        DAWLAB_CHECK(change.has_value() && change->size() == 1);
        DAWLAB_CHECK(p == old); // a description, not yet a change
        if (!change || !h.perform(p, *change)) {
            DAWLAB_CHECK(false);
            continue;
        }
        DAWLAB_CHECK(h.undoCount() == entries + 1);
        const AutomationLaneState* now = findLane(p, c.target);
        DAWLAB_CHECK(now != nullptr);
        if (now == nullptr)
            continue;
        DAWLAB_CHECK(now->mode == mode);
        const Deviation d = compare(before, now->curve, source, 24000, c.at, clip.length, 320000);
        std::printf("  pasted %s: off by %.1e outside, %.1e inside\n", c.what, d.outside, d.inside);
        DAWLAB_CHECK(d.outside < 1e-6 && d.inside < 1e-6);
        // The jump and the hold came along.
        DAWLAB_CHECK(now->curve.valueAt(c.at + 71999) == 0.8f && std::abs(now->curve.valueAt(c.at + 72000) - 0.1f) < 1e-6f);
        // Undo gives back exactly the project before; redo exactly the paste.
        const Project pasted = p;
        DAWLAB_CHECK(h.undo(p) && p == old);
        DAWLAB_CHECK(h.redo(p) && p == pasted);
    }

    // A copy that ends on a jump in a rising line takes the value BEFORE the jump.
    {
        AutomationCurve rise(0.5f);
        rise.add({0, 0.2f, Shape::Linear});
        rise.add({50000, 0.6f, Shape::Linear});
        rise.add({50000, 0.3f, Shape::Linear});
        rise.add({100000, 0.5f, Shape::Linear});
        const CurveClip upTo = copyRange(rise, 10000, 50000);
        DAWLAB_CHECK(!upTo.points.empty() && std::abs(upTo.points.back().value - 0.6f) < 1e-6f);
        const std::optional<Change> change = pasteChange(p, gain, upTo, 400000);
        DAWLAB_CHECK(change.has_value());
        if (change) {
            const Project copy = p;
            Project q = copy;
            History hq;
            DAWLAB_CHECK(hq.perform(q, *change));
            const AutomationLaneState* now = findLane(q, gain);
            const Deviation d = now != nullptr ? compare(findLane(copy, gain)->curve, now->curve, rise, 10000, 400000, 40000, 460000)
                                               : Deviation{1.0, 1.0};
            DAWLAB_CHECK(d.outside < 1e-6 && d.inside < 1e-6);
        }
    }

    // Nothing to paste onto, nothing to paste.
    DAWLAB_CHECK(!pasteChange(p, {StripRef::ofTrack(TrackId{5}), EffectId{}, "Gain"}, clip, 0).has_value());
    DAWLAB_CHECK(!pasteChange(p, {StripRef::ofTrack(TrackId{1}), EffectId{9}, "Ratio"}, clip, 0).has_value());
    DAWLAB_CHECK(!pasteChange(p, gain, CurveClip{}, 0).has_value());

    // Everything undone: the project as it was.
    while (h.undoCount() > 0)
        h.undo(p);
    DAWLAB_CHECK(p == start);
}

} // namespace minidaw::ex5_check

int main() {
    std::printf("Exercise 5 - copy and paste automation\n");
    minidaw::ex5_check::run();
    return dawlab::test::summary("l16_ex5");
}
