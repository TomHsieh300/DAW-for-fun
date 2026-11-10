#pragma once
// Exercise 4 - A tempo map.
//
// DawLab 18 has one tempo, and Step 1's sampleOfTick() is one multiplication. Real songs slow down at the
// end, speed up into a chorus, or follow a live drummer: a TEMPO MAP is a list of changes, each a tick and a
// tempo that holds from there until the next change. Notes stay in ticks, so they follow every change; only
// the conversion from ticks to samples changes - and its inverse, which the audio thread needs for every
// block's edges (Step 1).
//
//   sampleOfTick(t)        the change at or before t (the first one for t < 0): where that change starts in
//                          samples, plus (t - its tick) x its samplesPerTick, rounded. Where a change starts
//                          is the EXACT sum of the segments before it, kept unrounded: rounding every segment
//                          to a whole sample would put an error of up to half a sample into every change, and
//                          a thousand changes later the notes would be late.
//   firstTickAtOrAfter(s)  the smallest tick t with sampleOfTick(t) >= s, as in Step 1: the ticks of a block
//                          [s0, s1) are exactly [firstTickAtOrAfter(s0), firstTickAtOrAfter(s1)).
//
// Both are called on the audio thread: no allocation, and a binary search for the change, not a walk from the
// first one.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ex4 {

constexpr int kTicksPerQuarter = 960;

struct TempoChange {
    std::int64_t tick = 0;
    double bpm = 120.0;
};

class TempoMap {
public:
    // ---- provided ---------------------------------------------------------------------------------------
    // Sorted by tick; false (and the map stays as it was) unless the first change is at tick 0, every bpm is
    // in (0, 1000] and no two changes share a tick. Then prepare().
    bool set(double sampleRate, std::vector<TempoChange> changes) {
        std::sort(changes.begin(), changes.end(), [](const TempoChange& a, const TempoChange& b) { return a.tick < b.tick; });
        if (sampleRate <= 0.0 || changes.empty() || changes.front().tick != 0)
            return false;
        for (size_t i = 0; i < changes.size(); ++i)
            if (!(changes[i].bpm > 0.0 && changes[i].bpm <= 1000.0) || (i > 0 && changes[i].tick == changes[i - 1].tick))
                return false;
        sampleRate_ = sampleRate;
        changes_ = std::move(changes);
        prepare();
        return true;
    }
    double samplesPerTick(size_t change) const noexcept {
        return sampleRate_ * 60.0 / (changes_[change].bpm * kTicksPerQuarter);
    }
    const std::vector<TempoChange>& changes() const noexcept { return changes_; }
    // Where each change starts, in samples, unrounded (filled by prepare()).
    const std::vector<double>& starts() const noexcept { return starts_; }

    // ---- YOUR TURN (starter/ex4_tempo_map.cpp) ---------------------------------------------------------------
    std::int64_t sampleOfTick(std::int64_t tick) const noexcept;
    std::int64_t firstTickAtOrAfter(std::int64_t sample) const noexcept;

private:
    // Fills starts_: 0 for the first change, then each one where the one before it ends.
    void prepare();

    double sampleRate_ = 48000.0;
    std::vector<TempoChange> changes_{TempoChange{}};
    std::vector<double> starts_{0.0};
};

} // namespace ex4
