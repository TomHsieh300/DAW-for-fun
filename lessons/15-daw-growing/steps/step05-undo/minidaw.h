#pragma once
// minidaw.h - Step 5 snapshot of chapter 15: undo and redo.
//
// Every change the user makes to the project becomes an Edit: a plain value that says what to
// change, by track id. One function, applyEdit(), carries an edit out AND returns the edit that takes
// it back. That inverse is again an ordinary Edit, so undo is "apply the inverse", and the inverse
// of the inverse is the redo. There is no separate undo code for each kind of edit.
//
//   perform(Change)   apply every edit  -> push their inverses on the undo stack, clear redo
//   undo()            apply the inverses (last first) -> push THEIR inverses on the redo stack
//   redo()            the same, the other way round
//
// Ids (Step 1) are what make this work: an undo entry for "Bass" still finds Bass after other
// tracks were added, deleted or moved, and deleting Bass and undoing it brings Bass back with the
// SAME id, so older entries that name it keep working. The model is Step 4's Project; Step 7
// sends the same edits to the engine (Step 2) as well.
//
// YOUR TURN (Step 5): write applyEdit(), History::perform(), History::undo() and History::redo() in
// starter/step05.cpp. They are only declared here.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace minidaw {

// ---- Steps 1 and 4 (solved): the model -----------------------------------------------------------

struct TrackId {
    std::uint32_t value = 0;

    constexpr bool isValid() const noexcept { return value != 0; }
    friend constexpr bool operator==(TrackId a, TrackId b) noexcept { return a.value == b.value; }
    friend constexpr bool operator!=(TrackId a, TrackId b) noexcept { return a.value != b.value; }
};

struct TrackState {
    TrackId id;
    std::string name;
    float gainDb = 0.0f;
    float pan = 0.0f;
};

struct ClipState {
    TrackId track;
    std::int64_t start = 0;
    std::int64_t length = 0;
    std::string file;
};

struct Project {
    double sampleRate = 48000.0;
    std::uint32_t nextTrackId = 1;
    std::vector<TrackState> tracks; // in display order
    std::vector<ClipState> clips;
};

inline bool operator==(const TrackState& a, const TrackState& b) {
    return a.id == b.id && a.name == b.name && a.gainDb == b.gainDb && a.pan == b.pan;
}
inline bool operator==(const ClipState& a, const ClipState& b) {
    return a.track == b.track && a.start == b.start && a.length == b.length && a.file == b.file;
}
inline bool operator==(const Project& a, const Project& b) {
    return a.sampleRate == b.sampleRate && a.nextTrackId == b.nextTrackId && a.tracks == b.tracks && a.clips == b.clips;
}
inline bool operator!=(const Project& a, const Project& b) { return !(a == b); }

// The display position of a track, or -1.
inline int indexOf(const Project& p, TrackId id) noexcept {
    for (size_t i = 0; i < p.tracks.size(); ++i)
        if (p.tracks[i].id == id)
            return static_cast<int>(i);
    return -1;
}

// ---- Step 5: edits ------------------------------------------------------------------------------

// Puts `track` back at display position `index` (clamped), together with its clips, each at its
// old position in Project::clips (ascending). The inverse of RemoveTrack - and how a new track is
// added, with no clips (see newTrack()).
struct AddTrack {
    TrackState track;
    int index = 0;
    std::vector<std::pair<int, ClipState>> clips; // (position in Project::clips, clip)
};
// Removes the track and every clip on it.
struct RemoveTrack {
    TrackId id;
};
struct SetGain {
    TrackId id;
    float gainDb = 0.0f;
};
struct RenameTrack {
    TrackId id;
    std::string name;
};
// Moves the track to display position `index` (clamped), as in Step 1.
struct MoveTrack {
    TrackId id;
    int index = 0;
};

using Edit = std::variant<AddTrack, RemoveTrack, SetGain, RenameTrack, MoveTrack>;

// One user action: one or more edits that are done - and undone - together (deleting three
// selected tracks is one action, and one undo).
using Change = std::vector<Edit>;

// The edit that adds a NEW track at the end. Uses nextTrackId; applyEdit() then moves the counter on.
inline AddTrack newTrack(const Project& p, std::string name) {
    return AddTrack{TrackState{TrackId{p.nextTrackId}, std::move(name), 0.0f, 0.0f}, static_cast<int>(p.tracks.size()), {}};
}

