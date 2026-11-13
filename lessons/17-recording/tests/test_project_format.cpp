// test_project_format: the text of an application project file (chapter 15, Step 8) - pure
// functions, no JUCE, no files. ProjectIO (app/Source) does the files and the Session; the
// app's smoke test checks that part.

#include "ProjectFormat.h"

#include <dawlab/Check.h>

#include <algorithm>
#include <clocale>
#include <cstdio>
#include <iterator>
#include <locale>
#include <stdexcept>
#include <string>

using namespace dawlab;
using namespace dawlab::app::project;
using dawlab::engine::TrackFormat;
using dawlab::engine::TrackId;

namespace {

// A project that uses every field, with values that are easy to get wrong: names with quotes,
// backslashes and line breaks, floats without a short decimal form, an equal-power fade.
ProjectState richProject() {
    ProjectState p;
    p.sampleRate = 44100.0;
    p.nextTrackId = 31;
    p.tempo = 97.5;
    p.beatsPerBar = 7;
    p.beatUnit = 8;
    p.loopStart = 88200;
    p.loopEnd = 352800;
    p.loopEnabled = true;

    TrackState drums;
    drums.id = TrackId{17};
    drums.strip.name = "Drums \"live\"";
    drums.strip.gainDb = -2.0f / 3.0f; // needs all 9 digits to read back exactly
    drums.strip.pan = 0.1f;
    drums.strip.solo = true;
    drums.strip.sends[0] = {-18.25f, false};
    drums.strip.sends[1] = {kMinusInfinityDb, true}; // pre-fader although silent: still written
    InsertState comp;
    comp.slot = 1;
    comp.type = "Compressor";
    comp.params = {{"Threshold", -20.0f}, {"Ratio", 4.0f}, {"Attack", 1.0f / 3.0f}};
    InsertState plugin;
    plugin.slot = 3;
    plugin.vst3 = true;
    plugin.type = "C:\\Program Files\\Common Files\\VST3\\My Limiter.vst3";
    plugin.bypassed = true;
    plugin.state = "VC2!AAAA+/==";
    drums.strip.inserts = {comp, plugin};

    TrackState bass;
    bass.id = TrackId{3};
    bass.format = TrackFormat::Stereo;
    bass.strip.name = "Bass\nline 2\\";
    bass.strip.mute = true;
    p.tracks = {drums, bass}; // display order: ids need not be sorted

    p.buses[0].name = "Reverb";
    p.buses[0].gainDb = -3.0f;
    InsertState reverb;
    reverb.type = "Reverb";
    reverb.params = {{"Size", 0.7f}};
    p.buses[0].inserts = {reverb};
    p.buses[1].name = "Delay";
    p.buses[1].mute = true;
    p.master.name = "Master";
    InsertState limiter;
    limiter.slot = 3;
    limiter.type = "Limiter";
    limiter.params = {{"Ceiling", -1.0f}};
    p.master.inserts = {limiter};

    ClipState a;
    a.track = TrackId{17};
    a.start = 0;
    a.length = 96000;
    a.file = "Song_media/000.wav";
    a.fadeIn = 220;
    a.fadeOut = 221;
    a.name = "kick & snare";
    ClipState b = a;
    b.track = TrackId{3};
    b.start = 123456789012LL; // beyond 32 bits
    b.sourceOffset = 4410;
    b.equalPowerFades = true;
    b.gainDb = -0.3f;
    b.muted = true;
    b.file = "C:/samples/bass \"take 2\".wav";
    p.clips = {a, b};
    return p;
}

void testRoundTrip() {
    const ProjectState p = richProject();
    const std::string text = writeProject(p);
    const ReadResult r = readProject(text);
    const std::string again = r.ok ? writeProject(r.project) : std::string();
    std::printf("round trip: %zu bytes, %d lines, read back %s, equal %s, written again identically %s\n", text.size(),
                static_cast<int>(std::count(text.begin(), text.end(), '\n')), r.ok ? "ok" : r.error.c_str(),
                r.ok && r.project == p ? "yes" : "no", again == text ? "yes" : "no");
    DAWLAB_CHECK(r.ok && r.formatVersion == kFormatVersion);
    DAWLAB_CHECK(r.project == p);
    DAWLAB_CHECK(again == text);
    // An unused send is the default and is not written; the pre-fader one is.
    DAWLAB_CHECK(text.find("send track:17 0 -18.25 0\n") != std::string::npos);
    DAWLAB_CHECK(text.find("send track:17 1 -100 1\n") != std::string::npos);
    DAWLAB_CHECK(text.find("send track:3") == std::string::npos);
}

// The lines that refer to something may come in any order: a parameter before its insert, a
// clip before its track.
void testAnyOrder() {
    const std::string text = "DawLabProject 3\n"
                             "sampleRate 48000\n"
                             "clip 5 0 100 \"a.wav\"\n"
                             "param track:5 2 \"Gain\" 6\n"
                             "insert track:5 2 builtin \"Gain\" 0\n"
                             "insert track:5 0 builtin \"Gate\" 1\n"
                             "track 5 \"Vox\" 0 0 stereo 0 0\n";
    const ReadResult r = readProject(text);
    const bool ok = r.ok && r.project.tracks.size() == 1;
    std::printf("any order: %s\n", ok ? "read" : r.error.c_str());
    DAWLAB_CHECK(ok);
    if (ok) {
        const StripState& s = r.project.tracks[0].strip;
        DAWLAB_CHECK(s.inserts.size() == 2 && s.inserts[0].slot == 0 && s.inserts[1].slot == 2); // sorted by slot
        DAWLAB_CHECK(s.inserts[1].params.size() == 1 && s.inserts[1].params[0].value == 6.0f);
        DAWLAB_CHECK(r.project.nextTrackId == 6); // above every id in the file
        DAWLAB_CHECK(r.project.tempo == 120.0 && !r.project.loopEnabled); // defaults
    }
}

// Step 4's files (formats 1 and 2) still open; what they do not say keeps its default.
void testStep4Files() {
    const ReadResult v2 = readProject("DawLabProject 2\nsampleRate 44100\nnextTrackId 4\ntrack 2 \"Bass\" -6 0.25\n"
                                      "clip 2 0 96000 \"bass.wav\"\n");
    const ReadResult v1 = readProject("DawLabProject 1\nsampleRate 48000\ntrack 1 \"Old\" -3\n");
    const ReadResult v2Insert = readProject("DawLabProject 2\nsampleRate 48000\ntrack 1 \"A\" 0 0\n"
                                            "insert track:1 0 builtin \"Gain\" 0\n");
    std::printf("Step 4 files: format 2 %s, format 1 %s, format 2 with an insert: %s\n", v2.ok ? "read" : v2.error.c_str(),
                v1.ok ? "read" : v1.error.c_str(), v2Insert.error.c_str());
    DAWLAB_CHECK(v2.ok && v2.formatVersion == 2 && v2.project.tracks.size() == 1 && v2.project.clips.size() == 1);
    if (v2.ok) {
        const TrackState& t = v2.project.tracks[0];
        const ClipState& c = v2.project.clips[0];
        DAWLAB_CHECK(t.strip.pan == 0.25f && t.format == TrackFormat::Mono && !t.strip.mute);
        DAWLAB_CHECK(c.file == "bass.wav" && c.sourceOffset == 0 && c.fadeIn == 0 && c.name.empty());
    }
    DAWLAB_CHECK(v1.ok && v1.project.tracks.size() == 1 && v1.project.tracks[0].strip.pan == 0.0f);
    DAWLAB_CHECK(!v2Insert.ok && v2Insert.error.find("line 4") == 0);
}

// Outside data: every problem is refused with the line it is on.
void testErrors() {
    const std::string head = "DawLabProject 3\nsampleRate 48000\ntrack 1 \"A\" 0 0 mono 0 0\n";
    struct Case {
        std::string text;
        const char* expected; // the error must start with this
    } cases[] = {
        {"", "line 1: not a DawLab project"},
        {"DawLabProject 6\n", "line 1: made by a newer DawLab"},
        {"DawLabProject 3\ntrack 1 \"A\" 0 0 mono 0 0\n", "line 2: sampleRate must come"},
        {head + "volume 1\n", "line 4: unknown keyword"},
        {head + "track 2 \"B\" 0 0\n", "line 4: track needs 7 values"},
        {head + "track 2 \"B\" 0 0 quad 0 0\n", "line 4: the track format"},
        {head + "track 1 \"again\" 0 0 mono 0 0\n", "line 4: track id 1 used twice"},
        {head + "tempo 0 4 4\n", "line 4: tempo needs"},
        {head + "tempo 120 4 3\n", "line 4: the beat unit"},
        {head + "loop 100 50 0\n", "line 4: loop needs"},
        {head + "bus 2 \"C\" 0 0 0\n", "line 4: bus index"},
        {head + "master \"M\" 0 0 2\n", "line 4: mute must be"},
        {head + "send track:9 0 -6 0\n", "line 4: there is no track:9"},
        {head + "send bus:7 0 -6 0\n", "line 4: send needs"},
        {head + "insert track:1 4 builtin \"Gain\" 0\n", "line 4: slot must be"},
        {head + "insert track:1 0 builtin \"Gain\" 0\ninsert track:1 0 builtin \"Gate\" 0\n", "line 5: slot 0 of track:1 used twice"},
        {head + "insert master 0 vst3 \"x.vst3\" 0\n", "line 4: a vst3 insert needs"},
        {head + "param track:1 0 \"Gain\" 1\n", "line 4: param for slot 0 of track:1"},
        {head + "insert track:1 0 vst3 \"x.vst3\" 0 \"\"\nparam track:1 0 \"Gain\" 1\n", "line 5: param for slot 0"},
        {head + "clip 2 0 100 \"a.wav\"\n", "line 4: clip on track 2"},
        {head + "clip 1 0 100 \"a.wav\" 0 0 0 linear 0 0\n", "line 4: clip needs 4 or 11 values"},
        {head + "clip 1 0 100 \"a.wav\" 0 0 0 sine 0 0 \"x\"\n", "line 4: the fade shape"},
        {head + "clip 1 0 0 \"a.wav\"\n", "line 4: clip length"},
        {head + "track 3 \"open\n", "line 4: unterminated string"},
        {head + "gain 1,5\n", "line 4: unknown keyword"},
    };
    int ok = 0;
    for (const Case& c : cases) {
        const ReadResult r = readProject(c.text);
        const bool right = !r.ok && r.error.rfind(c.expected, 0) == 0;
        if (!right)
            std::printf("  expected \"%s...\", got %s\n", c.expected, r.ok ? "ok" : ("\"" + r.error + "\"").c_str());
        ok += right ? 1 : 0;
        DAWLAB_CHECK(right);
    }
    std::printf("errors: %d of %d bad files refused with the right line and message\n", ok,
                static_cast<int>(sizeof(cases) / sizeof(cases[0])));
}

// What editors and other systems do to a text file: CRLF line ends, a byte order mark, comments.
void testTextVariants() {
    const std::string text = "\xEF\xBB\xBF" "DawLabProject 3\r\n# saved by hand\r\nsampleRate 48000\r\n"
                             "track 1 \"A # not a comment\" 0 0 mono 0 0   # this is one\r\n";
    const ReadResult r = readProject(text);
    std::printf("BOM, CRLF and comments: %s\n", r.ok ? "read" : r.error.c_str());
    DAWLAB_CHECK(r.ok && r.project.tracks.size() == 1 && r.project.tracks[0].strip.name == "A # not a comment");
}

// The file must not depend on the user's language: a German locale writes "-6,5" with
// iostreams, and the file would then mean something else (or nothing) on another machine.
void testLocale() {
    const char* names[] = {"de_DE.UTF-8", "de_DE", "German_Germany.1252", "de-DE"};
    std::locale german;
    bool found = false;
    for (const char* n : names) {
        try {
            german = std::locale(n);
            found = true;
            break;
        } catch (const std::runtime_error&) {
        }
    }
    if (!found) {
        std::printf("locale: no German locale installed here, test skipped\n");
        return;
    }
    const std::locale old = std::locale::global(german);
    std::setlocale(LC_ALL, german.name().c_str());
    ProjectState p = richProject();
    const std::string text = writeProject(p);
    const ReadResult r = readProject(text);
    std::locale::global(old);
    std::setlocale(LC_ALL, "C");
    std::printf("locale %s: written with a decimal point %s, read back equal %s\n", german.name().c_str(),
                text.find("tempo 97.5 7 8") != std::string::npos ? "yes" : "no", r.ok && r.project == p ? "yes" : "no");
    DAWLAB_CHECK(text.find("tempo 97.5 7 8") != std::string::npos);
    DAWLAB_CHECK(r.ok && r.project == p);
}

} // namespace

