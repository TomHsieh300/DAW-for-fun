// Exercise 8 (solution): tap tempo in DawLab 19.
//
// Build and run:  l19_ex8_solution   (ctest, label "solution"; needs -Juce)
// Your version:   exercises/starter/ex8_tap_tempo.cpp -> l19_ex8_starter

#include "../exercises/ex8_tap_tempo.h"

namespace ex8 {

using namespace dawlab;
using namespace dawlab::app;

bool applyTapTempo(Session& session, const std::vector<double>& taps) {
    const double bpm = std::round(tempomodel::tapTempo(taps) * 10.0) / 10.0;
    if (!(bpm >= engine::TempoMap::kMinBpm && bpm <= engine::TempoMap::kMaxBpm))
        return false; // also fewer than 2 taps: tapTempo() says 0
    const engine::TempoMap& map = session.getTempoMap();
    if (map.point(0).bpm == bpm)
        return true; // nothing to undo
    std::vector<engine::TempoPoint> points;
    for (int i = 0; i < map.size(); ++i)
        points.push_back(map.point(i));
    points[0].bpm = bpm;
    return session.setTempoPoints(std::move(points), "Tap tempo");
}

bool followBeats(Session& session, const std::vector<double>& seconds) {
    std::vector<engine::TempoPoint> points;
    if (!tempomodel::mapFromBeats(seconds, points, engine::TempoMap::kMaxPoints))
        return false;
    return session.setTempoPoints(std::move(points), "Tempo from beats");
}

} // namespace ex8

#include "../exercises/ex8_check.h"
