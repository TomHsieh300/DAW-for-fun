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
        {"DawLabProject 9\n", "line 1: made by a newer DawLab"},
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

// Chapter 18: instrument tracks and MIDI clips (format 6).
void testMidi() {
    ProjectState p;
    p.nextTrackId = 3;
    TrackState a;
    a.id = TrackId{1};
    a.strip.name = "Bass";
    a.instrument = true;
    TrackState b;
    b.id = TrackId{2};
    b.strip.name = "Audio";
    p.tracks = {a, b};
    MidiClipState c;
    c.track = TrackId{1};
    c.start = 3840;
    c.length = 7680;
    c.offset = 240;
    c.name = "Riff \"one\"";
    c.grid = 120;
    c.strength = 0.75f;
    c.swing = 1.0f / 3.0f;
    c.notes = {{0, 480, 36, 100, 0}, {0, 240, 43, 90, 9}, {481, 1, 127, 1, 15}};
    c.controllers = {{0, 64, 127, 0}, {960, 1, 0, 3}};
    MidiClipState d;
    d.track = TrackId{1};
    d.start = 20000;
    d.length = 1;
    d.muted = true;
    p.midiClips = {c, d};
    const std::string text = writeProject(p);
    const ReadResult back = readProject(text);
    DAWLAB_CHECK(back.ok && back.formatVersion == kFormatVersion);
    DAWLAB_CHECK(back.project == p);
    DAWLAB_CHECK(writeProject(back.project) == text);
    DAWLAB_CHECK(back.project.tracks.size() == 2 && back.project.tracks[0].instrument && !back.project.tracks[1].instrument);

    const std::string head = "DawLabProject 6\nsampleRate 48000\ntrack 1 \"A\" 0 0 mono 0 0\n";
    const auto reads = [&head](const std::string& lines) { return readProject(head + lines + "\n").ok; };
    DAWLAB_CHECK(reads("instrument 1"));
    DAWLAB_CHECK(!reads("instrument 2"));    // no such track
    DAWLAB_CHECK(!reads("instrument x"));
    DAWLAB_CHECK(reads("midiclip 1 7 0 960 0 0 \"c\" 240 0 0"));
    DAWLAB_CHECK(!reads("midiclip 2 7 0 960 0 0 \"c\" 240 0 0"));     // no such track
    DAWLAB_CHECK(!reads("midiclip 1 7 0 0 0 0 \"c\" 240 0 0"));       // no length
    DAWLAB_CHECK(!reads("midiclip 1 7 -1 960 0 0 \"c\" 240 0 0"));    // before the song
    DAWLAB_CHECK(!reads("midiclip 1 7 0 960 -1 0 \"c\" 240 0 0"));    // a negative offset
    DAWLAB_CHECK(!reads("midiclip 1 7 0 960 0 0 c 240 0 0"));         // the name not in quotes
    DAWLAB_CHECK(!reads("midiclip 1 7 0 960 0 0 \"c\" 0 0 0"));       // no grid
    DAWLAB_CHECK(!reads("midiclip 1 7 0 960 0 0 \"c\" 240 1.5 0"));   // too strong
    DAWLAB_CHECK(!reads("midiclip 1 7 0 960 0 0 \"c\" 240 0 0.6"));   // too much swing
    DAWLAB_CHECK(!reads("midiclip 1 7 0 960 0 0 \"c\" 240 0 0\nmidiclip 1 7 0 960 0 0 \"d\" 240 0 0")); // one number twice
    const std::string clip = "midiclip 1 7 0 960 0 0 \"c\" 240 0 0\n";
    DAWLAB_CHECK(reads(clip + "note 7 0 480 60 100 0"));
    DAWLAB_CHECK(reads("note 7 0 480 60 100 0\n" + clip)); // in any order
    DAWLAB_CHECK(!reads(clip + "note 8 0 480 60 100 0"));  // no such clip
    DAWLAB_CHECK(!reads(clip + "note 7 0 0 60 100 0"));    // no length
    DAWLAB_CHECK(!reads(clip + "note 7 0 480 128 100 0")); // pitch
    DAWLAB_CHECK(!reads(clip + "note 7 0 480 60 0 0"));    // velocity 0 is a note off, not a note
    DAWLAB_CHECK(!reads(clip + "note 7 0 480 60 100 16")); // channel
    DAWLAB_CHECK(!reads(clip + "note 7 0 480 60 100"));
    DAWLAB_CHECK(reads(clip + "cc 7 0 64 127 0"));
    DAWLAB_CHECK(!reads(clip + "cc 7 0 128 127 0"));
    DAWLAB_CHECK(!reads(clip + "cc 7 0 64 128 0"));
    DAWLAB_CHECK(!reads(clip + "cc 9 0 64 127 0"));
    // Notes come back sorted by start, in line order at one start.
    const ReadResult sorted = readProject(head + clip + "note 7 500 1 60 1 0\nnote 7 0 1 61 1 0\nnote 7 500 1 62 1 0\n");
    DAWLAB_CHECK(sorted.ok && sorted.project.midiClips.size() == 1 && sorted.project.midiClips[0].notes.size() == 3);
    if (sorted.ok && sorted.project.midiClips[0].notes.size() == 3) {
        const auto& n = sorted.project.midiClips[0].notes;
        DAWLAB_CHECK(n[0].pitch == 61 && n[1].pitch == 60 && n[2].pitch == 62);
    }
    // An older file cannot have them.
    DAWLAB_CHECK(!readProject("DawLabProject 5\nsampleRate 48000\ntrack 1 \"A\" 0 0 mono 0 0\ninstrument 1\n").ok);
}

