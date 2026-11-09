#pragma once
// Exercise 8 - A "Measure Latency" button (optional, JUCE).
//
// DawLab 17 asks the user for the latency trim as a number (Transport > Recording Latency Trim...): "record
// a click through a cable, the distance from the click to its recording is this number". Nobody does that.
// The device test (Step 7, --device-test latency) measures it with chirps through the speaker into the
// microphone, and Exercise 4 is its core. This exercise makes it a button, without changing the app:
//
//   LatencyMeter::measure()     plays the probe (chirps), finds the round trip in what came back, and
//                               sets the recorder's trim - if the measurement can be trusted. Says what
//                               it found on the status line, and returns it.
//   LatencyMeter::revert()      puts back the trim from before the last measurement that set one.
//   LatencyMeter::addToMenu()   "Measure Latency..." and "Undo Latency Measurement", greyed out while a
//                               recording runs (a measurement needs the device to itself).
//
// The trim is what the driver does not report:
//
//     trim = round trip - reported input latency - reported output latency
//
// The mix's latency is not in it: the probe is played straight to the device, not through the mix, and the
// recorder adds the mix's latency itself (Step 2).
//
// How the probe reaches the device is not this exercise: the meter is given a function that plays a signal
// and returns what the input recorded meanwhile. In the app, that is the device test's code; in the check,
// a simulated room.

#include "MainComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

namespace ex8 {

// ---- provided: the probe ----------------------------------------------------------------------------
// kChirps chirps of 10 ms (Exercise 4's makeChirp), one every kSpacingSeconds after as much silence, and
// as much silence after the last.
struct Probe {
    static constexpr int kChirps = 8;
    static constexpr double kSpacingSeconds = 0.5;
    std::vector<float> chirp;
    std::vector<std::int64_t> chirpAt; // where each chirp starts in `signal`
    std::vector<float> signal;
};

inline Probe makeProbe(double sampleRate) {
    const double pi = 3.14159265358979323846;
    Probe probe;
    const int n = static_cast<int>(std::lround(0.010 * sampleRate));
    const double f0 = 1000.0, f1 = 6000.0, length = n / sampleRate;
    for (int i = 0; i < n; ++i) {
        const double t = i / sampleRate;
        const double window = 0.5 - 0.5 * std::cos(2.0 * pi * i / (n - 1));
        probe.chirp.push_back(static_cast<float>(0.5 * window * std::sin(2.0 * pi * (f0 * t + (f1 - f0) * t * t / (2.0 * length)))));
    }
    const auto spacing = static_cast<std::int64_t>(std::lround(Probe::kSpacingSeconds * sampleRate));
    probe.signal.assign(static_cast<size_t>(spacing * (Probe::kChirps + 1)), 0.0f);
    for (int c = 0; c < Probe::kChirps; ++c) {
        probe.chirpAt.push_back(spacing * (c + 1));
        std::copy(probe.chirp.begin(), probe.chirp.end(), probe.signal.begin() + static_cast<std::ptrdiff_t>(spacing * (c + 1)));
    }
    return probe;
}

// ---- provided: Exercise 4's per-chirp search --------------------------------------------------------
// For each chirp: the lag in [0, maxLag] where |correlation| of `recorded` with the chirp is largest, and
// whether it was heard (that peak > 8 x the median |correlation|). Positions outside `recorded` count as 0.
struct ChirpLag {
    int lag = 0;
    bool heard = false;
};

inline std::vector<ChirpLag> chirpLags(const std::vector<float>& recorded, const Probe& probe, int maxLag) {
    const auto size = static_cast<std::int64_t>(recorded.size());
    auto at = [&recorded, size](std::int64_t p) { return p >= 0 && p < size ? recorded[static_cast<size_t>(p)] : 0.0f; };
    std::vector<ChirpLag> lags;
    for (const std::int64_t c : probe.chirpAt) {
        std::vector<double> r;
        for (int d = 0; d <= maxLag; ++d) {
            double acc = 0.0;
            for (size_t k = 0; k < probe.chirp.size(); ++k)
                acc += static_cast<double>(probe.chirp[k]) * at(c + d + static_cast<std::int64_t>(k));
            r.push_back(std::abs(acc));
        }
        const auto best = std::max_element(r.begin(), r.end());
        std::vector<double> sorted = r;
        std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(sorted.size() / 2), sorted.end());
        lags.push_back({static_cast<int>(best - r.begin()), *best > 8.0 * sorted[sorted.size() / 2]});
    }
    return lags;
}

// ---- provided: the device, as the meter sees it -----------------------------------------------------
// input[i] was recorded in the same audio callback in which signal[i] was played; `reported` is what the
// driver says about its latencies (mix and trim are 0).
struct RoundTrip {
    std::vector<float> input;
    dawlab::engine::Latencies reported;
};
using PlayAndRecord = std::function<RoundTrip(const std::vector<float>& signal)>;

struct Result {
    bool applied = false; // the trim was set
    int heard = 0;        // chirps heard, of Probe::kChirps
    int roundTrip = 0;    // the median of the heard lags: the upper middle one of the sorted lags
    int spread = 0;       // largest minus smallest heard lag
    int trim = 0;         // roundTrip - reported input - reported output
    juce::String message; // also on the status line
};

class LatencyMeter {
public:
    // The lags searched: 0 .. kMaxLagSeconds. More than any device's round trip, less than the spacing.
    static constexpr double kMaxLagSeconds = 0.4;

    LatencyMeter(dawlab::app::MainComponent& main, PlayAndRecord device) : main_(main), device_(std::move(device)) {}

    // ---- YOUR TURN (starter/ex8_latency_button.cpp) ---------------------------------------------------
    // While the recorder is busy: nothing is played, the message contains "recording", nothing is applied.
    // Otherwise the probe at the session's sample rate is played, and:
    //   no chirp heard                    message contains "none" (is the input muted?)
    //   fewer than 3/4 of them heard      message contains "only <heard> of 8"; the trim stays
    //   spread > sampleRate / 1000        message contains "moves" and the spread in ms (one decimal); the
    //                                     trim stays: a trim cannot fix a latency that moves
    //   else                              the trim is set (remember the one before, for revert()); the
    //                                     message contains "trim" and the trim in samples
    // The message goes to the status line (MainComponent::setStatus) as well.
    Result measure();
    // The trim from before the last measurement that set one, once. False if there is none, or while the
    // recorder is busy.
    bool revert();
    // Two items: "Measure Latency..." (measure()) and "Undo Latency Measurement" (revert(); enabled only
    // when there is something to revert). Both disabled while the recorder is busy.
    void addToMenu(juce::PopupMenu& menu);

private:
    dawlab::app::MainComponent& main_;
    PlayAndRecord device_;
    int previousTrim_ = 0;
    bool canRevert_ = false;
};

} // namespace ex8
