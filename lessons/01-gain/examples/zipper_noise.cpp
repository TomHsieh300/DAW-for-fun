// Example 1: zipper noise and clicks -- changing gain instantly vs. smoothly
//
// What it demonstrates:
//   (A) A single jump: a 1 kHz sine plays until about 1.0 s, when the gain jumps from 0 dB to -20 dB.
//       It compares an "instant jump" with a "50 ms linear ramp", and uses the maximum first difference to quantify the click.
//   (B) Continuous fader movement (zipper): simulates a UI that sends a new parameter once per block (512 samples, about 10.7 ms),
//       lowering the gain by 1 dB per block from 0 dB down to -40 dB. It compares "a staircase jump every block" with
//       "SmoothedValue smoothing", and uses a Goertzel filter to measure the sidebands next to 1 kHz (the noise produced by the staircase modulation).
//
// How to run:
//   cd <build dir> && bin/Debug/l01_zipper_noise.exe
//
// Output files (in <build dir>/dawlab_output/):
//   l01_step_instant.wav    single jump, instant         -> a clear "click" at about 1.0 s
//   l01_step_smoothed.wav   single jump, 50 ms ramp      -> you only hear the level drop, no click
//   l01_zipper_stepped.wav  fader movement, one step per block -> a zipper-like "zzzzz" noise
//   l01_zipper_smoothed.wav fader movement, smoothed     -> a clean fade out
//
// What you should hear: the instant-jump file has a click / zipper sound when the gain changes; the smoothed version only changes level.

#include <dawlab/Common.h>
#include <dawlab/dsp/Gain.h>

#include <cstdio>
#include <vector>

using namespace dawlab;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr double kToneHz = 1000.0;
constexpr float kAmplitude = 0.5f; // -6.02 dBFS

// Maximum of the first difference, max |y[n] - y[n-1]|: for a pure sine the theoretical limit is A*2*pi*f/fs;
// exceeding that value means the waveform has an "unwanted steep slope" = a click
float maxFirstDifference(const AudioBuffer& b, int channel) {
    const float* x = b.getReadPointer(channel);
    float m = 0.0f;
    for (int i = 1; i < b.getNumSamples(); ++i)
        m = std::max(m, std::abs(x[i] - x[i - 1]));
    return m;
}

// Process block by block with a Gain (simulating the audio callback); onBlock(blockIndex, gain) is called before each block starts
template <typename OnBlock>
AudioBuffer renderWithGain(const AudioBuffer& input, double rampSeconds, OnBlock&& onBlock) {
    AudioBuffer out = input;
    dsp::Gain gain;
    gain.setRampSeconds(rampSeconds);
    gain.prepare(kSampleRate, kBlockSize, out.getNumChannels());
    AudioBlock all(out);
    const int total = out.getNumSamples();
    for (int start = 0, blockIndex = 0; start < total; start += kBlockSize, ++blockIndex) {
        const int len = std::min(kBlockSize, total - start);
        onBlock(blockIndex, gain);
        gain.process(all.getSubBlock(start, len));
    }
    return out;
}

double sidebandDb(const AudioBuffer& b, int start, int n, double freq) {
    const double carrier = goertzelAmplitude(b.getReadPointer(0) + start, n, kToneHz, kSampleRate);
    const double side = goertzelAmplitude(b.getReadPointer(0) + start, n, freq, kSampleRate);
    return gainToDb(side / carrier);
}

} // namespace

