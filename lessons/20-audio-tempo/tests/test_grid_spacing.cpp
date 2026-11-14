// test_grid_spacing: which bar lines the arrangement draws (app/Source/GridSpacing.h). No JUCE.
//
//   1. Zoomed fully out (1e6 samples per pixel) at 999 BPM 1/4 and at 120 BPM 4/4: a few hundred lines,
//      at least minPx apart, not one per bar.
//   2. Zoomed to fit a short song: every bar.
//   3. Every zoom from 4 to 1e6 samples per pixel, scrolled anywhere: the same rules, and the step only
//      grows as the view zooms out.
//   4. Nothing to draw (no width, an empty or backwards range, NaN, Inf): no lines, and no hang.
//   5. The bar numbers (minPx 30) fall on bar lines (minPx 8).

#include "GridSpacing.h"

#include <dawlab/Check.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

using dawlab::app::grid::BarGrid;
using dawlab::app::grid::barGrid;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr double kWidth = 1400.0; // pixels of timeline (the window minus the track headers)

double samplesPerBar(double bpm, int beatsPerBar) { return kSampleRate * 60.0 / bpm * beatsPerBar; }

bool isPowerOfTwo(int64_t n) { return n > 0 && (n & (n - 1)) == 0; }

// Every rule of the grid for one view; returns false (and says why) at the first broken one.
bool followsTheRules(const BarGrid& g, double firstBar, double lastBar, double widthPx, double minPx) {
    const double pxPerBar = widthPx / (lastBar - firstBar);
    const int64_t lastDrawn = g.first + (g.count - 1) * g.step;
    const char* broken = nullptr;
    if (g.count <= 0) {
        // Zoomed in inside one bar: no bar starts in view, so no line is right.
        if (g.step != 1 || std::ceil(std::max(0.0, firstBar)) <= lastBar)
            broken = "no line, though a bar starts in view";
    } else if (static_cast<double>(g.count) > std::floor(widthPx / minPx) + 1.0)
        broken = "more lines than width / minPx + 1";
    else if (!isPowerOfTwo(g.step))
        broken = "the step is not a power of 2";
    else if (g.step > 1 && static_cast<double>(g.step) * pxPerBar < minPx)
        broken = "lines closer than minPx";
    else if (g.step > 1 && static_cast<double>(g.step / 2) * pxPerBar >= minPx)
        broken = "a smaller step would do";
    else if (g.first % g.step != 0)
        broken = "the first line is not on a multiple of the step (lines would move while scrolling)";
    else if (static_cast<double>(g.first) < firstBar || static_cast<double>(g.first - g.step) >= firstBar)
        broken = "the first line is not the first multiple of the step in view";
    else if (static_cast<double>(lastDrawn) > lastBar || static_cast<double>(lastDrawn + g.step) <= lastBar)
        broken = "the last line is not the last multiple of the step in view";
    if (broken != nullptr)
        std::printf("  bars %.3f..%.3f over %.0f px, minPx %.0f: first %lld step %lld count %lld: %s\n", firstBar, lastBar,
                    widthPx, minPx, static_cast<long long>(g.first), static_cast<long long>(g.step),
                    static_cast<long long>(g.count), broken);
    return broken == nullptr;
}

void testZoomedOut() {
    std::printf("1. zoomed fully out\n");
    for (const auto& song : {std::pair<double, int>{999.0, 1}, std::pair<double, int>{120.0, 4}}) {
        const double bars = kWidth * 1.0e6 / samplesPerBar(song.first, song.second);
        const BarGrid g = barGrid(0.0, bars, kWidth, 8.0);
        std::printf("   %3.0f BPM %d/4: %.0f bars in view -> %lld lines, every %lld bars\n", song.first, song.second, bars,
                    static_cast<long long>(g.count), static_cast<long long>(g.step));
        DAWLAB_CHECK(g.count <= static_cast<int64_t>(kWidth / 8.0) + 1);
        DAWLAB_CHECK(followsTheRules(g, 0.0, bars, kWidth, 8.0));
    }
}

void testZoomToFit() {
    std::printf("2. zoomed to fit a short song\n");
    const BarGrid g = barGrid(0.0, 8.0, 1268.0, 8.0); // 8 bars over 1268 px: 158 px per bar
    DAWLAB_CHECK(g.first == 0 && g.step == 1 && g.count == 9);
    DAWLAB_CHECK_NEAR(g.pxPerBar, 1268.0 / 8.0, 1e-9); // the ruler's beat ticks ask for it
    DAWLAB_CHECK(followsTheRules(g, 0.0, 8.0, 1268.0, 8.0));
    // Bars exactly minPx apart are all drawn.
    const BarGrid tight = barGrid(0.0, 100.0, 800.0, 8.0);
    DAWLAB_CHECK(tight.step == 1 && tight.count == 101);
}

