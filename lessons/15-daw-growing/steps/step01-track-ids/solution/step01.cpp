// Step 1 (solution): stable track IDs.
//
// Build and run:  l15_step01_solution   (ctest, label "solution")
// Your version:   starter/step01.cpp -> l15_step01_starter

#include "../minidaw.h"

#include <algorithm>
#include <utility>

namespace minidaw {

TrackId TrackList::add(std::string name) {
    const TrackId id{nextId_++}; // counts up only: a removed track's id is never handed out again
    tracks_.push_back(TrackInfo{id, std::move(name), 0.0f});
    return id;
}

bool TrackList::remove(TrackId id) {
    const int i = indexOf(id);
    if (i < 0)
        return false;
    tracks_.erase(tracks_.begin() + i);
    return true;
}

int TrackList::indexOf(TrackId id) const noexcept {
    if (!id.isValid())
        return -1;
    for (size_t i = 0; i < tracks_.size(); ++i)
        if (tracks_[i].id == id)
            return static_cast<int>(i);
    return -1;
}

bool TrackList::move(TrackId id, int newIndex) {
    const int from = indexOf(id);
    if (from < 0)
        return false;
    const int to = std::clamp(newIndex, 0, size() - 1);
    TrackInfo track = std::move(tracks_[static_cast<size_t>(from)]);
    tracks_.erase(tracks_.begin() + from);
    tracks_.insert(tracks_.begin() + to, std::move(track)); // `to` is a position in the list without it
    return true;
}

} // namespace minidaw

#include "../check.h"
