// Chapter 20, Step 1 (starter): a warp map.
//
// Fill in the TODOs. Until then the self-check prints FAIL (expected). The solution is
// solution/step01.cpp. What the functions must do is in ../minidaw.h.

#include "../minidaw.h"

namespace minidaw {

double WarpMap::sourceAt(double tick) const noexcept {
    // TODO: the line through the markers pairOfTick(tick) and the one after it.
    (void)tick;
    return 0.0;
}

double WarpMap::tickAt(double source) const noexcept {
    // TODO: the same line the other way, through the markers pairOfSource(source) and the one after it.
    (void)source;
    return 0.0;
}

double sourceAtSample(const WarpMap& warp, const TempoMap& map, double songSample) noexcept {
    // TODO: song sample -> seconds -> tick (the tempo map) -> source (the warp map).
    (void)warp, (void)map;
    return songSample;
}

double songSecondsOf(const WarpMap& warp, const TempoMap& map, double source) noexcept {
    // TODO: source -> tick (the warp map) -> seconds (the tempo map).
    (void)warp;
    return source / map.sampleRate();
}

double speedAt(const WarpMap& warp, const TempoMap& map, double tick) noexcept {
    // TODO: (samples a tick of the pair of markers around `tick`) x (ticks a song sample at the tempo there).
    (void)warp, (void)map, (void)tick;
    return 1.0;
}

std::vector<WarpMarker> markersForLoop(std::int64_t lengthSamples, double beats, double startTick) {
    // TODO: two markers - the first sample at startTick, the sample after the last one `beats` quarter notes on.
    (void)lengthSamples, (void)beats, (void)startTick;
    return {};
}

std::vector<WarpMarker> markersForTake(const TempoMap& map, std::int64_t startSample, std::int64_t lengthSamples, double toleranceSeconds) {
    // TODO: the ticks of the take's start and end (map.tickAt); a marker there and at every tempo point between
    //       them, its source = map.secondsAt(tick) x rate - startSample; on a ramp (map.slopeOf() != 0) the
    //       number of equal pieces from minidaw.h's formula.
    (void)map, (void)startSample, (void)lengthSamples, (void)toleranceSeconds;
    return {};
}

} // namespace minidaw

#include "../check.h"