void testEveryZoom() {
    std::printf("3. every zoom, scrolled anywhere\n");
    int views = 0;
    bool allFollow = true;
    bool stepNeverShrinks = true;
    for (const double barSamples : {samplesPerBar(999.0, 1), samplesPerBar(120.0, 4), samplesPerBar(20.0, 32)}) {
        int64_t lastStep = 1;
        for (double spp = 4.0; spp <= 1.0e6; spp *= 1.07) {
            int64_t stepHere = 0;
            for (const double viewStart : {0.0, 12345.0, 3.7e6, 9.1e8, 1.0e10}) {
                const double firstBar = viewStart / barSamples;
                const double lastBar = (viewStart + kWidth * spp) / barSamples;
                for (const double minPx : {8.0, 30.0}) {
                    const BarGrid g = barGrid(firstBar, lastBar, kWidth, minPx);
                    allFollow = allFollow && followsTheRules(g, firstBar, lastBar, kWidth, minPx);
                    if (minPx == 8.0)
                        stepHere = g.step;
                    ++views;
                }
            }
            stepNeverShrinks = stepNeverShrinks && stepHere >= lastStep;
            lastStep = stepHere;
        }
    }
    std::printf("   %d views\n", views);
    DAWLAB_CHECK(allFollow);
    DAWLAB_CHECK(stepNeverShrinks);
}

void testNothingToDraw() {
    std::printf("4. nothing to draw\n");
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    DAWLAB_CHECK(barGrid(0.0, 10.0, 0.0, 8.0).count == 0);
    DAWLAB_CHECK(barGrid(0.0, 10.0, -5.0, 8.0).count == 0);
    DAWLAB_CHECK(barGrid(5.0, 5.0, 1400.0, 8.0).count == 0);
    DAWLAB_CHECK(barGrid(6.0, 5.0, 1400.0, 8.0).count == 0);
    DAWLAB_CHECK(barGrid(nan, 10.0, 1400.0, 8.0).count == 0);
    DAWLAB_CHECK(barGrid(0.0, nan, 1400.0, 8.0).count == 0);
    DAWLAB_CHECK(barGrid(0.0, inf, 1400.0, 8.0).count == 0);
    DAWLAB_CHECK(barGrid(-inf, 10.0, 1400.0, 8.0).count == 0);
    DAWLAB_CHECK(barGrid(0.0, 10.0, nan, 8.0).count == 0);
    DAWLAB_CHECK(barGrid(0.0, 10.0, inf, 8.0).count == 0);
    DAWLAB_CHECK(barGrid(0.0, 10.0, 1400.0, nan).count == 0);
    DAWLAB_CHECK(barGrid(0.0, 10.0, 1400.0, 0.0).count == 0);
    DAWLAB_CHECK(barGrid(0.0, 1.0e300, 1400.0, 8.0).count == 0); // beyond any song: refused, not looped over
    DAWLAB_CHECK(barGrid(0.0, 10.0, 1.0e-300, 8.0).count == 1);  // one line, not a step that overflows
    // A range before bar 0 starts at bar 0.
    const BarGrid before = barGrid(-3.5, 4.0, 1400.0, 8.0);
    DAWLAB_CHECK(before.first == 0 && before.count == 5);
}

void testNumbersOnLines() {
    std::printf("5. bar numbers fall on bar lines\n");
    bool onLines = true;
    for (double bars = 1.0; bars < 1.0e6; bars *= 1.3) {
        const BarGrid lines = barGrid(0.0, bars, kWidth, 8.0);
        const BarGrid numbers = barGrid(0.0, bars, kWidth, 30.0);
        onLines = onLines && numbers.step % lines.step == 0 && numbers.first % lines.step == 0;
    }
    DAWLAB_CHECK(onLines);
}

} // namespace

int main() {
    std::printf("test_grid_spacing: which bar lines the arrangement draws\n");
    testZoomedOut();
    testZoomToFit();
    testEveryZoom();
    testNothingToDraw();
    testNumbersOnLines();
    return dawlab::test::summary("test_grid_spacing");
}
