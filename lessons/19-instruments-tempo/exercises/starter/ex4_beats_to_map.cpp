// Exercise 4 (starter): a tempo map from beats.
//
// Fill in the TODOs. Until then the self-check prints FAIL (expected). The solution is
// solutions/ex4_beats_to_map.cpp. What the functions must do is in ../ex4_beats_to_map.h.

#include "../ex4_beats_to_map.h"

namespace ex4 {

bool mapFromBeats(const std::vector<double>& seconds, std::vector<TempoPoint>& out) {
    // TODO: per beat k the tempo 60 / (seconds[k + 1] - seconds[k]) at tick 960 k - a point only where it differs
    //       from the one before. Refuse what the header says, without touching `out`.
    (void)seconds;
    (void)out;
    return false;
}

double tapTempo(const std::vector<double>& seconds) {
    // TODO: 60 / the median of the intervals between the taps.
    (void)seconds;
    return 0.0;
}

} // namespace ex4

#include "../ex4_check.h"
