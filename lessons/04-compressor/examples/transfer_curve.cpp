// Example 1: print the compressor's static transfer curve (hard knee vs soft knee).
//
// What it demonstrates:
//   - The input -> output relationship of Compressor::computeOutputDb(), the "gain computer", for different
//     threshold / ratio / knee settings
//   - How the quadratic section of the knee rounds off the "corner" of the hard knee, and how it joins the two
//     straight lines smoothly at both ends of the knee
//   - Numbers that match the README formulas: e.g. T = -20 dB, R = 4, input -10 dB -> output -17.5 dB, GR = 7.5 dB
//   - For comparison: what the curves of an expander / gate look like (implemented fully in chapter 08)
// How to run: no arguments needed, just run l04_transfer_curve.
// Output files: none (numbers / ASCII plot only).
// What to look for: the soft knee's GR starts "early and gradually" around the threshold, while the hard knee's
//                   GR is exactly 0 all the way up to the threshold.

#include <dawlab/Common.h>
#include <dawlab/dsp/Compressor.h>

#include <cstdio>
#include <string>

using dawlab::dsp::Compressor;

namespace {

// For demonstration only: the static curve of a downward expander (below the threshold, every 1 dB becomes R dB)
float expanderOutputDb(float x, float thresholdDb, float ratio) {
    return x >= thresholdDb ? x : thresholdDb + (x - thresholdDb) * ratio;
}

void printTable(float thresholdDb, float ratio, float hardKneeDb, float softKneeDb) {
    std::printf("\n=== Transfer curve: threshold %.0f dB, ratio %.0f:1 ===\n", thresholdDb, ratio);
    std::printf("  knee = %.0f dB (hard)   vs   knee = %.0f dB (soft)\n", hardKneeDb, softKneeDb);
    std::printf("  In dB   |  out(hard)   GR(hard)  |  out(soft)   GR(soft)\n");
    std::printf("  --------+-----------------------+----------------------\n");
    for (float x = -60.0f; x <= 0.0f + 1e-3f; x += 3.0f) {
        const float yh = Compressor::computeOutputDb(x, thresholdDb, ratio, hardKneeDb);
        const float ys = Compressor::computeOutputDb(x, thresholdDb, ratio, softKneeDb);
        std::printf("  %7.1f | %9.2f   %7.2f   | %9.2f   %7.2f\n", x, yh, x - yh, ys, x - ys);
    }
}

// ASCII plot: input dB on the horizontal axis (-60..0), output dB on the vertical axis (-60..0), 2 dB per cell
void plotCurves(float thresholdDb, float ratio, float softKneeDb) {
    constexpr int kCols = 31; // -60..0, 2 dB per cell
    constexpr int kRows = 31;
    std::printf("\n  ASCII transfer curve (. = 1:1 reference line, # = hard knee, o = soft knee %.0f dB, * = overlap)\n",
                softKneeDb);
    std::printf("  Vertical axis: output dB (top = 0 dB); horizontal axis: input dB (left = -60, right = 0)\n");
    std::string grid[kRows];
    for (auto& row : grid)
        row.assign(kCols, ' ');
    auto plot = [&](float x, float y, char c) {
        const int col = static_cast<int>(std::lround((x + 60.0f) / 2.0f));
        const int row = static_cast<int>(std::lround(-y / 2.0f)); // 0 dB is at the top
        if (col < 0 || col >= kCols || row < 0 || row >= kRows)
            return;
        char& cell = grid[row][static_cast<size_t>(col)];
        if (cell == ' ' || cell == '.')
            cell = c;
        else if (cell != c)
            cell = '*';
    };
    for (int col = 0; col < kCols; ++col) {
        const float x = -60.0f + 2.0f * static_cast<float>(col);
        plot(x, x, '.');
    }
    for (float x = -60.0f; x <= 0.0f; x += 0.5f) {
        plot(x, Compressor::computeOutputDb(x, thresholdDb, ratio, 0.0f), '#');
        plot(x, Compressor::computeOutputDb(x, thresholdDb, ratio, softKneeDb), 'o');
    }
    for (int row = 0; row < kRows; ++row)
        std::printf("  %4d |%s|\n", -2 * row, grid[row].c_str());
    std::printf("       +-------------------------------+\n");
    std::printf("       -60           -30            0  (input dB)\n");
}

} // namespace

int main() {
    std::printf("Chapter 04, example 1: compressor transfer curve\n");

    // Main setting: T = -20 dB, R = 4, knee 0 vs 10 dB
    printTable(-20.0f, 4.0f, 0.0f, 10.0f);

    // Hand-calculated check points from the README
    std::printf("\n=== Hand-calculated check points (T = -20, R = 4) ===\n");
    const float probes[] = {-30.0f, -25.0f, -22.0f, -20.0f, -18.0f, -15.0f, -10.0f, 0.0f};
    std::printf("  In dB   | hard knee out | soft knee (10 dB) out | difference\n");
    for (float x : probes) {
        const float yh = Compressor::computeOutputDb(x, -20.0f, 4.0f, 0.0f);
        const float ys = Compressor::computeOutputDb(x, -20.0f, 4.0f, 10.0f);
        std::printf("  %7.1f | %13.3f | %21.3f | %6.3f\n", x, yh, ys, ys - yh);
    }
    std::printf("  Soft knee output at the threshold itself (-20 dB) = T + (1/R - 1)·W/8 = -20 + (-0.75)(10/8) = -20.9375\n");

    // Ratio comparison: 2:1, 4:1, 10:1, inf:1 (limiter)
    std::printf("\n=== Ratio comparison (T = -20 dB, hard knee, input 0 dB) ===\n");
    for (float r : {1.0f, 2.0f, 4.0f, 10.0f, 20.0f, 1000.0f}) {
        const float y = Compressor::computeOutputDb(0.0f, -20.0f, r, 0.0f);
        std::printf("  ratio %6.0f:1 → output %7.2f dB, GR %5.2f dB\n", r, y,
                    Compressor::computeGainReductionDb(0.0f, -20.0f, r, 0.0f)); // use the GR function to avoid printing -0.00
    }

    plotCurves(-20.0f, 4.0f, 10.0f);

    // For comparison: expander / gate (chapter 08)
    std::printf("\n=== For comparison: downward expander (T = -40 dB, ratio 2 => every 1 dB lower attenuates 1 dB more) and gate (very large ratio) ===\n");
    std::printf("  In dB   | compressor(T=-20,R=4) | expander(T=-40,R=2) | gate(T=-40,R=20)\n");
    for (float x = -60.0f; x <= 0.0f + 1e-3f; x += 10.0f) {
        std::printf("  %7.1f | %21.2f | %19.2f | %16.2f\n", x, Compressor::computeOutputDb(x, -20.0f, 4.0f, 0.0f),
                    expanderOutputDb(x, -40.0f, 2.0f), std::max(-100.0f, expanderOutputDb(x, -40.0f, 20.0f)));
    }
    std::printf("  The compressor squashes what is above the threshold; the expander / gate push what is below it lower.\n");
    return 0;
}
