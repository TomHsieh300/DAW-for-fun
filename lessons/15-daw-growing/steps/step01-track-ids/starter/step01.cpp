// Step 1 (starter): stable track IDs.
//
// Build and run:  l15_step01_starter   (prints [FAIL] until all four TODOs are done)
// Reference:      ../solution/step01.cpp, docs/step-01-track-ids.md ("Your turn")
//
// TrackId, TrackList (its data members and the provided helpers) and the edits are in ../minidaw.h.

#include "../minidaw.h"

namespace minidaw {

TrackId TrackList::add(std::string name) {
    // TODO 1: hand out TrackId{nextId_}, then count nextId_ up (never down, never reuse);
    //         append a TrackInfo with that id and the name (gain 0 dB); return the id.
    (void)name;
    (void)nextId_;
    return TrackId{};
}

bool TrackList::remove(TrackId id) {
    // TODO 2: find the track (indexOf); if it is not there return false; otherwise erase it from
    //         tracks_ (the tracks below move up one position) and return true.
    //         Do NOT touch nextId_.
    (void)id;
    return false;
}

int TrackList::indexOf(TrackId id) const noexcept {
    // TODO 3: an invalid id is never found (-1). Otherwise search tracks_ for the id and return its
    //         position, or -1. A linear search is fine: there are at most a few hundred tracks and
    //         this runs on the UI thread.
    (void)id;
    return -1;
}

bool TrackList::move(TrackId id, int newIndex) {
    // TODO 4: find the track (false if it is not there); clamp newIndex to [0, size() - 1];
    //         take the TrackInfo out and insert it again at newIndex (in the list without it),
    //         so that afterwards indexOf(id) == newIndex. The id itself does not change.
    (void)id;
    (void)newIndex;
    return false;
}

} // namespace minidaw

#include "../check.h"
