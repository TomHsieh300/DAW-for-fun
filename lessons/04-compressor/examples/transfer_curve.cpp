// 範例 1：印出 compressor 的靜態 transfer curve（hard knee vs soft knee）。
//
// 示範什麼：
//   - Compressor::computeOutputDb() 這個「gain computer」在不同 threshold / ratio / knee 下的輸入→輸出關係
//   - knee 區段的二次曲線怎麼把 hard knee 的「轉角」磨圓，以及它在 knee 兩端與直線平滑銜接
//   - 用數字對照 README 的公式：例如 T = -20 dB、R = 4、輸入 -10 dB → 輸出 -17.5 dB、GR = 7.5 dB
//   - 對照：expander / gate 的曲線長什麼樣（第 08 章會完整實作）
// 怎麼執行：不需要參數，直接執行 l04_transfer_curve。
// 輸出檔案：無（純數字 / ASCII 圖）。
// 該看到什麼：soft knee 的 GR 在 threshold 附近「提早、慢慢」開始，hard knee 則在 threshold 之前完全是 0。

#include <dawlab/Common.h>
#include <dawlab/dsp/Compressor.h>

#include <cstdio>
#include <string>

using dawlab::dsp::Compressor;

namespace {

// 只是示範用：downward expander 的靜態曲線（低於 threshold 時每 1 dB 變成 R dB）
float expanderOutputDb(float x, float thresholdDb, float ratio) {
    return x >= thresholdDb ? x : thresholdDb + (x - thresholdDb) * ratio;
}

void printTable(float thresholdDb, float ratio, float hardKneeDb, float softKneeDb) {
    std::printf("\n=== Transfer curve：threshold %.0f dB, ratio %.0f:1 ===\n", thresholdDb, ratio);
    std::printf("  knee = %.0f dB (hard)   vs   knee = %.0f dB (soft)\n", hardKneeDb, softKneeDb);
    std::printf("  輸入 dB |  輸出(hard)  GR(hard) |  輸出(soft)  GR(soft)\n");
    std::printf("  --------+-----------------------+----------------------\n");
    for (float x = -60.0f; x <= 0.0f + 1e-3f; x += 3.0f) {
        const float yh = Compressor::computeOutputDb(x, thresholdDb, ratio, hardKneeDb);
        const float ys = Compressor::computeOutputDb(x, thresholdDb, ratio, softKneeDb);
        std::printf("  %7.1f | %9.2f   %7.2f   | %9.2f   %7.2f\n", x, yh, x - yh, ys, x - ys);
    }
}

// ASCII 圖：橫軸輸入 dB（-60..0），縱軸輸出 dB（-60..0），每格 2 dB
void plotCurves(float thresholdDb, float ratio, float softKneeDb) {
    constexpr int kCols = 31; // -60..0，每格 2 dB
    constexpr int kRows = 31;
    std::printf("\n  ASCII transfer curve（. = 1:1 參考線、# = hard knee、o = soft knee %.0f dB、* = 兩者重疊）\n",
                softKneeDb);
    std::printf("  縱軸：輸出 dB（上 = 0 dB），橫軸：輸入 dB（左 = -60，右 = 0）\n");
    std::string grid[kRows];
    for (auto& row : grid)
        row.assign(kCols, ' ');
    auto plot = [&](float x, float y, char c) {
        const int col = static_cast<int>(std::lround((x + 60.0f) / 2.0f));
        const int row = static_cast<int>(std::lround(-y / 2.0f)); // 0 dB 在最上面
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
    std::printf("       -60           -30            0  (輸入 dB)\n");
}

} // namespace

int main() {
    std::printf("第 04 章 範例 1：compressor transfer curve\n");

    // 主要設定：T = -20 dB、R = 4、knee 0 vs 10 dB
    printTable(-20.0f, 4.0f, 0.0f, 10.0f);

    // README 裡的手算對照點
    std::printf("\n=== 手算對照（T = -20, R = 4）===\n");
    const float probes[] = {-30.0f, -25.0f, -22.0f, -20.0f, -18.0f, -15.0f, -10.0f, 0.0f};
    std::printf("  輸入 dB | hard knee 輸出 | soft knee(10 dB) 輸出 | 兩者差\n");
    for (float x : probes) {
        const float yh = Compressor::computeOutputDb(x, -20.0f, 4.0f, 0.0f);
        const float ys = Compressor::computeOutputDb(x, -20.0f, 4.0f, 10.0f);
        std::printf("  %7.1f | %14.3f | %21.3f | %6.3f\n", x, yh, ys, ys - yh);
    }
    std::printf("  soft knee 在 threshold 本身（-20 dB）的輸出 = T + (1/R - 1)·W/8 = -20 + (-0.75)(10/8) = -20.9375\n");

    // ratio 比較：2:1、4:1、10:1、∞:1（limiter）
    std::printf("\n=== ratio 比較（T = -20 dB, hard knee, 輸入 0 dB）===\n");
    for (float r : {1.0f, 2.0f, 4.0f, 10.0f, 20.0f, 1000.0f}) {
        const float y = Compressor::computeOutputDb(0.0f, -20.0f, r, 0.0f);
        std::printf("  ratio %6.0f:1 → 輸出 %7.2f dB，GR %5.2f dB\n", r, y,
                    Compressor::computeGainReductionDb(0.0f, -20.0f, r, 0.0f)); // 用 GR 函式，避免印出 -0.00
    }

    plotCurves(-20.0f, 4.0f, 10.0f);

    // 對照：expander / gate（第 08 章）
    std::printf("\n=== 對照：downward expander（T = -40 dB, ratio 2 ⇒ 每低 1 dB 多衰減 1 dB）與 gate（ratio 很大）===\n");
    std::printf("  輸入 dB | compressor(T=-20,R=4) | expander(T=-40,R=2) | gate(T=-40,R=20)\n");
    for (float x = -60.0f; x <= 0.0f + 1e-3f; x += 10.0f) {
        std::printf("  %7.1f | %21.2f | %19.2f | %16.2f\n", x, Compressor::computeOutputDb(x, -20.0f, 4.0f, 0.0f),
                    expanderOutputDb(x, -40.0f, 2.0f), std::max(-100.0f, expanderOutputDb(x, -40.0f, 20.0f)));
    }
    std::printf("  compressor 把「threshold 以上」壓扁；expander / gate 把「threshold 以下」推更低。\n");
    return 0;
}
