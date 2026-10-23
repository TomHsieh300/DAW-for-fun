// Example 1: the five LFO shapes, the click of a square wave, and tempo-sync conversion
//
// What it demonstrates:
//   1. The five shapes Sine / Triangle / Saw / Square / Sample&Hold: prints an ASCII plot
//      (two cycles) and writes the LFO itself as audio to a WAV (2 Hz is too low to hear, but you
//      can open it in a waveform editor to "see" it).
//   2. Per-shape statistics: maximum, minimum, and mean -- confirming the output stays within
//      -1..+1, that sine / triangle / square average 0, that saw also averages 0 (it ramps from
//      -1 up to +1), and that the S&H mean is down to luck (it is random).
//   3. Why square needs smoothing: use the square LFO directly as a gain to chop a 441 Hz sine.
//      With smoothing = 0 every switch is an instantaneous jump (a click); with smoothing = 1 ms
//      the "largest single-step jump" is much smaller.
//   4. Tempo sync: converting bpm and note length to an LFO rate (Lfo::tempoToRateHz).
//
// How to run: l10_lfo_shapes (no arguments)
//
// Output files (dawlab_output/):
//   l10_lfo_sine.wav / l10_lfo_triangle.wav / l10_lfo_saw.wav / l10_lfo_square.wav / l10_lfo_sample_hold.wav
//       The LFO itself (2 Hz, 2 seconds, amplitude 0.5; S&H uses 8 Hz).
//   l10_lfo_square_click.wav   441 Hz sine x square LFO (4 Hz), unsmoothed -> a "click" at every switch
//   l10_lfo_square_smooth.wav  same, but the square is smoothed by 1 ms -> clean staccato
//
// What you should hear: square_click has a short "click" at the start and end of every note;
// square_smooth is a clean "da-da-da-da".

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

// Generate the LFO output block by block (simulating the audio callback); returns a mono buffer
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

// Draw the first `seconds` of the buffer as an ASCII plot: `columns` columns, `rows` rows, vertical axis -1..+1
void asciiPlot(const dawlab::AudioBuffer& b, double seconds, int columns, int rows) {
    const int total = std::min(b.getNumSamples(), dawlab::secondsToSamples(seconds, kSampleRate));
    std::vector<int> rowOf(static_cast<size_t>(columns));
    for (int c = 0; c < columns; ++c) {
        const int idx = static_cast<int>(static_cast<long long>(c) * total / columns);
        const float v = b.getSample(0, idx);
        // v = +1 -> row 0, v = -1 -> last row
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

// Left-justify `text` in a field of `width` columns (all labels are ASCII).
void printPadded(const char* text, int width) {
    std::printf("%-*s", width, text);
}

// Largest single-step jump max |y[n] − y[n−1]|: a quantitative measure of a click
float maxStep(const dawlab::AudioBuffer& b) {
    const float* d = b.getReadPointer(0);
    float m = 0.0f;
    for (int i = 1; i < b.getNumSamples(); ++i)
        m = std::max(m, std::abs(d[i] - d[i - 1]));
    return m;
}

} // namespace

int main() {
    std::printf("=== Chapter 10 Example 1: LFO shapes ===\n\n");

    // ---- 1 + 2. The five shapes: ASCII plot, statistics, WAV -----------------
    for (const ShapeInfo& s : kShapes) {
        // 2 Hz -> one cycle is 0.5 s; plot two cycles. S&H has only one value per cycle, so use 8 Hz and plot 1 s (8 steps)
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
        std::printf("%s (%.0f Hz, first %.1f s plotted)  min %+.3f  max %+.3f  mean %+.4f\n", s.name, rate, plotSeconds, mn,
                    mx, sum / lfo.getNumSamples());
        asciiPlot(lfo, plotSeconds, 64, 11);

        dawlab::AudioBuffer wav = lfo;
        wav.applyGain(0.5f);
        dawlab::writeWav(dawlab::outputPath(s.filename), wav, kSampleRate);
        std::printf("  -> %s\n\n", s.filename);
    }

    // ---- 3. Square unsmoothed vs smoothed: quantifying the click ---------------
    {
        // Why 441 Hz rather than 440 Hz? A 4 Hz square switches every 6000 samples.
        // A 440 Hz sine at sample 6000·k is exactly 55·k whole cycles = a zero crossing, so the
        // amplitude is already 0 at the switch and no click could be measured (real music is never
        // that lucky). With 441 Hz the switch points land at 45°, 90°, ... of the sine.
        constexpr double kToneHz = 441.0;
        const dawlab::AudioBuffer tone = dawlab::makeSine(kToneHz, kSampleRate, 2.0, 0.5f, 1);
        const float naturalStep = maxStep(tone); // the sine's own largest single-step change = A·2πf/fs
        std::printf("Square LFO as a gain (4 Hz) chopping a %.0f Hz sine (amplitude 0.5):\n", kToneHz);
        std::printf("  The sine's own largest single-step jump = %.4f (theory A*2*pi*f/fs = %.4f)\n", naturalStep,
                    0.5 * dawlab::kTwoPi * kToneHz / kSampleRate);

        const float smoothings[] = {0.0f, 1.0f, 5.0f};
        const char* files[] = {"l10_lfo_square_click.wav", "l10_lfo_square_smooth.wav", nullptr};
        for (int k = 0; k < 3; ++k) {
            dawlab::AudioBuffer gate = renderLfo(LfoShape::Square, 4.0, 2.0, smoothings[k]);
            dawlab::AudioBuffer out = tone;
            float* d = out.getWritePointer(0);
            for (int i = 0; i < out.getNumSamples(); ++i)
                d[i] *= 0.5f * (1.0f + gate.getSample(0, i)); // gain = 0..1
            std::printf("  smoothing %4.1f ms: largest single-step jump = %.4f (%.1fx the sine's)", smoothings[k], maxStep(out),
                        maxStep(out) / naturalStep);
            if (files[k]) {
                dawlab::writeWav(dawlab::outputPath(files[k]), out, kSampleRate);
                std::printf("  -> %s", files[k]);
            }
            std::printf("\n");
        }
        std::printf("  -> Unsmoothed, the jump is several times the sine's own (the amplitude drops to zero instantly) = a broadband click;\n"
                    "     a 1 ms one-pole smoother brings it back to the sine's own order of magnitude.\n\n");
    }

    // ---- 4. Tempo sync -------------------------------------------------------
    {
        struct Division {
            const char* name;
            double beats;
        };
        const Division divs[] = {{"1 bar (4/4)", 4.0},    {"Half note", 2.0},    {"Quarter note", 1.0},
                                 {"Dotted eighth", 0.75}, {"Eighth note", 0.5},  {"Eighth triplet", 1.0 / 3.0},
                                 {"Sixteenth note", 0.25}};
        const double bpms[] = {90.0, 120.0, 140.0};
        std::printf("Tempo sync: rate = bpm / 60 / beats\n");
        std::printf("  ");
        printPadded("Note length", 14);
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
        std::printf("  -> At 120 bpm: quarter note = 2 Hz; eighth note = 4 Hz; dotted eighth = 2.667 Hz.\n");
    }
    return 0;
}
