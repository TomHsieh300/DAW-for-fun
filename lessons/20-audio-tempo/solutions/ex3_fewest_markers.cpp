// Exercise 3 (solution): the fewest markers for a take.
//
// Build and run:  l20_ex3_solution   (ctest, label "solution")
// Your version:   exercises/starter/ex3_fewest_markers.cpp -> l20_ex3_starter

#include "../exercises/ex3_fewest_markers.h"

#include <cmath>

namespace ex3 {

bool fewestMarkers(const TempoMap& map, std::int64_t startSample, std::int64_t lengthSamples, double toleranceSeconds, std::int64_t gridSamples,
                   std::vector<WarpMarker>& out) {
    if (lengthSamples < 1 || gridSamples < 1 || startSample < 0 || !(toleranceSeconds >= 0.0))
        return false;
    const double rate = map.sampleRate();
    const std::vector<std::int64_t> n = candidatesOf(map, startSample, lengthSamples, gridSamples);
    const size_t count = n.size();
    std::vector<WarpMarker> exact(count);
    std::vector<double> heard(count); // where each candidate was recorded, in seconds of the song
    for (size_t k = 0; k < count; ++k) {
        exact[k] = exactMarker(map, startSample, n[k]);
        heard[k] = static_cast<double>(startSample + n[k]) / rate;
    }
    // May the candidates between i and j go? Then the line from marker i to marker j says their ticks.
    const auto fits = [&](size_t i, size_t j) {
        const WarpMarker &a = exact[i], &b = exact[j];
        for (size_t p = i + 1; p < j; ++p) {
            const double tick = a.tick + (exact[p].source - a.source) * (b.tick - a.tick) / (b.source - a.source);
            if (std::fabs(map.secondsAt(tick) - heard[p]) > toleranceSeconds + kSlack)
                return false;
        }
        return true;
    };
    // fewest[j]: the fewest markers from the first candidate to candidate j, j included.
    std::vector<int> fewest(count, 1);
    std::vector<size_t> from(count, 0);
    for (size_t j = 1; j < count; ++j) {
        fewest[j] = fewest[j - 1] + 1;
        from[j] = j - 1;
        for (size_t i = 0; i + 1 < j; ++i)
            if (fewest[i] + 1 < fewest[j] && fits(i, j)) {
                fewest[j] = fewest[i] + 1;
                from[j] = i;
            }
    }
    std::vector<WarpMarker> result;
    for (size_t j = count - 1;; j = from[j]) {
        result.push_back(exact[j]);
        if (j == 0)
            break;
    }
    std::reverse(result.begin(), result.end());
    out = std::move(result);
    return true;
}

} // namespace ex3

#include "../exercises/ex3_check.h"
