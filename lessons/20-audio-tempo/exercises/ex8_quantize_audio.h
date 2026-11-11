#pragma once
// Exercise 8 - Audio quantize (optional, JUCE).
//
// A drummer is a few milliseconds early here and late there. Chapter 18 quantized MIDI notes by moving their ticks.
// Audio has no ticks - until it has warp markers. Put a marker on every hit, at the tick where the hit is heard
// now, and move that tick to the nearest line of the grid: the audio between two hits is stretched or squeezed
// so that each hit lands on its line.
//
//   findOnsets(audio, from, to, threshold, quietSamples)
//       The hits in the samples [from, to) of `audio`: the first sample whose |x| (the largest over the channels)
//       is at least threshold x the peak of [from, to), after at least quietSamples samples below that level
//       (or at `from`). In order. Empty if the range is silent or empty. A detector for clean, separate hits -
//       Exercise 6 says what a real one needs.
//   quantizeClip(session, track, clipIndex, gridTicks, strength, mode)
//       The clip gets to follow by `mode` (Varispeed or Stretch) with these markers, ONE undo step "Quantize audio":
//         - its first source sample (sourceOffset), at the tick where it is heard now;
//         - every onset of its source range (findOnsets with threshold 0.25 and 20 ms of quiet), at the tick
//           where it is heard now moved towards the nearest multiple of gridTicks by `strength`
//           (0: not at all, 1: onto it);
//         - the end of its source range, at the tick where it is heard now.
//       An onset marker whose tick is not at least one tick after the marker before it, or one tick before the
//       end's, is left out (two hits on one grid line: the first one stays).
//       "Where it is heard now": a clip that follows by varispeed or stretch already has markers - the tick of a
//       source sample is theirs (engine::WarpMap::tickAt()). Any other clip plays sample for sample: the tick of
//       source sample s is that of song sample clip.start + (s - clip.sourceOffset).
//       The source range: [sourceOffset, sourceOffset + n), n = follow->sourceLength for a warped clip, clip.length
//       for the others.
//       Refused (ok = false, nothing changes): a mode other than Varispeed and Stretch, gridTicks < 1, strength
//       outside 0 .. 1, a clip that is not there.

#include "MainComponent.h"

#include <cstdint>
#include <vector>

namespace ex8 {

struct Quantized {
    bool ok = false;
    int onsets = 0;  // hits found
    int markers = 0; // markers the clip has now
};

// ---- YOUR TURN (starter/ex8_quantize_audio.cpp) ---------------------------------------------------------------
std::vector<std::int64_t> findOnsets(const dawlab::AudioBuffer& audio, std::int64_t from, std::int64_t to, double threshold, std::int64_t quietSamples);
Quantized quantizeClip(dawlab::app::Session& session, dawlab::engine::TrackId track, int clipIndex, std::int64_t gridTicks, double strength,
                       dawlab::engine::FollowMode mode);

} // namespace ex8
