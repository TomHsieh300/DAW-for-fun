// Example 1: plot the EQ curve (theory) and compare it with measurement
//
// What it demonstrates:
//   - Uses ParametricEq::getMagnitudeDb() to compute the whole EQ curve and prints it as an ASCII plot (log frequency on the horizontal axis).
//   - Uses measureGainDb() to actually run sine waves through the EQ and measure the gain, overlaid on the same plot ('o'), showing that what is "drawn" matches what is "computed".
//   - Lists each band's dB contribution at several frequencies, plus the sum, to verify that cascaded dB values add.
//   - Low-shelf Q (slope S) and overshoot: Q = 0.707 (S = 1) is monotonic; a larger Q produces overshoot and a dip on either side of the transition.
// How to run: l03_eq_curve (no arguments)
// Output: console text only; no WAV files.
// What to look for: the theoretical curve '#' and the measured points 'o' nearly coincide; in the table, "theory - measured" is below 0.05 dB.
//   At the end DAWLAB_CHECK self-checks these conclusions (prints [PASS] if all hold; otherwise returns non-zero and ctest reports a failure).

#include <dawlab/Common.h>
#include <dawlab/dsp/ParametricEq.h>

#include <cstdio>
#include <string>
#include <vector>

using namespace dawlab;
using namespace dawlab::dsp;

namespace {

const char* typeName(EqBandType t) {
    switch (t) {
    case EqBandType::LowCut: return "LowCut";
    case EqBandType::LowShelf: return "LowShelf";
    case EqBandType::Peak: return "Peak";
    case EqBandType::HighShelf: return "HighShelf";
    case EqBandType::HighCut: return "HighCut";
    case EqBandType::Notch: return "Notch";
    }
    return "?";
}

// On a log axis over [fMin, fMax]: width columns horizontally, and one row per dbStep vertically from dbTop to dbBottom
void plotAscii(const ParametricEq& eq, const std::vector<double>& measuredFreqs, const std::vector<double>& measuredDb,
               double fMin, double fMax, int width, double dbTop, double dbBottom, double dbStep) {
    const int rows = static_cast<int>(std::lround((dbTop - dbBottom) / dbStep)) + 1;
    std::vector<std::string> grid(static_cast<size_t>(rows), std::string(static_cast<size_t>(width), ' '));
    auto rowOf = [&](double db) {
        const double clamped = std::clamp(db, dbBottom, dbTop);
        return static_cast<int>(std::lround((dbTop - clamped) / dbStep));
    };
    auto colOf = [&](double f) {
        const double t = std::log(f / fMin) / std::log(fMax / fMin);
        return std::clamp(static_cast<int>(std::lround(t * (width - 1))), 0, width - 1);
    };
    // 0 dB reference line
    const int zeroRow = rowOf(0.0);
    for (int c = 0; c < width; ++c)
        grid[static_cast<size_t>(zeroRow)][static_cast<size_t>(c)] = '-';
    // Theoretical curve
    for (int c = 0; c < width; ++c) {
        const double f = fMin * std::pow(fMax / fMin, static_cast<double>(c) / (width - 1));
        grid[static_cast<size_t>(rowOf(eq.getMagnitudeDb(f)))][static_cast<size_t>(c)] = '#';
    }
    // Measured points
    for (size_t i = 0; i < measuredFreqs.size(); ++i)
        grid[static_cast<size_t>(rowOf(measuredDb[i]))][static_cast<size_t>(colOf(measuredFreqs[i]))] = 'o';

    for (int r = 0; r < rows; ++r)
        std::printf("%+6.0f dB |%s|\n", dbTop - r * dbStep, grid[static_cast<size_t>(r)].c_str());
    std::printf("          +%s+\n", std::string(static_cast<size_t>(width), '-').c_str());
    // Frequency ticks
    std::string ticks(static_cast<size_t>(width), ' ');
    for (double f : {20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0}) {
        const int c = colOf(f);
        const std::string label = f >= 1000.0 ? std::to_string(static_cast<int>(f / 1000)) + "k" : std::to_string(static_cast<int>(f));
        for (size_t k = 0; k < label.size() && static_cast<size_t>(c) + k < ticks.size(); ++k)
            ticks[static_cast<size_t>(c) + k] = label[k];
    }
    std::printf("           %s\n", ticks.c_str());
}

} // namespace

