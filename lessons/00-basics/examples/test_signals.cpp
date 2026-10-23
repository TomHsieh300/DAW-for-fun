// l00_test_signals: generates every kind of test signal provided by common/Signals.h,
// writes each one out as a WAV file, and prints each signal's length, peak, RMS and crest factor.
//
// What it demonstrates:
//   - What the signals that feed the DSP throughout this course look like (every later
//     chapter uses them).
//   - How the three numbers peak / RMS / crest factor are computed, and how they differ
//     between waveforms.
//
// How to run:
//   cd <build directory>
//   bin/Debug/l00_test_signals.exe        (or let ctest run it)
//
// Output files (in <build directory>/dawlab_output/):
//   l00_sine_440.wav        440 Hz sine wave, -6 dBFS (amplitude 0.5)
//   l00_log_sweep.wav       20 Hz -> 20 kHz logarithmic sweep
//   l00_white_noise.wav     white noise (left and right channels uncorrelated)
//   l00_impulse.wav         a single impulse (one sample = 1.0, the rest 0)
//   l00_tone_bursts.wav     three 1 kHz sine segments at -30 / -10 / -30 dBFS
//   l00_drum_loop.wav       synthesized drum loop (kick / snare / hi-hat)
//   l00_pluck_110.wav       Karplus-Strong plucked note at 110 Hz
//   l00_pluck_sequence.wav  plucked arpeggio
//
// What you should see:
//   Open these files in Audacity and switch to the spectrogram view: the sine is a horizontal
//   line, the sweep is an upward-curving line, white noise is a uniform wash of noise, and
//   the impulse is a vertical line (all frequencies appear at once).

#include <dawlab/Common.h>

#include <cstdio>
#include <string>

namespace {

constexpr double kSampleRate = 48000.0;

// Prints a buffer's basic statistics (length, peak, RMS, crest factor) and writes it as a WAV file
void describeAndWrite(const char* name, const dawlab::AudioBuffer& buffer, const char* fileName) {
    using namespace dawlab;
    const float peak = peakAbs(buffer);
    const float rmsValue = rms(buffer, 0); // look at channel 0 only (the left channel)
    const double seconds = buffer.getNumSamples() / kSampleRate;

    // crest factor = peak / RMS; in dB that is peakDb - rmsDb
    const float peakDb = gainToDb(peak);
    const float rmsDb = gainToDb(rmsValue);
    const float crestDb = peakDb - rmsDb;

    std::printf("%-16s %d ch  %7d samples (%.2f s)  peak %6.2f dBFS  RMS %6.2f dBFS  crest %5.2f dB\n", name,
                buffer.getNumChannels(), buffer.getNumSamples(), seconds, peakDb, rmsDb, crestDb);

    std::string error;
    if (!writeWav(outputPath(fileName), buffer, kSampleRate, 24, &error))
        std::printf("  Write failed: %s\n", error.c_str());
}

} // namespace

int main() {
    using namespace dawlab;

    std::printf("sample rate = %.0f Hz, output directory = dawlab_output/\n\n", kSampleRate);

    // 1. Sine wave: the simplest signal, with a single frequency. Amplitude 0.5 = -6.02 dBFS.
    describeAndWrite("sine 440 Hz", makeSine(440.0, kSampleRate, 2.0, 0.5f), "l00_sine_440.wav");

    // 2. Log sweep: the frequency rises exponentially over time; used to "hear" the frequency response of filters and EQs.
    describeAndWrite("log sweep", makeLogSweep(20.0, 20000.0, kSampleRate, 4.0, 0.5f), "l00_log_sweep.wav");

    // 3. White noise: equal energy at all frequencies. Uniformly distributed (xorshift), amplitude 0.25.
    describeAndWrite("white noise", makeWhiteNoise(kSampleRate, 2.0, 0.25f), "l00_white_noise.wav");

    // 4. Impulse: one sample is 1.0 and all the others are 0. Fed into any linear system, the output is its impulse response.
    describeAndWrite("impulse", makeImpulse(secondsToSamples(0.5, kSampleRate), 1, 100), "l00_impulse.wav");

    // 5. Level steps: -30 -> -10 -> -30 dBFS, 0.5 s each. Ideal for watching a compressor's attack / release.
    describeAndWrite("tone bursts", makeToneBursts(1000.0, kSampleRate, {-30.0f, -10.0f, -30.0f}, 0.5),
                     "l00_tone_bursts.wav");

    // 6. Drum loop: very dynamic (high crest factor); good for compressors, limiters and saturation.
    describeAndWrite("drum loop", makeDrumLoop(kSampleRate), "l00_drum_loop.wav");

    // 7. Pluck: rich in harmonics, with the highs decaying first; good for EQ, saturation and distortion.
    describeAndWrite("pluck 110 Hz", makePluck(110.0, kSampleRate, 2.0, 0.5f), "l00_pluck_110.wav");

    // 8. Plucked arpeggio: more "musical" material.
    describeAndWrite("pluck sequence", makePluckSequence(kSampleRate), "l00_pluck_sequence.wav");

    std::printf("\nObservation: a sine wave has a crest factor of 3.01 dB (peak/RMS = √2), "
                "uniformly distributed white noise about 4.77 dB (√3), and the drum loop is far higher -- that is what \"highly dynamic\" means.\n");
    return 0;
}
