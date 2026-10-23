// Example 2: tempo-synced ping-pong delay
//
// What it demonstrates:
//   1. Converting BPM to milliseconds (Delay::noteToMs), including dotted notes and triplets.
//   2. Verifying the ping-pong signal path with an impulse: left -> right -> left -> right,
//      with each hop multiplied by the feedback.
//   3. Feeding a 120 BPM plucked arpeggio (makePluckSequence, eighth note = 250 ms) into a
//      ping-pong delay set to a dotted eighth note = 375 ms -- the most common delay setting
//      in pop / rock. The echoes land between the beats and create a rhythm that bounces
//      left and right.
//
// How to run: no arguments needed, just run l09_pingpong_tempo.
// Output files (dawlab_output/):
//   l09_pingpong_dry.wav   the original arpeggio
//   l09_pingpong_wet.wav   dotted-eighth ping-pong delay (feedback 0.45, mix 0.35, 150 Hz HP + 3.5 kHz LP in the feedback path)
//   l09_echo_wet.wav       the same arpeggio through a plain (non-ping-pong) eighth-note stereo echo, for comparison
// What you should hear: in the wet version the echoes alternate between the left and right
//             channels and get darker and darker (the lowpass in the feedback path).

#include <dawlab/Common.h>
#include <dawlab/dsp/Delay.h>

#include <cmath>
#include <cstdio>
#include <vector>

using dawlab::dsp::Delay;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr double kBpm = 120.0;

// Process block by block (mimics an audio callback)
void processInBlocks(Delay& delay, dawlab::AudioBuffer& buffer) {
    dawlab::AudioBlock whole(buffer);
    for (int start = 0; start < buffer.getNumSamples(); start += kBlockSize) {
        const int n = std::min(kBlockSize, buffer.getNumSamples() - start);
        delay.process(whole.getSubBlock(start, n));
    }
}

// Find the local peaks in one channel of a buffer that exceed a threshold (used to see where
// the echoes of an impulse response sit).
void printEchoes(const dawlab::AudioBuffer& b, int ch, const char* name, float threshold) {
    const float* d = b.getReadPointer(ch);
    std::printf("  %s: ", name);
    int count = 0;
    for (int i = 1; i + 1 < b.getNumSamples() && count < 6; ++i) {
        const float v = std::abs(d[i]);
        if (v > threshold && v >= std::abs(d[i - 1]) && v >= std::abs(d[i + 1])) {
            std::printf("[n=%d (%.0f ms) amp=%.4f] ", i, dawlab::samplesToMs(i, kSampleRate), d[i]);
            ++count;
        }
    }
    std::printf("\n");
}

} // namespace