// ---- format 4: automation --------------------------------------------------------------------------

void testLanes() {
    const std::string head = "DawLabProject 4\nsampleRate 48000\ntrack 1 \"A\" 0 0 mono 0 0\ninsert track:1 2 builtin \"Gain\" 0\n"; // 4 lines
    const ReadResult r = readProject(head +
                                     "point 1 2000 0.25 hold\n" // before its lane, out of order, and then a jump
                                     "point 1 1000 0.5 linear\n"
                                     "point 1 1000 0.75 linear\n"
                                     "lane 1 track:1 2 \"Gain\" touch 0.5\n"
                                     "lane 2 track:1 - \"Pan\" latch 0.125\n"
                                     "lane 3 master - \"Gain\" write 0\n"
                                     "lane 4 bus:1 - \"Gain\" read 1\n"
                                     "lane 5 track:1 2 \"Unknown\" read 0.5\n");
    DAWLAB_CHECK(r.ok);
    if (!r.ok) {
        std::printf("  %s\n", r.error.c_str());
        return;
    }
    const auto& lanes = r.project.tracks[0].strip.lanes;
    DAWLAB_CHECK(lanes.size() == 3 && r.project.master.lanes.size() == 1 && r.project.buses[1].lanes.size() == 1);
    DAWLAB_CHECK(lanes[0].slot == 2 && lanes[0].parameter == "Gain" && lanes[0].mode == dawlab::engine::AutomationMode::Touch && lanes[0].defaultValue == 0.5f);
    DAWLAB_CHECK(lanes[0].points.size() == 3 && lanes[0].points[0].value == 0.5f && lanes[0].points[1].value == 0.75f &&
                 lanes[0].points[2].position == 2000 && lanes[0].points[2].hold);
    DAWLAB_CHECK(lanes[1].slot == -1 && lanes[1].parameter == "Pan" && lanes[1].mode == dawlab::engine::AutomationMode::Latch);
    DAWLAB_CHECK(lanes[2].parameter == "Unknown"); // kept: the file format does not know the effects
    DAWLAB_CHECK(r.project.master.lanes[0].mode == dawlab::engine::AutomationMode::Write);

    // Everything reads back exactly, also a float that needs 9 digits.
    ProjectState p = r.project;
    p.master.lanes[0].points.push_back({123456789012LL, 1.0f / 3.0f, false});
    const std::string text = writeProject(p);
    const ReadResult back = readProject(text);
    DAWLAB_CHECK(back.ok && back.project == p);
    DAWLAB_CHECK(back.ok && writeProject(back.project) == text);

    struct Bad {
        std::string body;
        const char* message;
    };
    const std::string lane = "lane 1 track:1 - \"Gain\" read 0.5\n"; // line 5
    const Bad bad[] = {
        {head + "lane 1 track:9 - \"Gain\" read 0.5\ntrack 2 \"B\" 0 0 mono 0 0\n", "line 5: lane on track:9"}, // not just the last line
        {head + "lane 1 track:1 0 \"Gain\" read 0.5\n", "line 5: lane for slot 0"},
        {head + "lane 1 track:1 9 \"Gain\" read 0.5\n", "line 5: slot must be"},
        {head + lane + lane, "line 6: lane number 1 used twice"},
        {head + lane + "lane 2 track:1 - \"Gain\" read 0.5\n", "line 6: two lanes"},
        {head + lane + "point 2 0 0.5 linear\n", "line 6: point for lane 2"},
        {head + "lane 0 track:1 - \"Gain\" read 0.5\n", "line 5: lane needs"},
        {head + "lane 1 track:1 - Gain read 0.5\n", "line 5: lane needs"},
        {head + "lane 1 track:1 - \"Gain\" auto 0.5\n", "line 5: unknown mode"},
        {head + "lane 1 track:1 - \"Gain\" read 1.5\n", "line 5: the default"},
        {head + lane + "point 1 0 1.5 linear\n", "line 6: a point's value"},
        {head + lane + "point 1 0 nan linear\n", "line 6: a point's value"},
        {head + lane + "point 1 -1 0.5 linear\n", "line 6: a point's position"},
        {head + lane + "point 1 0 0.5 curvy\n", "line 6: unknown shape"},
        {head + lane + "point 1 0 0.5\n", "line 6: point needs"},
        {"DawLabProject 3\nsampleRate 48000\ntrack 1 \"A\" 0 0 mono 0 0\nlane 1 track:1 - \"Gain\" read 0\n", "line 4: \"lane\" needs format 4"},
        {"DawLabProject 3\nsampleRate 48000\ntrack 1 \"A\" 0 0 mono 0 0\npoint 1 0 0 linear\n", "line 4: \"point\" needs format 4"},
    };
    for (const Bad& b : bad) {
        const ReadResult result = readProject(b.body);
        const bool right = !result.ok && result.error.rfind(b.message, 0) == 0;
        if (!right)
            std::printf("  expected \"%s\", got \"%s\"\n", b.message, result.ok ? "ok" : result.error.c_str());
        DAWLAB_CHECK(right);
    }
    std::printf("  lanes: the file reads back exactly; %d broken files refused with the right line\n", static_cast<int>(std::size(bad)));
}

