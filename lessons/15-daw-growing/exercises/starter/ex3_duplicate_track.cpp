// Exercise 3 (starter): duplicate a track.
//
// Fill in the two TODOs. Until then the self-check prints FAIL (expected). The solution is
// solutions/ex3_duplicate_track.cpp. What the functions must do is in ../ex3_duplicate_track.h.

#include "../ex3_duplicate_track.h"

namespace minidaw {

std::string copyName(const Project& p, const std::string& name) {
    // TODO: strip a trailing " (<digits>)" from `name` to get the base name, then try
    //       base + " (2)", base + " (3)", ... until no track of `p` has that name.
    (void)p;
    return name;
}

std::optional<Change> duplicateTrack(const Project& p, TrackId id) {
    // TODO: find the track (indexOf); nullopt if it is not there.
    //       Build one AddTrack: id p.nextTrackId, copyName(), the same gain and pan, at the index
    //       after the original, and a copy of each of its clips (with the new track id) at
    //       positions p.clips.size(), p.clips.size() + 1, ...
    (void)p;
    (void)id;
    return std::nullopt;
}

} // namespace minidaw

#include "../ex3_check.h"
