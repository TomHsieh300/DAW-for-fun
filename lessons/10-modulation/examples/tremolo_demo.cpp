// Example 5: Tremolo (amplitude modulation) and autopan
//
// What it demonstrates:
//   1. A plucked arpeggio through the tremolo: sine at 5 Hz, square at 4 Hz (smoothed), and an
//      autopan WAV at 0.5 Hz.
//   2. Verifying the AM spectrum: multiply a 1 kHz sine by the gain g(t) = (1 − depth/2) + (depth/2)·sin(2πrt);
//      the spectrum is 1000 Hz (amplitude x(1 − depth/2)) plus two sidebands at 1000 ± r (amplitude
//      x depth/4 each). Goertzel measures the amplitudes at 990 / 1000 / 1010 Hz for comparison with theory.
//   3. A contrast: ring modulation (multiplying directly by the LFO, with no DC offset) eats the
//      carrier (1000 Hz) and leaves only the sidebands -- a preview of Exercise 4.
//   4. Autopan: the left and right channels are in antiphase, and gL + gR = 2 − depth is constant;
//      prints ASCII bars of the left/right RMS every 0.25 s.
//
// How to run: l10_tremolo (no arguments)
//
// Output files (dawlab_output/):
//   l10_tremolo_dry.wav       plucked arpeggio
//   l10_tremolo_sine.wav      sine 5 Hz, depth 0.8
//   l10_tremolo_square.wav    square 4 Hz, depth 1, 1 ms smoothing (staccato / "helicopter" feel)
//   l10_tremolo_autopan.wav   autopan sine 0.5 Hz, depth 1 (the image swings left and right once every 2 s)
//
// What you should hear: the sine version's volume rises and falls like waves; the square version
// is a hard "da-da-da" chop; the autopan version keeps the same volume but moves left and right.

#include <dawlab/Common.h>
#include <dawlab/dsp/Tremolo.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

using dawlab::dsp::LfoShape;
using dawlab::dsp::Tremolo;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

template <typename Effect>
void processInBlocks(Effect& fx, dawlab::AudioBuffer& buffer) {
    dawlab::AudioBlock whole(buffer);
    for (int start = 0; start < buffer.getNumSamples(); start += kBlockSize) {
        const int n = std::min(kBlockSize, buffer.getNumSamples() - start);
        fx.process(whole.getSubBlock(start, n));
    }
}

Tremolo makeTremolo(int channels, float rate, float depth, LfoShape shape, bool autoPan) {
    Tremolo t;
    t.prepare(kSampleRate, kBlockSize, channels);
    t.setRateHz(rate);
    t.setDepth(depth);
    t.setShape(shape);
    t.setAutoPan(autoPan);
    t.setLfoPhase(0.25f); // the sine starts at +1: loudest on the first beat
    t.reset();
    return t;
}

void writeProcessed(const dawlab::AudioBuffer& input, Tremolo& t, const char* filename) {
    dawlab::AudioBuffer out = input;
    processInBlocks(t, out);
    dawlab::writeWav(dawlab::outputPath(filename), out, kSampleRate);
    std::printf("  %-26s peak %.3f\n", filename, dawlab::peakAbs(out));
}

} // namespace

