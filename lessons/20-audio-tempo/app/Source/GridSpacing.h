#pragma once
// GridSpacing: which bar lines the arrangement draws (chapter 14 on). No JUCE, no engine: the test
// tests/test_grid_spacing.cpp runs in every build.
//
//   barGrid(firstBar, lastBar, widthPx, minPx)
//       The bars of the visible range [firstBar, lastBar] (0-based, fractions allowed) that get a line.
//
// Zoomed out as far as the view goes, thousands of bars share a few hundred pixels.
// For now every bar is a line.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace dawlab::app::grid {

// The bars that get a line: first, first + step, ... (count of them).
struct BarGrid {
    int64_t first = 0; // 0-based bar number of the first line
    int64_t step = 1;  // bars from one line to the next
    int64_t count = 0; // number of lines; 0 = nothing to draw
};

inline BarGrid barGrid(double firstBar, double lastBar, double widthPx, double minPx) {
    (void)widthPx;
    (void)minPx;
    BarGrid grid;
    if (!std::isfinite(firstBar) || !std::isfinite(lastBar) || lastBar <= firstBar)
        return grid;
    grid.first = static_cast<int64_t>(std::ceil(std::max(0.0, firstBar)));
    const auto last = static_cast<int64_t>(std::floor(lastBar));
    grid.count = last >= grid.first ? last - grid.first + 1 : 0;
    return grid;
}

} // namespace dawlab::app::grid
