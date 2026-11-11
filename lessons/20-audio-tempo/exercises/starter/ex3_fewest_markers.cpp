// Exercise 3 (starter): the fewest markers for a take.
//
// Fill in the TODO. Until then the self-check prints FAIL (expected). The solution is
// solutions/ex3_fewest_markers.cpp. What the function must do is in ../ex3_fewest_markers.h.

#include "../ex3_fewest_markers.h"

namespace ex3 {

bool fewestMarkers(const TempoMap& map, std::int64_t startSample, std::int64_t lengthSamples, double toleranceSeconds, std::int64_t gridSamples,
                   std::vector<WarpMarker>& out) {
    // TODO: 1. refuse what the header says;
    //       2. the candidates (candidatesOf()) and their exact markers (exactMarker());
    //       3. a test "may the candidates between i and j go?": the line from marker i to marker j gives each of
    //          them a tick; is map.secondsAt() of that tick within the tolerance of where it was recorded?
    //       4. the fewest markers from the first candidate to each candidate j, as in chapter 19's Exercise 5;
    //       5. walk back from the last candidate and write the list.
    (void)map, (void)startSample, (void)lengthSamples, (void)toleranceSeconds, (void)gridSamples, (void)out;
    return false;
}

} // namespace ex3

#include "../ex3_check.h"
