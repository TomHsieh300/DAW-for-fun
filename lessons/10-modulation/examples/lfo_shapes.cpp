// 範例 1：LFO 的五種波形、square 的 click、tempo sync 換算
//
// 示範什麼：
//   1. Sine / Triangle / Saw / Square / Sample&Hold 五種波形：印出 ASCII 圖（兩個週期），
//      並把 LFO 本身當成音訊寫成 WAV（2 Hz，太低聽不到，但可以用波形編輯器打開來「看」）。
//   2. 每種波形的統計：最大值、最小值、平均值——確認輸出在 -1..+1，sine / triangle / square 的平均是 0，
//      saw 也是 0（-1 一路爬到 +1），S&H 的平均則看運氣（亂數）。
//   3. 為什麼 square 要平滑：拿 square LFO 直接當 gain 去切一個 441 Hz 正弦，
//      smoothing = 0 時每次切換都是瞬間跳變（click）；smoothing = 1 ms 後「最大單步跳變」小非常多。
//   4. tempo sync：bpm 與音符長度換算成 LFO rate（Lfo::tempoToRateHz）。
//
// 怎麼執行：l10_lfo_shapes（不需要參數）
//
// 輸出檔案（dawlab_output/）：
//   l10_lfo_sine.wav / l10_lfo_triangle.wav / l10_lfo_saw.wav / l10_lfo_square.wav / l10_lfo_sample_hold.wav
//       LFO 本身（2 Hz、2 秒、振幅 0.5；S&H 用 8 Hz）。
//   l10_lfo_square_click.wav   441 Hz 正弦 × square LFO（4 Hz），不平滑 → 每次切換都「喀」一聲
//   l10_lfo_square_smooth.wav  同上，但 square 經過 1 ms 平滑 → 乾淨的斷音
//
// 該聽到什麼：square_click 的每個音頭音尾都有一個短促的「喀」；square_smooth 則是乾淨的「噠噠噠噠」。

#include <dawlab/Common.h>
#include <dawlab/dsp/Lfo.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using dawlab::dsp::Lfo;
using dawlab::dsp::LfoShape;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

struct ShapeInfo {
    LfoShape shape;
    const char* name;
    const char* filename;
};

const ShapeInfo kShapes[] = {
    {LfoShape::Sine, "Sine", "l10_lfo_sine.wav"},
    {LfoShape::Triangle, "Triangle", "l10_lfo_triangle.wav"},
    {LfoShape::Saw, "Saw", "l10_lfo_saw.wav"},
    {LfoShape::Square, "Square", "l10_lfo_square.wav"},
    {LfoShape::SampleAndHold, "Sample&Hold", "l10_lfo_sample_hold.wav"},
};

// 逐 block 產生 LFO 輸出（模擬 audio callback），回傳單聲道 buffer
dawlab::AudioBuffer renderLfo(LfoShape shape, double rateHz, double seconds, float smoothingMs) {
    Lfo lfo;
    lfo.prepare(kSampleRate);
    lfo.setShape(shape);
    lfo.setRateHz(static_cast<float>(rateHz));
    lfo.setSmoothingMs(smoothingMs);
    lfo.reset();

    dawlab::AudioBuffer out(1, dawlab::secondsToSamples(seconds, kSampleRate));
    float* d = out.getWritePointer(0);
    for (int start = 0; start < out.getNumSamples(); start += kBlockSize) {
        const int n = std::min(kBlockSize, out.getNumSamples() - start);
        for (int i = 0; i < n; ++i)
            d[start + i] = lfo.getNextValue();
    }
    return out;
}

// 把 buffer 的前 seconds 秒畫成 ASCII 圖：columns 欄、rows 列，縱軸 -1..+1
void asciiPlot(const dawlab::AudioBuffer& b, double seconds, int columns, int rows) {
    const int total = std::min(b.getNumSamples(), dawlab::secondsToSamples(seconds, kSampleRate));
    std::vector<int> rowOf(static_cast<size_t>(columns));
    for (int c = 0; c < columns; ++c) {
        const int idx = static_cast<int>(static_cast<long long>(c) * total / columns);
        const float v = b.getSample(0, idx);
        // v = +1 → 第 0 列，v = -1 → 最後一列
        rowOf[static_cast<size_t>(c)] = static_cast<int>(std::lround((1.0 - v) * 0.5 * (rows - 1)));
    }
    for (int r = 0; r < rows; ++r) {
        const double level = 1.0 - 2.0 * r / (rows - 1);
        std::printf("  %+5.2f |", level);
        for (int c = 0; c < columns; ++c)
            std::putchar(rowOf[static_cast<size_t>(c)] == r ? '*' : (r == (rows - 1) / 2 ? '-' : ' '));
        std::putchar('\n');
    }
    std::printf("        +%s\n", std::string(static_cast<size_t>(columns), '-').c_str());
}

// 中文字在 console 上佔兩格寬，printf 的 %-14s 是以 byte 計，會對不齊。
// 這裡以 UTF-8 code point 計寬度：ASCII 1 格、其他（中文）2 格，補空白到 width 格。
void printPadded(const char* text, int width) {
    int w = 0;
    for (const char* p = text; *p != '\0'; ++p) {
        const auto c = static_cast<unsigned char>(*p);
        if (c < 0x80)
            w += 1;
        else if ((c & 0xC0) != 0x80) // UTF-8 的起始 byte（不是延續 byte）
            w += 2;
    }
    std::printf("%s%*s", text, std::max(0, width - w), "");
}

