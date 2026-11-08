#pragma once
// Exercise 3 - the self-check (main). Included at the end of the starter and of the solution.

#include "ex3_duplicate_track.h"

#include <dawlab/Check.h>

#include <cstdio>

namespace minidaw::ex3_check {

inline void checkNames() {
    Project p = exampleProject();
    DAWLAB_CHECK(copyName(p, "Bass") == "Bass (2)");
    p.tracks.push_back({TrackId{9}, "Bass (2)", 0.0f, 0.0f});
    DAWLAB_CHECK(copyName(p, "Bass") == "Bass (3)");
    DAWLAB_CHECK(copyName(p, "Bass (2)") == "Bass (3)"); // a copy of a copy is still a Bass
    p.tracks.push_back({TrackId{10}, "Bass (4)", 0.0f, 0.0f});
    DAWLAB_CHECK(copyName(p, "Bass (4)") == "Bass (3)"); // the smallest free number
    DAWLAB_CHECK(copyName(p, "Take (x)") == "Take (x) (2)");     // not a number
    DAWLAB_CHECK(copyName(p, "Take ()") == "Take () (2)");       // no digits
    DAWLAB_CHECK(copyName(p, "Take(2)") == "Take(2) (2)");       // no space before it
    DAWLAB_CHECK(copyName(p, "Mix 2 (12)") == "Mix 2 (2)");      // only the last " (n)" goes
    DAWLAB_CHECK(copyName(p, "Keys") == "Keys (2)");
}

inline void checkDuplicate() {
    const Project before = exampleProject();
    Project p = before;
    History h;

    DAWLAB_CHECK(!duplicateTrack(p, TrackId{7}).has_value()); // no such track
    DAWLAB_CHECK(p == before);

    const std::optional<Change> change = duplicateTrack(p, TrackId{2});
    DAWLAB_CHECK(change.has_value());
    if (!change)
        return;
    DAWLAB_CHECK(p == before); // only a description of the change
    DAWLAB_CHECK(change->size() == 1 && std::holds_alternative<AddTrack>(change->front()));
    if (change->size() != 1 || !std::holds_alternative<AddTrack>(change->front()))
        return;
    const AddTrack& add = std::get<AddTrack>(change->front());
    DAWLAB_CHECK(add.track.id == TrackId{4});
    DAWLAB_CHECK(add.track.name == "Bass (2)");
    DAWLAB_CHECK(add.track.gainDb == -6.0f && add.track.pan == -0.25f);
    DAWLAB_CHECK(add.index == 2);
    DAWLAB_CHECK(add.clips.size() == 2);
    if (add.clips.size() == 2) {
        DAWLAB_CHECK(add.clips[0].first == 4 && add.clips[1].first == 5); // appended, in order
        DAWLAB_CHECK((add.clips[0].second == ClipState{TrackId{4}, 0, 48000, "bass-a.wav"}));
        DAWLAB_CHECK((add.clips[1].second == ClipState{TrackId{4}, 48000, 48000, "bass-b.wav"}));
    }

    // One step in the history, and back.
    DAWLAB_CHECK(h.perform(p, *change));
    DAWLAB_CHECK(p.tracks.size() == 4 && p.tracks[2].name == "Bass (2)" && p.tracks[3].name == "Keys");
    DAWLAB_CHECK(p.clips.size() == 6 && p.nextTrackId == 5);
    const Project duplicated = p;
    DAWLAB_CHECK(h.undo(p));
    Project expected = before;
    expected.nextTrackId = 5; // an undone add keeps its id used (Step 5)
    DAWLAB_CHECK(p == expected);
    DAWLAB_CHECK(h.redo(p));
    DAWLAB_CHECK(p == duplicated); // the same id again

    // Again: the next number, the next id, right after the original.
    const std::optional<Change> again = duplicateTrack(p, TrackId{2});
    DAWLAB_CHECK(again.has_value() && h.perform(p, *again));
    DAWLAB_CHECK(p.tracks.size() == 5 && p.tracks[2].name == "Bass (3)" && p.tracks[2].id == TrackId{5});
    // A copy of the copy is a Bass too.
    const std::optional<Change> third = duplicateTrack(p, TrackId{4});
    DAWLAB_CHECK(third.has_value() && h.perform(p, *third));
    DAWLAB_CHECK(indexOf(p, TrackId{6}) == indexOf(p, TrackId{4}) + 1 && p.tracks[static_cast<size_t>(indexOf(p, TrackId{6}))].name == "Bass (4)");

    // The copies are independent: deleting the original leaves them and their clips.
    DAWLAB_CHECK(h.perform(p, {RemoveTrack{TrackId{2}}}));
    int copies = 0;
    for (const ClipState& c : p.clips)
        copies += c.file.rfind("bass", 0) == 0 ? 1 : 0;
    DAWLAB_CHECK(copies == 6);
    DAWLAB_CHECK(h.undo(p));
    DAWLAB_CHECK(indexOf(p, TrackId{2}) == 1);

    // A track without clips, and the last track.
    Project q = before;
    History hq;
    DAWLAB_CHECK(hq.perform(q, {newTrack(q, "Empty")}));
    const std::optional<Change> empty = duplicateTrack(q, TrackId{4});
    DAWLAB_CHECK(empty.has_value() && std::get<AddTrack>(empty->front()).clips.empty());
    DAWLAB_CHECK(empty.has_value() && std::get<AddTrack>(empty->front()).index == 4);

    // Undo everything: back to the start, but no id comes back.
    while (h.undo(p)) {
    }
    expected.nextTrackId = 7;
    DAWLAB_CHECK(p == expected);
}

} // namespace minidaw::ex3_check

int main() {
    std::printf("Exercise 3 - duplicate a track\n");
    minidaw::ex3_check::checkNames();
    minidaw::ex3_check::checkDuplicate();
    return dawlab::test::summary("l15_ex3");
}
