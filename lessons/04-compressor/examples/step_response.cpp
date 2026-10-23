// Example 2: attack / release step response.
//
// What it demonstrates:
//   (A) How AttackReleaseFilter alone responds to a "dB step": measure t63 (time to 63.2%) and t90 (time to 90%),
//       confirming that the tau of alpha = exp(-1/(tau*fs)) is exactly the "time to 63.2%" and t90 = ln(10)*tau ~ 2.30*tau.
//   (B) Feed tone bursts (-30 -> -10 -> -30 dBFS, 0.5 s each) into a Compressor, print how gain reduction evolves over
//       time, measure the attack / release times, and compare:
//         - Decoupled vs Branching smoothing (Branching underestimates the peak of a periodic signal)
//         - Peak vs RMS detector
//         - log (dB) domain smoothing (what Compressor does) vs linear domain smoothing (EnvelopeFollower -> static curve)
//       Finally, overlay the release curves of B1, B2 and B4 in one ASCII chart (GR vs time).
// How to run: just run l04_step_response.
// Output files: dawlab_output/l04_step_in.wav, l04_step_out.wav (the tone bursts before / after compression).
// What you should hear: the loud second segment "jumps out" during its first 10 ms (the attack has not clamped down
//            yet) and is then squashed to a lower level; when it returns to the quiet segment, the volume slowly
//            "floats" back up over the first 100 ms (release).

#include <dawlab/Common.h>
#include <dawlab/dsp/Compressor.h>
#include <dawlab/dsp/EnvelopeFollower.h>

#include <cstdio>
#include <vector>

using namespace dawlab;
using dawlab::dsp::AttackReleaseFilter;
using dawlab::dsp::Compressor;
using dawlab::dsp::DetectorMode;
using dawlab::dsp::EnvelopeFollower;
using dawlab::dsp::SmoothingMode;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr float kThresholdDb = -20.0f;
constexpr float kRatio = 4.0f;
constexpr float kAttackMs = 10.0f;
constexpr float kReleaseMs = 100.0f;
constexpr double kSegmentSeconds = 0.5;

// Find the time (in milliseconds) at which the timeline first reaches the target; the search starts at startIndex
double firstTimeReaching(const std::vector<float>& v, int startIndex, float target, bool rising) {
    for (size_t i = static_cast<size_t>(startIndex); i < v.size(); ++i) {
        if (rising ? v[i] >= target : v[i] <= target)
            return samplesToMs(static_cast<double>(i) - startIndex, kSampleRate);
    }
    return -1.0;
}

// Run the tone bursts through the compressor block by block and record the gain at every sample
// (the GR derived from output / input)
struct Run {
    std::vector<float> grDb; // Gain reduction (dB) at every sample. getGainReductionDb() is per block, which is not
                             // fine-grained enough, so the value is recovered from the input / output ratio.
    AudioBuffer output;
};

Run runCompressor(const AudioBuffer& input, Compressor& comp) {
    Run run;
    run.output = input;
    const int n = input.getNumSamples();
    run.grDb.assign(static_cast<size_t>(n), 0.0f);
    AudioBlock whole(run.output);
    for (int start = 0; start < n; start += kBlockSize) {
        const int len = std::min(kBlockSize, n - start);
        comp.process(whole.getSubBlock(start, len));
    }
    // Recover the gain from |y| / |x|: this is inaccurate near a sine's zero crossings, so use the peak ratio over the
    // most recent 1 ms window at every sample
    const int win = msToSamples(1.0, kSampleRate);
    for (int i = 0; i < n; ++i) {
        const int a = std::max(0, i - win + 1);
        const float px = peakAbs(input.getReadPointer(0) + a, i - a + 1);
        const float py = peakAbs(run.output.getReadPointer(0) + a, i - a + 1);
        run.grDb[static_cast<size_t>(i)] = (px > 1e-6f && py > 1e-9f) ? gainToDb(px) - gainToDb(py) : 0.0f;
    }
    return run;
}