int main() {
    std::printf("=== Chapter 09, Example 2: tempo-synced ping-pong delay ===\n\n");

    // ---- 1. BPM conversion table -----------------------------------------------
    std::printf("Note lengths at BPM = %.0f (ms = beats x 60000 / BPM):\n", kBpm);
    struct Note { const char* name; double beats; };
    const Note notes[] = {
        {"quarter note (1 beat)", 1.0},        {"eighth note", 0.5},            {"dotted eighth (0.75 beat)", 0.75},
        {"eighth triplet (1/3 beat)", 1.0 / 3.0}, {"sixteenth note", 0.25},     {"dotted sixteenth (0.375 beat)", 0.375},
        {"quarter triplet (2/3 beat)", 2.0 / 3.0}, {"half note (2 beats)", 2.0},
    };
    for (const auto& n : notes)
        std::printf("  %8.2f ms   %s\n", Delay::noteToMs(kBpm, n.beats), n.name); // numbers first so the columns line up
    std::printf("\n");

    // ---- 2. Verify the ping-pong path with an impulse ------------------------------
    {
        const float delayMs = Delay::noteToMs(kBpm, 0.75); // 375 ms = 18000 samples
        Delay delay;
        delay.setMaxDelayMs(1000.0f);
        delay.prepare(kSampleRate, kBlockSize, 2);
        delay.setDelayMs(delayMs);
        delay.setFeedback(0.5f);
        delay.setMix(1.0f);
        delay.setPingPong(true);
        delay.setLowpassHz(20000.0f); // disable filtering so the echoes stay clean impulses
        delay.setHighpassHz(20.0f);
        delay.reset();

        dawlab::AudioBuffer impulse(2, dawlab::secondsToSamples(2.0, kSampleRate));
        impulse.setSample(0, 0, 1.0f); // left channel only: ping-pong first mixes down to mono (0.5)
        processInBlocks(delay, impulse);
        std::printf("Echo positions of an impulse (left channel, amplitude 1) through a %.0f ms ping-pong, feedback 0.5:\n", delayMs);
        printEchoes(impulse, 0, "Left ", 0.01f);
        printEchoes(impulse, 1, "Right", 0.01f);
        std::printf("  -> Alternating left and right, %.0f ms apart; the first echo is 0.5 after the mono mix, then x 0.5 per hop.\n\n", delayMs);
    }

    // ---- 3. Musical material: plucked arpeggio + dotted-eighth ping-pong -------------
    {
        const double seqSeconds = 4.0;
        const double totalSeconds = 8.0; // leave 4 seconds for the delay tail to die away
        dawlab::AudioBuffer seq = dawlab::makePluckSequence(kSampleRate, seqSeconds, 0.4f);
        dawlab::AudioBuffer dry(2, dawlab::secondsToSamples(totalSeconds, kSampleRate));
        for (int ch = 0; ch < 2; ++ch)
            dry.copyFrom(ch, 0, seq, ch, 0, seq.getNumSamples());

        // (a) Dotted-eighth ping-pong
        dawlab::AudioBuffer pingpong = dry;
        {
            Delay delay;
            delay.setMaxDelayMs(2000.0f);
            delay.prepare(kSampleRate, kBlockSize, 2);
            delay.setDelayMs(Delay::noteToMs(kBpm, 0.75));
            delay.setFeedback(0.45f);
            delay.setMix(0.35f);
            delay.setPingPong(true);
            delay.setHighpassHz(150.0f);  // remove the low-frequency rumble from the echoes
            delay.setLowpassHz(3500.0f);  // a little darker on every trip around the loop: the tape-echo flavour
            delay.reset();
            processInBlocks(delay, pingpong);
        }

        // (b) A plain eighth-note stereo echo (two independent channels) for comparison
        dawlab::AudioBuffer echo = dry;
        {
            Delay delay;
            delay.prepare(kSampleRate, kBlockSize, 2);
            delay.setDelayMs(Delay::noteToMs(kBpm, 0.5));
            delay.setFeedback(0.35f);
            delay.setMix(0.3f);
            delay.setPingPong(false);
            delay.setLowpassHz(5000.0f);
            delay.reset();
            processInBlocks(delay, echo);
        }

        dawlab::writeWav(dawlab::outputPath("l09_pingpong_dry.wav"), dry, kSampleRate);
        dawlab::writeWav(dawlab::outputPath("l09_pingpong_wet.wav"), pingpong, kSampleRate);
        dawlab::writeWav(dawlab::outputPath("l09_echo_wet.wav"), echo, kSampleRate);

        // The tail: the arpeggio ends at 4 seconds, after which only the delay remains
        const int tailStart = dawlab::secondsToSamples(4.5, kSampleRate);
        const int tailLen = dawlab::secondsToSamples(0.5, kSampleRate);
        std::printf("Arpeggio + ping-pong: peak dry %.3f / wet %.3f; RMS of the 4.5-5.0 s tail: dry %.5f / wet %.5f\n",
                    dawlab::peakAbs(dry), dawlab::peakAbs(pingpong),
                    dawlab::rms(dry.getReadPointer(0) + tailStart, tailLen),
                    dawlab::rms(pingpong.getReadPointer(0) + tailStart, tailLen));
        std::printf("NaN/Inf check: %s\n", dawlab::hasNanOrInf(pingpong) ? "FAIL" : "OK");
        std::printf("Wrote dawlab_output/l09_pingpong_dry.wav, l09_pingpong_wet.wav, l09_echo_wet.wav\n");
    }
    return 0;
}
