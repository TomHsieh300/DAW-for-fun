// Step 5 (solution): undo and redo.
//
// Build and run:  l15_step05_solution   (ctest, label "solution")
// Your version:   starter/step05.cpp -> l15_step05_starter

#include "../minidaw.h"

#include <algorithm>
#include <utility>

namespace minidaw {

namespace {

// One overload per kind of edit; each returns the inverse, or nullopt with `p` untouched.

std::optional<Edit> applyKind(Project& p, const AddTrack& e) {
    if (!e.track.id.isValid() || indexOf(p, e.track.id) >= 0)
        return std::nullopt;
    const int index = std::clamp(e.index, 0, static_cast<int>(p.tracks.size()));
    p.tracks.insert(p.tracks.begin() + index, e.track);
    for (const auto& [pos, clip] : e.clips) { // ascending positions: each lands where it was
        const int at = std::clamp(pos, 0, static_cast<int>(p.clips.size()));
        p.clips.insert(p.clips.begin() + at, clip);
    }
    p.nextTrackId = std::max(p.nextTrackId, e.track.id.value + 1); // up only: an undone add keeps its id used
    return RemoveTrack{e.track.id};
}

std::optional<Edit> applyKind(Project& p, const RemoveTrack& e) {
    const int index = indexOf(p, e.id);
    if (index < 0)
        return std::nullopt;
    AddTrack inverse{p.tracks[static_cast<size_t>(index)], index, {}};
    for (size_t i = 0; i < p.clips.size(); ++i)
        if (p.clips[i].track == e.id)
            inverse.clips.emplace_back(static_cast<int>(i), p.clips[i]);
    p.clips.erase(std::remove_if(p.clips.begin(), p.clips.end(), [&e](const ClipState& c) { return c.track == e.id; }),
                  p.clips.end());
    p.tracks.erase(p.tracks.begin() + index);
    return inverse;
}

std::optional<Edit> applyKind(Project& p, const SetGain& e) {
    const int index = indexOf(p, e.id);
    if (index < 0)
        return std::nullopt;
    TrackState& t = p.tracks[static_cast<size_t>(index)];
    const SetGain inverse{e.id, t.gainDb};
    t.gainDb = e.gainDb;
    return inverse;
}

std::optional<Edit> applyKind(Project& p, const RenameTrack& e) {
    const int index = indexOf(p, e.id);
    if (index < 0)
        return std::nullopt;
    TrackState& t = p.tracks[static_cast<size_t>(index)];
    RenameTrack inverse{e.id, std::move(t.name)};
    t.name = e.name;
    return inverse;
}

std::optional<Edit> applyKind(Project& p, const MoveTrack& e) {
    const int from = indexOf(p, e.id);
    if (from < 0)
        return std::nullopt;
    const int to = std::clamp(e.index, 0, static_cast<int>(p.tracks.size()) - 1);
    TrackState t = std::move(p.tracks[static_cast<size_t>(from)]);
    p.tracks.erase(p.tracks.begin() + from);
    p.tracks.insert(p.tracks.begin() + to, std::move(t));
    return MoveTrack{e.id, from};
}

// Applies `edits` last first (an undo); on success `inverses` is in the ORIGINAL order again.
bool applyReversed(Project& p, const Change& edits, Change& inverses) {
    const Change reversed(edits.rbegin(), edits.rend());
    if (!applyAll(p, reversed, inverses))
        return false;
    std::reverse(inverses.begin(), inverses.end());
    return true;
}

} // namespace

std::optional<Edit> applyEdit(Project& p, const Edit& edit) {
    return std::visit([&p](const auto& e) { return applyKind(p, e); }, edit);
}

bool History::perform(Project& p, const Change& change, std::uint32_t mergeKey) {
    Change inverses;
    if (!applyAll(p, change, inverses))
        return false; // nothing changed, not even the redo stack
    redo_.clear();
    if (mergeKey != 0 && mergeKey == openMergeKey_ && !undo_.empty())
        return true; // the newest entry already leads back to before the drag
    undo_.push_back(Entry{std::move(inverses), mergeKey, nextSerial_++});
    if (undo_.size() > limit_)
        undo_.pop_front();
    openMergeKey_ = mergeKey;
    return true;
}

bool History::undo(Project& p) {
    endMerge();
    if (undo_.empty())
        return false;
    Change redoEdits;
    if (!applyReversed(p, undo_.back().edits, redoEdits)) {
        undo_.pop_back(); // no longer applies (the project changed behind our back): drop it
        return false;
    }
    redo_.push_back(Entry{std::move(redoEdits), 0, undo_.back().serial});
    undo_.pop_back();
    return true;
}

bool History::redo(Project& p) {
    endMerge();
    if (redo_.empty())
        return false;
    Change undoEdits;
    if (!applyAll(p, redo_.back().edits, undoEdits)) {
        redo_.pop_back();
        return false;
    }
    undo_.push_back(Entry{std::move(undoEdits), 0, redo_.back().serial});
    redo_.pop_back();
    if (undo_.size() > limit_)
        undo_.pop_front();
    return true;
}

} // namespace minidaw

#include "../check.h"
