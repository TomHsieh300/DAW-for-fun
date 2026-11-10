#pragma once
// Chapter 20, Step 1: a warp map.
//
// In DawLab 19 a note is at a tick and an audio clip is at a sample. Change the tempo, and the notes move with
// the bars while the guitar stays where it was. For audio to follow the tempo, a clip has to know where its
// audio belongs IN THE MUSIC. A WARP MAP says so: a list of markers, each a pair
//
//     (source, tick)     "this sample of the file belongs to this tick of the song"
//
// Between two markers the audio is spread evenly over the ticks; before the first and after the last marker the
// nearest pair's line goes on. The tempo map (chapter 19) turns ticks into time, so the two maps together answer
// both questions a player has:
//
//     song sample --tempo map--> tick --warp map--> source sample       "what do I play now?"
//     source sample --warp map--> tick --tempo map--> seconds           "when is this heard?"
//
// A source position and a tick are both fractions here (double): a marker is rarely on a whole tick, and one
// tick at 120 BPM is 25 samples.
//
//   sourceAt(tick), tickAt(source)   the line of the pair of markers around it, both ways
//   speedAt(tick)                    source samples per song sample: the slope of the warp map (samples a tick)
//                                    times the tempo map's ticks a sample. 1 = as recorded, 2 = twice as fast.
//   markersForLoop()                 a loop of a known number of beats: two markers
//   markersForTake()                 a take recorded over a tempo map: the markers with which it plays as it
//                                    was recorded - and follows when the map changes later. On a jump the
//                                    ticks of a take are a straight line of its samples, so a marker at the
//                                    take's start, at every tempo point under it and at its end is EXACT. On a
//                                    ramp the line is a curve; n equal pieces of a curve f over h ticks each
//                                    are off by at most h^2 / 8 x max|f''| - which says how many pieces a
//                                    tolerance needs.

#include "../tempo_map.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace minidaw {

struct WarpMarker {
    double source = 0.0; // a position in the file, in samples
    double tick = 0.0;   // where it belongs in the song
};

class WarpMap {
public:
    // ---- provided -----------------------------------------------------------------------------------------
    // False (the map stays as it was) unless: at least two markers, finite, and both the sources and the ticks
    // strictly increasing (audio does not play backwards, and no two samples share a tick).
    bool set(std::vector<WarpMarker> markers) {
        if (markers.size() < 2)
            return false;
        for (size_t i = 0; i < markers.size(); ++i)
            if (!std::isfinite(markers[i].source) || !std::isfinite(markers[i].tick) ||
                (i > 0 && (markers[i].source <= markers[i - 1].source || markers[i].tick <= markers[i - 1].tick)))
                return false;
        markers_ = std::move(markers);
        return true;
    }
    const std::vector<WarpMarker>& markers() const noexcept { return markers_; }

    // The index i of the pair (i, i + 1) whose line holds `tick`: the pair around it, the first pair before the
    // first marker, the last pair after the last one.
    size_t pairOfTick(double tick) const noexcept {
        size_t lo = 0, hi = markers_.size() - 1;
        while (hi - lo > 1) {
            const size_t mid = (lo + hi) / 2;
            (markers_[mid].tick <= tick ? lo : hi) = mid;
        }
        return lo;
    }
    size_t pairOfSource(double source) const noexcept {
        size_t lo = 0, hi = markers_.size() - 1;
        while (hi - lo > 1) {
            const size_t mid = (lo + hi) / 2;
            (markers_[mid].source <= source ? lo : hi) = mid;
        }
        return lo;
    }

    // ---- YOUR TURN (starter/step01.cpp) -----------------------------------------------------------------------
    double sourceAt(double tick) const noexcept;
    double tickAt(double source) const noexcept;

private:
    std::vector<WarpMarker> markers_{{0.0, 0.0}, {1.0, 1.0}};
};

// ---- YOUR TURN (starter/step01.cpp) ---------------------------------------------------------------------------

// The source sample the song needs at a song sample (a fraction; the file is at the map's rate).
double sourceAtSample(const WarpMap& warp, const TempoMap& map, double songSample) noexcept;
// When a source sample is heard, in seconds of the song.
double songSecondsOf(const WarpMap& warp, const TempoMap& map, double source) noexcept;
// Source samples per song sample at a tick.
double speedAt(const WarpMap& warp, const TempoMap& map, double tick) noexcept;

// A loop of `lengthSamples` samples that is `beats` quarter notes long, its first sample at `startTick`: two
// markers. Empty unless the length and the beats are above 0.
std::vector<WarpMarker> markersForLoop(std::int64_t lengthSamples, double beats, double startTick);

// A take of `lengthSamples` samples recorded over `map`, its first sample at song sample `startSample` (>= 0).
// With these markers and the same map, no sample of the take plays more than `toleranceSeconds` from where it was
// recorded. Markers: the take's start, every tempo point under the take, its end - and on a ramp as many equal
// pieces (in ticks) as the tolerance needs:
//     f(t) = seconds of tick t,  f'' = -(60/960) k / B(t)^2  (k: the ramp's BPM a tick),
//     pieces = ceil( ticks x sqrt( max|f''| / (8 x tolerance) ) ),  max|f''| at the slower end of the ramp.
// Empty unless the length and the tolerance are above 0 and the start is not negative.
std::vector<WarpMarker> markersForTake(const TempoMap& map, std::int64_t startSample, std::int64_t lengthSamples, double toleranceSeconds);

} // namespace minidaw
