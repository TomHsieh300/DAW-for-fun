#pragma once
// check.h - the self-check of Step 4 (saving and opening projects). Defines main(); included at
// the END of starter/step04.cpp and solution/step04.cpp, so both run exactly the same checks.
//
// Tests only what Step 4 adds: an exact round trip (awkward names, float values), ids and the id
// counter surviving a save, an old format version, a newer one, broken files with the line of the
// problem, another sample rate, a locale that writes decimal commas, and the save on disk.

#include <dawlab/Common.h>
#include <dawlab/WavFile.h>

#include <cstdio>
#include <filesystem>
#include <locale>
#include <string>
#include <vector>

namespace step04_check {

using namespace minidaw;

inline Project sampleProject() {
    Project p;
    p.sampleRate = 48000.0;
    p.nextTrackId = 5; // tracks 1..4 existed; 3 was deleted
    p.tracks = {
        {TrackId{2}, "Bass", -6.5f, 0.25f},
        {TrackId{1}, "Drums \"live\" \\ room", 0.1f, -1.0f}, // a quote and a backslash in the name
        {TrackId{4}, "Gitarre \xC3\xBC / \xE9\x8D\xB5\xE7\x9B\xA4", -0.333333343f, 0.0f}, // UTF-8: "Gitarre ü / 鍵盤"
    };
    p.clips = {
        {TrackId{2}, 0, 96000, "audio/bass take 1.wav"},
        {TrackId{1}, 48000, 1, "kick.wav"},
        {TrackId{4}, 123456789012LL, 480, "two\nlines.wav"}, // a position past 2^32 samples
    };
    return p;
}

inline const char* yesNo(bool b) { return b ? "yes" : "no"; }

inline void roundTrip() {
    const Project p = sampleProject();
    const std::string text = writeProject(p);
    const LoadResult r = readProject(text, 0.0);
    std::printf("  write, then read back: ok %s, version %d, equal %s (expected yes, %d, yes)\n", yesNo(r.ok),
                r.formatVersion, yesNo(r.ok && r.project == p), kFormatVersion);
    DAWLAB_CHECK(text.rfind("DawLabProject 2\n", 0) == 0);
    DAWLAB_CHECK(r.ok && r.formatVersion == kFormatVersion);
    DAWLAB_CHECK(r.ok && r.project == p); // exact: names, floats, 64-bit positions, display order
    DAWLAB_CHECK(r.ok && r.project.nextTrackId == 5); // id 3 stays used up after the reopen
    if (!r.ok)
        std::printf("    (error: %s)\n", r.error.c_str());

    // The same file as Windows Notepad saves it: a byte order mark and CRLF line ends.
    std::string notepad = "\xEF\xBB\xBF";
    for (const char c : text)
        notepad += (c == '\n') ? std::string("\r\n") : std::string(1, c);
    const LoadResult crlf = readProject(notepad, 0.0);
    std::printf("  the same text with a BOM and CRLF line ends: equal %s (expected yes)\n", yesNo(crlf.ok && crlf.project == p));
    DAWLAB_CHECK(crlf.ok && crlf.project == p);
}

inline void handWrittenAndOld() {
    // Hand-written, format 1 (no pan), with comments, blank lines and extra spaces; no nextTrackId.
    const char* v1 =
        "# my first song\n"
        "DawLabProject 1\n"
        "\n"
        "sampleRate   44100\n"
        "track 7 \"Vox\"  -3   # lead\n"
        "track 3 \"Keys\" 0\n"
        "clip 7 0 44100 \"vox.wav\"\n";
    const LoadResult r = readProject(v1, 0.0);
    const bool shape = r.ok && r.project.tracks.size() == 2 && r.project.clips.size() == 1;
    std::printf("  a hand-written format-1 file: ok %s, version %d, Vox pan %.1f, nextTrackId %u (expected yes, 1, 0.0, 8)\n",
                yesNo(r.ok), r.formatVersion, shape ? r.project.tracks[0].pan : -9.0f, r.project.nextTrackId);
    DAWLAB_CHECK(r.ok && r.formatVersion == 1 && shape);
    DAWLAB_CHECK(shape && r.project.tracks[0].id == TrackId{7} && r.project.tracks[0].name == "Vox");
    DAWLAB_CHECK(shape && r.project.tracks[0].gainDb == -3.0f && r.project.tracks[0].pan == 0.0f);
    DAWLAB_CHECK(r.ok && r.project.nextTrackId == 8); // above the highest id, although the file did not say
    DAWLAB_CHECK(r.ok && r.project.sampleRate == 44100.0);
    // Saving it again writes the current format.
    DAWLAB_CHECK(r.ok && writeProject(r.project).rfind("DawLabProject 2\n", 0) == 0);

    // A file from a LATER DawLab: refuse, and say why.
    const LoadResult newer = readProject("DawLabProject 3\nsampleRate 48000\n", 0.0);
    std::printf("  a format-3 file: ok %s, error \"%s\"\n", yesNo(newer.ok), newer.error.c_str());
    DAWLAB_CHECK(!newer.ok && newer.error.find("line 1") == 0 && newer.error.find("newer") != std::string::npos);
}

inline void brokenFiles() {
    struct Case {
        const char* what;
        const char* text;
        int line; // the line the error must name
    };
    const Case cases[] = {
        {"empty file", "", 1},
        {"not a project", "hello\n", 1},
        {"version 0", "DawLabProject 0\n", 1},
        {"no sample rate", "DawLabProject 2\ntrack 1 \"A\" 0 0\n", 2},
        {"sample rate twice", "DawLabProject 2\nsampleRate 48000\nsampleRate 44100\n", 3},
        {"sample rate 0", "DawLabProject 2\nsampleRate 0\n", 2},
        {"unknown keyword", "DawLabProject 2\nsampleRate 48000\ntempo 120\n", 3},
        {"track id 0", "DawLabProject 2\nsampleRate 48000\ntrack 0 \"A\" 0 0\n", 3},
        {"duplicate id", "DawLabProject 2\nsampleRate 48000\ntrack 1 \"A\" 0 0\ntrack 1 \"B\" 0 0\n", 4},
        {"name not quoted", "DawLabProject 2\nsampleRate 48000\ntrack 1 A 0 0\n", 3},
        {"gain not a number", "DawLabProject 2\nsampleRate 48000\ntrack 1 \"A\" loud 0\n", 3},
        {"decimal comma", "DawLabProject 2\nsampleRate 48000\ntrack 1 \"A\" -6,5 0\n", 3},
        {"pan out of range", "DawLabProject 2\nsampleRate 48000\ntrack 1 \"A\" 0 2\n", 3},
        {"pan missing (format 2)", "DawLabProject 2\nsampleRate 48000\ntrack 1 \"A\" 0\n", 3},
        {"pan given (format 1)", "DawLabProject 1\nsampleRate 48000\ntrack 1 \"A\" 0 0\n", 3},
        {"unterminated name", "DawLabProject 2\nsampleRate 48000\ntrack 1 \"A 0 0\n", 3},
        {"negative id", "DawLabProject 2\nsampleRate 48000\ntrack -1 \"A\" 0 0\n", 3},
        {"clip on a missing track", "DawLabProject 2\nsampleRate 48000\ntrack 1 \"A\" 0 0\nclip 9 0 10 \"x.wav\"\n", 4},
        {"clip length 0", "DawLabProject 2\nsampleRate 48000\ntrack 1 \"A\" 0 0\nclip 1 0 0 \"x.wav\"\n", 4},
        {"clip before 0", "DawLabProject 2\nsampleRate 48000\ntrack 1 \"A\" 0 0\nclip 1 -5 10 \"x.wav\"\n", 4},
    };
    int right = 0, total = 0;
    for (const Case& c : cases) {
        ++total;
        const LoadResult r = readProject(c.text, 0.0);
        const std::string prefix = "line " + std::to_string(c.line) + ":";
        const bool good = !r.ok && r.error.rfind(prefix, 0) == 0;
        right += good ? 1 : 0;
        if (!good)
            std::printf("    %-24s ok %s, error \"%s\" (expected it to start with \"%s\")\n", c.what, yesNo(r.ok),
                        r.error.c_str(), prefix.c_str());
    }
    const LoadResult sample = readProject(cases[17].text, 0.0);
    std::printf("  %d of %d broken files refused with the right line, e.g. \"%s\"\n", right, total, sample.error.c_str());
    DAWLAB_CHECK(right == total);
}

inline void otherSampleRate() {
    // 100 clips back to back at 48 kHz, lengths 1001..1100 samples; opened at 44.1 kHz.
    Project p;
    p.tracks = {{TrackId{1}, "A", 0.0f, 0.0f}};
    p.nextTrackId = 2;
    std::int64_t pos = 0;
    for (int len = 1001; len <= 1100; ++len) {
        p.clips.push_back({TrackId{1}, pos, len, "c.wav"});
        pos += len;
    }
    const std::int64_t end48 = pos;
    const LoadResult r = readProject(writeProject(p), 44100.0);
    int gaps = 0;
    std::int64_t end44 = -1;
    if (r.ok && r.project.clips.size() == p.clips.size()) {
        const auto& c = r.project.clips;
        for (size_t i = 1; i < c.size(); ++i)
            if (c[i - 1].start + c[i - 1].length != c[i].start)
                ++gaps;
        end44 = c.back().start + c.back().length;
    }
    const std::int64_t expectedEnd = std::llround(static_cast<double>(end48) * 44100.0 / 48000.0);
    std::printf("  100 touching clips, 48 kHz -> 44.1 kHz: sampleRate %.0f, %d gaps or overlaps, end %lld (expected 44100, 0, %lld)\n",
                r.project.sampleRate, gaps, static_cast<long long>(end44), static_cast<long long>(expectedEnd));
    DAWLAB_CHECK(r.ok && r.project.sampleRate == 44100.0);
    DAWLAB_CHECK(r.ok && gaps == 0);
    DAWLAB_CHECK(end44 == expectedEnd);

    // A clip never shrinks to nothing; the same rate changes nothing.
    Project tiny;
    tiny.tracks = {{TrackId{1}, "A", 0.0f, 0.0f}};
    tiny.clips = {{TrackId{1}, 10, 1, "t.wav"}};
    convertSampleRate(tiny, 8000.0);
    const bool minOne = tiny.sampleRate == 8000.0 && tiny.clips[0].length == 1 && tiny.clips[0].start == 2;
    Project same = sampleProject();
    convertSampleRate(same, 48000.0);
    std::printf("  a 1-sample clip at 48 kHz -> 8 kHz: start %lld, length %lld (expected 2, 1); same rate unchanged: %s\n",
                static_cast<long long>(tiny.clips[0].start), static_cast<long long>(tiny.clips[0].length),
                yesNo(same == sampleProject()));
    DAWLAB_CHECK(minOne);
    DAWLAB_CHECK(same == sampleProject());
}

// A user whose language writes "-6,5": the file must still say "-6.5".
struct DecimalComma : std::numpunct<char> {
    char do_decimal_point() const override { return ','; }
    std::string do_grouping() const override { return "\3"; }
    char do_thousands_sep() const override { return '.'; }
};

inline void locale() {
    const std::locale before = std::locale::global(std::locale(std::locale::classic(), new DecimalComma));
    const std::string text = writeProject(sampleProject());
    const LoadResult r = readProject(text, 0.0);
    std::locale::global(before);
    const bool point = text.find("-6.5") != std::string::npos && text.find("-6,5") == std::string::npos;
    const bool noGrouping = text.find("123456789012") != std::string::npos;
    std::printf("  with a decimal-comma locale: \"-6.5\" written %s, 123456789012 ungrouped %s, reads back equal %s (expected yes x3)\n",
                yesNo(point), yesNo(noGrouping), yesNo(r.ok && r.project == sampleProject()));
    DAWLAB_CHECK(point && noGrouping);
    DAWLAB_CHECK(r.ok && r.project == sampleProject());
}

inline void onDisk() {
    const std::filesystem::path path = dawlab::outputPath("l15_step04.dawlab");
    std::filesystem::path tmp = path;
    tmp += ".tmp";
    std::string error;
    const bool saved = saveProjectFile(sampleProject(), path, error);
    const LoadResult opened = openProjectFile(path, 0.0);

    // Save a SMALLER project over it: nothing of the longer old file may remain.
    Project small;
    small.tracks = {{TrackId{9}, "Solo", 0.0f, 0.0f}};
    small.nextTrackId = 10;
    const bool savedAgain = saveProjectFile(small, path, error);
    const LoadResult reopened = openProjectFile(path, 0.0);

    std::string missingError;
    const bool intoNowhere = saveProjectFile(small, path.parent_path() / "no such folder" / "x.dawlab", missingError);
    std::printf("  on disk (%s): saved %s, opened equal %s; saved a smaller one over it: equal %s; .tmp left: %s;\n"
                "  saving into a missing folder: %s (expected yes, yes, yes, no, refused)\n",
                path.string().c_str(), yesNo(saved), yesNo(opened.ok && opened.project == sampleProject()),
                yesNo(reopened.ok && reopened.project == small), yesNo(std::filesystem::exists(tmp)),
                intoNowhere ? "saved?!" : "refused");
    DAWLAB_CHECK(saved && opened.ok && opened.project == sampleProject());
    DAWLAB_CHECK(savedAgain && reopened.ok && reopened.project == small);
    DAWLAB_CHECK(!std::filesystem::exists(tmp));
    DAWLAB_CHECK(!intoNowhere && !missingError.empty());
}

inline int run() {
    std::printf("Step 4: saving and opening projects (build-along)\n");
    roundTrip();
    handWrittenAndOld();
    brokenFiles();
    otherSampleRate();
    locale();
    onDisk();
    return dawlab::test::summary("l15_step04");
}

} // namespace step04_check

int main() { return step04_check::run(); }