// Chapter 19: the tempo map and a loop in ticks (format 7).
void testTempoMap() {
    ProjectState p;
    p.tempo = 97.5;
    p.tempoPoints = {{0, 97.5, true}, {7680, 140.0, false}, {11520, 100.123456789012, true}, {19200, 70.0, false}};
    p.loopStart = 3840;
    p.loopEnd = 11520;
    p.loopEnabled = true;
    p.loopInTicks = true;
    const std::string text = writeProject(p);
    DAWLAB_CHECK(text.find("loop 3840 11520 1 ticks\n") != std::string::npos);
    DAWLAB_CHECK(text.find("tempopoint 7680 140 0\n") != std::string::npos && text.find("tempopoint 0 97.5 1\n") != std::string::npos);
    const ReadResult back = readProject(text);
    DAWLAB_CHECK(back.ok && back.project == p && writeProject(back.project) == text);
    DAWLAB_CHECK(back.ok && back.project.tempoPoints.size() == 4 && back.project.tempoPoints[2].bpm == 100.123456789012);
    std::printf("the tempo map: 4 points and a loop in ticks written and read back exactly\n");

    // One tempo: no tempopoint lines, a loop in samples as before.
    ProjectState plain;
    plain.loopEnd = 48000;
    const std::string plainText = writeProject(plain);
    DAWLAB_CHECK(plainText.find("tempopoint") == std::string::npos && plainText.find("loop 0 48000 0\n") != std::string::npos);

    const std::string head = "DawLabProject 7\nsampleRate 48000\ntempo 120 4 4\n";
    const auto reads = [&head](const std::string& lines) { return readProject(head + lines + "\n").ok; };
    DAWLAB_CHECK(reads("tempopoint 0 120 0\ntempopoint 960 60 1\ntempopoint 1920 90 0"));
    DAWLAB_CHECK(!reads("tempopoint 960 120 0"));                       // the first is not at 0
    DAWLAB_CHECK(!reads("tempopoint 0 120 0\ntempopoint 0 90 0"));      // twice at one tick
    DAWLAB_CHECK(!reads("tempopoint 0 120 0\ntempopoint 960 90 0\ntempopoint 480 90 0")); // back in time
    DAWLAB_CHECK(!reads("tempopoint 0 19 0") && !reads("tempopoint 0 1000 0") && !reads("tempopoint 0 nan 0"));
    DAWLAB_CHECK(!reads("tempopoint 0 120") && !reads("tempopoint 0 120 2") && !reads("tempopoint x 120 0"));
    DAWLAB_CHECK(reads("loop 0 3840 1 ticks") && !reads("loop 0 3840 1 beats") && !reads("loop 3840 0 1 ticks"));
    std::string many;
    for (int i = 0; i < 256; ++i)
        many += "tempopoint " + std::to_string(i * 10) + " 120 0\n";
    DAWLAB_CHECK(reads(many) && !reads(many + "tempopoint 9999 120 0"));
    // An older file cannot have them.
    DAWLAB_CHECK(!readProject("DawLabProject 6\nsampleRate 48000\ntempopoint 0 120 0\n").ok);
    DAWLAB_CHECK(!readProject("DawLabProject 6\nsampleRate 48000\nloop 0 3840 1 ticks\n").ok);
    DAWLAB_CHECK(readProject("DawLabProject 6\nsampleRate 48000\nloop 0 3840 1\n").ok);
}

