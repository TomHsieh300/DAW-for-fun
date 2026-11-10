// Exercise 4 (solution): a tempo map from beats.
//
// Build and run:  l19_ex4_solution   (ctest, label "solution")
// Your version:   exercises/starter/ex4_beats_to_map.cpp -> l19_ex4_starter

#include "../exercises/ex4_beats_to_map.h"

#include <algorithm>
#include <cmath>

namespace ex4 {

bool mapFromBeats(const std::vector<double>& seconds, std::vector<TempoPoint>& out) {
    if (seconds.size() < 2)
        return false;
    std::vector<TempoPoint> points;
    for (size_t k = 0; k + 1 < seconds.size(); ++k) {
        const double interval = seconds[k + 1] - seconds[k];
        if (!(interval > 0.0))
            return false; // not one after another (or not a number)
        const double bpm = 60.0 / interval;
        if (!(bpm >= 20.0 && bpm <= 999.0))
            return false;
        if (!points.empty() && std::fabs(points.back().bpm - bpm) <= 1e-9)
            continue; // the tempo goes on
        if (static_cast<int>(points.size()) == kMaxPoints)
            return false;
        points.push_back({static_cast<std::int64_t>(k) * minidaw::kTicksPerQuarter, bpm, false});
    }
    out = std::move(points);
    return true;
}

double tapTempo(const std::vector<double>& seconds) {
    if (seconds.size() < 2)
        return 0.0;
    std::vector<double> intervals;
    for (size_t k = 0; k + 1 < seconds.size(); ++k)
        intervals.push_back(seconds[k + 1] - seconds[k]);
    // The median: the middle one, or the mean of the two in the middle.
    std::sort(intervals.begin(), intervals.end());
    const size_t n = intervals.size();
    const double median = n % 2 == 1 ? intervals[n / 2] : 0.5 * (intervals[n / 2 - 1] + intervals[n / 2]);
    return median > 0.0 ? 60.0 / median : 0.0;
}

} // namespace ex4

#include "../exercises/ex4_check.h"