int main() {
    constexpr double sr = 48000.0;

    ParametricEq eq;
    eq.prepare(sr, 512, 1);
    eq.setBand(0, {EqBandType::LowCut, 60.0f, 0.707f, 0.0f, true});
    eq.setSlope(0, EqSlope::Db24);
    eq.setBand(1, {EqBandType::LowShelf, 120.0f, 0.707f, 3.0f, true});
    eq.setBand(2, {EqBandType::Peak, 300.0f, 1.5f, -4.0f, true});
    eq.setBand(3, {EqBandType::Peak, 3000.0f, 1.0f, 5.0f, true});
    eq.setBand(4, {EqBandType::HighShelf, 10000.0f, 0.707f, 2.0f, true});
    eq.setBand(5, {EqBandType::HighCut, 18000.0f, 0.707f, 0.0f, true});
    eq.reset(); // Make the smoothers jump straight to the targets, so the measurement does not have to wait for a ramp

    std::printf("=== Chapter 03 Example 1: EQ curve (theory '#' vs measured 'o') @ %.0f Hz ===\n\n", sr);
    std::printf("band  type       freq(Hz)   Q      gain(dB)  slope\n");
    for (int i = 0; i < ParametricEq::kNumBands; ++i) {
        const EqBand& b = eq.getBand(i);
        std::printf("  %d   %-10s %8.1f  %5.3f  %+6.1f    %s\n", i, typeName(b.type), b.frequency, b.q, b.gainDb,
                    (b.type == EqBandType::LowCut || b.type == EqBandType::HighCut)
                        ? (eq.getSlope(i) == EqSlope::Db24 ? "24 dB/oct" : "12 dB/oct")
                        : "-");
    }

    // Measurement: feed a sine wave at each frequency through the EQ (sample by sample) and measure output / input in dB
    const std::vector<double> freqs = {30, 60, 120, 200, 300, 500, 1000, 2000, 3000, 5000, 8000, 12000, 16000, 18000, 21000};
    std::vector<double> measured;
    for (double f : freqs) {
        eq.reset();
        measured.push_back(measureGainDb([&](float x) { return eq.processSample(0, x); }, f, sr));
    }

    std::printf("\n");
    plotAscii(eq, freqs, measured, 20.0, 22000.0, 72, 8.0, -30.0, 2.0);

    std::printf("\n   freq(Hz)   theory(dB)  measured(dB)   diff\n");
    double maxDiff = 0.0;
    for (size_t i = 0; i < freqs.size(); ++i) {
        const double theory = eq.getMagnitudeDb(freqs[i]);
        const double diff = theory - measured[i];
        maxDiff = std::max(maxDiff, std::abs(diff));
        std::printf("  %8.0f   %+9.2f   %+9.2f      %+6.3f\n", freqs[i], theory, measured[i], diff);
    }
    std::printf("  max difference = %.3f dB (the Goertzel measurement is slightly off below -30 dB near Nyquist)\n", maxDiff);

    // Cascade = dB addition: list each band's contribution
    std::printf("\n=== dB contribution of each band (they add up to the whole curve) ===\n");
    std::printf("   freq(Hz) ");
    for (int i = 0; i < ParametricEq::kNumBands; ++i)
        std::printf("   band%d", i);
    std::printf("      sum   getMagnitudeDb\n");
    for (double f : {60.0, 120.0, 300.0, 1000.0, 3000.0, 10000.0, 18000.0}) {
        std::printf("  %8.0f ", f);
        double sum = 0.0;
        for (int i = 0; i < ParametricEq::kNumBands; ++i) {
            const double db = eq.getBandMagnitudeDb(i, f);
            sum += db;
            std::printf("  %+6.2f", db);
        }
        std::printf("   %+6.2f   %+6.2f\n", sum, eq.getMagnitudeDb(f));
    }

    // Shelf Q (or slope S): Q = 0.707 (S = 1) is "steepest without overshoot"; anything larger produces overshoot and a dip on either side of the transition
    std::printf("\n=== Low shelf +6 dB @ 1 kHz: Q and overshoot (README section 2.6) ===\n");
    std::printf("      Q     S(equiv)  |H(f0)|(dB)   curve max(dB)   curve min(dB)\n");
    for (double q : {0.5, 0.707, 1.0, 1.5, 2.0}) {
        const BiquadCoefficients c = BiquadCoefficients::makeLowShelf(sr, 1000.0, q, 6.0);
        double hi = -100.0, lo = 100.0;
        for (double f = 10.0; f < 23000.0; f *= 1.01) {
            hi = std::max(hi, c.magnitudeDb(f, sr));
            lo = std::min(lo, c.magnitudeDb(f, sr));
        }
        // Solve 1/Q² = (A + 1/A)(1/S - 1) + 2 for S: S = 1 / ((1/Q² - 2) / (A + 1/A) + 1)
        const double A = std::pow(10.0, 6.0 / 40.0);
        const double s = 1.0 / ((1.0 / (q * q) - 2.0) / (A + 1.0 / A) + 1.0);
        std::printf("   %5.3f    %5.2f       %+6.2f        %+6.2f         %+6.2f\n", q, s, c.magnitudeDb(1000.0, sr), hi, lo);
    }
    std::printf("   shelfSlopeToQ(S = 1, +6 dB) = %.4f (1/√2 at any gain)\n", ParametricEq::shelfSlopeToQ(1.0, 6.0));

    // Self-check: the conclusions printed by the example must really hold, otherwise ctest reports a failure
    std::printf("\n");
    DAWLAB_CHECK(maxDiff < 0.05);                                    // theoretical curve = measurement
    DAWLAB_CHECK_NEAR(eq.getBandMagnitudeDb(5, 18000.0), -3.0103, 0.01); // HighCut is -3 dB at fc
    DAWLAB_CHECK_NEAR(eq.getBandMagnitudeDb(0, 60.0), -3.0103, 0.01);    // 24 dB/oct LowCut is also -3 dB at fc
    DAWLAB_CHECK_NEAR(ParametricEq::shelfSlopeToQ(1.0, 6.0), 0.70711, 1e-4);
    return dawlab::test::summary("l03_eq_curve");
}
