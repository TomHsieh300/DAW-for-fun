#pragma once
// minidaw.h - Step 1 snapshot of chapter 15: stable track IDs.
//
// In chapter 14 a track *is* its index: tracks live in std::array<Track, 8> and everything that
// refers to a track (clip lists, commands, the GUI's selection) stores 0..7. That is fine while
// the track list never changes. Chapter 15 lets the user add, delete and reorder tracks, and then
// an index stops naming a track: delete the first track and every other track moves up one.
//
// This snapshot is the model on the UI thread only - no audio yet. It has
//   - TrackId     a track's identity: handed out once, never reused, never changed
//   - TrackList   the tracks in display order, plus the id counter
//   - GainEdit    something recorded earlier that names a track (an undo entry, a command in
//                 flight, an automation write...), and IndexedGainEdit, the chapter-14 way
//
// Step 2 adds the audio side: a pool of kMaxTracks pre-allocated slots, and a map id -> slot.
//
// YOUR TURN (Step 1): write TrackList::add(), remove(), indexOf() and move() in
// starter/step01.cpp. They are only declared here.

#include <cstdint>
#include <string>
#include <vector>

namespace minidaw {

// A track's identity. 0 means "no track", so a default-constructed TrackId is invalid.
struct TrackId {
    std::uint32_t value = 0;

    constexpr bool isValid() const noexcept { return value != 0; }
    friend constexpr bool operator==(TrackId a, TrackId b) noexcept { return a.value == b.value; }
    friend constexpr bool operator!=(TrackId a, TrackId b) noexcept { return a.value != b.value; }
};

struct TrackInfo {
    TrackId id;
    std::string name;
    float gainDb = 0.0f;
};

class TrackList {
public:
    // ---- YOUR TURN (starter/step01.cpp) -------------------------------------------------------
    // Appends a track and returns its new id. Ids count up from 1 and are never reused, also not
    // after the track with the highest id has been removed.
    TrackId add(std::string name);
    // Removes the track; false if there is no track with this id (also for an invalid id).
    bool remove(TrackId id);
    // The track's current display position (0 = top), or -1 if there is no such track.
    int indexOf(TrackId id) const noexcept;
    // Moves the track so that afterwards indexOf(id) == newIndex, clamped to [0, size() - 1].
    // The other tracks keep their order. False if there is no such track.
    bool move(TrackId id, int newIndex);

    // ---- provided -------------------------------------------------------------------------------
    int size() const noexcept { return static_cast<int>(tracks_.size()); }
    const std::vector<TrackInfo>& tracks() const noexcept { return tracks_; }

    // By display position; nullptr / an invalid id when out of range.
    TrackInfo* at(int index) noexcept { return inRange(index) ? &tracks_[static_cast<size_t>(index)] : nullptr; }
    TrackId idAt(int index) const noexcept { return inRange(index) ? tracks_[static_cast<size_t>(index)].id : TrackId{}; }

    // By id; nullptr when the track is gone. Never returns a *different* track.
    TrackInfo* find(TrackId id) noexcept { return at(indexOf(id)); }
    const TrackInfo* find(TrackId id) const noexcept {
        const int i = indexOf(id);
        return inRange(i) ? &tracks_[static_cast<size_t>(i)] : nullptr;
    }

private:
    bool inRange(int index) const noexcept { return index >= 0 && index < size(); }

    std::vector<TrackInfo> tracks_; // in display order
    std::uint32_t nextId_ = 1;      // the id the next add() hands out; only ever counts up
};

// ---- things that refer to a track (provided) ------------------------------------------------------

// Chapter 15 style: the edit names its track by id.
struct GainEdit {
    TrackId track;
    float gainDb = 0.0f;
};

// Chapter 14 style, for comparison: the edit names its track by display index.
struct IndexedGainEdit {
    int track = 0;
    float gainDb = 0.0f;
};

// Applies the edits whose track still exists; returns how many were applied.
inline int applyEdits(TrackList& list, const std::vector<GainEdit>& edits) {
    int applied = 0;
    for (const GainEdit& e : edits)
        if (TrackInfo* t = list.find(e.track)) {
            t->gainDb = e.gainDb;
            ++applied;
        }
    return applied;
}

// The same with indices: an index that is still in range is applied - to whatever track is there now.
inline int applyIndexedEdits(TrackList& list, const std::vector<IndexedGainEdit>& edits) {
    int applied = 0;
    for (const IndexedGainEdit& e : edits)
        if (TrackInfo* t = list.at(e.track)) {
            t->gainDb = e.gainDb;
            ++applied;
        }
    return applied;
}

} // namespace minidaw
