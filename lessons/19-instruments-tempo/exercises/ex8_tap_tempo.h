#pragma once
// Exercise 8 - Tap tempo in DawLab 19 (optional, JUCE).
//
// Exercise 4 turns the times of beats into a tempo. Here it meets the app: the session's tempo map, and the
// history, so that a tap is undone like every other edit.
//
//   applyTapTempo(session, taps)
//       The "Tap" button. The tempo of the taps - Exercise 4's tapTempo(), rounded to a tenth of a BPM - becomes
//       the tempo of the map's FIRST point; the other points stay. One undo step, "Tap tempo".
//       The tempo the map already has: true, and no undo step. Refused (false, nothing changes): fewer than
//       2 taps, a tempo outside 20 .. 999 BPM.
//   followBeats(session, seconds)
//       "The grid follows the band": beat k of the song was heard at seconds[k]. The map becomes Exercise 4's
//       mapFromBeats(), so that beat k plays seconds[k] - seconds[0] after the start. One undo step,
//       "Tempo from beats". Refused (false, nothing changes): what mapFromBeats() refuses, with the engine's 256
//       points as the limit.
//
// Exercise 4, solved, is in tempo_model.h. The session changes its map, undoably, with setTempoPoints().

#include "MainComponent.h"
#include "tempo_model.h"

namespace ex8 {

// ---- YOUR TURN (starter/ex8_tap_tempo.cpp) ------------------------------------------------------------------
bool applyTapTempo(dawlab::app::Session& session, const std::vector<double>& taps);
bool followBeats(dawlab::app::Session& session, const std::vector<double>& seconds);

} // namespace ex8
