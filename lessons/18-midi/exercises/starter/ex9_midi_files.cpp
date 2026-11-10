// Exercise 9 (starter): MIDI files in DawLab 18.
//
// Fill in the TODOs. Until then the self-check prints FAIL (expected). The solution is
// solutions/ex9_midi_files.cpp. What the functions must do is in ../ex9_midi_files.h.

#include "../ex9_midi_files.h"

namespace ex9 {

Exported songOf(dawlab::app::MainComponent& main) {
    // TODO: one ex3::SmfTrack per instrument track with something to play: per unmuted clip, its notes() inside
    //       [offset, offset + length), placed at clip.start + (tick - offset), a note off at the end or at the
    //       clip's end; its controller points inside the window. The tempo from the transport.
    (void)main;
    return {};
}

Imported importSong(dawlab::app::MainComponent& main, const ex3::Song& song) {
    // TODO: per song track a MidiClipModel (notes paired oldest first per channel and pitch, controllers, the
    //       rest counted), whole bars long, with an id from Session::newMidiClipId(); an edit::AddTrack from
    //       Session::makeNewTrack(..., true) carrying it (give each one its own id: makeNewTrack() names the same
    //       next id every time); a SetTempo; one Session::perform(..., "Import MIDI").
    (void)main;
    (void)song;
    Imported r;
    r.error = "not done yet";
    return r;
}

} // namespace ex9

#include "../ex9_check.h"
