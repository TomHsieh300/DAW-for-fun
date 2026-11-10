#pragma once
// Step 1's warp map, solved: Step 3 starts from it. (Do Step 1 first: this file gives it away.)
//
// Markers tie a sample of the file to a tick of the song, with straight lines between them and beyond them.

#include "tempo_map.h"

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
    // False (the map stays as it was) unless: at least two markers, finite, sources and ticks strictly increasing.
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

    double sourceAt(double tick) const noexcept {
        size_t lo = 0, hi = markers_.size() - 1;
        while (hi - lo > 1) {
            const size_t mid = (lo + hi) / 2;
            (markers_[mid].tick <= tick ? lo : hi) = mid;
        }
        const WarpMarker &a = markers_[lo], &b = markers_[lo + 1];
        return a.source + (tick - a.tick) * (b.source - a.source) / (b.tick - a.tick);
    }
    double tickAt(double source) const noexcept {
        size_t lo = 0, hi = markers_.size() - 1;
        while (hi - lo > 1) {
            const size_t mid = (lo + hi) / 2;
            (markers_[mid].source <= source ? lo : hi) = mid;
        }
        const WarpMarker &a = markers_[lo], &b = markers_[lo + 1];
        return a.tick + (source - a.source) * (b.tick - a.tick) / (b.source - a.source);
    }

private:
    std::vector<WarpMarker> markers_{{0.0, 0.0}, {1.0, 1.0}};
};

// A loop of `lengthSamples` samples that is `beats` quarter notes long, its first sample at `startTick`.
inline std::vector<WarpMarker> markersForLoop(std::int64_t lengthSamples, double beats, double startTick) {
    if (lengthSamples <= 0 || !(beats > 0.0))
        return {};
    return {{0.0, startTick}, {static_cast<double>(lengthSamples), startTick + beats * kTicksPerQuarter}};
}

// A take recorded over `map` from song sample `startSample`: it plays within `toleranceSeconds` of where it was.
inline std::vector<WarpMarker> markersForTake(const TempoMap& map, std::int64_t startSample, std::int64_t lengthSamples, double toleranceSeconds) {
    if (startSample < 0 || lengthSamples <= 0 || !(toleranceSeconds > 0.0))
        return {};
    const double rate = map.sampleRate();
    const double startTick = map.tickAt(static_cast<double>(startSample) / rate);
    const double endTick = map.tickAt(static_cast<double>(startSample + lengthSamples) / rate);
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
        int pieces = 1;
        if (k != 0.0) {
            const TempoPoint& p = map.points()[segment];
            const double slow = std::min(p.bpm + k * (a - static_cast<double>(p.tick)), p.bpm + k * (b - static_cast<double>(p.tick)));
            pieces = std::max(1, static_cast<int>(std::ceil((b - a) * std::sqrt(60.0 / kTicksPerQuarter * std::fabs(k) / (slow * slow) / (8.0 * toleranceSeconds)))));
        }
        for (int j = 1; j <= pieces; ++j) {
            const double tick = j == pieces ? b : a + (b - a) * j / pieces;
            markers.push_back({map.secondsAt(tick) * rate - static_cast<double>(startSample), tick});
        }
    }
    markers.back().source = static_cast<double>(lengthSamples);
    return markers;
}

} // namespace minidaw
