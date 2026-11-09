#pragma once
// Exercise 4 - Measure the latency, and know when not to trust it.
//
// Step 2's measureRoundTrip() finds ONE click by its first loud frame. That works through a cable.
// Through the air, from a laptop's speaker into its microphone, it does not: the click comes back
// quiet, smeared, with noise and the room's echo, and the first loud frame may be a door. DawLab 17's
// device test (Step 7, "On a real device") does better, and this exercise is its core:
//
//   - The test plays several short chirps (a sweep is easy to find again: it correlates with itself in
//     one sharp peak) at known timeline positions.
//   - The take is placed as usual (Step 2), so a chirp that the driver's latencies describe exactly
//     comes back at its own position. For each chirp, cross-correlate the recording with the chirp at
//     every lag in [-early, +late] and take the lag with the largest |correlation| (|..|: a microphone
//     may invert the polarity).
//   - A chirp is HEARD when that peak is clearly above the rest: peak > 8 x the median |correlation|.
//   - The answer is the median lag of the heard chirps: the trim to set.
//   - And a verdict: trust it only if at least 3/4 of the chirps were heard and their lags agree within
//     1 ms. A laptop's microphone array, with the system's voice processing, gave lags 10-90 ms apart
//     within one take (Step 7): a trim cannot fix a latency that moves, and the user must be told.
//
// Here the recording is already on the timeline: recorded[p] is what the take placed at position p.

#include <cmath>
#include <cstdint>
#include <vector>

namespace ex4 {

struct LatencyMeasurement {
    std::vector<int> lags;      // per chirp, the lag with the largest |correlation|
    std::vector<bool> heard;    // per chirp: peak > 8 x median
    int heardCount = 0;
    int lag = 0;                // the median of the heard lags (0 if none): the trim to set
    int spread = 0;             // largest minus smallest heard lag
    bool trustworthy = false;   // heardCount >= 3/4 of the chirps and spread <= sampleRate / 1000
};

// ---- provided -----------------------------------------------------------------------------------
// A 10 ms sweep from 1 to 6 kHz under a Hann window, peak `gain`: the same chirp as the device test's.
inline std::vector<float> makeChirp(double sampleRate, float gain = 0.5f) {
    const double pi = 3.14159265358979323846;
    const int n = static_cast<int>(std::lround(0.010 * sampleRate));
    std::vector<float> chirp(static_cast<size_t>(n));
    const double f0 = 1000.0, f1 = 6000.0, length = n / sampleRate;
    for (int i = 0; i < n; ++i) {
        const double t = i / sampleRate;
        const double window = 0.5 - 0.5 * std::cos(2.0 * pi * i / (n - 1));
        chirp[static_cast<size_t>(i)] = static_cast<float>(gain * window * std::sin(2.0 * pi * (f0 * t + (f1 - f0) * t * t / (2.0 * length))));
    }
    return chirp;
}

// ---- YOUR TURN (starter/ex4_measure_latency.cpp) -------------------------------------------------
// `recorded`: the take on the timeline (index = position). `chirpAt`: where each chirp was played.
// Lags from -early to +late samples are tried; positions outside `recorded` count as 0.
LatencyMeasurement measureLatency(const std::vector<float>& recorded, const std::vector<std::int64_t>& chirpAt,
                                  const std::vector<float>& chirp, int early, int late, double sampleRate);

} // namespace ex4
