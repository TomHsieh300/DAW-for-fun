// Exercise 4 (solution): measure the latency, and know when not to trust it.
//
// Build and run:  l17_ex4_solution   (ctest, label "solution")
// Your version:   exercises/starter/ex4_measure_latency.cpp -> l17_ex4_starter

#include "../exercises/ex4_measure_latency.h"

#include <algorithm>
#include <cmath>

namespace ex4 {

LatencyMeasurement measureLatency(const std::vector<float>& recorded, const std::vector<std::int64_t>& chirpAt,
                                  const std::vector<float>& chirp, int early, int late, double sampleRate) {
    LatencyMeasurement m;
    const auto size = static_cast<std::int64_t>(recorded.size());
    auto at = [&recorded, size](std::int64_t p) { return p >= 0 && p < size ? recorded[static_cast<size_t>(p)] : 0.0f; };
    std::vector<int> heardLags;
    for (const std::int64_t c : chirpAt) {
        std::vector<double> r;
        r.reserve(static_cast<size_t>(early + late + 1));
        for (int d = -early; d <= late; ++d) {
            double acc = 0.0;
            for (size_t k = 0; k < chirp.size(); ++k)
                acc += static_cast<double>(chirp[k]) * at(c + d + static_cast<std::int64_t>(k));
            r.push_back(std::abs(acc)); // a microphone may invert the polarity
        }
        const auto best = std::max_element(r.begin(), r.end());
        const int lag = static_cast<int>(best - r.begin()) - early;
        std::vector<double> sorted = r;
        std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(sorted.size() / 2), sorted.end());
        const double median = sorted[sorted.size() / 2];
        const bool heard = *best > 8.0 * median; // all zeros: 0 > 0 is false
        m.lags.push_back(lag);
        m.heard.push_back(heard);
        if (heard)
            heardLags.push_back(lag);
    }
    m.heardCount = static_cast<int>(heardLags.size());
    if (!heardLags.empty()) {
        std::sort(heardLags.begin(), heardLags.end());
        m.lag = heardLags[heardLags.size() / 2];
        m.spread = heardLags.back() - heardLags.front();
    }
    m.trustworthy = !chirpAt.empty() && 4 * m.heardCount >= 3 * static_cast<int>(chirpAt.size()) &&
                    m.spread <= static_cast<int>(sampleRate / 1000.0);
    return m;
}

} // namespace ex4

#include "../exercises/ex4_check.h"
