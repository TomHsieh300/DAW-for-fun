// Exercise 6 (solution): comping - the best parts of every pass, in one take.
//
// Build and run:  l17_ex6_solution   (ctest, label "solution")
// Your version:   exercises/starter/ex6_comping.cpp -> l17_ex6_starter

#include "../exercises/ex6_comping.h"

namespace minidaw {

Comp chooseTake(const Comp& comp, std::int64_t from, std::int64_t to, int take) {
    // No need to clip [from, to) to the window, or to look at an empty range: the pieces outside the
    // window come out empty, and push() drops empty pieces.
    Comp out;
    auto push = [&out](CompSegment s) {
        if (s.to <= s.from)
            return;
        if (!out.empty() && out.back().take == s.take && out.back().to == s.from)
            out.back().to = s.to; // the same take on both sides: one segment
        else
            out.push_back(s);
    };
    for (const CompSegment& s : comp) {
        push({s.from, std::min(s.to, from), s.take});              // the part before the range
        push({std::max(s.from, from), std::min(s.to, to), take});  // the part inside it
        push({std::max(s.from, to), s.to, s.take});                // the part after it
    }
    return out;
}

std::vector<CompClip> compClips(const Comp& comp, const std::vector<Placement>& takes, std::int64_t fade) {
    // The crossfade at each boundary between segment i and i + 1.
    std::vector<std::int64_t> cross(comp.size(), 0);
    for (size_t i = 0; i + 1 < comp.size(); ++i) {
        const CompSegment& a = comp[i];
        const CompSegment& b = comp[i + 1];
        const Placement& ta = takes[static_cast<size_t>(a.take)];
        const Placement& tb = takes[static_cast<size_t>(b.take)];
        const std::int64_t boundary = a.to;
        const std::int64_t aBeyond = ta.timelineStart + ta.numFrames - boundary; // a's pass after the boundary
        const std::int64_t bBefore = boundary - tb.timelineStart;               // b's pass before it
        std::int64_t x = std::min({std::max<std::int64_t>(fade, 0), a.to - a.from, b.to - b.from, 2 * aBeyond, 2 * bBefore});
        x = std::max<std::int64_t>(x, 0);
        cross[i] = x - x % 2; // both clips reach exactly half of it past the boundary
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

} // namespace minidaw

#include "../exercises/ex6_check.h"
