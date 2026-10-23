// l00_wav_info: reads a WAV file, prints its information, then normalizes it to -1 dBFS
// and saves the result as a new file.
//
// What it demonstrates:
//   - How to use readWav / writeWav / outputPath from WavFile.h.
//   - The most basic "health check" of an audio file: channel count, sample rate, bit depth,
//     length, per-channel peak / RMS / crest factor, DC offset, and the number of clipped samples.
//   - Normalization: find the largest |sample| and work out how much to multiply by so that
//     the peak lands exactly on the target dBFS.
//
// How to run:
//   cd <build directory>
//   bin/Debug/l00_wav_info.exe                    no arguments: generates a plucked arpeggio as input
//   bin/Debug/l00_wav_info.exe path/to/your.wav   analyzes your own file (16/24/32-bit PCM or float)
//
// Output files:
//   l00_wav_info_input.wav        (only when run without arguments) the input the program generated
//   l00_wav_info_normalized.wav   the result of normalizing to -1 dBFS
//
// What you should hear:
//   The normalized version is louder (or quieter) than the original, but the waveform shape is
//   exactly the same -- normalization just multiplies by a constant.

#include <dawlab/Common.h>

#include <cstdio>
#include <string>

namespace {

constexpr float kTargetPeakDb = -1.0f;

// Scales the peak to targetPeakDb. Returns the gain applied (in dB); for pure silence it does nothing and returns 0.
float normalizeToPeakDb(dawlab::AudioBuffer& buffer, float targetPeakDb) {
    const float peak = dawlab::peakAbs(buffer);
    if (peak <= 0.0f)
        return 0.0f; // avoid dividing by 0
    const float gain = dawlab::dbToGain(targetPeakDb) / peak;
    buffer.applyGain(gain);
    return dawlab::gainToDb(gain);
}

// Samples with |x| >= 1.0 count as clipped (in an integer PCM file these are the samples that "hit the ceiling")
int countClippedSamples(const dawlab::AudioBuffer& buffer) {
    int count = 0;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (std::abs(buffer.getSample(ch, i)) >= 1.0f)
                ++count;
    return count;
}

void printChannelStats(const dawlab::AudioBuffer& buffer) {
    using namespace dawlab;
    std::printf("  %-8s %10s %10s %10s %12s\n", "channel", "peak dBFS", "RMS dBFS", "crest dB", "DC offset");
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        const float* x = buffer.getReadPointer(ch);
        const int n = buffer.getNumSamples();
        const float peak = peakAbs(x, n);
        const float r = rms(x, n);
        double sum = 0.0;
        for (int i = 0; i < n; ++i)
            sum += x[i];
        const double dc = n > 0 ? sum / n : 0.0;
        std::printf("  %-8d %10.2f %10.2f %10.2f %12.6f\n", ch, gainToDb(peak), gainToDb(r),
                    gainToDb(peak) - gainToDb(r), dc);
    }
}

} // namespace

int main(int argc, char** argv) {
    using namespace dawlab;

    WavData wav;
    std::string source;
    if (argc >= 2) {
        source = argv[1];
        std::string error;
        if (!readWav(source, wav, &error)) {
            std::printf("Read failed: %s\n", error.c_str());
            return 1;
        }
    } else {
        // No file given: generate one, write it out and read it back, which also demonstrates the readWav flow
        source = outputPath("l00_wav_info_input.wav");
        AudioBuffer generated = makePluckSequence(48000.0, 4.0, 0.3f);
        writeWav(source, generated, 48000.0, 24);
        std::string error;
        if (!readWav(source, wav, &error)) {
            std::printf("Read failed: %s\n", error.c_str());
            return 1;
        }
    }

    const AudioBuffer& buffer = wav.buffer;
    const double seconds = buffer.getNumSamples() / wav.sampleRate;
    std::printf("File: %s\n", source.c_str());
    std::printf("  channels    = %d\n", buffer.getNumChannels());
    std::printf("  sample rate = %.0f Hz\n", wav.sampleRate);
    std::printf("  bit depth   = %d bit\n", wav.bitDepth);
    std::printf("  frames      = %d (%.3f s)\n", buffer.getNumSamples(), seconds);
    std::printf("  memory      = %.1f KB (float, %d ch × %d samples × 4 bytes)\n",
                4.0 * buffer.getNumChannels() * buffer.getNumSamples() / 1024.0, buffer.getNumChannels(),
                buffer.getNumSamples());
    std::printf("  clipped     = %d samples (|x| >= 1.0)\n", countClippedSamples(buffer));
    if (hasNanOrInf(buffer))
        std::printf("  Warning: contains NaN or Inf!\n");
    std::printf("Before processing:\n");
    printChannelStats(buffer);

    // Normalize to -1 dBFS: every channel is multiplied by the same gain so the left/right balance does not change
    AudioBuffer normalized = buffer;
    const float appliedDb = normalizeToPeakDb(normalized, kTargetPeakDb);
    std::printf("Normalize to %.1f dBFS: applied %+.2f dB (× %.4f)\n", kTargetPeakDb, appliedDb, dbToGain(appliedDb));
    std::printf("After processing:\n");
    printChannelStats(normalized);
    std::printf("  overall peak = %.2f dBFS (target %.1f)\n", gainToDb(peakAbs(normalized)), kTargetPeakDb);

    const std::string outPath = outputPath("l00_wav_info_normalized.wav");
    // Write as 32-bit float: even if someone adds more gain later, nothing gets truncated
    if (!writeWav(outPath, normalized, wav.sampleRate, 32)) {
        std::printf("Write failed: %s\n", outPath.c_str());
        return 1;
    }
    std::printf("Wrote %s\n", outPath.c_str());
    return 0;
}
