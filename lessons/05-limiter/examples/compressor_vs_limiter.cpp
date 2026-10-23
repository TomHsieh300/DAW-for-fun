// Example 1: overshoot comparison, fast-attack compressor vs lookahead limiter
//
// What it shows:
//   The same "sudden loud" signal (1 kHz, -30 dBFS -> -6 dBFS -> -30 dBFS) is fed to the
//   Chapter 04 Compressor (threshold -20 dB, ratio 100:1, attack 1 ms) and to this chapter's
//   Limiter (ceiling -20 dB, lookahead 2 ms).
//   The compressor's envelope follower has to "see" the signal get louder before it starts
//   reducing gain, so for the first few milliseconds after the jump the signal runs well past
//   the threshold (overshoot). The limiter delays the audio by 2 ms, so its gain can come down
//   ahead of time, and the output peak is always <= the ceiling.
//
// How to run: l05_compressor_vs_limiter (no arguments)
//
// Output files (in dawlab_output/):
//   l05_cmp_vs_lim_input.wav       the original tone bursts
//   l05_cmp_vs_lim_compressor.wav  Compressor output
//   l05_cmp_vs_lim_limiter.wav     Limiter output (2 ms later than the input)
//
// What to listen for / look at:
//   The compressor version has a short "pop" at 0.4 s (the few milliseconds of overshoot are louder);
//   the limiter version changes level smoothly, and the peaks in the table never exceed -20 dBFS.

#include <dawlab/Common.h>
#include <dawlab/dsp/Compressor.h>
#include <dawlab/dsp/Limiter.h>

#include <cstdio>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

// Simulate an audio callback: process one block at a time
template <typename Processor>
void processInBlocks(dawlab::AudioBuffer& buffer, Processor& processor) {
    dawlab::AudioBlock whole(buffer);
    for (int start = 0; start < buffer.getNumSamples(); start += kBlockSize) {
        const int len = std::min(kBlockSize, buffer.getNumSamples() - start);
        processor.process(whole.getSubBlock(start, len));
    }
}

// The largest |sample| (in dBFS) within [start, start + length)
float peakDbIn(const dawlab::AudioBuffer& b, int start, int length) {
    const int n = std::min(length, std::max(0, b.getNumSamples() - start));
    return dawlab::gainToDb(dawlab::peakAbs(b.getReadPointer(0) + start, n));
}

} // namespace

int main() {
    const float thresholdDb = -20.0f;
    const double segmentSeconds = 0.4;
    dawlab::AudioBuffer input = dawlab::makeToneBursts(1000.0, kSampleRate, {-30.0f, -6.0f, -30.0f}, segmentSeconds);
    const int stepIndex = dawlab::secondsToSamples(segmentSeconds, kSampleRate); // the signal suddenly gets 24 dB louder here

    // ---- Compressor: set up to imitate a limiter as closely as possible (ratio 100:1, attack 1 ms, hard knee) ----
    dawlab::AudioBuffer compressed = input;
    dawlab::dsp::Compressor compressor;
    compressor.prepare(kSampleRate, kBlockSize, 2);
    compressor.setThresholdDb(thresholdDb);
    compressor.setRatio(100.0f);
    compressor.setKneeDb(0.0f);
    compressor.setAttackMs(1.0f);
    compressor.setReleaseMs(100.0f);
    compressor.setMakeupGainDb(0.0f);
    compressor.setStereoLink(true);
    processInBlocks(compressed, compressor);

    // ---- Limiter: ceiling set to the same -20 dB, lookahead 2 ms ----
    dawlab::AudioBuffer limited = input;
    dawlab::dsp::Limiter limiter;
    limiter.prepare(kSampleRate, kBlockSize, 2);
    limiter.setCeilingDb(thresholdDb);
    limiter.setLookaheadMs(2.0f);
    limiter.setReleaseMs(100.0f);
    processInBlocks(limited, limiter);
    const int latency = limiter.getLatencySamples();

    std::printf("Input: 1 kHz tone bursts  -30 -> -6 -> -30 dBFS (%.1f s per segment), gets louder at sample %d\n",
                segmentSeconds, stepIndex);
    std::printf("Compressor: threshold %.0f dB, ratio 100:1, attack 1 ms   |   Limiter: ceiling %.0f dB, lookahead 2 ms (= %d samples)\n\n",
                thresholdDb, thresholdDb, latency);

    // Output peak for each 1 ms after the jump (the limiter's output is `latency` samples late, so align when comparing)
    const int ms = dawlab::msToSamples(1.0, kSampleRate);
    std::printf("%-16s %14s %14s %14s\n", "Time after jump", "Input (dBFS)", "Compressor", "Limiter");
    for (int k : {0, 1, 2, 3, 5, 8, 12, 20, 50}) {
        const int start = stepIndex + k * ms;
        std::printf("%4d ms ~ %-7d %12.2f   %12.2f   %12.2f\n", k, k + 1, peakDbIn(input, start, ms),
                    peakDbIn(compressed, start, ms), peakDbIn(limited, start + latency, ms));
    }

    const float cmpOvershoot = peakDbIn(compressed, stepIndex, 20 * ms) - thresholdDb;
    const float limOvershoot = peakDbIn(limited, stepIndex + latency, 20 * ms) - thresholdDb;
    const float cmpOverall = dawlab::gainToDb(dawlab::peakAbs(compressed)) - thresholdDb;
    const float limOverall = dawlab::gainToDb(dawlab::peakAbs(limited)) - thresholdDb;
    std::printf("\nOvershoot relative to threshold/ceiling within 20 ms after the jump: Compressor %+.2f dB, Limiter %+.2f dB\n",
                cmpOvershoot, limOvershoot);
    std::printf("Peak of the whole output relative to threshold/ceiling:           Compressor %+.2f dB, Limiter %+.2f dB\n",
                cmpOverall, limOverall);
    std::printf("Limiter safety clamp trigger count: %llu (should be 0)\n",
                static_cast<unsigned long long>(limiter.getSafetyClipCount()));

    dawlab::writeWav(dawlab::outputPath("l05_cmp_vs_lim_input.wav"), input, kSampleRate);
    dawlab::writeWav(dawlab::outputPath("l05_cmp_vs_lim_compressor.wav"), compressed, kSampleRate);
    dawlab::writeWav(dawlab::outputPath("l05_cmp_vs_lim_limiter.wav"), limited, kSampleRate);
    std::printf("\nWrote dawlab_output/l05_cmp_vs_lim_{input,compressor,limiter}.wav\n");

    // The limiter's guarantee is hard: exceeding the ceiling counts as a failure. The safety clamp
    // must also never have fired -- it clamps any algorithm error back to the ceiling, so checking
    // only the peak would miss bugs.
    return (limOverall <= 1.0e-4f && limiter.getSafetyClipCount() == 0) ? 0 : 1;
}