// Chapter 19, Step 6: the synth's sound and a plugin instrument (format 7).
void testInstrumentSound() {
    ProjectState p;
    TrackState a, b;
    a.id = engine::TrackId{1};
    a.strip.name = "Lead";
    a.instrument = true;
    a.synth = {8000.0, 2.5, 0.25, 0.123456789012345, 0.5, 1.5, 0.75};
    a.instrumentFile = "C:/Plugins/My Synth.vst3";
    a.instrumentState = "4....f9.";
    b.id = engine::TrackId{2};
    b.strip.name = "Bass";
    b.instrument = true; // the default sound: no line
    p.tracks = {a, b};
    p.nextTrackId = 3;
    const std::string text = writeProject(p);
    DAWLAB_CHECK(text.find("synth 1 8000 2.5 0.25 0.123456789012345") != std::string::npos && text.find(" 0.5 1.5 0.75\n") != std::string::npos);
    DAWLAB_CHECK(text.find("plugininstrument 1 \"C:/Plugins/My Synth.vst3\" \"4....f9.\"") != std::string::npos);
    DAWLAB_CHECK(text.find("synth 2") == std::string::npos && text.find("plugininstrument 2") == std::string::npos);
    const ReadResult back = readProject(text);
    DAWLAB_CHECK(back.ok && back.project == p && writeProject(back.project) == text);
    std::printf("the instrument's sound: a synth's settings and a plugin written and read back exactly\n");

    const std::string head = "DawLabProject 7\nsampleRate 48000\ntrack 1 \"A\" 0 0 mono 0 0\n";
    const auto reads = [&head](const std::string& lines) { return readProject(head + lines + "\n").ok; };
    DAWLAB_CHECK(reads("synth 1 4000 0.9 0.005 0.2 0.6 0.3 0.25"));
    DAWLAB_CHECK(!reads("synth 2 4000 0.9 0.005 0.2 0.6 0.3 0.25"));  // no such track
    DAWLAB_CHECK(!reads("synth 1 4000 0.9 0.005 0.2 0.6 0.3"));       // a number short
    DAWLAB_CHECK(!reads("synth 1 10 0.9 0.005 0.2 0.6 0.3 0.25"));    // cutoff below
    DAWLAB_CHECK(!reads("synth 1 4000 0.9 0 0.2 0.6 0.3 0.25"));      // an attack of 0
    DAWLAB_CHECK(!reads("synth 1 4000 0.9 0.005 0.2 1.1 0.3 0.25"));  // sustain above 1
    DAWLAB_CHECK(!reads("synth 1 4000 0.9 0.005 0.2 0.6 0.3 nan"));
    DAWLAB_CHECK(!reads("synth 1 4000 0.9 0.005 0.2 0.6 0.3 0.25\nsynth 1 4000 0.9 0.005 0.2 0.6 0.3 0.25")); // twice
    DAWLAB_CHECK(reads("plugininstrument 1 \"a.vst3\" \"\""));
    DAWLAB_CHECK(!reads("plugininstrument 1 a.vst3 \"\""));           // the file not in quotes
    DAWLAB_CHECK(!reads("plugininstrument 1 \"\" \"\""));              // no file
    DAWLAB_CHECK(!reads("plugininstrument 1 \"a.vst3\""));            // no state
    DAWLAB_CHECK(!reads("plugininstrument 3 \"a.vst3\" \"\""));       // no such track
    DAWLAB_CHECK(reads("plugininstrument 1 \"a.vst3\" \"\"\nsynth 1 4000 0.9 0.005 0.2 0.6 0.3 0.25")); // both: the synth's sound is kept
    DAWLAB_CHECK(!readProject("DawLabProject 6\nsampleRate 48000\ntrack 1 \"A\" 0 0 mono 0 0\nsynth 1 4000 0.9 0.005 0.2 0.6 0.3 0.25\n").ok);
}

