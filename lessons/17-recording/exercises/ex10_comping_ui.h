#pragma once
// Exercise 10 - Comping in the app (optional, JUCE).
//
// After a loop recording, a clip plays one whole pass, and the clip's menu chooses another (Step 7). Exercise
// 6 built the model of comping: the best part of each pass, joined with crossfades. This exercise puts it on
// the track, without changing the app. A comp lives in the clips themselves: one clip per segment, all on the
// recording's buffer, each with the source offset of its take, neighbours overlapping by their crossfade. So
// it is saved, undone and played like any clips, and nothing new is stored anywhere:
//
//   Comper::takeOf(clip, takes)      which take a clip plays a part of
//   Comper::compOf(track, clip)      the comp the track plays of that clip's recording, read from its clips
//   Comper::choose(track, clip, from, to, take)
//                                    `take` between from and to: the recording's clips are replaced with
//                                    the new comp's, one undo step ("Comp Take 3")
//   Comper::addToMenu(menu, track, clip)
//                                    "Take N Between the Locators" for each take: the loop's locators are the
//                                    range, as they are the punch points
//
// The comp's own operations are Exercise 6's, given below in this chapter's types: do Exercise 6 first.

#include "MainComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace ex10 {

using dawlab::engine::Placement;

// ---- provided: Exercise 6 -----------------------------------------------------------------------------
struct CompSegment {
    std::int64_t from = 0, to = 0; // timeline, [from, to)
    int take = 0;
    bool operator==(const CompSegment& o) const noexcept { return from == o.from && to == o.to && take == o.take; }
};
using Comp = std::vector<CompSegment>; // in order, each segment starts where the one before ends

struct CompClip {
    std::int64_t timelineStart = 0, skipFrames = 0, numFrames = 0, fadeIn = 0, fadeOut = 0;
    int take = 0;
};

inline Comp chooseTake(const Comp& comp, std::int64_t from, std::int64_t to, int take) {
    Comp out;
    auto push = [&out](CompSegment s) {
        if (s.to <= s.from)
            return;
        if (!out.empty() && out.back().take == s.take && out.back().to == s.from)
            out.back().to = s.to;
        else
            out.push_back(s);
    };
    for (const CompSegment& s : comp) {
        push({s.from, std::min(s.to, from), s.take});
        push({std::max(s.from, from), std::min(s.to, to), take});
        push({std::max(s.from, to), s.to, s.take});
    }
    return out;
}

// One clip per segment; the crossfade at a boundary is min(fade, both segments, twice what each pass has
// beyond the boundary), made even, and each clip reaches half of it past the boundary. No fade at the ends.
inline std::vector<CompClip> compClips(const Comp& comp, const std::vector<Placement>& takes, std::int64_t fade) {
    std::vector<std::int64_t> cross(comp.size(), 0);
    for (size_t i = 0; i + 1 < comp.size(); ++i) {
        const CompSegment& a = comp[i];
        const CompSegment& b = comp[i + 1];
        const Placement& ta = takes[static_cast<size_t>(a.take)];
        const Placement& tb = takes[static_cast<size_t>(b.take)];
        const std::int64_t aBeyond = ta.timelineStart + ta.numFrames - a.to;
        const std::int64_t bBefore = a.to - tb.timelineStart;
        std::int64_t x = std::min({std::max<std::int64_t>(fade, 0), a.to - a.from, b.to - b.from, 2 * aBeyond, 2 * bBefore});
        x = std::max<std::int64_t>(x, 0);
        cross[i] = x - x % 2;
    }
    std::vector<CompClip> clips;
    for (size_t i = 0; i < comp.size(); ++i) {
        const CompSegment& s = comp[i];
        const Placement& t = takes[static_cast<size_t>(s.take)];
        const std::int64_t fadeIn = i > 0 ? cross[i - 1] : 0;
        const std::int64_t fadeOut = i + 1 < comp.size() ? cross[i] : 0;
        const std::int64_t start = s.from - fadeIn / 2;
        const std::int64_t end = s.to + fadeOut / 2;
        clips.push_back({start, t.skipFrames + (start - t.timelineStart), end - start, fadeIn, fadeOut, s.take});
    }
    return clips;
}

class Comper {
public:
    static constexpr double kFadeSeconds = 0.010;    // the crossfades
    static constexpr double kEdgeFadeSeconds = 0.005; // at the comp's two ends: what a new take gets

    explicit Comper(dawlab::app::Session& session) : session_(session) {}

    // ---- YOUR TURN (starter/ex10_comping_ui.cpp) ------------------------------------------------------
    // The take whose audio the clip plays: at the clip's start, the take's frame is the clip's,
    //     clip.sourceOffset - take.skipFrames == clip.start - take.timelineStart,
    // and the clip lies within the take on the timeline. -1 if there is none.
    static int takeOf(const dawlab::engine::Clip& clip, const std::vector<Placement>& takes);
    // The track's clips on the same buffer as clip `clipIndex`, in order, as segments. A fade between two of
    // them is a crossfade: half of it belongs to each side, so a segment runs from start + fadeIn / 2 to
    // end - fadeOut / 2 - except at the comp's two ends, where it runs from the first clip's start and to the
    // last clip's end (their fades are not crossfades). Empty if the clip has no takes, or if one of
    // those clips plays none of them.
    Comp compOf(dawlab::engine::TrackId track, int clipIndex) const;
    // [from, to) is first limited to `take` (what it has on the timeline). Then the comp with `take` there,
    // in clips: compClips() with kFadeSeconds, each clip a copy of clip `clipIndex` (name, gain, source) with
    // the new position, offset, length and fades, except at the comp's two ends: there the first clip fades
    // in and the last fades out over kEdgeFadeSeconds, at most half the clip's length (as Session gives a new
    // take). Not the fade the end clip has now: a short end clip would shrink it for good. The track's other
    // clips stay. One undo step, "Comp Take <take + 1>". False - and nothing changes - if there is no comp, no
    // such take, no range left (to <= from: chooseTake() wants a range), or the comp would be the same.
    bool choose(dawlab::engine::TrackId track, int clipIndex, std::int64_t from, std::int64_t to, int take);
    // For a clip with a comp: "Take 1 Between the Locators" ... one per take, each choose()ing it between the
    // loop's locators, disabled when the locators make no range (end <= start). Nothing for other clips.
    void addToMenu(juce::PopupMenu& menu, dawlab::engine::TrackId track, int clipIndex);

private:
    dawlab::app::Session& session_;
};

} // namespace ex10
