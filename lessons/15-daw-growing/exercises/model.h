#pragma once
// model.h - the model of Step 5, solved, for Exercises 3 and 5.
//
// Step 5's minidaw.h (Project, TrackState, ClipState, the edits, History) with applyEdit() and
// History defined here, exactly as in steps/step05-undo/solution/step05.cpp. Include it in ONE
// source file per program: the functions minidaw.h only declares are defined here.

#include "../steps/step05-undo/minidaw.h"

#include <algorithm>
#include <utility>

namespace minidaw {

namespace model_detail {

inline std::optional<Edit> applyKind(Project& p, const AddTrack& e) {
    if (!e.track.id.isValid() || indexOf(p, e.track.id) >= 0)
        return std::nullopt;
    const int index = std::clamp(e.index, 0, static_cast<int>(p.tracks.size()));
    p.tracks.insert(p.tracks.begin() + index, e.track);
    for (const auto& [pos, clip] : e.clips) {
        const int at = std::clamp(pos, 0, static_cast<int>(p.clips.size()));
        p.clips.insert(p.clips.begin() + at, clip);
    }
    p.nextTrackId = std::max(p.nextTrackId, e.track.id.value + 1);
    return RemoveTrack{e.track.id};
}

inline std::optional<Edit> applyKind(Project& p, const RemoveTrack& e) {
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

inline std::optional<Edit> applyKind(Project& p, const SetGain& e) {
    const int index = indexOf(p, e.id);
    if (index < 0)
        return std::nullopt;
    TrackState& t = p.tracks[static_cast<size_t>(index)];
    const SetGain inverse{e.id, t.gainDb};
    t.gainDb = e.gainDb;
    return inverse;
}

inline std::optional<Edit> applyKind(Project& p, const RenameTrack& e) {
    const int index = indexOf(p, e.id);
    if (index < 0)
        return std::nullopt;
    TrackState& t = p.tracks[static_cast<size_t>(index)];
    RenameTrack inverse{e.id, std::move(t.name)};
    t.name = e.name;
    return inverse;
}

inline std::optional<Edit> applyKind(Project& p, const MoveTrack& e) {
    const int from = indexOf(p, e.id);
    if (from < 0)
        return std::nullopt;
    const int to = std::clamp(e.index, 0, static_cast<int>(p.tracks.size()) - 1);
    TrackState t = std::move(p.tracks[static_cast<size_t>(from)]);
    p.tracks.erase(p.tracks.begin() + from);
    p.tracks.insert(p.tracks.begin() + to, std::move(t));
    return MoveTrack{e.id, from};
}

} // namespace model_detail

std::optional<Edit> applyEdit(Project& p, const Edit& edit) {
    return std::visit([&p](const auto& e) { return model_detail::applyKind(p, e); }, edit);
}

// Applies `edits` last first (an undo); on success `inverses` is in the ORIGINAL order again.
inline bool applyReversed(Project& p, const Change& edits, Change& inverses) {
    const Change reversed(edits.rbegin(), edits.rend());
    if (!applyAll(p, reversed, inverses))
        return false;
    std::reverse(inverses.begin(), inverses.end());
    return true;
}

bool History::perform(Project& p, const Change& change, std::uint32_t mergeKey) {
    Change inverses;
    if (!applyAll(p, change, inverses))
        return false;
    redo_.clear();
    if (mergeKey != 0 && mergeKey == openMergeKey_ && !undo_.empty())
        return true;
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
        undo_.pop_back();
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

// A small project for the checks: Drums (id 1), Bass (id 2), Keys (id 3), four clips.
inline Project exampleProject() {
    Project p;
    p.nextTrackId = 4;
    p.tracks = {{TrackId{1}, "Drums", -3.0f, 0.0f}, {TrackId{2}, "Bass", -6.0f, -0.25f}, {TrackId{3}, "Keys", 0.0f, 0.5f}};
    p.clips = {{TrackId{1}, 0, 96000, "drums.wav"},
               {TrackId{2}, 0, 48000, "bass-a.wav"},
               {TrackId{3}, 24000, 24000, "keys.wav"},
               {TrackId{2}, 48000, 48000, "bass-b.wav"}};
    return p;
}

} // namespace minidaw