// YOUR TURN (starter/step05.cpp). Applies `edit` to `p` and returns the edit that undoes it. If
// the edit does not apply - its track does not exist, or (AddTrack) its id is invalid or already
// in use - returns std::nullopt and leaves `p` unchanged.
//   AddTrack     insert the track and its clips; nextTrackId = max(nextTrackId, id + 1)
//                (never DOWN: undoing an add must not free the id)       inverse: RemoveTrack
//   RemoveTrack  remember the track, its position and its clips (with their positions), then
//                erase the clips and the track                             inverse: AddTrack
//   SetGain, RenameTrack, MoveTrack: remember the old value                inverse: the same kind
std::optional<Edit> applyEdit(Project& p, const Edit& edit);

// Provided: applies a whole change, all or nothing. On success returns true and `inverses` holds
// the inverse of each edit, in the order they were applied. If one edit does not apply, the ones
// already applied are taken back (last first), `p` is as before, and the result is false.
inline bool applyAll(Project& p, const Change& change, Change& inverses) {
    inverses.clear();
    for (const Edit& e : change) {
        std::optional<Edit> inverse = applyEdit(p, e);
        if (!inverse) {
            for (auto it = inverses.rbegin(); it != inverses.rend(); ++it)
                applyEdit(p, *it);
            inverses.clear();
            return false;
        }
        inverses.push_back(std::move(*inverse));
    }
    return true;
}

// ---- Step 5: the history ------------------------------------------------------------------------

class History {
public:
    explicit History(std::size_t limit = 100) : limit_(std::max<std::size_t>(limit, 1)) {}

    // ---- YOUR TURN (starter/step05.cpp) ---------------------------------------------------------
    // Applies the change (applyAll). False and no change to project or history if it does not
    // apply. Otherwise:
    //   - mergeKey != 0 and equal to the open merge key, and there is an undo entry: MERGE - keep
    //     that entry as it is (its inverses already lead back to the state before the first
    //     edit of the drag); the new inverses are not needed;
    //   - else push a new entry {inverses, mergeKey, a new serial}, drop the oldest entry when
    //     there are more than limit; the open merge key becomes mergeKey;
    //   - either way the redo stack is cleared (a new edit starts a new future).
    bool perform(Project& p, const Change& change, std::uint32_t mergeKey = 0);
    // Takes back the newest entry: applies its inverses LAST FIRST, and pushes the inverses of
    // those (in the original order) as a redo entry with the same serial. Closes the open merge.
    // False if there is nothing to undo - or if the entry no longer applies (the project was
    // changed outside the history): then the project is unchanged and that entry is dropped.
    bool undo(Project& p);
    // The mirror image of undo(): applies the newest redo entry and pushes its inverses back on
    // the undo stack (dropping the oldest entry when over the limit). Closes the open merge.
    bool redo(Project& p);

    // ---- provided -----------------------------------------------------------------------------
    // Ends a merge, e.g. when the mouse button is released after a fader drag.
    void endMerge() noexcept { openMergeKey_ = 0; }

    // "Modified since the last save": the newest undo entry is not the one that was newest at
    // the save. Serials are never reused, so this stays right after undo, redo and dropped entries.
    void markSaved() noexcept {
        savedSerial_ = currentSerial();
        endMerge(); // a drag continued after saving must count as a modification
    }
    bool isModified() const noexcept { return currentSerial() != savedSerial_; }

    bool canUndo() const noexcept { return !undo_.empty(); }
    bool canRedo() const noexcept { return !redo_.empty(); }
    std::size_t undoCount() const noexcept { return undo_.size(); }
    std::size_t redoCount() const noexcept { return redo_.size(); }

private:
    struct Entry {
        Change edits;               // undo stack: the inverses; redo stack: the edits to redo
        std::uint32_t mergeKey = 0;
        std::uint64_t serial = 0;   // which change this is; 0 = "nothing"
    };

    std::uint64_t currentSerial() const noexcept { return undo_.empty() ? 0 : undo_.back().serial; }

    std::deque<Entry> undo_;        // oldest first
    std::vector<Entry> redo_;       // the next redo last
    std::size_t limit_;
    std::uint32_t openMergeKey_ = 0;
    std::uint64_t nextSerial_ = 1;
    std::uint64_t savedSerial_ = 0; // a new, empty history counts as saved
};

} // namespace minidaw
