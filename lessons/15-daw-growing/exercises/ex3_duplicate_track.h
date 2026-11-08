#pragma once
// Exercise 3 - Duplicate a track. The model is Step 5's (model.h); you write two functions.
//
//   copyName(p, "Bass")      -> "Bass (2)"   ("Bass (3)" if "Bass (2)" is taken, and so on)
//   copyName(p, "Bass (2)")  -> "Bass (3)"   (a trailing " (<digits>)" is a number, not the name)
//   duplicateTrack(p, Bass)  -> one Change = { AddTrack{copy of Bass, new id, right after Bass,
//                                              copies of Bass's clips} }
//
// Duplicating is not a new kind of edit: it is an AddTrack, so Step 5 already knows how to undo it.

#include "model.h"

#include <optional>
#include <string>

namespace minidaw {

// The name for a copy of a track called `name`: its base name (without a trailing " (n)", n made
// of digits) followed by " (k)", with the smallest k >= 2 that no track of `p` uses as its name.
std::string copyName(const Project& p, const std::string& name);

// The change that duplicates track `id`, or nullopt if there is no such track. The new track:
//   - gets the next id (p.nextTrackId), the name copyName(), the same gain and pan;
//   - sits right after the original in the display order;
//   - gets a copy of every clip of the original (same start, length and file, its own track id),
//     in their order in p.clips, appended after all existing clips.
// `p` is not changed: History::perform() applies the change, so one undo takes it back.
std::optional<Change> duplicateTrack(const Project& p, TrackId id);

} // namespace minidaw
