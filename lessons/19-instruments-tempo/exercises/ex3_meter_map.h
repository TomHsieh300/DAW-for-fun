#pragma once
// Exercise 3 - A meter map.
//
// Chapter 19 gave the tempo a map and left the time signature alone: one meter for the whole song. Songs change
// it - a bar of 2/4 before the chorus, a middle part in 6/8 - and then "bar 17, beat 3" is no longer a division.
// A METER MAP is the tempo map's quieter sibling: a list of changes, each a BAR and a time signature that holds
// from that bar on. It has nothing to do with time in seconds - only with ticks:
//
//   a beat    is the note the denominator names: 960 x 4 / denominator ticks (a quarter 960, an eighth 480)
//   a bar     is numerator beats
//   a change  starts at a bar line (bar numbers from 0 here; a display adds 1), never inside a bar
//
//   tickOfBar(bar)    the tick where a bar starts: the bars before it, each as long as its own meter says
//   positionOf(tick)  the bar the tick is in, the beat in that bar and the ticks into that beat (all from 0)
//   tickOf(position)  the inverse
//
// All of it in whole numbers: there is nothing to round. prepare() computes where each change starts in ticks,
// so that both directions are a binary search and a division - a ruler asks for every bar line it draws.
// Ticks before 0 belong to the first meter (a count-in): bar -1 ends at tick 0.

#include <algorithm>
#include <cstdint>
#include <vector>

namespace ex3 {

constexpr int kTicksPerQuarter = 960;

struct MeterChange {
    std::int64_t bar = 0; // the first bar it holds for
    int numerator = 4;
    int denominator = 4;  // 1, 2, 4, 8, 16 or 32
};

struct Position {
    std::int64_t bar = 0;
    int beat = 0;
    int tick = 0; // into the beat
    friend bool operator==(const Position& a, const Position& b) noexcept { return a.bar == b.bar && a.beat == b.beat && a.tick == b.tick; }
};

class MeterMap {
public:
    // ---- provided -----------------------------------------------------------------------------------------
    // Sorted by bar; false (the map stays as it was) unless the first change is at bar 0, no two share a bar,
    // every numerator is 1 .. 32 and every denominator a power of two up to 32. Then prepare().
    bool set(std::vector<MeterChange> changes) {
        std::sort(changes.begin(), changes.end(), [](const MeterChange& a, const MeterChange& b) { return a.bar < b.bar; });
        if (changes.empty() || changes.front().bar != 0)
            return false;
        for (size_t i = 0; i < changes.size(); ++i) {
            const MeterChange& c = changes[i];
            const bool power = c.denominator >= 1 && c.denominator <= 32 && (c.denominator & (c.denominator - 1)) == 0;
            if (c.numerator < 1 || c.numerator > 32 || !power || (i > 0 && c.bar == changes[i - 1].bar))
                return false;
        }
        changes_ = std::move(changes);
        prepare();
        return true;
    }
    const std::vector<MeterChange>& changes() const noexcept { return changes_; }
    const std::vector<std::int64_t>& starts() const noexcept { return starts_; }
    static std::int64_t ticksPerBeat(const MeterChange& c) noexcept { return kTicksPerQuarter * 4 / c.denominator; }
    static std::int64_t ticksPerBar(const MeterChange& c) noexcept { return ticksPerBeat(c) * c.numerator; }

    // ---- YOUR TURN (starter/ex3_meter_map.cpp) -------------------------------------------------------------
    std::int64_t tickOfBar(std::int64_t bar) const noexcept;
    Position positionOf(std::int64_t tick) const noexcept;
    std::int64_t tickOf(const Position& position) const noexcept;

private:
    // Fills starts_: the tick where each change starts.
    void prepare();

    std::vector<MeterChange> changes_{MeterChange{}};
    std::vector<std::int64_t> starts_{0};
};

} // namespace ex3
