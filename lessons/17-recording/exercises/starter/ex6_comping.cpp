// Exercise 6 (starter): comping - the best parts of every pass, in one take.
//
// Fill in the two TODOs. Until then the self-check prints FAIL (expected). The solution is
// solutions/ex6_comping.cpp. What the functions must do is in ../ex6_comping.h.

#include "../ex6_comping.h"

namespace minidaw {

Comp chooseTake(const Comp& comp, std::int64_t from, std::int64_t to, int take) {
    // TODO: each segment gives up to three
    //       pieces (before the range, inside it with `take`, after it); empty pieces go, and a piece
    //       with the same take as the one before it is merged into it.
    (void)from;
    (void)to;
    (void)take;
    return comp;
}

std::vector<CompClip> compClips(const Comp& comp, const std::vector<Placement>& takes, std::int64_t fade) {
    // TODO: first the crossfade at each boundary: min(fade, both segment lengths, twice the frames
    //       the earlier pass has after it, twice the frames the later pass has before it), made even.
    //       Then one clip per segment: from - fadeIn / 2 to to + fadeOut / 2, skipFrames
    //       take.skipFrames + (start - take.timelineStart).
    (void)comp;
    (void)takes;
    (void)fade;
    return {};
}

} // namespace minidaw

#include "../ex6_check.h"
