#pragma once
// Chapter 20, Step 3: varispeed.
//
// Step 2's clip follows with its start and comes apart at its seams. For EVERYTHING in a clip to follow, the clip
// is played at another speed: Step 1's speedAt(), the tempo now over the tempo the audio was made at. The oldest
// way to do that is a tape's: read the audio faster or slower. Pitch goes with the speed - twice as fast is an
// octave up - which is fine for drums and wrong for a singer (Step 4).
//
//   The position       Reading at a speed that is not 1 means reading BETWEEN the samples of the file: the
//                      position in the file is a fraction. Where is it for a song sample s?
//                          position(s) = warp.sourceAt(map.tickAt(s / rate))          (Step 1, exact)
//                      It is NOT "the last position plus the speed": a sum of a million rounded steps drifts,
//                      and a seek or a loop would have to know where the sum stands. The position of a block
//                      comes from the block's own samples, every block.
//   Inside a block     position(s) for every sample is a binary search and an exp() each. Cheaper: it is a
//                      straight line between its KNOTS - the tempo points and the warp markers - while the
//                      tempo holds, and nearly one on a ramp. So: the exact position at the block's two ends
//                      and at every knot inside it, and a line between those. On a ramp a line over a whole
//                      block of 512 samples can be most of a sample off in its middle, so the position is
//                      also exact at every 64th sample of the song: 64 times shorter lines are 64 x 64 times
//                      closer (Step 1's h^2 / 8).
//   Between samples    Three ways to read at a fraction, in the order of their cost:
//                        linear    the line between the two neighbours. Dulls and distorts the top octaves.
//                        Hermite   a cubic through the two neighbours with the slopes of their neighbours
//                                  (Catmull-Rom). Four samples, a few multiplications; good far below Nyquist.
//                        sinc      what the sampling theorem says: a sum of sin(pi x) / (pi x) around the
//                                  position, here 16 zero crossings to each side under a Blackman window.
//   Faster than 1      Reading faster is sampling again at a lower rate: what was above the new Nyquist comes
//                      back down as an alias. The sinc can be its own low-pass: stretched by the speed, it cuts
//                      at Nyquist / speed. The other two cannot.
//   The clip           plays a range of its source, [sourceFrom, sourceTo), wherever the two maps put it: the
//                      song samples whose position is in that range. Two clips cut from one take share a
//                      marker's worth of truth - the cut - so they meet at every tempo: no gap, no overlap.
//
// No JUCE, no threads. Header-only: the starter and the solution define the functions marked YOUR TURN.

#include "../warp_map.h"

#include <dawlab/DspMath.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace minidaw {

enum class Interpolation { Linear, Hermite, Sinc };

constexpr int kSincZeroCrossings = 16; // to each side of the position, at speed 1
constexpr int kPositionGrid = 64;      // the position is exact at every song sample that is a multiple of this

struct WarpedClip {
    int source = 0;          // which audio (an index into the sources of renderWarped())
    WarpMap warp;            // where the audio belongs in the music
    double sourceFrom = 0.0; // the clip plays the source positions [sourceFrom, sourceTo)
    double sourceTo = 0.0;
};

// ---- provided ---------------------------------------------------------------------------------------------

// Step 1: the exact position in the file for a song sample.
inline double exactPosition(const WarpedClip& clip, const TempoMap& map, double songSample) noexcept {
    return clip.warp.sourceAt(map.tickAt(songSample / map.sampleRate()));
}

// A sample of a source, 0 outside it.
inline float sampleAt(const std::vector<float>& source, std::int64_t i) noexcept {
    return i >= 0 && i < static_cast<std::int64_t>(source.size()) ? source[static_cast<size_t>(i)] : 0.0f;
}

// The Blackman window over [-1, 1] (0 outside).
inline double blackman(double x) noexcept {
    if (x <= -1.0 || x >= 1.0)
        return 0.0;
    return 0.42 + 0.5 * std::cos(dawlab::kPi * x) + 0.08 * std::cos(2.0 * dawlab::kPi * x);
}

// sin(pi x) / (pi x).
inline double sinc(double x) noexcept {
    return std::fabs(x) < 1e-9 ? 1.0 : std::sin(dawlab::kPi * x) / (dawlab::kPi * x);
}

// ---- YOUR TURN (starter/step03.cpp) -----------------------------------------------------------------------

// How many semitones a speed moves the pitch: 12 for 2, -12 for 0.5, 0 for 1.
double semitonesOf(double speed) noexcept;

// The source at a position between its samples.
//   Linear    a + frac x (b - a)
//   Hermite   with the four samples y0 y1 y2 y3 around the position (it is between y1 and y2, frac from y1):
//                 c1 = (y2 - y0) / 2,  c2 = y0 - 2.5 y1 + 2 y2 - 0.5 y3,  c3 = 1.5 (y1 - y2) + 0.5 (y3 - y0)
//                 y  = ((c3 frac + c2) frac + c1) frac + y1
//   Sinc      cutoff c = min(1, 1 / speed). Over the samples i within kSincZeroCrossings / c of the position:
//                 w(i) = sinc(c x) x blackman(x c / kSincZeroCrossings),  x = i - position
//                 y    = sum of w(i) x sample(i)  /  sum of w(i)
//             (dividing by the sum makes a constant come out as itself, whatever the fraction).
// `speed` (above 0) is only used by Sinc. Samples outside the source are 0.
float readAt(const std::vector<float>& source, double position, double speed, Interpolation how) noexcept;

// The positions in the file for the song samples [from, from + n): `positions` gets n values. Exact at `from`,
// at every knot inside the block - a tempo point of the map, a marker of the clip's warp map -, at every song
// sample inside it that is a multiple of kPositionGrid, and at `from + n`; a straight line between those.
// (A knot is rarely on a whole sample: it is at map.secondsAt(tick) x rate, and its position is
// warp.sourceAt(tick).)
void positionsFor(const WarpedClip& clip, const TempoMap& map, std::int64_t from, int n, std::vector<double>& positions);

// The song samples the clip covers: [firstSample, endSample) are exactly those whose exactPosition() is in
// [sourceFrom, sourceTo). (Find the sample from the seconds of the position's tick - and then make sure, as
// chapter 19's firstTickAtOrAfter() does: rounding must not put the edge a sample off.)
std::int64_t firstSample(const WarpedClip& clip, const TempoMap& map) noexcept;
std::int64_t endSample(const WarpedClip& clip, const TempoMap& map) noexcept;

// ADDS the clip to out[0 .. n) for the song samples [from, from + n): for each sample the clip covers, the
// source read at its position (positionsFor()), with the speed of that sample - the step of the position from
// this sample to the next - for the sinc.
void renderWarped(const WarpedClip& clip, const std::vector<std::vector<float>>& sources, const TempoMap& map, std::int64_t from, int n, float* out,
                  Interpolation how);

} // namespace minidaw
