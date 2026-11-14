#pragma once
// GridSpacing: which bar lines the arrangement draws (chapter 14 on). No JUCE, no engine: the test
// tests/test_grid_spacing.cpp runs in every build.
//
//   barGrid(firstBar, lastBar, widthPx, minPx)
//       The bars of the visible range [firstBar, lastBar] (0-based, fractions allowed) that get a line
//       when the range is widthPx pixels wide: every step-th bar, where step is the smallest power of 2
//       that puts the lines at least minPx apart.
//
// Zoomed out as far as the view goes (1e6 samples per pixel), a song at 999 BPM in 1/4 puts some 440 000
// bars into 1400 pixels. Walking every bar to find the few that can be drawn cost more than a 30 Hz frame
// (review A8). Here only the lines that are drawn are counted, so the work is at most widthPx / minPx + 1
// lines, at any zoom:
//
//     pixels per bar  >= 8   4    2    1    0.5 ...
//     step (minPx 8)     1   2    4    8    16  ...      bar 1, 3, 5 ... ; bar 1, 5, 9 ... ; ...
//
// The lines sit on multiples of the step (counted from bar 1), not on the first bar in view, so they
// stay where they are while the view scrolls. Steps of a larger minPx are multiples of the steps of a
// smaller one: the ruler's numbers (minPx 30) fall on its bar lines (minPx 8).
//
// With a tempo map (chapter 19 on) bars are not all the same width; the spacing is then the average
// over the view, and lines in a fast part may be closer than minPx. The count stays within the bound.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace dawlab::app::grid {

// The bars that get a line: first, first + step, ... (count of them).
struct BarGrid {
    int64_t first = 0; // 0-based bar number of the first line, a multiple of step
    int64_t step = 1;  // bars from one line to the next, a power of 2
    int64_t count = 0; // number of lines; 0 = nothing to draw
    double pxPerBar = 0.0; // average pixels from one bar to the next (0 when the input is refused)
};

// Far beyond any song (2^40 bars is 70 000 years at 999 BPM): a range past it is refused, not walked.
inline constexpr double kMaxBar = 1099511627776.0;
inline constexpr int64_t kMaxStep = int64_t{1} << 40; // one line for the whole of that range

inline BarGrid barGrid(double firstBar, double lastBar, double widthPx, double minPx) {
    BarGrid grid;
    const bool usable = std::isfinite(firstBar) && std::isfinite(lastBar) && std::isfinite(widthPx) &&
                        std::isfinite(minPx) && widthPx > 0.0 && minPx > 0.0 && lastBar > firstBar &&
                        lastBar <= kMaxBar;
    if (!usable)
        return grid;
    grid.pxPerBar = widthPx / (lastBar - firstBar);
    const double barsPerLine = minPx / grid.pxPerBar; // bars that fit into minPx pixels
    while (static_cast<double>(grid.step) < barsPerLine && grid.step < kMaxStep)
        grid.step *= 2;
    grid.first = static_cast<int64_t>(std::ceil(std::max(0.0, firstBar) / static_cast<double>(grid.step))) * grid.step;
    const int64_t last = static_cast<int64_t>(std::floor(lastBar / static_cast<double>(grid.step))) * grid.step;
    grid.count = last >= grid.first ? (last - grid.first) / grid.step + 1 : 0;
    return grid;
}

} // namespace dawlab::app::grid
