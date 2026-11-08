// Exercise 3 (solution): duplicate a track.
//
// Build and run:  l15_ex3_solution   (ctest, label "solution")
// Your version:   exercises/starter/ex3_duplicate_track.cpp -> l15_ex3_starter

#include "../exercises/ex3_duplicate_track.h"

#include <algorithm>
#include <cctype>

namespace minidaw {

namespace {

// "Bass (12)" -> "Bass"; anything else stays as it is.
std::string baseName(const std::string& name) {
    if (name.size() < 4 || name.back() != ')')
        return name;
    const size_t open = name.rfind(" (");
    if (open == std::string::npos)
        return name;
    const size_t first = open + 2, last = name.size() - 1; // the digits are [first, last)
    if (first == last)
        return name;
    for (size_t i = first; i < last; ++i)
        if (!std::isdigit(static_cast<unsigned char>(name[i])))
            return name;
    return name.substr(0, open);
}

bool nameTaken(const Project& p, const std::string& name) {
    return std::any_of(p.tracks.begin(), p.tracks.end(), [&name](const TrackState& t) { return t.name == name; });
}

} // namespace

std::string copyName(const Project& p, const std::string& name) {
    const std::string base = baseName(name);
    for (int k = 2;; ++k) {
        std::string candidate = base + " (" + std::to_string(k) + ")";
        if (!nameTaken(p, candidate))
            return candidate;
    }
}

std::optional<Change> duplicateTrack(const Project& p, TrackId id) {
    const int index = indexOf(p, id);
    if (index < 0)
        return std::nullopt;
    const TrackState& original = p.tracks[static_cast<size_t>(index)];
    AddTrack add{TrackState{TrackId{p.nextTrackId}, copyName(p, original.name), original.gainDb, original.pan}, index + 1, {}};
    int position = static_cast<int>(p.clips.size());
    for (const ClipState& clip : p.clips) {
        if (clip.track != id)
            continue;
        ClipState copy = clip;
        copy.track = add.track.id;
        add.clips.emplace_back(position++, std::move(copy));
    }
    return Change{std::move(add)};
}

} // namespace minidaw

#include "../exercises/ex3_check.h"
