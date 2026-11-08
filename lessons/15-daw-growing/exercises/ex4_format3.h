#pragma once
// Exercise 4 - Format 3: an upgrade, and lines from a newer version.
//
// Step 4 reads formats 1 and 2 and refuses everything newer. This exercise makes two changes:
//
// 1. Format 3 adds a mute flag to every track:   track 2 "Bass" -6 0.25 1
//    Files of format 1 and 2 still open (the upgrade): their tracks are not muted. The program
//    always writes format 3.
//
// 2. Versions get a minor number: "DawLabProject 3.2". A minor version only ADDS lines that an
//    older program can skip without changing what the rest means (a colour, a marker, a note).
//    So a file from a newer MINOR version opens: every line with a keyword this program does not
//    know is KEPT, as it is, with a warning, and written back when the project is saved - so
//    opening and saving with an old version does not destroy what the new one wrote. A newer MAJOR
//    version (4) is still refused, and so is an unknown keyword in a file of a version this program
//    knows completely (3 or 3.0): that file is broken, not newer.
//
//    Saving keeps the minor version of the kept lines: a file with kept lines from 3.2 is written
//    as "DawLabProject 3.2", or this program would refuse its own file the next time.
//
// The model, splitLine(), quote() and parseNumber() are Step 4's (steps/step04-project-files).

#include "../steps/step04-project-files/minidaw.h"

#include <string>
#include <string_view>
#include <vector>

namespace ex4 {

using minidaw::parseNumber;
using minidaw::quote;
using minidaw::splitLine;
using minidaw::Token;
using minidaw::TrackId;

constexpr int kFormatMajor = 3; // 1: no pan; 2: pan; 3: mute
constexpr int kFormatMinor = 0; // this program knows every line of 3.0

struct TrackState {
    TrackId id;
    std::string name;
    float gainDb = 0.0f;
    float pan = 0.0f;
    bool mute = false; // format 3
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
    std::vector<TrackState> tracks;
    std::vector<ClipState> clips;
    // Lines of a newer minor version, as they were in the file (without the line end), in order,
    // and that minor version (0 if there are none).
    std::vector<std::string> keptLines;
    int keptMinor = 0;
};

inline bool operator==(const TrackState& a, const TrackState& b) {
    return a.id == b.id && a.name == b.name && a.gainDb == b.gainDb && a.pan == b.pan && a.mute == b.mute;
}
inline bool operator==(const ClipState& a, const ClipState& b) {
    return a.track == b.track && a.start == b.start && a.length == b.length && a.file == b.file;
}
inline bool operator==(const Project& a, const Project& b) {
    return a.sampleRate == b.sampleRate && a.nextTrackId == b.nextTrackId && a.tracks == b.tracks && a.clips == b.clips &&
           a.keptLines == b.keptLines && a.keptMinor == b.keptMinor;
}

struct LoadResult {
    bool ok = false;
    Project project;
    std::string error;                 // "line 4: ...", the first problem
    int major = 0, minor = 0;          // the version the file was written in
    std::vector<std::string> warnings; // "line 7: \"colour\" is from DawLab format 3.2, kept as it is"
};

// ---- YOUR TURN (starter/ex4_format3.cpp) ------------------------------------------------------
// The file in format 3: "DawLabProject 3", or "DawLabProject 3.<keptMinor>" when there are kept
// lines; the kept lines come last, as they are.
std::string writeProject(const Project& project);
// Step 4's reader, with versions "N" and "N.M", the mute flag, and kept lines (see above).
LoadResult readProject(std::string_view text, double targetSampleRate);

// ---- provided -----------------------------------------------------------------------------------
// Step 4's conversion, unchanged.
inline void convertSampleRate(Project& project, double newRate) {
    if (newRate <= 0.0 || newRate == project.sampleRate)
        return;
    const double ratio = newRate / project.sampleRate;
    for (ClipState& c : project.clips) {
        const std::int64_t start = std::llround(static_cast<double>(c.start) * ratio);
        const std::int64_t end = std::llround(static_cast<double>(c.start + c.length) * ratio);
        c.start = start;
        c.length = std::max<std::int64_t>(1, end - start);
    }
    project.sampleRate = newRate;
}

} // namespace ex4