// Chapter 17, Step 7: a recording's takes on one audio file.
void testTakes() {
    ProjectState p;
    TrackState track;
    track.id = TrackId{1};
    p.tracks.push_back(track);
    p.nextTrackId = 2;
    TakeSetState set;
    set.file = "Song_media/my \"take\".wav";
    set.takes = {{48000, 1472, 5000}, {48000, 6472, 5000}, {0, 11472, 1}};
    p.takeSets.push_back(set);
    const std::string text = writeProject(p);
    const ReadResult back = readProject(text);
    DAWLAB_CHECK(back.ok);
    DAWLAB_CHECK(back.project.takeSets.size() == 1);
    DAWLAB_CHECK(back.project == p);
    DAWLAB_CHECK(writeProject(back.project) == text);

    const std::string head = "DawLabProject 5\nsampleRate 48000\ntrack 1 \"A\" 0 0 mono 0 0\n";
    const auto reads = [&head](const std::string& line) { return readProject(head + line + "\n").ok; };
    DAWLAB_CHECK(reads("takes \"a.wav\" 0 0 1"));
    DAWLAB_CHECK(reads("takes \"a.wav\" 0 0 1 5 5 5"));
    DAWLAB_CHECK(!reads("takes \"a.wav\""));              // no take
    DAWLAB_CHECK(!reads("takes \"a.wav\" 0 0"));          // not a whole take
    DAWLAB_CHECK(!reads("takes \"a.wav\" 0 0 1 5"));
    DAWLAB_CHECK(!reads("takes a.wav 0 0 1"));              // not in quotes
    DAWLAB_CHECK(!reads("takes \"a.wav\" -1 0 1"));       // before the song
    DAWLAB_CHECK(!reads("takes \"a.wav\" 0 -1 1"));
    DAWLAB_CHECK(!reads("takes \"a.wav\" 0 0 0"));        // no length
    DAWLAB_CHECK(!reads("takes \"a.wav\" 0 0 x"));
    // An older file cannot have them.
    DAWLAB_CHECK(!readProject("DawLabProject 4\nsampleRate 48000\ntakes \"a.wav\" 0 0 1\n").ok);
}

