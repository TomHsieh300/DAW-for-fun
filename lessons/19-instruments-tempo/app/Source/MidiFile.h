#pragma once
// MidiFile: Standard MIDI Files, read and written (chapter 19, Step 5; chapter 18's Exercise 3 with tempo changes).
//
// No JUCE: bytes in, a Song out, and back - tested in tests/test_midi_file.cpp in every build.
//
//   write()   format 1 at 960 ticks a quarter: a conductor track with the time signature and EVERY tempo of the
//             song at its tick, then one track per song track, with running status.
//   read()    format 0 or 1 at any division, into ticks of 960 (each event's absolute tick converted, so nothing
//             drifts). Every tempo event of any track becomes a tempo of the song, in tick order; a later one at
//             the same tick replaces the earlier. The first time signature is the song's.
//
// A file is untrusted: read() refuses what it cannot read, with a reason, and never reads past the end.
// What it does not keep: SysEx, key signatures, lyrics, markers, format 2, SMPTE time.

#include <dawlab/engine/Midi.h>

#include <cstdint>
#include <string>
#include <vector>

namespace dawlab::app::midifile {

struct Event {
    std::int64_t tick = 0;
    engine::MidiMessage message;
};

struct Track {
    std::string name;
    std::vector<Event> events; // channel messages
};

// A tempo from its tick on (a file has no ramps: only steps).
struct Tempo {
    std::int64_t tick = 0;
    double bpm = 120.0;
};

struct Song {
    std::vector<Tempo> tempos; // by tick, the first at 0; empty: 120 BPM
    int numerator = 4, denominator = 4;
    std::vector<Track> tracks;
};

struct ReadResult {
    bool ok = false;
    std::string error;
    Song song;
    int format = 0;
    int division = 0;
    int skipped = 0; // SysEx events and unknown chunks
};

std::vector<std::uint8_t> write(const Song& song);
ReadResult read(const std::vector<std::uint8_t>& file);

} // namespace dawlab::app::midifile
