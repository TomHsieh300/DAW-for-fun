#pragma once
// MidiFileIO: MIDI files in and out of the project (chapter 19, Step 5; chapter 18's Exercise 9 with the tempo map).
//
//   songOf()      the project as it plays: every instrument track with something to play (its name), each clip that
//                 is not muted, what sounds in its window after quantize, cut at the clip's end; the time signature;
//                 and the tempo map. A file has no ramps, so a ramp is written as steps of a sixteenth, each at the
//                 tempo that makes the step as long as the ramp is over it: every sixteenth stays on time.
//   importSong()  every track of a song as a new instrument track at the end, with one clip from tick 0 to the end of
//                 its last bar. A note off ends the note of its key and channel that started first; a note that is
//                 never ended ends with the track. Controllers become the clip's; the rest (pitch bend, program
//                 changes) is counted. The song's tempos become the project's tempo map - jumps, thinned to the map's
//                 256 points if there are more. All of it is ONE undo step, "Import MIDI".
//
// The files themselves are MidiFile.h's.

#include "MidiFile.h"
#include "Session.h"

#include <juce_core/juce_core.h>

namespace dawlab::app {

struct MidiExport {
    midifile::Song song;
    int notes = 0;
};

struct MidiImport {
    bool ok = false;
    juce::String error;
    std::vector<engine::TrackId> tracks; // the new ones, in order
    int notes = 0;
    int dropped = 0;      // events that are neither notes nor controllers
    int tempos = 0;       // tempo points of the map now
    bool thinned = false; // the file had more tempos than the map can hold
};

MidiExport songOf(const Session& session);
MidiImport importSong(Session& session, const midifile::Song& song);

bool exportMidiFile(const Session& session, const juce::File& file);
MidiImport importMidiFile(Session& session, const juce::File& file);

} // namespace dawlab::app
