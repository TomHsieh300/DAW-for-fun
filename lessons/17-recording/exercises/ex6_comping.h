#pragma once
// Exercise 6 - Comping: the best parts of every pass, in one take.
//
// A loop recorded four passes of a solo (Step 4's takesOf(): four Placements on one file). The first
// bars were best in pass 2, the middle in pass 4, the end in pass 1. A COMP says which pass plays
// where, and turns that into clips:
//
//   window      |<------------------------------ the loop ------------------------------>|
//   comp        | pass 2           | pass 4                          | pass 1               |
//   clips       [pass 2 ......... ]~
//                              ~[pass 4 ............................ ]~
//                                                               ~[pass 1 ................]
//                              ^^^^ a crossfade of `fade` frames, centred on each boundary
//
// Two functions, both pure - a comp is a value, so undo is keeping the last one (CompHistory):
//
//   chooseTake(comp, from, to, take)   the comp with `take` playing in [from, to) of the window
//   compClips(comp, takes, fade)       the clips: one per segment, with a crossfade at every boundary
//
// A segment's clip plays its pass's frames: timeline p comes from frame
// take.skipFrames + (p - take.timelineStart) of the file. At a boundary between segments, the earlier
// clip runs on fade/2 frames past it (fading out over `fade` frames) and the later one starts fade/2
// frames before it (fading in): with linear fades the two gains add up to 1 at every frame. A crossfade
// is shorter where there is less: never longer than either segment, never past the frames its pass has.

#include "model.h"

#include <vector>

namespace minidaw {

struct CompSegment {
    std::int64_t from = 0, to = 0; // the timeline, [from, to)
    int take = 0;                  // index into the takes
    friend bool operator==(const CompSegment& a, const CompSegment& b) { return a.from == b.from && a.to == b.to && a.take == b.take; }
};

// The segments in order, without gaps or overlaps, covering the comp's window exactly; neighbours
// never have the same take (they would be one segment).
using Comp = std::vector<CompSegment>;

// A clip of the take's file.
struct CompClip {
    std::int64_t timelineStart = 0;
    std::int64_t skipFrames = 0; // the first frame of the file it plays
    std::int64_t numFrames = 0;
    std::int64_t fadeIn = 0;     // frames of linear fade-in at its start
    std::int64_t fadeOut = 0;    // frames of linear fade-out at its end
    int take = 0;
};

// ---- provided -----------------------------------------------------------------------------------
// A comp of one take over [from, to).
inline Comp wholeComp(std::int64_t from, std::int64_t to, int take) { return {{from, to, take}}; }

// Undo and redo for comps: a comp is a value, so the history only keeps values.
class CompHistory {
public:
    explicit CompHistory(Comp start) : now_(std::move(start)) {}
    const Comp& now() const noexcept { return now_; }
    void apply(Comp next) {
        if (next == now_)
            return; // nothing changed: no undo step
        past_.push_back(std::move(now_));
        now_ = std::move(next);
        future_.clear();
    }
    bool undo() {
        if (past_.empty())
            return false;
        future_.push_back(std::move(now_));
        now_ = std::move(past_.back());
        past_.pop_back();
        return true;
    }
    bool redo() {
        if (future_.empty())
            return false;
        past_.push_back(std::move(now_));
        now_ = std::move(future_.back());
        future_.pop_back();
        return true;
    }

private:
    std::vector<Comp> past_, future_;
    Comp now_;
};

// ---- YOUR TURN (starter/ex6_comping.cpp) -------------------------------------------------------
// [from, to) is clipped to the comp's window; nothing left (or from >= to): the comp as it is.
// Segments are cut where needed, the part in [from, to) becomes `take`, and neighbours with the same
// take are merged.
Comp chooseTake(const Comp& comp, std::int64_t from, std::int64_t to, int take);

// One clip per segment (see above). `fade` >= 0. The crossfade at a boundary is
//   min(fade, the shorter of the two segments, and twice the frames each pass has beyond the boundary
//   on the side it must reach into), rounded down to an even number,
// so that both clips reach exactly half of it past the boundary.
std::vector<CompClip> compClips(const Comp& comp, const std::vector<Placement>& takes, std::int64_t fade);

} // namespace minidaw