void printTimeline(const char* title, const std::vector<float>& gr, int stepStart, int releaseStart) {
    std::printf("\n  %s\n", title);
    std::printf("    attack phase (t = 0 is where the loud segment starts):\n      t(ms):");
    for (int ms : {0, 2, 5, 10, 15, 20, 30, 50, 100, 200})
        std::printf(" %6d", ms);
    std::printf("\n      GR(dB):");
    for (int ms : {0, 2, 5, 10, 15, 20, 30, 50, 100, 200})
        std::printf(" %6.2f", gr[static_cast<size_t>(stepStart + msToSamples(ms, kSampleRate))]);
    std::printf("\n    release phase (t = 0 is where it returns to the quiet segment):\n      t(ms):");
    for (int ms : {0, 20, 50, 100, 150, 200, 300, 400, 490})
        std::printf(" %6d", ms);
    std::printf("\n      GR(dB):");
    for (int ms : {0, 20, 50, 100, 150, 200, 300, 400, 490})
        std::printf(" %6.2f", gr[static_cast<size_t>(releaseStart + msToSamples(ms, kSampleRate))]);
    std::printf("\n");
}

// Overlay several release curves (GR vs time) on one ASCII chart.
// Horizontal axis: 0..490 ms after returning to the quiet segment, 10 ms per column; vertical axis: GR, 0.5 dB per row.
// Each curve uses one character; '*' is printed where two or more fall in the same cell; '-' is refLevelDb
// (the 36.8% reference line of B1).
struct Curve {
    char symbol;
    const std::vector<float>* gr;
};

void printReleaseChart(const Curve* curves, int numCurves, int releaseStart, float refLevelDb) {
    constexpr int kCols = 50;    // 0, 10, ..., 490 ms
    constexpr int kRows = 16;    // 8.0 dB down to 0.5 dB; the 0 dB row serves as the horizontal axis
    constexpr float kRowDb = 0.5f;
    const int refRow = static_cast<int>(std::lround(refLevelDb / kRowDb));
    std::printf("\n  Overlaid release curves (GR vs time, t = 0 is where it returns to the quiet segment)\n");
    for (int row = kRows; row >= 0; --row) {
        if (row % 2 == 0)
            std::printf("  %4.1f |", row * kRowDb);
        else
            std::printf("       |");
        for (int c = 0; c < kCols; ++c) {
            char ch = (row == refRow) ? '-' : ' ';
            if (row == 0)
                ch = '_';
            int hits = 0;
            for (int k = 0; k < numCurves; ++k) {
                const float v = (*curves[k].gr)[static_cast<size_t>(releaseStart + msToSamples(c * 10.0, kSampleRate))];
                if (static_cast<int>(std::lround(v / kRowDb)) == row) {
                    ch = (hits == 0) ? curves[k].symbol : '*';
                    ++hits;
                }
            }
            std::putchar(ch);
        }
        std::printf("\n");
    }
    // Column c is printed at character 7 + c, so the 0 / 100 / 200 ms ticks line up with columns 0 / 10 / 20
    std::printf("       0         100       200       300       400      (ms)\n");
}

void measureAndPrint(const std::vector<float>& gr, int stepStart, int releaseStart, float staticGrDb) {
    // Steady-state GR: the average over the last 50 ms of the loud segment
    double sum = 0.0;
    const int tail = msToSamples(50.0, kSampleRate);
    for (int i = releaseStart - tail; i < releaseStart; ++i)
        sum += gr[static_cast<size_t>(i)];
    const float settled = static_cast<float>(sum / tail);
    const double t63 = firstTimeReaching(gr, stepStart, 0.632f * settled, true);
    const double t90 = firstTimeReaching(gr, stepStart, 0.90f * settled, true);
    const double r37 = firstTimeReaching(gr, releaseStart, 0.368f * settled, false);
    const double r10 = firstTimeReaching(gr, releaseStart, 0.10f * settled, false);
    std::printf("    Steady-state GR = %.2f dB (static curve %.2f dB); attack: t63 = %.1f ms, t90 = %.1f ms; "
                "release: t37 = %.1f ms, t10 = %.1f ms\n",
                settled, staticGrDb, t63, t90, r37, r10);
}

} // namespace