// Chapter 20: clips that follow the tempo (format 8).
void testFollow() {
    ProjectState p;
    TrackState t;
    t.id = TrackId{1};
    t.strip.name = "A";
    p.tracks = {t};
    p.nextTrackId = 2;
    ClipState plain;
    plain.track = TrackId{1};
    plain.length = 96000;
    plain.file = "a.wav";
    plain.name = "plain";
    ClipState start = plain;
    start.name = "start";
    start.follow = dawlab::engine::FollowMode::Start;
    start.startTick = 3840.0 + 1.0 / 3.0; // a fraction that 6 digits would lose
    ClipState tape = plain;
    tape.name = "tape";
    tape.follow = dawlab::engine::FollowMode::Varispeed;
    tape.sourceLength = 96000;
    tape.markers = {{0.0, 0.0}, {48000.125, 1920.0 + 1.0 / 7.0}, {96000.0, 3840.0}};
    ClipState kept = tape;
    kept.name = "kept";
    kept.follow = dawlab::engine::FollowMode::Stretch;
    kept.sourceOffset = 100;
    kept.markers = {{-50.5, -960.0}, {123456.75, 7680.0}};
    p.clips = {plain, start, tape, kept};

    const std::string text = writeProject(p);
    DAWLAB_CHECK(text.rfind("DawLabProject 8\n", 0) == 0);
    DAWLAB_CHECK(text.find("follow start 3840.3333333333335\n") != std::string::npos && text.find("follow varispeed 96000\n") != std::string::npos &&
                 text.find("follow stretch 96000\n") != std::string::npos && text.find("marker 48000.125 1920.1428571428571\n") != std::string::npos);
    const ReadResult back = readProject(text);
    DAWLAB_CHECK(back.ok && back.project == p && writeProject(back.project) == text);
    DAWLAB_CHECK(back.ok && back.project.clips.size() == 4 && back.project.clips[0].follow == dawlab::engine::FollowMode::Off &&
                 back.project.clips[0].markers.empty() && back.project.clips[1].startTick == start.startTick && back.project.clips[3].markers == kept.markers);
    std::printf("clips that follow: a start in ticks, varispeed and stretch with their markers written and read back exactly\n");

    // A project where nothing follows has none of these lines: it reads as it always did.
    ProjectState old = p;
    old.clips = {plain};
    DAWLAB_CHECK(writeProject(old).find("follow") == std::string::npos && writeProject(old).find("marker") == std::string::npos);
    // The data is part of what a clip is.
    ClipState other = tape;
    other.markers[1].tick += 1.0;
    DAWLAB_CHECK(!(other == tape) && !(start == plain));

    const std::string head = "DawLabProject 8\nsampleRate 48000\ntrack 1 \"A\" 0 0 mono 0 0\n";
    const std::string clip = "clip 1 0 100 \"a.wav\"\n";
    const auto reads = [&head](const std::string& lines) { return readProject(head + lines + "\n").ok; };
    DAWLAB_CHECK(reads(clip + "follow start 960.5"));
    DAWLAB_CHECK(reads(clip + "follow varispeed 100\nmarker 0 0\nmarker 100 480"));
    DAWLAB_CHECK(reads(clip + "follow stretch 100\nmarker 0 0\nmarker 50 100\nmarker 100 480\n" + clip + "follow start 0"));
    DAWLAB_CHECK(!reads("follow start 0"));                                            // no clip above it
    DAWLAB_CHECK(!reads(clip + "follow start 0\nfollow start 5"));                     // twice
    DAWLAB_CHECK(!reads(clip + "follow start") && !reads(clip + "follow start x") && !reads(clip + "follow start nan"));
    DAWLAB_CHECK(!reads(clip + "follow tape 100") && !reads(clip + "follow \"start\" 0"));
    DAWLAB_CHECK(!reads(clip + "follow varispeed 0\nmarker 0 0\nmarker 100 480"));     // no length in the source
    DAWLAB_CHECK(!reads(clip + "follow varispeed 100"));                               // no markers
    DAWLAB_CHECK(!reads(clip + "follow varispeed 100\nmarker 0 0"));                   // one marker
    DAWLAB_CHECK(!reads(clip + "follow varispeed 100\nmarker 0 0\nmarker 0 480"));     // the source does not rise
    DAWLAB_CHECK(!reads(clip + "follow varispeed 100\nmarker 0 0\nmarker 100 0"));     // the tick does not rise
    DAWLAB_CHECK(!reads(clip + "follow varispeed 100\nmarker 0 0\nmarker 100"));       // half a marker
    DAWLAB_CHECK(!reads(clip + "marker 0 0\nmarker 100 480"));                         // markers of a clip that does not follow
    DAWLAB_CHECK(!reads(clip + "follow start 0\nmarker 0 0"));                         // or that follows with its start
    // The error names the line of the clip that lacks its markers.
    const ReadResult lacking = readProject(head + clip + "follow stretch 100\nmarker 0 0\n");
    DAWLAB_CHECK(!lacking.ok && lacking.error.find("line 4") != std::string::npos && lacking.error.find("two markers") != std::string::npos);
    // Numbers that would put the clip outside the song, or that no tempo makes: the file is refused where it is
    // read, not later, when placing the clip would cast an enormous number to a sample or never end.
    DAWLAB_CHECK(!reads(clip + "follow start -96000"));                                // a start before the song's
    DAWLAB_CHECK(!reads(clip + "follow start 1e300"));                                 // a start no sample count reaches
    DAWLAB_CHECK(!reads(clip + "follow varispeed 100\nmarker 0 0\nmarker 1 1e300"));   // a tick no sample count reaches
    DAWLAB_CHECK(!reads(clip + "follow varispeed 100\nmarker 0 0\nmarker 1e300 480")); // a position no source has
    DAWLAB_CHECK(!reads(clip + "follow varispeed 100\nmarker 0 0\nmarker 1e-9 480"));  // a pair no tempo makes
    DAWLAB_CHECK(!reads(clip + "follow varispeed 100\nmarker 0 0\nmarker 100 1e-9"));  // the other way
    DAWLAB_CHECK(!reads(clip + "follow varispeed 100\nmarker 0 0\nmarker 50 480"));    // the clip plays past its markers
    DAWLAB_CHECK(!reads("clip 1 0 100 \"a.wav\" 40 0 0 linear 0 0 \"x\"\nfollow varispeed 100\nmarker 50 0\nmarker 140 480")); // or before them
    // Up to a sample outside them is a rounding (a change of sample rate scales the markers, not the samples).
    DAWLAB_CHECK(reads(clip + "follow varispeed 100\nmarker 0.5 0\nmarker 99.5 480"));
    const ReadResult far = readProject(head + clip + "follow varispeed 100\nmarker 0 0\nmarker 1 1e300\n");
    DAWLAB_CHECK(!far.ok && far.error.find("line 4") != std::string::npos);
    // An older file cannot have them.
    DAWLAB_CHECK(!readProject("DawLabProject 7\nsampleRate 48000\ntrack 1 \"A\" 0 0 mono 0 0\n" + clip + "follow start 0\n").ok);
    DAWLAB_CHECK(readProject("DawLabProject 7\nsampleRate 48000\ntrack 1 \"A\" 0 0 mono 0 0\n" + clip).ok);
}