int main() {
    std::printf("=== Chapter 10 Example 5: Tremolo / autopan ===\n\n");

    // ---- 1. Listening ----------------------------------------------------------
    {
        dawlab::AudioBuffer pluck = dawlab::makePluckSequence(kSampleRate, 4.0, 0.4f, 2);
        dawlab::writeWav(dawlab::outputPath("l10_tremolo_dry.wav"), pluck, kSampleRate);
        std::printf("Output WAVs:\n  %-26s peak %.3f\n", "l10_tremolo_dry.wav", dawlab::peakAbs(pluck));
        Tremolo a = makeTremolo(2, 5.0f, 0.8f, LfoShape::Sine, false);
        writeProcessed(pluck, a, "l10_tremolo_sine.wav");
        Tremolo b = makeTremolo(2, 4.0f, 1.0f, LfoShape::Square, false);
        b.setSmoothingMs(1.0f);
        writeProcessed(pluck, b, "l10_tremolo_square.wav");
        Tremolo c = makeTremolo(2, 0.5f, 1.0f, LfoShape::Sine, true);
        writeProcessed(pluck, c, "l10_tremolo_autopan.wav");
        std::printf("\n");
    }

    // ---- 2. AM spectrum --------------------------------------------------------
    {
        const double fc = 1000.0, r = 10.0;
        const float A = 0.5f, depth = 1.0f;
        dawlab::AudioBuffer tone = dawlab::makeSine(fc, kSampleRate, 3.0, A, 1);
        Tremolo t = makeTremolo(1, static_cast<float>(r), depth, LfoShape::Sine, false);
        processInBlocks(t, tone);
        const int skip = dawlab::secondsToSamples(0.5, kSampleRate);
        const float* x = tone.getReadPointer(0) + skip;
        const int n = tone.getNumSamples() - skip;
        std::printf("AM spectrum: %.0f Hz sine (amplitude %.2f) x sine tremolo %.0f Hz, depth %.1f\n", fc, A, r, depth);
        std::printf("   freq(Hz)   measured   theory\n");
        std::printf("   %8.0f   %8.4f   %8.4f   (carrier x(1 − depth/2))\n", fc,
                    dawlab::goertzelAmplitude(x, n, fc, kSampleRate), A * (1.0 - depth / 2.0));
        for (double f : {fc - r, fc + r})
            std::printf("   %8.0f   %8.4f   %8.4f   (sideband x depth/4)\n", f,
                        dawlab::goertzelAmplitude(x, n, f, kSampleRate), A * depth / 4.0);
        std::printf("   %8.0f   %8.4f   %8.4f   (the LFO itself does not appear in the output)\n", r,
                    dawlab::goertzelAmplitude(x, n, r, kSampleRate), 0.0);
        std::printf("  -> tremolo = AM: the spectrum is the carrier plus two sidebands. When the LFO is too fast (> 20 Hz) the sidebands separate far enough to be heard as new tones.\n\n");

        // ---- 3. Contrast: ring modulation -------------------------------------
        dawlab::AudioBuffer ring = dawlab::makeSine(fc, kSampleRate, 3.0, A, 1);
        float* d = ring.getWritePointer(0);
        for (int i = 0; i < ring.getNumSamples(); ++i)
            d[i] *= static_cast<float>(std::sin(dawlab::kTwoPi * r * i / kSampleRate));
        const float* y = ring.getReadPointer(0) + skip;
        std::printf("Contrast with ring modulation (multiplying directly by sin(2π·%.0f·t), no DC offset):\n", r);
        std::printf("   %8.0f Hz amplitude %.4f (carrier gone)   %8.0f / %8.0f Hz amplitude %.4f / %.4f (theory A/2 = %.4f)\n", fc,
                    dawlab::goertzelAmplitude(y, n, fc, kSampleRate), fc - r, fc + r,
                    dawlab::goertzelAmplitude(y, n, fc - r, kSampleRate),
                    dawlab::goertzelAmplitude(y, n, fc + r, kSampleRate), A / 2.0);
        std::printf("  -> sin·sin = ½[cos(difference) − cos(sum)]: only the sum and difference frequencies remain. In Exercise 4 you build a ring modulator yourself.\n\n");
    }

    // ---- 4. Autopan: gL + gR is constant; ASCII bars of the left/right RMS -------
    {
        dawlab::AudioBuffer tone = dawlab::makeSine(440.0, kSampleRate, 4.0, 0.5f, 2);
        Tremolo t = makeTremolo(2, 0.5f, 1.0f, LfoShape::Sine, true);
        // Check gL + gR = 2 − depth
        float maxSumError = 0.0f;
        dawlab::AudioBlock whole(tone);
        for (int start = 0; start < tone.getNumSamples(); start += kBlockSize) {
            const int n = std::min(kBlockSize, tone.getNumSamples() - start);
            t.process(whole.getSubBlock(start, n));
            maxSumError = std::max(maxSumError, std::abs(t.getCurrentGain(0) + t.getCurrentGain(1) - (2.0f - 1.0f)));
        }
        std::printf("Autopan (sine 0.5 Hz, depth 1): largest |gL + gR − (2 − depth)| at the end of each block = %.2e\n", maxSumError);
        std::printf("   time(s)   L RMS   R RMS\n");
        const int seg = dawlab::secondsToSamples(0.25, kSampleRate);
        for (int start = 0; start + seg <= tone.getNumSamples(); start += seg) {
            const float l = dawlab::rms(tone.getReadPointer(0) + start, seg);
            const float r = dawlab::rms(tone.getReadPointer(1) + start, seg);
            std::printf("   %5.2f   ", start / kSampleRate);
            for (int i = 0; i < 20; ++i)
                std::putchar(i < static_cast<int>(std::lround(l / 0.3536f * 20.0f)) ? '#' : '.');
            std::printf("  ");
            for (int i = 0; i < 20; ++i)
                std::putchar(i < static_cast<int>(std::lround(r / 0.3536f * 20.0f)) ? '#' : '.');
            std::printf("   %.3f  %.3f\n", l, r);
        }
        std::printf("  -> Left and right trade off, one cycle every 2 s. gL + gR is fixed, i.e. a linear pan law;\n"
                    "     for constant power use sin/cos instead (Chapter 01 Panner).\n");
    }
    return 0;
}
