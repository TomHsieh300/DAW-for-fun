// Exercise 5 (solution): thin a tempo list.
//
// Build and run:  l19_ex5_solution   (ctest, label "solution")
// Your version:   exercises/starter/ex5_thin_tempo.cpp -> l19_ex5_starter

#include "../exercises/ex5_thin_tempo.h"

#include <algorithm>
#include <cmath>

namespace ex5 {

namespace {

// One tempo from point i to point j that keeps j's time: seconds a tick.
double secondsPerTick(const std::vector<TempoPoint>& points, const std::vector<double>& times, size_t i, size_t j) {
    return (times[j] - times[i]) / static_cast<double>(points[j].tick - points[i].tick);
}

// May the points between i and j go?
bool fits(const std::vector<TempoPoint>& points, const std::vector<double>& times, size_t i, size_t j, double tolerance) {
    const double perTick = secondsPerTick(points, times, i, j);
    for (size_t p = i + 1; p < j; ++p)
        if (std::fabs(times[i] + static_cast<double>(points[p].tick - points[i].tick) * perTick - times[p]) > tolerance + kSlack)
            return false;
    return true;
}

} // namespace

bool thinTempo(const std::vector<TempoPoint>& points, double toleranceSeconds, std::vector<TempoPoint>& out) {
    if (!(toleranceSeconds >= 0.0) || !minidaw::TempoMap().set(48000.0, points))
        return false;
    for (const TempoPoint& p : points)
        if (p.rampToNext)
            return false;

    // fewest[j]: the fewest runs that lead from the first point to point j; from[j]: where the last one starts.
    const size_t n = points.size();
    const std::vector<double> times = pointTimes(points);
    std::vector<int> fewest(n, 0);
    std::vector<size_t> from(n, 0);
    for (size_t j = 1; j < n; ++j) {
        fewest[j] = fewest[j - 1] + 1; // a run of one point always fits
        from[j] = j - 1;
        for (size_t i = 0; i + 1 < j; ++i)
            if (fewest[i] + 1 < fewest[j] && fits(points, times, i, j, toleranceSeconds)) {
                fewest[j] = fewest[i] + 1;
                from[j] = i;
            }
    }

    // Back from the last point, which stays as it is.
    std::vector<TempoPoint> result{points.back()};
    for (size_t j = n - 1; j > 0; j = from[j])
        result.push_back({points[from[j]].tick, 60.0 / (960.0 * secondsPerTick(points, times, from[j], j)), false});
    std::reverse(result.begin(), result.end());
    out = std::move(result);
    return true;
}

} // namespace ex5

#include "../exercises/ex5_check.h"
