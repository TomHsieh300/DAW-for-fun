#pragma once
// Exercise 5 - Thin a tempo list.
//
// A tempo map made from a band's beats (Exercise 4) has a point on every beat, and a MIDI file exported by
// another program may have a tempo event every few ticks. DawLab's map holds 256 points, and nobody wants to edit
// a lane with a thousand. The easy way is to keep every k-th point - and the song drifts, because the tempos
// thrown away are simply gone. (DawLab 19's import did that at first. Today it does what this exercise asks
// for: app/Source/TempoThinning.h gives the exercise away.) Do it properly: the FEWEST points with which no tick of the song moves by more than
// a tolerance.
//
//   thinTempo(points, toleranceSeconds, out)
//       `points` are jumps (no ramps). The points of `out` are some of them, at their ticks:
//         - the first point stays, and so does the last one, with its tempo: it holds for ever, and any other
//           tempo would drift without end;
//         - every other point that stays gets the tempo that brings the NEXT point that stays at exactly its old
//           time. So a point that stays never moves, and errors do not add up from run to run;
//         - a point may go if, with it gone, it plays within toleranceSeconds (+ kSlack) of its old time. Between
//           two points time is a straight line in both maps, so then every tick in between is within it too;
//         - and of all the lists that do this, `out` is one with the fewest points.
//       Refused (false, `out` untouched): a list Step 1's map would refuse, a ramp, a tolerance that is negative
//       or not a number.
//
// Taking the longest run that fits, again and again, is NOT always the fewest: a shorter first run can make a
// much longer second one possible. The self-check tries every choice for small lists and compares.
//
// The model is Step 1's map (steps/step02-transport/minidaw.h has it solved).

#include "../steps/step02-transport/minidaw.h"

#include <vector>

namespace ex5 {

using minidaw::TempoPoint;

// A nanosecond for the arithmetic: a point exactly on the tolerance may go.
constexpr double kSlack = 1e-9;

// GIVEN: when each point of a list of jumps plays, in seconds from the first.
inline std::vector<double> pointTimes(const std::vector<TempoPoint>& points) {
    std::vector<double> times(points.size(), 0.0);
    for (size_t i = 1; i < points.size(); ++i)
        times[i] = times[i - 1] + static_cast<double>(points[i].tick - points[i - 1].tick) * 60.0 / (960.0 * points[i - 1].bpm);
    return times;
}

// ---- YOUR TURN (starter/ex5_thin_tempo.cpp) -----------------------------------------------------------------
bool thinTempo(const std::vector<TempoPoint>& points, double toleranceSeconds, std::vector<TempoPoint>& out);

} // namespace ex5
