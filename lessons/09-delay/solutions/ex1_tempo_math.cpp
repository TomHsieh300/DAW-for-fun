// Exercise 1 solution (numerical check of the calculation problems): BPM -> ms, feedback decay time
//
// The answers to the calculation problems are written in solutions/README.md; this program
// "runs" the same numbers once to verify them:
//   1. Delay::noteToMs() for various note lengths (including dotted notes and triplets).
//   2. ms -> samples (48 kHz).
//   3. With feedback g, the amplitude of the k-th echo = g^(k−1): fire an impulse into Delay and
//      read off the peak of every echo directly.
//   4. The time for the echoes to decay by 60 dB: T60 = D · 60 / (−20·log10 g) = 3D / (−log10 g),
//      and the reverse, "find g for a given T60": g = 10^(−3D / T60).
//   5. setFeedback clamps values >= 1 to 0.99 (a stability safeguard).
//
// Run: l09_ex1_solution (no arguments needed, no WAV output).

#include <dawlab/Common.h>
#include <dawlab/dsp/Delay.h>

#include <cmath>
#include <cstdio>

using dawlab::dsp::Delay;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

double t60Seconds(double delaySeconds, double g) { return 3.0 * delaySeconds / -std::log10(g); }

double feedbackForT60(double delaySeconds, double t60) { return std::pow(10.0, -3.0 * delaySeconds / t60); }

} // namespace

int main() {
    std::printf("=== Chapter 09, Exercise 1: tempo sync and feedback decay calculations ===\n\n");

    // ---- 1. BPM -> ms ----------------------------------------------------------
    struct Case {
        double bpm, beats, expectMs;
        const char* name;
    };
    const Case cases[] = {
        {120.0, 1.0, 500.0, "120 BPM quarter note"},
        {120.0, 0.5, 250.0, "120 BPM eighth note"},
        {120.0, 0.75, 375.0, "120 BPM dotted eighth"},
        {120.0, 1.0 / 3.0, 166.6667, "120 BPM eighth triplet"},
        {120.0, 0.25, 125.0, "120 BPM sixteenth note"},
        {95.0, 0.75, 473.6842, "95 BPM dotted eighth"},
        {140.0, 2.0 / 3.0, 285.7143, "140 BPM quarter triplet"},
        {90.0, 1.5, 1000.0, "90 BPM dotted quarter"},
        {174.0, 0.25, 86.2069, "174 BPM sixteenth note"},
    };
    std::printf("  %-24s  noteToMs  expected   samples@48k\n", "note");
    for (const auto& c : cases) {
        const float ms = Delay::noteToMs(c.bpm, c.beats);
        std::printf("  %-24s  %8.3f  %8.3f   %9.1f\n", c.name, ms, c.expectMs, ms * 0.001 * kSampleRate);
        DAWLAB_CHECK_NEAR(ms, c.expectMs, 1.0e-3);
    }
    DAWLAB_CHECK_NEAR(Delay::noteToMs(95.0, 0.75) * 0.001 * kSampleRate, 22736.84, 0.05); // not an integer -> fractional
    std::printf("\n");

    // ---- 2. Echo amplitudes with feedback 0.5 ---------------------------------------
    {
        const float delayMs = 375.0f;
        const float g = 0.5f;
        Delay delay;
        delay.setMaxDelayMs(1000.0f);
        delay.prepare(kSampleRate, kBlockSize, 1);
        delay.setDelayMs(delayMs);
        delay.setFeedback(g);
        delay.setMix(1.0f);
        delay.setLowpassHz(20000.0f); // turn off the feedback filters so only g matters
        delay.setHighpassHz(20.0f);
        delay.reset();

        dawlab::AudioBuffer ir = dawlab::makeImpulse(dawlab::secondsToSamples(4.0, kSampleRate), 1);
        dawlab::AudioBlock whole(ir);
        for (int start = 0; start < ir.getNumSamples(); start += kBlockSize)
            delay.process(whole.getSubBlock(start, std::min(kBlockSize, ir.getNumSamples() - start)));

        const int D = dawlab::msToSamples(delayMs, kSampleRate);
        std::printf("impulse -> Delay (%.0f ms, feedback %.1f, mix 1):\n", delayMs, g);
        std::printf("   echo k   position (sample)   amplitude   theory g^(k-1)   dB\n");
        for (int k = 1; k <= 8; ++k) {
            const float a = ir.getSample(0, k * D);
            const double expect = std::pow(g, k - 1);
            std::printf("   %5d   %16d   %.6f   %.6f   %+7.2f\n", k, k * D, a, expect, dawlab::gainToDb(a));
            DAWLAB_CHECK_NEAR(a, expect, 1.0e-4);
        }
        DAWLAB_CHECK_NEAR(ir.getSample(0, D - 1), 0.0, 1.0e-6); // integer delay: the sample before an echo is 0
        std::printf("  -> -6.02 dB per trip; a 60 dB decay takes 60/6.02 = 9.97 trips ~ 3.74 s\n\n");
    }

    // ---- 3. The T60 formula --------------------------------------------------------
    {
        const double t1 = t60Seconds(0.375, 0.5);
        const double t2 = t60Seconds(0.375, 0.7);
        const double t3 = t60Seconds(0.250, 0.9);
        const double g4 = feedbackForT60(0.5, 2.0);
        std::printf("T60 = 3D / (-log10 g):\n");
        std::printf("  D = 375 ms, g = 0.5 -> %.3f s\n", t1);
        std::printf("  D = 375 ms, g = 0.7 -> %.3f s\n", t2);
        std::printf("  D = 250 ms, g = 0.9 -> %.3f s\n", t3);
        std::printf("  Inverse: D = 500 ms, decay by 60 dB in 2 s -> g = 10^(-0.75) = %.4f\n\n", g4);
        DAWLAB_CHECK_NEAR(t1, 3.737, 0.001);
        DAWLAB_CHECK_NEAR(t2, 7.263, 0.001);
        DAWLAB_CHECK_NEAR(t3, 16.391, 0.001);
        DAWLAB_CHECK_NEAR(g4, 0.1778, 1.0e-4);
    }

    // ---- 4. Stability safeguard -----------------------------------------------------
    {
        Delay delay;
        delay.setFeedback(1.5f);
        std::printf("setFeedback(1.5) -> getFeedback() = %.2f (clamped to kMaxFeedback)\n", delay.getFeedback());
        DAWLAB_CHECK_NEAR(delay.getFeedback(), Delay::kMaxFeedback, 1.0e-6);
        delay.setFeedback(-0.5f);
        DAWLAB_CHECK_NEAR(delay.getFeedback(), 0.0, 1.0e-6);
    }

    return dawlab::test::summary("l09_ex1");
}
