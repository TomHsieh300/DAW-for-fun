#pragma once
// Exercise 9 - the self-check (main). Included at the end of the starter and of the solution.

#include "app_rig.h"
#include "ex9_midi_files.h"

#include <dawlab/Check.h>

#include <cstdio>
#include <random>
#include <string>

namespace ex9::check {

using namespace ex18rig;
using dawlab::engine::Note;
using dawlab::engine::TrackId;

std::string describe(const std::vector<ex3::TimedEvent>& events) {
    std::vector<ex3::TimedEvent> sorted = events;
    minidaw::sortEvents(sorted);
    std::string s;
    char buf[64];
    for (const ex3::TimedEvent& e : sorted) {
        std::snprintf(buf, sizeof buf, "%02X %d %d @%lld, ", e.message.status, e.message.data1, e.message.data2, static_cast<long long>(e.tick));
        s += buf;
    }
    return s;
}

bool sameNotes(const std::vector<Note>& a, const std::vector<Note>& b) {
    return a == b;
}

void checkExport() {
    Rig rig;
    rig.engine.getTransport().setTempo(96.0);
    const TrackId bass = rig.instrumentTrack();
    rig.session.setStripName(rig.session.trackRef(bass), "Bass");
    rig.instrumentTrack(); // empty: no track in the song
    MidiClipModel a;          // bar 2, the material from tick 480 on, one bar long
    a.start = 3840;
    a.offset = 480;
    a.length = 3840;
    a.played = {
        {0, 240, 30, 90, 0},     // before the window: left out
        {500, 480, 36, 100, 0},  // quantized to 480, the window's start: at 3840
        {2000, 400, 40, 80, 1},  // channel 2, quantized to 1920: at 3840 + 1440
        {4000, 960, 43, 70, 0},  // quantized to 3840, past the window's end (4320): cut there, at 3840 + 3840
        {4320, 100, 45, 60, 0},  // starts at the window's end: left out
    };
    a.quantize = {480, 1.0, 0.0};
    a.controllers = {{100, 64, 127, 0}, {1000, 64, 0, 0}, {5000, 1, 10, 0}};
    MidiClipModel muted = a;
    muted.start = 0;
    muted.muted = true;
    rig.session.setMidiClips(bass, {muted, a});
    rig.engine.applyPendingCommandsOffline();

    const Exported e = songOf(*rig.main);
    DAWLAB_CHECK(e.song.bpm == 96.0 && e.song.tracks.size() == 1 && e.notes == 3);
    if (e.song.tracks.size() != 1)
        return;
    DAWLAB_CHECK(e.song.tracks[0].name == "Bass");
    const std::vector<ex3::TimedEvent> expected = {
        {3840, {0x90, 36, 100}},       {3840 + 480, {0x80, 36, 0}},
        {3840 + 1440, {0x91, 40, 80}}, {3840 + 1840, {0x81, 40, 0}},
        {3840 + 3360, {0x90, 43, 70}}, {3840 + 3840, {0x80, 43, 0}},
        {3840 + 520, {0xB0, 64, 0}}, // the pedal up at 1000; the points at 100 and 5000 are outside
    };
    const std::string got = describe(e.song.tracks[0].events), want = describe(expected);
    DAWLAB_CHECK(got == want);
    if (got != want)
        std::printf("    got:  %s\n    want: %s\n", got.c_str(), want.c_str());
    std::printf("  a quantized clip with an offset, a muted copy, an empty track: %d notes on one track \"Bass\" at 96 BPM\n", e.notes);
}

void checkImport() {
    Rig rig;
    const int before = rig.session.getNumTracks();
    ex3::Song song;
    song.bpm = 90.0;
    ex3::SmfTrack piano;
    piano.name = "Piano";
    piano.events = {
        {0, {0x90, 60, 100}},   {100, {0x90, 60, 90}}, {200, {0x80, 60, 0}}, {300, {0x90, 60, 0}}, // two notes of one key
        {0, {0x91, 60, 50}},    {50, {0x81, 60, 0}},                                                // the same key, channel 2
        {400, {0xB0, 64, 127}}, {420, {0xE0, 0, 80}},  {430, {0xC0, 5, 0}},                         // a pedal; a bend, a program
        {500, {0x90, 72, 64}},  {9000, {0xB0, 64, 0}},                                              // 72 never ends: at 9000
    };
    ex3::SmfTrack unnamed;
    unnamed.events = {{3840, {0x90, 50, 100}}, {3840, {0x80, 50, 0}}}; // a note of no length: 1 tick
    song.tracks = {piano, unnamed};
    const size_t undoBefore = rig.session.getHistory().undoCount();
    const Imported r = importSong(*rig.main, song);
    rig.engine.applyPendingCommandsOffline();
    DAWLAB_CHECK(r.ok && r.tracks.size() == 2 && r.notes == 5 && r.dropped == 2);
    DAWLAB_CHECK(rig.session.getNumTracks() == before + 2 && rig.session.getHistory().undoCount() == undoBefore + 1);
    DAWLAB_CHECK(rig.engine.getTransport().getTempo() == 90.0);
    if (r.tracks.size() != 2 || rig.session.getNumTracks() != before + 2)
        return;
    DAWLAB_CHECK(rig.session.getTrackId(before) == r.tracks[0] && rig.session.getTrackId(before + 1) == r.tracks[1]);
    DAWLAB_CHECK(rig.session.isInstrument(r.tracks[0]) && rig.session.isInstrument(r.tracks[1]));
    DAWLAB_CHECK(rig.session.getStripName(rig.session.trackRef(r.tracks[0])) == "Piano" &&
                 rig.session.getStripName(rig.session.trackRef(r.tracks[1])) == "MIDI 2");
    const auto& clips0 = rig.session.getMidiClips(r.tracks[0]);
    const auto& clips1 = rig.session.getMidiClips(r.tracks[1]);
    DAWLAB_CHECK(clips0.size() == 1 && clips1.size() == 1);
    if (clips0.size() == 1 && clips1.size() == 1) {
        const MidiClipModel& p = clips0[0];
        const std::vector<Note> notes = {{0, 200, 60, 100, 0}, {0, 50, 60, 50, 1}, {100, 200, 60, 90, 0}, {500, 8500, 72, 64, 0}};
        DAWLAB_CHECK(p.start == 0 && p.offset == 0 && p.length == 3 * 3840 && p.name == "Piano" && sameNotes(p.played, notes));
        DAWLAB_CHECK(p.controllers.size() == 2 && p.controllers[0].tick == 400 && p.controllers[0].value == 127 && p.controllers[1].tick == 9000);
        const MidiClipModel& u = clips1[0];
        DAWLAB_CHECK(u.length == 2 * 3840 && u.played.size() == 1 && u.played[0].length == 1 && u.played[0].start == 3840);
        DAWLAB_CHECK(p.id != u.id && p.id != 0 && u.id != 0);
    }
    std::printf("  two tracks, overlapping notes of one key, a note that never ends, a bend and a program: %d notes, %d dropped\n",
                r.notes, r.dropped);

    // The instrument tracks play what came in.
    rig.engine.getTransport().play();
    rig.play(4800);
    DAWLAB_CHECK(rig.engineTrack(r.tracks[0])->getSynth()->activeVoices() >= 1);
    // One undo step takes it all back, and redo brings it again.
    DAWLAB_CHECK(rig.session.undo());
    rig.engine.applyPendingCommandsOffline();
    DAWLAB_CHECK(rig.session.getNumTracks() == before && rig.engine.getTransport().getTempo() == 120.0);
    DAWLAB_CHECK(rig.session.redo());
    rig.engine.applyPendingCommandsOffline();
    DAWLAB_CHECK(rig.session.getNumTracks() == before + 2 && rig.engine.getTransport().getTempo() == 90.0 &&
                 rig.session.getMidiClips(r.tracks[0]).size() == 1);
    // Nothing to import: refused, nothing changes.
    const Imported none = importSong(*rig.main, ex3::Song{});
    DAWLAB_CHECK(!none.ok && none.error.isNotEmpty() && rig.session.getNumTracks() == before + 2);
    std::printf("  undo, redo, and a song with no track refused\n");
}

void checkRoundTrip() {
    // Random clips out to a file and back into another project: the same notes.
    std::mt19937 rng(9);
    auto pick = [&rng](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
    Rig from;
    const TrackId t = from.instrumentTrack();
    MidiClipModel clip;
    clip.length = 8 * 3840;
    for (int i = 0; i < 200; ++i)
        clip.played.push_back({pick(0, 7 * 3840), pick(1, 3840), pick(0, 127), pick(1, 127), pick(0, 15)});
    sortNotes(clip.played);
    // No two notes of one key and channel at once: a file cannot tell which of them ends first.
    std::vector<Note> kept;
    for (const Note& n : clip.played) {
        bool overlaps = false;
        for (const Note& k : kept)
            overlaps = overlaps || (k.pitch == n.pitch && k.channel == n.channel && n.start < k.start + k.length + 1);
        if (!overlaps)
            kept.push_back(n);
    }
    clip.played = kept;
    from.session.setMidiClips(t, {clip});
    from.engine.applyPendingCommandsOffline();
    const juce::File file = juce::File::getCurrentWorkingDirectory().getChildFile("dawlab_output").getChildFile("l18_ex9_round_trip.mid");
    file.getParentDirectory().createDirectory();
    DAWLAB_CHECK(exportFile(*from.main, file));
    Rig to;
    const Imported r = importFile(*to.main, file);
    DAWLAB_CHECK(r.ok && r.tracks.size() == 1);
    if (r.ok && r.tracks.size() == 1) {
        const auto& clips = to.session.getMidiClips(r.tracks[0]);
        DAWLAB_CHECK(clips.size() == 1 && sameNotes(clips[0].played, clip.played));
        std::printf("  %zu random notes to %s (%lld bytes) and back into another project: %s\n", clip.played.size(),
                    file.getFileName().toRawUTF8(), static_cast<long long>(file.getSize()),
                    clips.size() == 1 && sameNotes(clips[0].played, clip.played) ? "the same" : "DIFFERENT");
    }
    file.deleteFile();
    const Imported missing = importFile(*to.main, file);
    DAWLAB_CHECK(!missing.ok && missing.error.isNotEmpty());
}

} // namespace ex9::check

int main() {
    juce::ScopedJuceInitialiser_GUI juce;
    using namespace ex9::check;
    std::printf("Exercise 9 - MIDI files in DawLab 18\n");
    checkExport();
    checkImport();
    checkRoundTrip();
    return dawlab::test::summary("l18_ex9");
}
