#pragma once
// Exercise 4 - A tempo map from beats.
//
// A band recorded without a click. The drummer speeds up into the chorus and relaxes after it, and the grid of
// a song with one tempo fits nowhere. The usual cure is to turn it around: mark where the beats ARE - by tapping
// along, or with a beat detector - and build the tempo map that puts the grid there. The notes drawn afterwards
// and the metronome then follow the band.
//
//   mapFromBeats(seconds)  beat k was heard at seconds[k]. The map has beat k (tick 960 k) play at
//                          seconds[k] - seconds[0]: a jump at each beat to the tempo that makes the next beat
//                          arrive on time, 60 / (seconds[k + 1] - seconds[k]). The last tempo holds. Beats that
//                          keep the tempo of the one before (to 1e-9 BPM) need no point.
//                          Refused (false, `out` untouched): fewer than 2 beats, beats that do not come one after
//                          another, a tempo outside 20 .. 999 BPM, more than kMaxPoints points.
//   tapTempo(seconds)      one tempo from a few taps, as a "tap tempo" button gives it: 60 / the MEDIAN interval,
//                          so that one late or doubled tap does not move it. 0 for fewer than 2 taps.
//
// The model is Step 1's map (steps/step02-transport/minidaw.h has it solved).

#include "../steps/step02-transport/minidaw.h"

#include <vector>

namespace ex4 {

using minidaw::TempoPoint;

constexpr int kMaxPoints = 256;

// ---- YOUR TURN (starter/ex4_beats_to_map.cpp) ---------------------------------------------------------------
bool mapFromBeats(const std::vector<double>& seconds, std::vector<TempoPoint>& out);
double tapTempo(const std::vector<double>& seconds);

} // namespace ex4
