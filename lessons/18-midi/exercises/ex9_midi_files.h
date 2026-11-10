#pragma once
// Exercise 9 - MIDI files in DawLab 18 (optional, JUCE).
//
// Exercise 3 reads and writes Standard MIDI Files as events. DawLab keeps notes - a start and a length - in clips
// on instrument tracks. This exercise is the step between, without changing the app:
//
//   songOf()      the project as it plays, as a song of events: one track per instrument track that has anything
//                 to play (its name), the session's tempo, 4/4. Per clip that is not muted, what sounds inside its
//                 window - Step 2's [offset, offset + length) of the clip's material, after quantize (what the
//                 engine plays, MidiClipModel::notes()):
//                   a note that starts in the window: note on at clip.start + (start - offset), note off (velocity
//                   0) at its end, or at the clip's end if it goes on past it; a note that starts outside is left
//                   out. A controller point inside the window: a controller message at its place.
//   importSong()  every track of a song as a new instrument track at the end (its name, or "MIDI <n>" for the
//                 song's n-th track, from 1), with one clip from tick 0 to the end of the bar in which its last
//                 event lies (at least one bar), named like the track:
//                   notes   a note on opens a note; a note off - or a note on with velocity 0 - of the same pitch and
//                           channel ends the one of them that opened first. A note still open after the track's last
//                           event ends there (at least 1 tick long).
//                   controllers become the clip's controller points. Everything else - pitch bend, program
//                           change, aftertouch - is counted in `dropped`.
//                 The song's tempo becomes the project's. All of it is ONE undo step, "Import MIDI": the AddTrack
//                 edits carry their clips (TrackSnapshot::midiClips), and a SetTempo goes with them. A song with
//                 no track is refused, and nothing changes.

#include "MainComponent.h"
#include "smf_model.h"

namespace ex9 {

struct Exported {
    ex3::Song song;
    int notes = 0; // note ons written
};

struct Imported {
    bool ok = false;
    juce::String error;
    std::vector<dawlab::engine::TrackId> tracks; // the new ones, in order
    int notes = 0;
    int dropped = 0;
};

// ---- YOUR TURN (starter/ex9_midi_files.cpp) ------------------------------------------------------------------
Exported songOf(dawlab::app::MainComponent& main);
Imported importSong(dawlab::app::MainComponent& main, const ex3::Song& song);

// ---- provided: the files ----------------------------------------------------------------------------------------
inline bool exportFile(dawlab::app::MainComponent& main, const juce::File& file) {
    const std::vector<std::uint8_t> bytes = ex3::writeSmf(songOf(main).song);
    return file.replaceWithData(bytes.data(), bytes.size());
}
inline Imported importFile(dawlab::app::MainComponent& main, const juce::File& file) {
    juce::MemoryBlock data;
    if (!file.loadFileAsData(data)) {
        Imported r;
        r.error = "cannot read " + file.getFullPathName();
        return r;
    }
    const auto* p = static_cast<const std::uint8_t*>(data.getData());
    const ex3::SmfRead read = ex3::readSmf(std::vector<std::uint8_t>(p, p + data.getSize()));
    if (!read.ok) {
        Imported r;
        r.error = juce::String("not a MIDI file DawLab can read: ") + read.error.c_str();
        return r;
    }
    return importSong(main, read.song);
}

} // namespace ex9
