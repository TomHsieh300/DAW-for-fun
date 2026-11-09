// Exercise 4 (starter): measure the latency, and know when not to trust it.
//
// Fill in the TODO. Until then the self-check prints FAIL (expected). The solution is
// solutions/ex4_measure_latency.cpp. What the function must do is in ../ex4_measure_latency.h.

#include "../ex4_measure_latency.h"

#include <algorithm>
#include <cmath>

namespace ex4 {

LatencyMeasurement measureLatency(const std::vector<float>& recorded, const std::vector<std::int64_t>& chirpAt,
                                  const std::vector<float>& chirp, int early, int late, double sampleRate) {
    // TODO: per chirp at c, for every lag d in [-early, late]: |sum_k chirp[k] * recorded[c + d + k]|
    //       (0 outside the recording). The largest one gives lags[]; heard when it is > 8 x the median
    //       of all of them. Then lag = the median of the heard lags, spread = max - min of them, and
    //       trustworthy = at least 3/4 heard and spread <= sampleRate / 1000.
    (void)recorded;
    (void)early;
    (void)late;
    (void)sampleRate;
    LatencyMeasurement m;
    m.lags.assign(chirpAt.size(), 0);
    m.heard.assign(chirpAt.size(), false);
    (void)chirp;
    return m;
}

} // namespace ex4

#include "../ex4_check.h"
