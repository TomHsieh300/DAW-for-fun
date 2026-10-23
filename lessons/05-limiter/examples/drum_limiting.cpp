// Example 2: push a drum loop into the limiter -- "louder without clipping"
//
// What it shows:
//   Drums have a high peak but a modest RMS (large crest factor). After raising the input gain
//   by +6 and +12 dB and handing the signal to the limiter (ceiling -1 dBFS), the peak is held
//   at -1 dBFS while the RMS really does go up -- this is how a mastering limiter "adds
//   loudness". The control case is plain hard clipping (cutting off everything beyond +/-1).
//
// How to run: l05_drum_limiting (no arguments); optional argv[1] = path to your own WAV file
//
// Output files (in dawlab_output/):
//   l05_drums_dry.wav            the original drum loop
//   l05_drums_limited_6dB.wav    +6 dB -> limiter
//   l05_drums_limited_12dB.wav   +12 dB -> limiter
//   l05_drums_clipped_12dB.wav   +12 dB -> hard clip (control: audible distortion)
//
// What to listen for:
//   The limited versions are clearly louder than dry. In the +12 dB version the kick's "punch"
//   starts to dull (gain reduction flattens the transients), but there is no distortion; the
//   clipped version's kick / snare have an obvious gritty crackle.

#include <dawlab/Common.h>
#include <dawlab/dsp/Limiter.h>

#include <cmath>
#include <cstdio>
#include <string>

namespace {

constexpr int kBlockSize = 512;

template <typename Processor>
void processInBlocks(dawlab::AudioBuffer& buffer, Processor& processor) {
    dawlab::AudioBlock whole(buffer);
    for (int start = 0; start < buffer.getNumSamples(); start += kBlockSize) {
        const int len = std::min(kBlockSize, buffer.getNumSamples() - start);
        processor.process(whole.getSubBlock(start, len));
    }
}

float rmsDb(const dawlab::AudioBuffer& b) { return dawlab::gainToDb(dawlab::rms(b, 0)); }
float peakDb(const dawlab::AudioBuffer& b) { return dawlab::gainToDb(dawlab::peakAbs(b)); }

} // namespace

int main(int argc, char** argv) {
    double sampleRate = 48000.0;
    dawlab::AudioBuffer dry;
    if (argc > 1) {
        dawlab::WavData wav;
        std::string error;
        if (!dawlab::readWav(argv[1], wav, &error)) {
            std::printf("%s\n", error.c_str());
            return 1;
        }
        dry = wav.buffer;
        sampleRate = wav.sampleRate;
    } else {
        dry = dawlab::makeDrumLoop(sampleRate, 2, 120.0, 0.8f);
    }

    const float ceilingDb = -1.0f;
    const float lookaheadMs = 5.0f;
    const float releaseMs = 80.0f;

    std::printf("Drum loop: %d samples @ %.0f Hz, peak %.2f dBFS, RMS %.2f dBFS, crest factor %.2f dB\n\n",
                dry.getNumSamples(), sampleRate, peakDb(dry), rmsDb(dry), peakDb(dry) - rmsDb(dry));
    std::printf("Limiter: ceiling %.1f dBFS, lookahead %.1f ms, release %.0f ms\n\n", ceilingDb, lookaheadMs,
                releaseMs);
    std::printf("%-10s %12s %12s %12s %12s %12s\n", "input gain", "in peak", "out peak", "out RMS", "RMS gain",
                "max GR");

    bool ok = true;
    for (float gainDb : {0.0f, 6.0f, 12.0f}) {
        dawlab::AudioBuffer out = dry;
        dawlab::dsp::Limiter limiter;
        limiter.prepare(sampleRate, kBlockSize, out.getNumChannels());
        limiter.setCeilingDb(ceilingDb);
        limiter.setLookaheadMs(lookaheadMs);
        limiter.setReleaseMs(releaseMs);
        limiter.setInputGainDb(gainDb);
        limiter.reset(); // put the input gain in place immediately (no ramp at the start)

        float maxGr = 0.0f;
        dawlab::AudioBlock whole(out);
        for (int start = 0; start < out.getNumSamples(); start += kBlockSize) {
            const int len = std::min(kBlockSize, out.getNumSamples() - start);
            limiter.process(whole.getSubBlock(start, len));
            maxGr = std::max(maxGr, limiter.getGainReductionDb()); // this is how a UI gain reduction meter reads it
        }

        const float inPeak = peakDb(dry) + gainDb;
        std::printf("%+7.0f dB %10.2f dB %10.2f dB %10.2f dB %+10.2f dB %10.2f dB\n", gainDb, inPeak, peakDb(out),
                    rmsDb(out), rmsDb(out) - rmsDb(dry), maxGr);

        // Checking the peak alone is not enough: the safety clamp pulls any algorithm error back to the ceiling, so we also require that it never fired
        if (dawlab::peakAbs(out) > dawlab::dbToGain(ceilingDb) * (1.0f + 1.0e-6f) || dawlab::hasNanOrInf(out) ||
            limiter.getSafetyClipCount() != 0)
            ok = false;
        // Lookahead brings the gain down "exactly" far enough: the max GR should be max(0, input peak - ceiling), no more and no less.
        // (The largest peak has the smallest g; the L+1 window minima around it all equal that g, so their average is exactly g.)
        const float expectedGr = std::max(0.0f, inPeak - ceilingDb);
        if (std::abs(maxGr - expectedGr) > 0.05f) {
            std::printf("  [FAIL] max GR %.2f dB, expected %.2f dB\n", maxGr, expectedGr);
            ok = false;
        }

        if (gainDb == 0.0f)
            dawlab::writeWav(dawlab::outputPath("l05_drums_dry.wav"), dry, sampleRate);
        else
            dawlab::writeWav(dawlab::outputPath("l05_drums_limited_" + std::to_string(static_cast<int>(gainDb)) + "dB.wav"),
                             out, sampleRate);
    }

    // Control case: after +12 dB, hard clip straight to +/-1
    {
        dawlab::AudioBuffer clipped = dry;
        clipped.applyGain(dawlab::dbToGain(12.0f));
        for (int ch = 0; ch < clipped.getNumChannels(); ++ch) {
            float* d = clipped.getWritePointer(ch);
            for (int i = 0; i < clipped.getNumSamples(); ++i)
                d[i] = std::clamp(d[i], -1.0f, 1.0f);
        }
        std::printf("%-10s %10.2f dB %10.2f dB %10.2f dB %+10.2f dB %10s\n", "+12 dB clip", peakDb(dry) + 12.0f,
                    peakDb(clipped), rmsDb(clipped), rmsDb(clipped) - rmsDb(dry), "-");
        dawlab::writeWav(dawlab::outputPath("l05_drums_clipped_12dB.wav"), clipped, sampleRate);
    }

    std::printf("\nWrote dawlab_output/l05_drums_{dry,limited_6dB,limited_12dB,clipped_12dB}.wav\n");
    std::printf("%s\n", ok ? "All limited outputs are <= ceiling, the safety clamp never fired, and max GR = input peak - ceiling."
                          : "[FAIL] An output exceeded the ceiling, the safety clamp fired, or max GR is wrong!");
    return ok ? 0 : 1;
}
