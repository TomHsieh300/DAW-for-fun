#pragma once
// Exercise 7 - A click track in DawLab 19 (optional, JUCE).
//
// The metronome clicks while you record. A drummer who gets the song as a file needs the click IN the song: a
// track that can be exported, muted, and edited where the band wants a bar of silence. With the chapter's tempo
// map it costs almost nothing, because notes are kept in ticks: write a note on every beat once, and the click
// follows every tempo change by itself.
//
//   addClickTrack(main, firstBar, bars)
//       A new instrument track at the end, named "Click", with one clip named "Click" that starts at the start
//       of bar `firstBar` (from 1) and is `bars` bars of 4/4 long. In it a note on every beat, a sixteenth
//       (240 ticks) long, on channel 0: pitch 84 with velocity 127 on the first beat of a bar, pitch 72 with
//       velocity 90 on the others. All of it is ONE undo step, "Add click track": the AddTrack edit carries the
//       clip (TrackSnapshot::midiClips), with an id from Session::newMidiClipId().
//       Refused (ok = false, nothing changes): firstBar < 1, bars outside 1 .. 999, a project with no room for
//       another track.

#include "MainComponent.h"

namespace ex7 {

struct Click {
    bool ok = false;
    dawlab::engine::TrackId track; // the new one
    int notes = 0;
};

// ---- YOUR TURN (starter/ex7_click_track.cpp) ----------------------------------------------------------------
Click addClickTrack(dawlab::app::MainComponent& main, int firstBar, int bars);

} // namespace ex7
