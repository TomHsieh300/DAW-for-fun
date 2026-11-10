#pragma once
// Exercise 9 - A better MIDI import in DawLab 19 (optional, JUCE).
//
// A MIDI file may have more tempo events than the engine's map has points (256). An import that keeps every
// k-th one, each with its own tempo, loses the tempos it drops, and the notes drift away from where the file
// had them. Exercise 5 knows better. Put it before the import, without changing the app.
// (The app's import did keep every k-th tempo at first; today it does this itself, in TempoThinning.h. The
// exercise is to write it yourself: the self-check compares yours with every k-th tempo and with the app's.)
//
//   prepareTempos(tempos, maxPoints)
//       The file's tempos as a list a map can be made of, and then as few points as it takes:
//         - a tempo is clamped to 20 .. 999 BPM; the first one goes to tick 0; a tempo at the tick of the one
//           before it replaces it (the later one is what a player hears); one at an earlier tick is dropped;
//           no tempos at all are 120 BPM;
//         - then Exercise 5's thinTempo() with a tolerance of 0 - that alone drops every tempo that only repeats
//           the one before - and, while there are more than maxPoints points, with 0.5 ms, 1 ms, 2 ms ...:
//           the first of these tolerances that fits. (thinTempo() computes tempos again: clamp them once more.)
//       Refused (ok = false): maxPoints < 2 (a first and a last point are always kept).
//   importSongBetter(session, song)
//       The app's importSong() - tracks, clips, ONE undo step "Import MIDI" - for the song with its tempos
//       prepared for the engine's map, and the tolerance that took.
//
// Exercise 5, solved, is in tempo_model.h.

#include "MidiFileIO.h"
#include "tempo_model.h"

namespace ex9 {

struct Prepared {
    bool ok = false;
    std::vector<dawlab::app::midifile::Tempo> tempos;
    double tolerance = 0.0; // seconds: no tick of the song moves by more than this
};

struct BetterImport {
    dawlab::app::MidiImport result;
    double tolerance = 0.0;
};

// ---- YOUR TURN (starter/ex9_better_import.cpp) --------------------------------------------------------------
Prepared prepareTempos(const std::vector<dawlab::app::midifile::Tempo>& tempos, int maxPoints);
BetterImport importSongBetter(dawlab::app::Session& session, const dawlab::app::midifile::Song& song);

} // namespace ex9
