// Chapter 20, Step 1 (solution): a warp map.
//
// Build and run:  l20_step01_solution   (ctest, label "solution")
// Your version:   starter/step01.cpp -> l20_step01_starter

#include "../minidaw.h"

namespace minidaw {

double WarpMap::sourceAt(double tick) const noexcept {
    const WarpMarker& a = markers_[pairOfTick(tick)];
    const WarpMarker& b = markers_[pairOfTick(tick) + 1];
    return a.source + (tick - a.tick) * (b.source - a.source) / (b.tick - a.tick);
}

double WarpMap::tickAt(double source) const noexcept {
    const WarpMarker& a = markers_[pairOfSource(source)];
    const WarpMarker& b = markers_[pairOfSource(source) + 1];
    return a.tick + (source - a.source) * (b.tick - a.tick) / (b.source - a.source);
}

double sourceAtSample(const WarpMap& warp, const TempoMap& map, double songSample) noexcept {
    return warp.sourceAt(map.tickAt(songSample / map.sampleRate()));
}

double songSecondsOf(const WarpMap& warp, const TempoMap& map, double source) noexcept {
    return map.secondsAt(warp.tickAt(source));
}

double speedAt(const WarpMap& warp, const TempoMap& map, double tick) noexcept {
    const WarpMarker& a = warp.markers()[warp.pairOfTick(tick)];
    const WarpMarker& b = warp.markers()[warp.pairOfTick(tick) + 1];
    const double samplesPerTick = (b.source - a.source) / (b.tick - a.tick);
    const double ticksPerSongSample = map.bpmAt(tick) * kTicksPerQuarter / (60.0 * map.sampleRate());
    return samplesPerTick * ticksPerSongSample;
}

std::vector<WarpMarker> markersForLoop(std::int64_t lengthSamples, double beats, double startTick) {
    if (lengthSamples <= 0 || !(beats > 0.0))
        return {};
    return {{0.0, startTick}, {static_cast<double>(lengthSamples), startTick + beats * kTicksPerQuarter}};
}

std::vector<WarpMarker> markersForTake(const TempoMap& map, std::int64_t startSample, std::int64_t lengthSamples, double toleranceSeconds) {
    if (startSample < 0 || lengthSamples <= 0 || !(toleranceSeconds > 0.0))
        return {};
    const double rate = map.sampleRate();
    const double startTick = map.tickAt(static_cast<double>(startSample) / rate);
    const double endTick = map.tickAt(static_cast<double>(startSample + lengthSamples) / rate);

    // Where the line of ticks bends: at the tempo points under the take.
    std::vector<double> edges{startTick};
    for (const TempoPoint& p : map.points())
        if (static_cast<double>(p.tick) > startTick && static_cast<double>(p.tick) < endTick)
            edges.push_back(static_cast<double>(p.tick));
    edges.push_back(endTick);

    std::vector<WarpMarker> markers{{0.0, startTick}};
    for (size_t e = 0; e + 1 < edges.size(); ++e) {
        const double a = edges[e], b = edges[e + 1];
        const size_t segment = map.segmentOfTick(a);
        const double k = map.slopeOf(segment);
        int pieces = 1; // a jump: the ticks are a straight line of the samples
        if (k != 0.0) {
            const TempoPoint& p = map.points()[segment];
            const double slow = std::min(p.bpm + k * (a - static_cast<double>(p.tick)), p.bpm + k * (b - static_cast<double>(p.tick)));
            const double curve = 60.0 / kTicksPerQuarter * std::fabs(k) / (slow * slow); // max |f''|
            pieces = std::max(1, static_cast<int>(std::ceil((b - a) * std::sqrt(curve / (8.0 * toleranceSeconds)))));
        }
        for (int j = 1; j <= pieces; ++j) {
            const double tick = j == pieces ? b : a + (b - a) * j / pieces;
            markers.push_back({map.secondsAt(tick) * rate - static_cast<double>(startSample), tick});
        }
    }
    markers.back().source = static_cast<double>(lengthSamples); // the take's last sample, not a rounding of it
    return markers;
}

} // namespace minidaw

#include "../check.h"