int main() {
    std::printf("Chapter 04, example 2: attack / release step response (fs = %.0f Hz)\n", kSampleRate);

    // ---------- (A) AttackReleaseFilter response to a dB step ----------
    std::printf("\n=== (A) AttackReleaseFilter on its own: target 0 -> 10 -> 0, attack %.0f ms, release %.0f ms ===\n",
                kAttackMs, kReleaseMs);
    const float alphaA = dsp::attackReleaseCoefficient(kAttackMs, kSampleRate);
    const float alphaR = dsp::attackReleaseCoefficient(kReleaseMs, kSampleRate);
    std::printf("  αA = exp(-1/(0.010·48000)) = %.6f, αR = exp(-1/(0.100·48000)) = %.6f\n", alphaA, alphaR);
    std::printf("  Inverted: τA = %.3f ms, τR = %.3f ms\n", dsp::coefficientToTimeMs(alphaA, kSampleRate),
                dsp::coefficientToTimeMs(alphaR, kSampleRate));
    for (SmoothingMode mode : {SmoothingMode::Branching, SmoothingMode::Decoupled}) {
        AttackReleaseFilter f;
        f.prepare(kSampleRate);
        f.setAttackMs(kAttackMs);
        f.setReleaseMs(kReleaseMs);
        f.setMode(mode);
        const int half = msToSamples(500.0, kSampleRate);
        std::vector<float> y(static_cast<size_t>(2 * half));
        for (int i = 0; i < 2 * half; ++i)
            y[static_cast<size_t>(i)] = f.process(i < half ? 10.0f : 0.0f);
        const double t63 = firstTimeReaching(y, 0, 6.32f, true);
        const double t90 = firstTimeReaching(y, 0, 9.0f, true);
        const double r37 = firstTimeReaching(y, half, 3.68f, false);
        const double r10 = firstTimeReaching(y, half, 1.0f, false);
        std::printf("  %-9s attack: t63 = %6.2f ms (theory τ = %.0f), t90 = %6.2f ms (theory ln10·τ = %.2f) | "
                    "release: t37 = %6.2f ms (theory %.0f), t10 = %6.2f ms (theory %.2f)\n",
                    mode == SmoothingMode::Branching ? "Branching" : "Decoupled", t63, kAttackMs, t90,
                    kAttackMs * 2.302585, r37, kReleaseMs, r10, kReleaseMs * 2.302585);
    }
    std::printf("  (Decoupled's release adds the delay of the attack filter, so t37 is roughly τR + τA)\n");

    // ---------- (B) Compressor response to tone bursts ----------
    const AudioBuffer input = makeToneBursts(1000.0, kSampleRate, {-30.0f, -10.0f, -30.0f}, kSegmentSeconds, 2);
    const int segLen = secondsToSamples(kSegmentSeconds, kSampleRate);
    const int stepStart = segLen;
    const int releaseStart = 2 * segLen;
    const float staticGr = Compressor::computeGainReductionDb(-10.0f, kThresholdDb, kRatio, 0.0f);
    std::printf("\n=== (B) Compressor: 1 kHz tone bursts -30 → -10 → -30 dBFS, T = %.0f dB, R = %.0f:1, hard knee, "
                "attack %.0f ms, release %.0f ms ===\n",
                kThresholdDb, kRatio, kAttackMs, kReleaseMs);
    std::printf("  Static curve: input -10 dB → output %.2f dB, GR = %.2f dB\n",
                Compressor::computeOutputDb(-10.0f, kThresholdDb, kRatio, 0.0f), staticGr);

    auto makeComp = [&](SmoothingMode mode, DetectorMode det) {
        Compressor c;
        c.prepare(kSampleRate, kBlockSize, 2);
        c.setThresholdDb(kThresholdDb);
        c.setRatio(kRatio);
        c.setKneeDb(0.0f);
        c.setAttackMs(kAttackMs);
        c.setReleaseMs(kReleaseMs);
        c.setSmoothingMode(mode);
        c.setDetectorMode(det);
        c.setRmsWindowMs(5.0f);
        return c;
    };

    // For the release overlay chart at the end
    std::vector<float> grB1, grB2, grB4;
    float settledB1 = 0.0f;

    // B1: the defaults (Decoupled + Peak) — this is the run that writes the WAV files
    {
        Compressor comp = makeComp(SmoothingMode::Decoupled, DetectorMode::Peak);
        Run run = runCompressor(input, comp);
        printTimeline("B1. Decoupled smoothing + Peak detector (the Compressor default)", run.grDb, stepStart,
                      releaseStart);
        measureAndPrint(run.grDb, stepStart, releaseStart, staticGr);
        grB1 = run.grDb;
        settledB1 = run.grDb[static_cast<size_t>(releaseStart - 1)];
        std::printf("    getGainReductionDb() (last block) = %.3f dB\n", comp.getGainReductionDb());
        writeWav(outputPath("l04_step_in.wav"), input, kSampleRate);
        writeWav(outputPath("l04_step_out.wav"), run.output, kSampleRate);
        std::printf("    Wrote dawlab_output/l04_step_in.wav, l04_step_out.wav\n");
        std::printf("    Output peak: segment 1 %.2f dBFS, segment 2 %.2f dBFS (overshoot peak in the first 10 ms: %.2f dBFS)\n",
                    gainToDb(peakAbs(run.output.getReadPointer(0), segLen)),
                    gainToDb(peakAbs(run.output.getReadPointer(0) + stepStart + msToSamples(100.0, kSampleRate),
                                     segLen - msToSamples(100.0, kSampleRate))),
                    gainToDb(peakAbs(run.output.getReadPointer(0) + stepStart, msToSamples(10.0, kSampleRate))));
    }

    // B2: Branching smoothing
    {
        Compressor comp = makeComp(SmoothingMode::Branching, DetectorMode::Peak);
        Run run = runCompressor(input, comp);
        printTimeline("B2. Branching smoothing + Peak detector", run.grDb, stepStart, releaseStart);
        measureAndPrint(run.grDb, stepStart, releaseStart, staticGr);
        grB2 = run.grDb;
        std::printf("    → At every zero crossing of the sine the instantaneous level drops to -inf dB, and Branching\n"
                    "      pulls the state down with the release coefficient during that time, so the steady-state GR is\n"
                    "      below the static curve (the peak is underestimated); Decoupled's peak-hold does not have this problem.\n");
    }

    // B3: RMS detector
    {
        Compressor comp = makeComp(SmoothingMode::Decoupled, DetectorMode::Rms);
        Run run = runCompressor(input, comp);
        printTimeline("B3. Decoupled smoothing + RMS detector (5 ms window)", run.grDb, stepStart, releaseStart);
        measureAndPrint(run.grDb, stepStart, releaseStart,
                        Compressor::computeGainReductionDb(-10.0f - 3.01f, kThresholdDb, kRatio, 0.0f));
        std::printf("    → A sine's RMS is 3.01 dB below its peak, so the GR is computed from a \"-13 dB input\" = %.2f dB;\n"
                    "      the RMS window is itself a layer of smoothing, so the attack is a bit slower than nominal.\n",
                    Compressor::computeGainReductionDb(-13.01f, kThresholdDb, kRatio, 0.0f));
    }

    // B4: linear domain smoothing (EnvelopeFollower -> static curve), compared with the log domain
    {
        EnvelopeFollower env;
        env.prepare(kSampleRate);
        env.setMode(DetectorMode::Peak);
        env.setSmoothingMode(SmoothingMode::Decoupled);
        env.setAttackMs(kAttackMs);
        env.setReleaseMs(kReleaseMs);
        const int n = input.getNumSamples();
        std::vector<float> gr(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            const float e = env.processSample(input.getSample(0, i));
            gr[static_cast<size_t>(i)] = Compressor::computeGainReductionDb(gainToDb(e), kThresholdDb, kRatio, 0.0f);
        }
        printTimeline("B4. For comparison: linear domain smoothing (the EnvelopeFollower envelope fed into the static curve)",
                      gr, stepStart, releaseStart);
        measureAndPrint(gr, stepStart, releaseStart, staticGr);
        grB4 = gr;
        std::printf("    → During release the envelope decays exponentially in the linear domain = a \"constant-rate fall\" in dB (%.1f dB/s);\n"
                    "      the GR falls in a straight line, hits 0 and stops; in the log domain the GR itself decays exponentially,\n"
                    "      with a long tail that sounds more natural.\n",
                    20.0 * std::log10(std::exp(1.0)) / (kReleaseMs * 0.001));
    }

    // ---------- Overlaid release curves ----------
    const Curve curves[] = {{'1', &grB1}, {'2', &grB2}, {'4', &grB4}};
    printReleaseChart(curves, 3, releaseStart, 0.368f * settledB1);
    std::printf("  1 = B1 decoupled (log domain), 2 = B2 branching, 4 = B4 linear domain, * = overlap;\n"
                "  '-' is 36.8%% of B1's steady-state GR (%.2f dB): B1 crosses it at about τR + τA ≈ 110 ms.\n",
                0.368f * settledB1);
    std::printf("  B1 and B2 decay exponentially (a long tail that slowly settles to 0); B4 falls in a straight line in dB and drops straight to 0 at about 150 ms.\n");

    return 0;
}