// Numbers that pass for their sign but that DawLab never writes: a sample rate no device has (1e-9 made the
// conversion of the clips cast an enormous size), and positions or lengths beyond 2^40 samples or ticks (260 days at
// 48 kHz), where `start + length` would overflow. The file is refused where it is read (ProjectLimits.h).
void testRanges() {
    const std::string version = "DawLabProject " + std::to_string(kFormatVersion) + "\n";
    const auto opens = [&version](const std::string& rate) { return readProject(version + "sampleRate " + rate + "\n").ok; };
    DAWLAB_CHECK(opens("8000") && opens("44100") && opens("192000") && opens("768000"));
    DAWLAB_CHECK(!opens("1e-9") && !opens("7999") && !opens("768001") && !opens("1e9"));

    const std::string head = version + "sampleRate 48000\ntrack 1 \"A\" 0 0 mono 0 0\n";
    const auto reads = [&head](const std::string& lines) { return readProject(head + lines + "\n").ok; };
    const std::string edge = "1099511627776", past = "1099511627777", huge = "9223372036854775807";
    DAWLAB_CHECK(reads("clip 1 " + edge + " 100 \"a.wav\""));                        // 2^40: the last that is taken
    DAWLAB_CHECK(!reads("clip 1 " + past + " 100 \"a.wav\"") && !reads("clip 1 " + huge + " " + huge + " \"a.wav\""));
    DAWLAB_CHECK(!reads("clip 1 0 " + past + " \"a.wav\""));                         // a length
    DAWLAB_CHECK(!reads("clip 1 0 100 \"a.wav\" " + past + " 0 0 linear 0 0 \"x\"")); // a source offset
    // A fade: at most 60 seconds at the file's rate, 2880000 samples at 48 kHz. The engine adds 1 to it in an int, and
    // a change of rate scales it: INT_MAX overflowed.
    DAWLAB_CHECK(reads("clip 1 0 100 \"a.wav\" 0 2880000 2880000 linear 0 0 \"x\""));
    DAWLAB_CHECK(!reads("clip 1 0 100 \"a.wav\" 0 2880001 0 linear 0 0 \"x\"") && !reads("clip 1 0 100 \"a.wav\" 0 0 2880001 linear 0 0 \"x\""));
    DAWLAB_CHECK(!reads("clip 1 0 100 \"a.wav\" 0 2147483647 0 linear 0 0 \"x\"") &&
                 !reads("clip 1 0 100 \"a.wav\" 0 0 2147483647 linear 0 0 \"x\""));
    DAWLAB_CHECK(!reads("loop 0 " + past + " 1") && reads("loop 0 " + edge + " 1"));
    const std::string lane = "insert track:1 2 builtin \"Gain\" 0\nlane 1 track:1 - \"Gain\" read 0.5\n";
    DAWLAB_CHECK(reads(lane + "point 1 " + edge + " 0.5 linear") && !reads(lane + "point 1 " + past + " 0.5 linear"));
    DAWLAB_CHECK(!reads("takes \"a.wav\" " + past + " 0 1") && !reads("takes \"a.wav\" 0 " + huge + " 1") && !reads("takes \"a.wav\" 0 0 " + past));
    std::printf("ranges: a sample rate of 8000 to 768000 Hz, every position and length at most 2^40, a fade at most 60 s\n");
}

int main() {
    std::printf("l17_test_project_format: the application's project file, format %d\n", kFormatVersion);
    testTakes();
    testLanes();
    testRoundTrip();
    testAnyOrder();
    testStep4Files();
    testErrors();
    testTextVariants();
    testLocale();
    testRanges();
    return dawlab::test::summary("l17_test_project_format");
}
