// Exercise 5 (starter): thin a tempo list.
//
// Fill in the TODO. Until then the self-check prints FAIL (expected). The solution is
// solutions/ex5_thin_tempo.cpp. What the function must do is in ../ex5_thin_tempo.h.

#include "../ex5_thin_tempo.h"

namespace ex5 {

bool thinTempo(const std::vector<TempoPoint>& points, double toleranceSeconds, std::vector<TempoPoint>& out) {
    // TODO: 1. refuse what the header says;
    //       2. a test "may the points between i and j go?": one tempo from point i to point j that keeps j's
    //          time, and every point between within the tolerance of its old time (pointTimes() helps);
    //       3. the fewest runs from the first point to the last: for every point j, the best way to get there
    //          is the best way to some earlier i from which a run to j fits, plus one;
    //       4. walk back from the last point and write the list.
    (void)points;
    (void)toleranceSeconds;
    (void)out;
    return false;
}

} // namespace ex5

#include "../ex5_check.h"