// 最大單步跳變 max |y[n] − y[n−1]|：click 的量化指標
float maxStep(const dawlab::AudioBuffer& b) {
    const float* d = b.getReadPointer(0);
    float m = 0.0f;
    for (int i = 1; i < b.getNumSamples(); ++i)
        m = std::max(m, std::abs(d[i] - d[i - 1]));
    return m;
}

} // namespace

int main() {
    std::printf("=== 第 10 章 範例 1：LFO 波形 ===\n\n");

    // ---- 1 + 2. 五種波形：ASCII 圖、統計、WAV --------------------------------
    for (const ShapeInfo& s : kShapes) {
        // 2 Hz → 一個週期 0.5 秒，畫兩個週期。S&H 一個週期只有一個值，改用 8 Hz、畫 1 秒（8 個階梯）
        const bool isSh = (s.shape == LfoShape::SampleAndHold);
        const double rate = isSh ? 8.0 : 2.0;
        const double plotSeconds = isSh ? 1.0 : 2.0 / rate;
        dawlab::AudioBuffer lfo = renderLfo(s.shape, rate, 2.0, 1.0f);

        double sum = 0.0;
        float mn = 1.0f, mx = -1.0f;
        for (int i = 0; i < lfo.getNumSamples(); ++i) {
            const float v = lfo.getSample(0, i);
            sum += v;
            mn = std::min(mn, v);
            mx = std::max(mx, v);
        }
        std::printf("%s（%.0f Hz，畫出前 %.1f 秒）  min %+.3f  max %+.3f  mean %+.4f\n", s.name, rate, plotSeconds, mn,
                    mx, sum / lfo.getNumSamples());
        asciiPlot(lfo, plotSeconds, 64, 11);

        dawlab::AudioBuffer wav = lfo;
        wav.applyGain(0.5f);
        dawlab::writeWav(dawlab::outputPath(s.filename), wav, kSampleRate);
        std::printf("  -> %s\n\n", s.filename);
    }

    // ---- 3. square 不平滑 vs 平滑：click 的量化 --------------------------------
    {
        // 為什麼是 441 Hz 而不是 440 Hz？4 Hz 的 square 每 6000 個 sample 切換一次，
        // 440 Hz 正弦在第 6000·k 個 sample 剛好是 55·k 個整週期 = 過零點，切換時振幅本來就是 0，
        // 量不到 click（真實音樂不會這麼巧）。441 Hz 時切換點落在正弦的 45°、90°… 處。
        constexpr double kToneHz = 441.0;
        const dawlab::AudioBuffer tone = dawlab::makeSine(kToneHz, kSampleRate, 2.0, 0.5f, 1);
        const float naturalStep = maxStep(tone); // 正弦本身最大的單步變化 = A·2πf/fs
        std::printf("square LFO 當 gain（4 Hz）切 %.0f Hz 正弦（振幅 0.5）：\n", kToneHz);
        std::printf("  正弦本身的最大單步跳變 = %.4f（理論 A·2πf/fs = %.4f）\n", naturalStep,
                    0.5 * dawlab::kTwoPi * kToneHz / kSampleRate);

        const float smoothings[] = {0.0f, 1.0f, 5.0f};
        const char* files[] = {"l10_lfo_square_click.wav", "l10_lfo_square_smooth.wav", nullptr};
        for (int k = 0; k < 3; ++k) {
            dawlab::AudioBuffer gate = renderLfo(LfoShape::Square, 4.0, 2.0, smoothings[k]);
            dawlab::AudioBuffer out = tone;
            float* d = out.getWritePointer(0);
            for (int i = 0; i < out.getNumSamples(); ++i)
                d[i] *= 0.5f * (1.0f + gate.getSample(0, i)); // gain = 0..1
            std::printf("  smoothing %4.1f ms：最大單步跳變 = %.4f（正弦的 %.1f 倍）", smoothings[k], maxStep(out),
                        maxStep(out) / naturalStep);
            if (files[k]) {
                dawlab::writeWav(dawlab::outputPath(files[k]), out, kSampleRate);
                std::printf("  -> %s", files[k]);
            }
            std::printf("\n");
        }
        std::printf("  -> 不平滑時跳變是正弦本身的好幾倍（振幅瞬間歸零）= 寬頻的 click；\n"
                    "     1 ms 的 one-pole 平滑就把它壓回正弦本身的量級。\n\n");
    }

    // ---- 4. tempo sync -------------------------------------------------------
    {
        struct Division {
            const char* name;
            double beats;
        };
        const Division divs[] = {{"1 小節 (4/4)", 4.0}, {"二分音符", 2.0},   {"四分音符", 1.0},
                                 {"附點八分", 0.75},    {"八分音符", 0.5},   {"八分三連音", 1.0 / 3.0},
                                 {"十六分音符", 0.25}};
        const double bpms[] = {90.0, 120.0, 140.0};
        std::printf("tempo sync：rate = bpm / 60 / beats\n");
        std::printf("  ");
        printPadded("音符長度", 14);
        for (double bpm : bpms)
            std::printf("  %6.0f bpm", bpm);
        std::printf("\n");
        for (const Division& dv : divs) {
            std::printf("  ");
            printPadded(dv.name, 14);
            for (double bpm : bpms)
                std::printf("  %7.3f Hz", Lfo::tempoToRateHz(bpm, dv.beats));
            std::printf("\n");
        }
        std::printf("  -> 120 bpm 的四分音符 = 2 Hz；八分音符 = 4 Hz；附點八分 = 2.667 Hz。\n");
    }
    return 0;
}