// Numbers that pass for their sign but that DawLab never writes: a sample rate no device has (1e-9 made the
// conversion of the clips cast an enormous size), and positions or lengths beyond 2^40 samples or ticks (260 days at
// 48 kHz), where `start + length` would overflow. The file is refused where it is read.
void testRanges() {
    const auto opens = [](const std::string& rate) { return readProject("DawLabProject 8\nsampleRate " + rate + "\n").ok; };
    DAWLAB_CHECK(opens("8000") && opens("44100") && opens("192000") && opens("768000"));
    DAWLAB_CHECK(!opens("1e-9") && !opens("7999") && !opens("768001") && !opens("1e9"));

    const std::string head = "DawLabProject 8\nsampleRate 48000\ntrack 1 \"A\" 0 0 mono 0 0\n";
    const auto reads = [&head](const std::string& lines) { return readProject(head + lines + "\n").ok; };
    const std::string edge = "1099511627776", past = "1099511627777", huge = "9223372036854775807";
    DAWLAB_CHECK(reads("clip 1 " + edge + " 100 \"a.wav\""));                        // 2^40: the last that is taken
    DAWLAB_CHECK(!reads("clip 1 " + past + " 100 \"a.wav\"") && !reads("clip 1 " + huge + " " + huge + " \"a.wav\""));
    DAWLAB_CHECK(!reads("clip 1 0 " + past + " \"a.wav\""));                         // a length
    DAWLAB_CHECK(!reads("clip 1 0 100 \"a.wav\" " + past + " 0 0 linear 0 0 \"x\"")); // a source offset
    DAWLAB_CHECK(!reads("clip 1 0 100 \"a.wav\" 0 " + past + " 0 linear 0 0 \"x\"")); // a fade
    DAWLAB_CHECK(!reads("clip 1 0 100 \"a.wav\"\nfollow varispeed " + past + "\nmarker 0 0\nmarker 100 480"));
    DAWLAB_CHECK(!reads("loop 0 " + past + " 1") && reads("loop 0 " + edge + " 1"));
    DAWLAB_CHECK(!reads("takes \"a.wav\" " + past + " 0 1") && !reads("takes \"a.wav\" 0 " + huge + " 1") && !reads("takes \"a.wav\" 0 0 " + past));
    DAWLAB_CHECK(!reads("tempopoint 0 120 0\ntempopoint " + past + " 100 0"));
    const std::string lane = "lane 1 track:1 - \"Gain\" read 0.5\n";
    DAWLAB_CHECK(reads(lane + "point 1 " + edge + " 0.5 linear") && !reads(lane + "point 1 " + past + " 0.5 linear"));
    const std::string midi = "midiclip 1 7 0 960 0 0 \"c\" 240 0 0\n";
    DAWLAB_CHECK(!reads("midiclip 1 7 " + past + " 960 0 0 \"c\" 240 0 0") && !reads("midiclip 1 7 0 " + past + " 0 0 \"c\" 240 0 0"));
    DAWLAB_CHECK(!reads("midiclip 1 7 0 960 " + past + " 0 \"c\" 240 0 0"));
    DAWLAB_CHECK(!reads(midi + "note 7 " + past + " 480 60 100 0") && !reads(midi + "note 7 0 " + past + " 60 100 0"));
    DAWLAB_CHECK(!reads(midi + "cc 7 " + past + " 64 127 0"));
    std::printf("ranges: a sample rate of 8000 to 768000 Hz, every position and length at most 2^40\n");
}

int main() {
    std::printf("l20_test_project_format: the application's project file, format %d\n", kFormatVersion);
    testMidi();
    testTakes();
    testLanes();
    testRoundTrip();
    testAnyOrder();
    testStep4Files();
    testErrors();
    testTextVariants();
    testLocale();
    testTempoMap();
    testInstrumentSound();
    testFollow();
    testRanges();
    return dawlab::test::summary("l20_test_project_format");
}
