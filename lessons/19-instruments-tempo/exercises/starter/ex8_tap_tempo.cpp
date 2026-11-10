// Exercise 8 (starter): tap tempo in DawLab 19.
//
// Fill in the TODOs. Until then the self-check prints FAIL (expected). The solution is
// solutions/ex8_tap_tempo.cpp. What the functions must do is in ../ex8_tap_tempo.h.

#include "../ex8_tap_tempo.h"

namespace ex8 {

bool applyTapTempo(dawlab::app::Session& session, const std::vector<double>& taps) {
    // TODO: tempomodel::tapTempo(), rounded to a tenth; the map's points (Session::getTempoMap(): size(),
    //       point(i)) with the first one's bpm replaced; Session::setTempoPoints(points, "Tap tempo").
    (void)session;
    (void)taps;
    return false;
}

bool followBeats(dawlab::app::Session& session, const std::vector<double>& seconds) {
    // TODO: tempomodel::mapFromBeats() with engine::TempoMap::kMaxPoints; Session::setTempoPoints(points,
    //       "Tempo from beats").
    (void)session;
    (void)seconds;
    return false;
}

} // namespace ex8

#include "../ex8_check.h"