int main() {
    std::printf("=== Chapter 01 Example 1: zipper noise and clicks ===\n\n");

    const AudioBuffer tone = makeSine(kToneHz, kSampleRate, 2.0, kAmplitude, 1);
    const float naturalDiff = maxFirstDifference(tone, 0);
    const double theoreticalDiff = kAmplitude * kTwoPi * kToneHz / kSampleRate;
    std::printf("Input: %.0f Hz sine, amplitude %.2f (%.2f dBFS)\n", kToneHz, kAmplitude, gainToDb(kAmplitude));
    std::printf("Max first difference of the pure sine: measured %.4f, theory A*2*pi*f/fs = %.4f\n\n", naturalDiff, theoreticalDiff);

    // ---- (A) Single jump: 0 dB -> -20 dB at about 1.0 s -----------------------
    // Note: the jump position cannot be chosen arbitrarily. Block 93 starts at sample 47616 = 992 whole 1 kHz periods,
    // which lands exactly on a zero crossing; the signal value is 0, so 0*1 and 0*0.1 make no difference -- there would be no click!
    // (This is the principle behind a DAW's "zero-crossing edit".) So we pick block 94: it starts at sample 48128,
    // phase = 2*pi*(48128 mod 48)/48 = 240 degrees, the signal is around -0.43, and the jump is clearly audible.
    const int jumpBlock = secondsToSamples(1.0, kSampleRate) / kBlockSize + 1; // the 94th block
    const int jumpSample = jumpBlock * kBlockSize;
    auto jumpAt = [jumpBlock](int blockIndex, dsp::Gain& g) {
        if (blockIndex == jumpBlock)
            g.setGainDb(-20.0f);
    };
    AudioBuffer instant = renderWithGain(tone, 0.0, jumpAt);
    AudioBuffer smoothed = renderWithGain(tone, 0.05, jumpAt);

    const float diffInstant = maxFirstDifference(instant, 0);
    const float diffSmoothed = maxFirstDifference(smoothed, 0);
    std::printf("(A) Single jump 0 dB -> -20 dB (at sample %d = %.4f s, original signal value there %.3f)\n", jumpSample,
                jumpSample / kSampleRate, tone.getSample(0, jumpSample));
    std::printf("    Instant jump: sample before the jump %.4f -> after the jump %.4f, first difference %.4f\n",
                instant.getSample(0, jumpSample - 1), instant.getSample(0, jumpSample),
                std::abs(instant.getSample(0, jumpSample) - instant.getSample(0, jumpSample - 1)));
    std::printf("    Max first difference: instant jump %.4f (%.1fx the natural value of the sine)\n", diffInstant,
                diffInstant / naturalDiff);
    std::printf("    Max first difference: 50 ms ramp %.4f (%.2fx, almost equal to the natural value)\n", diffSmoothed,
                diffSmoothed / naturalDiff);
    std::printf("    Steady-state amplitude after the ramp: %.4f (expected %.4f)\n\n", peakAbs(smoothed.getReadPointer(0) + 70000, 20000),
                kAmplitude * dbToGain(-20.0f));
    writeWav(outputPath("l01_step_instant.wav"), instant, kSampleRate);
    writeWav(outputPath("l01_step_smoothed.wav"), smoothed, kSampleRate);

    // ---- (B) Continuous fader movement: -1 dB per block, 0 -> -40 dB ----------
    const int sweepStartBlock = secondsToSamples(0.5, kSampleRate) / kBlockSize;
    constexpr int kSteps = 40;
    auto sweep = [sweepStartBlock](int blockIndex, dsp::Gain& g) {
        const int step = blockIndex - sweepStartBlock;
        if (step >= 0 && step <= kSteps)
            g.setGainDb(-static_cast<float>(step));
    };
    AudioBuffer stepped = renderWithGain(tone, 0.0, sweep);
    AudioBuffer zipperSmoothed = renderWithGain(tone, 0.05, sweep);

    const int winStart = sweepStartBlock * kBlockSize;
    const int winLen = kSteps * kBlockSize;
    const double blockRate = kSampleRate / kBlockSize; // 93.75 Hz: parameter update rate
    std::printf("(B) Fader movement: -1 dB per block (%.2f ms, update rate %.2f Hz), %d steps in total\n",
                1000.0 * kBlockSize / kSampleRate, blockRate, kSteps);
    std::printf("    Max first difference: stepped %.4f (%.2fx the natural value), smoothed %.4f (%.2fx)\n",
                maxFirstDifference(stepped, 0), maxFirstDifference(stepped, 0) / naturalDiff,
                maxFirstDifference(zipperSmoothed, 0), maxFirstDifference(zipperSmoothed, 0) / naturalDiff);
    std::printf("    Sidebands next to 1 kHz (relative to the 1 kHz carrier, measured over the %.2f s of fader movement):\n",
                winLen / kSampleRate);
    std::printf("    %-22s %12s %12s\n", "Frequency", "Stepped (dB)", "Smoothed (dB)");
    const double probes[] = {kToneHz + blockRate, kToneHz + 2 * blockRate, kToneHz + 4 * blockRate,
                             kToneHz + 8 * blockRate, kToneHz + 16 * blockRate};
    for (double f : probes)
        std::printf("    %7.2f Hz (1k+%2.0f*fb) %12.1f %12.1f\n", f, (f - kToneHz) / blockRate,
                    sidebandDb(stepped, winStart, winLen, f), sidebandDb(zipperSmoothed, winStart, winLen, f));
    std::printf("    (Lower sidebands are cleaner; the stepped sidebands decay slowly as 1/k, which is the \"zipper sound\")\n\n");
    writeWav(outputPath("l01_zipper_stepped.wav"), stepped, kSampleRate);
    writeWav(outputPath("l01_zipper_smoothed.wav"), zipperSmoothed, kSampleRate);

    // ---- Behavior of SmoothedValue itself --------------------------------------
    dsp::SmoothedValue sv;
    sv.reset(kSampleRate, 0.05);
    sv.setCurrentAndTargetValue(1.0f);
    sv.setTargetValue(0.0f);
    int count = 0;
    while (sv.isSmoothing()) {
        sv.getNextValue();
        ++count;
    }
    std::printf("SmoothedValue: the 50 ms ramp at %.0f Hz took %d samples (expected %d), final value %.3f\n", kSampleRate,
                count, msToSamples(50.0, kSampleRate), sv.getCurrentValue());

    std::printf("\nOutput: dawlab_output/l01_step_instant.wav, l01_step_smoothed.wav, "
                "l01_zipper_stepped.wav, l01_zipper_smoothed.wav\n");
    return 0;
}
