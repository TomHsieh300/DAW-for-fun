// l00_block_processing: simulates an audio callback by cutting a whole signal into
// fixed-size blocks and processing them one block at a time.
//
// What it demonstrates:
//   - Real audio programs never "process the whole song at once". Instead, the audio driver
//     calls your callback every so often, hands you blockSize samples, and you must finish
//     processing them and hand them back before the deadline.
//   - latency = blockSize / sampleRate: prints a latency table for common block sizes at
//     three sample rates.
//   - AudioBlock::getSubBlock() carves out sub-blocks; all DSP processes in place, block by block.
//   - Peak / RMS of each block (this is the seed of a meter).
//   - Verifies that block-wise processing gives exactly the same result as processing the
//     whole signal at once (for processing with no internal state).
//
// How to run:
//   cd <build directory>
//   bin/Debug/l00_block_processing.exe
//
// Output files:
//   l00_block_input.wav       the original drum loop
//   l00_block_processed.wav   the result of applying a -6 dB gain block by block
//
// What you should hear:
//   The processed version is half as loud as the input (-6.02 dB), and you cannot hear any
//   block seams, because gain has no internal state, so block boundaries do not affect the result.

#include <dawlab/Common.h>

#include <cstdio>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

// Per-block measurements: peak and RMS (linear values)
struct BlockStats {
    float peak = 0.0f;
    float rms = 0.0f;
};

// This function simulates the "audio callback". In the real world it is called from the
// audio driver's thread, so it must not allocate, take locks, or do I/O. Here it only
// multiplies and measures.
BlockStats audioCallback(dawlab::AudioBlock block, float gain) noexcept {
    BlockStats stats;
    const int numChannels = block.getNumChannels();
    const int numSamples = block.getNumSamples();
    double sumSquares = 0.0;
    for (int ch = 0; ch < numChannels; ++ch) {
        float* data = block.getChannelPointer(ch);
        for (int i = 0; i < numSamples; ++i) {
            data[i] *= gain;                                   // process in place
            stats.peak = std::max(stats.peak, std::abs(data[i])); // measure the output
            sumSquares += double(data[i]) * data[i];
        }
    }
    const int count = numChannels * numSamples;
    stats.rms = count > 0 ? static_cast<float>(std::sqrt(sumSquares / count)) : 0.0f;
    return stats;
}

} // namespace

int main() {
    using namespace dawlab;

    // ---- 1. Latency table -------------------------------------------------
    std::printf("== Latency = blockSize / sampleRate (in ms; one way, excluding extra driver buffering) ==\n");
    const double sampleRates[] = {44100.0, 48000.0, 96000.0};
    const int blockSizes[] = {32, 64, 128, 256, 512, 1024, 2048};
    std::printf("%10s", "block");
    for (double sr : sampleRates)
        std::printf("  %9.0f Hz", sr);
    std::printf("\n");
    for (int bs : blockSizes) {
        std::printf("%10d", bs);
        for (double sr : sampleRates)
            std::printf("  %9.2f ms", samplesToMs(bs, sr));
        std::printf("\n");
    }
    std::printf("A round trip (one block in + one block out) is at least double: e.g. 512 @ 48 kHz ≈ %.2f ms.\n\n",
                2.0 * samplesToMs(kBlockSize, kSampleRate));

    // ---- 2. Setup (the prepare phase: allocating is allowed here) ---------
    AudioBuffer input = makeDrumLoop(kSampleRate);
    AudioBuffer processed = input; // make a copy; it will be processed in place below
    const int totalSamples = input.getNumSamples();
    const int numBlocks = (totalSamples + kBlockSize - 1) / kBlockSize; // the last block may be partial
    std::vector<BlockStats> stats(static_cast<size_t>(numBlocks));      // allocated up front so the callback never allocates
    const float gain = dbToGain(-6.0f);

    std::printf("== Simulated audio callback: %d samples, block size %d -> %d blocks (last block %d samples) ==\n",
                totalSamples, kBlockSize, numBlocks, totalSamples - (numBlocks - 1) * kBlockSize);

    // ---- 3. The "audio thread": process block by block -------------------
    AudioBlock whole(processed);
    for (int b = 0; b < numBlocks; ++b) {
        const int start = b * kBlockSize;
        const int length = std::min(kBlockSize, totalSamples - start);
        AudioBlock block = whole.getSubBlock(start, length);
        stats[static_cast<size_t>(b)] = audioCallback(block, gain);
    }

    // ---- 4. Print the meter readings of some blocks -----------------------
    std::printf("%8s %10s %10s %10s\n", "block", "time(ms)", "peak dBFS", "RMS dBFS");
    const int stride = std::max(1, numBlocks / 16);
    for (int b = 0; b < numBlocks; b += stride) {
        const BlockStats& s = stats[static_cast<size_t>(b)];
        std::printf("%8d %10.1f %10.2f %10.2f\n", b, samplesToMs(b * kBlockSize, kSampleRate), gainToDb(s.peak),
                    gainToDb(s.rms));
    }

    float maxBlockPeak = 0.0f;
    for (const BlockStats& s : stats)
        maxBlockPeak = std::max(maxBlockPeak, s.peak);

    // ---- 5. Verify: block-wise processing == whole-signal processing ------
    AudioBuffer reference = input;
    reference.applyGain(gain);
    float maxDiff = 0.0f;
    for (int ch = 0; ch < reference.getNumChannels(); ++ch)
        for (int i = 0; i < totalSamples; ++i)
            maxDiff = std::max(maxDiff, std::abs(reference.getSample(ch, i) - processed.getSample(ch, i)));

    std::printf("\nInput peak %.2f dBFS -> output peak %.2f dBFS (largest block peak %.2f dBFS)\n",
                gainToDb(peakAbs(input)), gainToDb(peakAbs(processed)), gainToDb(maxBlockPeak));
    std::printf("Max difference between block-wise and whole-signal processing = %g (gain has no state, so it should be 0)\n", maxDiff);

    writeWav(outputPath("l00_block_input.wav"), input, kSampleRate);
    writeWav(outputPath("l00_block_processed.wav"), processed, kSampleRate);
    std::printf("Wrote l00_block_input.wav and l00_block_processed.wav\n");
    return maxDiff == 0.0f ? 0 : 1;
}
