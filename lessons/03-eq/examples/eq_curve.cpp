// 範例 1：畫 EQ 曲線（理論）並與實測比對
//
// 示範什麼：
//   - 用 ParametricEq::getMagnitudeDb() 算出整條 EQ 曲線，以 ASCII 圖印出（橫軸 log 頻率）。
//   - 用 measureGainDb() 把正弦波真的送進 EQ 量增益，疊在同一張圖上（'o'），證明「畫的」跟「算的」一致。
//   - 列出每個 band 在幾個頻率的 dB 貢獻與總和，驗證串接的 dB 會相加。
//   - Low shelf 的 Q（slope S）與過衝：Q = 0.707（S = 1）單調；Q 更大時轉折兩側出現過衝與凹陷。
// 怎麼執行：l03_eq_curve（不需參數）
// 輸出：只有 console 文字，不輸出 WAV。
// 該看到什麼：理論曲線 '#' 與實測點 'o' 幾乎重疊；表格裡「理論 − 實測」小於 0.05 dB。
//   最後用 DAWLAB_CHECK 自我檢查這些結論（全部成立時印 [PASS]；不成立會回傳非 0，ctest 判定失敗）。

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

// 在 [fMin, fMax] 的 log 軸上，橫向 width 格、縱向從 dbTop 到 dbBottom 每 dbStep 一列
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
    // 0 dB 基準線
    const int zeroRow = rowOf(0.0);
    for (int c = 0; c < width; ++c)
        grid[static_cast<size_t>(zeroRow)][static_cast<size_t>(c)] = '-';
    // 理論曲線
    for (int c = 0; c < width; ++c) {
        const double f = fMin * std::pow(fMax / fMin, static_cast<double>(c) / (width - 1));
        grid[static_cast<size_t>(rowOf(eq.getMagnitudeDb(f)))][static_cast<size_t>(c)] = '#';
    }
    // 實測點
    for (size_t i = 0; i < measuredFreqs.size(); ++i)
        grid[static_cast<size_t>(rowOf(measuredDb[i]))][static_cast<size_t>(colOf(measuredFreqs[i]))] = 'o';

    for (int r = 0; r < rows; ++r)
        std::printf("%+6.0f dB |%s|\n", dbTop - r * dbStep, grid[static_cast<size_t>(r)].c_str());
    std::printf("          +%s+\n", std::string(static_cast<size_t>(width), '-').c_str());
    // 頻率刻度
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
    eq.reset(); // 讓平滑器直接跳到目標，量測時不用等 ramp

    std::printf("=== 第 03 章 範例 1：EQ 曲線（理論 '#' vs 實測 'o'）@ %.0f Hz ===\n\n", sr);
    std::printf("band  type       freq(Hz)   Q      gain(dB)  slope\n");
    for (int i = 0; i < ParametricEq::kNumBands; ++i) {
        const EqBand& b = eq.getBand(i);
        std::printf("  %d   %-10s %8.1f  %5.3f  %+6.1f    %s\n", i, typeName(b.type), b.frequency, b.q, b.gainDb,
                    (b.type == EqBandType::LowCut || b.type == EqBandType::HighCut)
                        ? (eq.getSlope(i) == EqSlope::Db24 ? "24 dB/oct" : "12 dB/oct")
                        : "-");
    }

    // 實測：對每個頻率送正弦波進 EQ（逐 sample），量輸出 / 輸入的 dB
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
    std::printf("  最大差異 = %.3f dB（Goertzel 量測在接近 Nyquist 的 -30 dB 以下會略有誤差）\n", maxDiff);

    // 串接 = dB 相加：列出每個 band 的貢獻
    std::printf("\n=== 每個 band 的 dB 貢獻（相加就是整條曲線）===\n");
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

    // Shelf 的 Q（或 slope S）：Q = 0.707（S = 1）是「最陡但不過衝」，再大就會在轉折兩側出現過衝與凹陷
    std::printf("\n=== Low shelf +6 dB @ 1 kHz：Q 與過衝（README 2.6 節）===\n");
    std::printf("      Q     S(對應)   |H(f0)|(dB)   曲線最高(dB)   曲線最低(dB)\n");
    for (double q : {0.5, 0.707, 1.0, 1.5, 2.0}) {
        const BiquadCoefficients c = BiquadCoefficients::makeLowShelf(sr, 1000.0, q, 6.0);
        double hi = -100.0, lo = 100.0;
        for (double f = 10.0; f < 23000.0; f *= 1.01) {
            hi = std::max(hi, c.magnitudeDb(f, sr));
            lo = std::min(lo, c.magnitudeDb(f, sr));
        }
        // 由 1/Q² = (A + 1/A)(1/S − 1) + 2 反解 S = 1 / ((1/Q² − 2) / (A + 1/A) + 1)
        const double A = std::pow(10.0, 6.0 / 40.0);
        const double s = 1.0 / ((1.0 / (q * q) - 2.0) / (A + 1.0 / A) + 1.0);
        std::printf("   %5.3f    %5.2f       %+6.2f        %+6.2f         %+6.2f\n", q, s, c.magnitudeDb(1000.0, sr), hi, lo);
    }
    std::printf("   shelfSlopeToQ(S = 1, +6 dB) = %.4f（任何 gain 都是 1/√2）\n", ParametricEq::shelfSlopeToQ(1.0, 6.0));

    // 自我檢查：範例印出的結論要真的成立，否則 ctest 判定失敗
    std::printf("\n");
    DAWLAB_CHECK(maxDiff < 0.05);                                    // 理論曲線 = 實測
    DAWLAB_CHECK_NEAR(eq.getBandMagnitudeDb(5, 18000.0), -3.0103, 0.01); // HighCut 在 fc 是 -3 dB
    DAWLAB_CHECK_NEAR(eq.getBandMagnitudeDb(0, 60.0), -3.0103, 0.01);    // 24 dB/oct LowCut 在 fc 也是 -3 dB
    DAWLAB_CHECK_NEAR(ParametricEq::shelfSlopeToQ(1.0, 6.0), 0.70711, 1e-4);
    return dawlab::test::summary("l03_eq_curve");
}
