#pragma once
// Exercise 4 - the self-check (main). Included at the end of the starter and of the solution.

#include "ex4_format3.h"

#include <dawlab/Check.h>

#include <cstdio>

namespace ex4::check {

inline bool contains(const std::string& s, std::string_view part) { return s.find(part) != std::string::npos; }

inline Project song() {
    Project p;
    p.sampleRate = 48000.0;
    p.nextTrackId = 4;
    p.tracks = {{TrackId{1}, "Drums", -3.0f, 0.0f, false}, {TrackId{3}, "Bass \"low\"", -6.5f, -0.25f, true}};
    p.clips = {{TrackId{1}, 0, 96000, "drums.wav"}, {TrackId{3}, 48000, 48000, "bass.wav"}};
    return p;
}

inline void checkFormat3() {
    const Project p = song();
    const std::string text = writeProject(p);
    DAWLAB_CHECK(text.rfind("DawLabProject 3\n", 0) == 0);
    DAWLAB_CHECK(contains(text, "track 3 \"Bass \\\"low\\\"\" -6.5 -0.25 1\n"));
    DAWLAB_CHECK(contains(text, "track 1 \"Drums\" -3 0 0\n"));
    const LoadResult r = readProject(text, 0.0);
    DAWLAB_CHECK(r.ok && r.major == 3 && r.minor == 0 && r.warnings.empty());
    DAWLAB_CHECK(r.project == p);

    // A mute flag is 0 or 1, and format 3 needs it.
    LoadResult bad = readProject("DawLabProject 3\nsampleRate 48000\ntrack 1 \"A\" 0 0\n", 0.0);
    DAWLAB_CHECK(!bad.ok && contains(bad.error, "line 3"));
    bad = readProject("DawLabProject 3\nsampleRate 48000\ntrack 1 \"A\" 0 0 2\n", 0.0);
    DAWLAB_CHECK(!bad.ok && contains(bad.error, "line 3") && contains(bad.error, "mute"));
}

inline void checkUpgrade() {
    // Format 1 (no pan) and 2 (no mute) open; their tracks are not muted.
    const LoadResult one = readProject("DawLabProject 1\nsampleRate 44100\ntrack 1 \"Old\" -2\nclip 1 0 44100 \"a.wav\"\n", 48000.0);
    DAWLAB_CHECK(one.ok && one.major == 1);
    DAWLAB_CHECK(one.ok && one.project.tracks.size() == 1 && !one.project.tracks[0].mute && one.project.tracks[0].pan == 0.0f);
    DAWLAB_CHECK(one.ok && one.project.clips.size() == 1 && one.project.clips[0].length == 48000); // converted
    const LoadResult two = readProject("DawLabProject 2\nsampleRate 48000\nnextTrackId 3\ntrack 2 \"B\" 0 0.5\n", 0.0);
    DAWLAB_CHECK(two.ok && two.major == 2 && two.project.tracks.size() == 1 && !two.project.tracks[0].mute);
    // ... and are written as format 3.
    if (two.ok) {
        const std::string text = writeProject(two.project);
        DAWLAB_CHECK(text.rfind("DawLabProject 3\n", 0) == 0 && contains(text, "track 2 \"B\" 0 0.5 0\n"));
    }
    // A format-2 track line with a mute flag is wrong.
    DAWLAB_CHECK(!readProject("DawLabProject 2\nsampleRate 48000\ntrack 2 \"B\" 0 0.5 1\n", 0.0).ok);
}

inline void checkNewerMinor() {
    const std::string newer = "DawLabProject 3.2\r\n"
                              "sampleRate 48000\r\n"
                              "colour 1 \"#ff8800\"\r\n"
                              "track 1 \"Drums\" -3 0 0\r\n"
                              "marker 96000 \"Chorus\" # the first chorus\r\n"
                              "clip 1 0 96000 \"drums.wav\"\r\n";
    const LoadResult r = readProject(newer, 0.0);
    DAWLAB_CHECK(r.ok);
    if (!r.ok) {
        std::printf("  newer minor: %s\n", r.error.c_str());
        return;
    }
    DAWLAB_CHECK(r.major == 3 && r.minor == 2);
    DAWLAB_CHECK(r.project.keptMinor == 2);
    DAWLAB_CHECK(r.project.keptLines.size() == 2);
    if (r.project.keptLines.size() == 2) {
        DAWLAB_CHECK(r.project.keptLines[0] == "colour 1 \"#ff8800\"");                          // as it was,
        DAWLAB_CHECK(r.project.keptLines[1] == "marker 96000 \"Chorus\" # the first chorus");    // without "\r"
    }
    DAWLAB_CHECK(r.warnings.size() == 2);
    if (r.warnings.size() == 2) {
        std::printf("  %s\n  %s\n", r.warnings[0].c_str(), r.warnings[1].c_str());
        DAWLAB_CHECK(contains(r.warnings[0], "line 3") && contains(r.warnings[0], "colour") && contains(r.warnings[0], "3.2"));
        DAWLAB_CHECK(contains(r.warnings[1], "line 5") && contains(r.warnings[1], "marker"));
    }
    DAWLAB_CHECK(r.project.tracks.size() == 1 && r.project.clips.size() == 1);

    // Saved by this version: the kept lines go back, under the newer minor version, and it opens again.
    const std::string saved = writeProject(r.project);
    DAWLAB_CHECK(saved.rfind("DawLabProject 3.2\n", 0) == 0);
    DAWLAB_CHECK(contains(saved, "\ncolour 1 \"#ff8800\"\nmarker 96000 \"Chorus\" # the first chorus\n"));
    const LoadResult again = readProject(saved, 0.0);
    DAWLAB_CHECK(again.ok && again.project == r.project);

    // A newer minor version without unknown lines keeps nothing, and is written as plain 3.
    const LoadResult plain = readProject("DawLabProject 3.1\nsampleRate 48000\ntrack 1 \"A\" 0 0 1\n", 0.0);
    DAWLAB_CHECK(plain.ok && plain.minor == 1 && plain.project.keptLines.empty() && plain.project.keptMinor == 0);
    DAWLAB_CHECK(plain.ok && writeProject(plain.project).rfind("DawLabProject 3\n", 0) == 0);
}

inline void checkRefused() {
    // An unknown line in a version this program knows completely: broken, not newer.
    for (const char* header : {"DawLabProject 3\n", "DawLabProject 3.0\n", "DawLabProject 2\n"}) {
        const LoadResult r = readProject(std::string(header) + "sampleRate 48000\ncolour 1 \"red\"\n", 0.0);
        DAWLAB_CHECK(!r.ok && contains(r.error, "line 3") && contains(r.error, "colour"));
    }
    // A newer major version.
    for (const char* header : {"DawLabProject 4\n", "DawLabProject 4.0\n"}) {
        const LoadResult r = readProject(std::string(header) + "sampleRate 48000\n", 0.0);
        DAWLAB_CHECK(!r.ok && contains(r.error, "line 1") && contains(r.error, "newer"));
    }
    // Versions that are not versions.
    for (const char* header : {"DawLabProject 3.\n", "DawLabProject .2\n", "DawLabProject 3.x\n", "DawLabProject 3.-1\n",
                               "DawLabProject 3.1.1\n", "DawLabProject 0\n"}) {
        const LoadResult r = readProject(std::string(header) + "sampleRate 48000\n", 0.0);
        DAWLAB_CHECK(!r.ok && contains(r.error, "line 1"));
    }
    // A kept line must still be a line: an unterminated string is an error even in 3.2.
    DAWLAB_CHECK(!readProject("DawLabProject 3.2\nsampleRate 48000\ncolour 1 \"red\n", 0.0).ok);
}

} // namespace ex4::check

int main() {
    std::printf("Exercise 4 - format 3, and lines from a newer version\n");
    ex4::check::checkFormat3();
    ex4::check::checkUpgrade();
    ex4::check::checkNewerMinor();
    ex4::check::checkRefused();
    return dawlab::test::summary("l15_ex4");
}
