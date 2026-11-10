// Exercise 7 (starter): a click track in DawLab 19.
//
// Fill in the TODO. Until then the self-check prints FAIL (expected). The solution is
// solutions/ex7_click_track.cpp. What the function must do is in ../ex7_click_track.h.

#include "../ex7_click_track.h"

namespace ex7 {

Click addClickTrack(dawlab::app::MainComponent& main, int firstBar, int bars) {
    // TODO: a MidiClipModel with a note on every beat (960 ticks; a bar is 3840), its id from
    //       Session::newMidiClipId(); an edit::AddTrack from Session::makeNewTrack(Mono, -1, "Click", true) that
    //       carries it in track.midiClips; one Session::perform(..., "Add click track").
    (void)main;
    (void)firstBar;
    (void)bars;
    return {};
}

} // namespace ex7

#include "../ex7_check.h"
