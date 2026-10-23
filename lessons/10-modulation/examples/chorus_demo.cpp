// Example 2: Chorus -- a short delay stretched and shrunk by an LFO, plus the dry signal
//
// What it demonstrates:
//   1. Feed a plucked arpeggio into the Chorus (2 voices / 3 voices) and write WAVs to listen to.
//   2. A numerical check of the Doppler effect: with delay d(t) = d0 + D·sin(2πrt) the read speed
//      differs from the write speed, so the pitch drifts by a ratio of 1 − d'(t) = 1 − 2πrD·cos(2πrt).
//      Using a 1 kHz sine (1 voice, mix = 1), measure the maximum / minimum instantaneous frequency
//      and compare with 1000·(1 ± 2πrD).
//   3. Stereo spread: the left and right LFOs differ by up to 90°, so the left/right detuning is out
//      of sync -> the left/right correlation drops (the image widens). White noise and the plucked
//      arpeggio are compared too: for broadband material, a tiny left/right delay difference is
//      enough to decorrelate the wet signal almost completely.
//   4. Interpolation quality: the same chorus reads a 12 kHz sine with linear and with cubic
//      interpolation, to compare the high-frequency loss.
//
// How to run: l10_chorus (no arguments needed; optional argv[1] = input WAV path)
//
// Output files (dawlab_output/):
//   l10_chorus_dry.wav      the original plucked arpeggio
//   l10_chorus_wet.wav      2 voices, rate 0.8 Hz, depth 3 ms, centre 15 ms, mix 0.5, spread 1
//   l10_chorus_3voice.wav   3 voices, depth 4 ms, rate 0.5 Hz (thicker, more "ensemble")
//   l10_chorus_mono.wav     spread 0 (left and right identical; compare its width with wet)
//
// What you should hear: wet is thicker and wider than dry, like two guitars playing together but
// not perfectly aligned; 3voice sounds more like a crowd; the mono version has the same thickness
// but the image collapses to the center.

#include <dawlab/Common.h>
#include <dawlab/dsp/Chorus.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using dawlab::dsp::Chorus;

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

// Left/right channel correlation (Pearson): 1 = identical, 0 = uncorrelated, -1 = inverted
double stereoCorrelation(const dawlab::AudioBuffer& b, int skipSamples) {
    const float* l = b.getReadPointer(0);
    const float* r = b.getReadPointer(1);
    double sll = 0.0, srr = 0.0, slr = 0.0;
    for (int i = skipSamples; i < b.getNumSamples(); ++i) {
        sll += double(l[i]) * l[i];
        srr += double(r[i]) * r[i];
        slr += double(l[i]) * r[i];
    }
    return (sll > 0.0 && srr > 0.0) ? slr / std::sqrt(sll * srr) : 0.0;
}

// Estimate the instantaneous frequency from the spacing between adjacent upward zero crossings;
// returns the minimum and maximum after `skip`
void instantaneousFrequencyRange(const float* x, int n, int skip, double& fMin, double& fMax) {
    fMin = 1.0e9;
    fMax = 0.0;
    double lastCross = -1.0;
    for (int i = std::max(1, skip); i < n; ++i) {
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) {
            // Find the exact crossing position by linear interpolation
            const double cross = (i - 1) + double(-x[i - 1]) / (double(x[i]) - x[i - 1]);
            if (lastCross >= 0.0) {
                const double f = kSampleRate / (cross - lastCross);
                fMin = std::min(fMin, f);
                fMax = std::max(fMax, f);
            }
            lastCross = cross;
        }
    }
}

Chorus makeChorus(int channels, float rate, float depth, float centre, int voices, float mix, float spread) {
    Chorus c;
    c.prepare(kSampleRate, kBlockSize, channels);
    c.setRateHz(rate);
    c.setDepthMs(depth);
    c.setCentreDelayMs(centre);
    c.setVoices(voices);
    c.setMix(mix);
    c.setStereoSpread(spread);
    c.reset(); // Make the smoothers snap straight to their targets
    return c;
}

void renderAndWrite(const dawlab::AudioBuffer& input, Chorus& chorus, const char* filename) {
    dawlab::AudioBuffer out = input;
    processInBlocks(chorus, out);
    dawlab::writeWav(dawlab::outputPath(filename), out, kSampleRate);
    std::printf("  %-24s peak %.3f   L/R correlation %.3f\n", filename, dawlab::peakAbs(out),
                out.getNumChannels() > 1 ? stereoCorrelation(out, 0) : 1.0);
}

} // namespace

int main(int argc, char** argv) {
    std::printf("=== Chapter 10 Example 2: Chorus ===\n\n");

    // ---- 1. Plucked arpeggio -> WAV ------------------------------------------
    dawlab::AudioBuffer input;
    if (argc > 1) {
        dawlab::WavData wav;
        std::string err;
        if (!dawlab::readWav(argv[1], wav, &err)) {
            std::printf("%s\n", err.c_str());
            return 1;
        }
        input = wav.buffer;
        if (input.getNumChannels() == 1) { // Expand to stereo
            dawlab::AudioBuffer st(2, input.getNumSamples());
            st.copyFrom(0, 0, input, 0, 0, input.getNumSamples());
            st.copyFrom(1, 0, input, 0, 0, input.getNumSamples());
            input = st;
        }
    } else {
        input = dawlab::makePluckSequence(kSampleRate, 4.0, 0.4f, 2);
    }
    dawlab::writeWav(dawlab::outputPath("l10_chorus_dry.wav"), input, kSampleRate);
    std::printf("Output WAVs:\n");
    std::printf("  %-24s peak %.3f   L/R correlation %.3f\n", "l10_chorus_dry.wav", dawlab::peakAbs(input),
                stereoCorrelation(input, 0));
    {
        Chorus c = makeChorus(2, 0.8f, 3.0f, 15.0f, 2, 0.5f, 1.0f);
        renderAndWrite(input, c, "l10_chorus_wet.wav");
    }
    {
        Chorus c = makeChorus(2, 0.5f, 4.0f, 18.0f, 3, 0.5f, 1.0f);
        renderAndWrite(input, c, "l10_chorus_3voice.wav");
    }
    {
        Chorus c = makeChorus(2, 0.8f, 3.0f, 15.0f, 2, 0.5f, 0.0f);
        renderAndWrite(input, c, "l10_chorus_mono.wav");
    }
    std::printf("  -> With spread 0, left and right are identical (correlation 1.000); with spread 1 the correlation drops clearly = a wider image.\n\n");

    // ---- 2. Doppler: the amount of instantaneous-frequency drift ---------------
    {
        const float rate = 0.8f, depthMs = 3.0f;
        const double dev = dawlab::kTwoPi * rate * depthMs * 0.001; // 2πrD (D in seconds)
        dawlab::AudioBuffer tone = dawlab::makeSine(1000.0, kSampleRate, 4.0, 0.5f, 1);
        Chorus c = makeChorus(1, rate, depthMs, 15.0f, 1, 1.0f, 0.0f); // 1 voice, fully wet = a purely modulated delay
        processInBlocks(c, tone);
        double fMin, fMax;
        instantaneousFrequencyRange(tone.getReadPointer(0), tone.getNumSamples(),
                                    dawlab::secondsToSamples(1.0, kSampleRate), fMin, fMax);
        std::printf("Doppler pitch drift (1 kHz sine, 1 voice, mix 1, rate %.1f Hz, depth %.0f ms):\n", rate, depthMs);
        std::printf("  Theory: ratio 1 +/- 2*pi*r*D = 1 +/- %.4f -> %.1f .. %.1f Hz (%+.1f / %+.1f cents)\n", dev,
                    1000.0 * (1.0 - dev), 1000.0 * (1.0 + dev), 1200.0 * std::log2(1.0 + dev),
                    1200.0 * std::log2(1.0 - dev));
        std::printf("  Measured: instantaneous frequency %.1f .. %.1f Hz\n", fMin, fMax);
        std::printf("  -> depth x rate decides how far the pitch strays: 3 ms x 0.8 Hz is about +/-26 cents (slightly more down than up); much more and it sounds \"drunk\".\n\n");
    }

    // ---- 3. Effect of stereo spread on the correlation ------------------------
    {
        dawlab::AudioBuffer mono = dawlab::makeWhiteNoise(kSampleRate, 3.0, 0.25f, 1, 7u);
        dawlab::AudioBuffer noise(2, mono.getNumSamples());
        noise.copyFrom(0, 0, mono, 0, 0, mono.getNumSamples());
        noise.copyFrom(1, 0, mono, 0, 0, mono.getNumSamples());
        const dawlab::AudioBuffer pluck = dawlab::makePluckSequence(kSampleRate, 4.0, 0.4f, 2);
        const int skip = dawlab::secondsToSamples(0.5, kSampleRate);
        std::printf("Stereo spread vs left/right correlation (2 voices, mix 0.5, depth 3 ms):\n");
        std::printf("   spread   L/R LFO offset   white noise   plucked arpeggio\n");
        for (float spread : {0.0f, 0.1f, 0.25f, 0.5f, 1.0f}) {
            dawlab::AudioBuffer a = noise, b = pluck;
            Chorus c1 = makeChorus(2, 0.8f, 3.0f, 15.0f, 2, 0.5f, spread);
            processInBlocks(c1, a);
            Chorus c2 = makeChorus(2, 0.8f, 3.0f, 15.0f, 2, 0.5f, spread);
            processInBlocks(c2, b);
            std::printf("   %5.2f      %5.1f°          %.3f         %.3f\n", spread, 90.0f * spread, stereoCorrelation(a, skip),
                        stereoCorrelation(b, skip));
        }
        std::printf("  -> White noise: a tiny left/right delay difference fully decorrelates the wet signal, so the correlation\n"
                    "     drops to the floor as soon as spread leaves 0 (the dry half is identical in both channels,\n"
                    "     so it stops at about 0.7 rather than 0).\n"
                    "     The plucked arpeggio is rich in harmonics and behaves much the same: for any spread above 0.1\n"
                    "     the correlation lands at 0.72-0.77.\n"
                    "     So spread is almost an on/off switch for \"how wide\"; what it really changes is how the left and\n"
                    "     right detuning drift apart over time (it sounds like it is rotating).\n\n");
    }

    // ---- 4. High-frequency loss of linear vs cubic interpolation ---------------
    {
        std::printf("Interpolation quality (12 kHz sine, 1 voice, mix 1, depth 3 ms, rate 0.8 Hz):\n");
        const dawlab::AudioBuffer tone = dawlab::makeSine(12000.0, kSampleRate, 3.0, 0.25f, 1);
        const int skip = dawlab::secondsToSamples(0.5, kSampleRate);
        const float inRms = dawlab::rms(tone.getReadPointer(0) + skip, tone.getNumSamples() - skip);
        for (bool cubic : {false, true}) {
            dawlab::AudioBuffer out = tone;
            Chorus c = makeChorus(1, 0.8f, 3.0f, 15.0f, 1, 1.0f, 0.0f);
            c.setUseCubicInterpolation(cubic);
            processInBlocks(c, out);
            const float outRms = dawlab::rms(out.getReadPointer(0) + skip, out.getNumSamples() - skip);
            std::printf("  %-6s interpolation: output RMS relative to input %+.2f dB\n", cubic ? "cubic" : "linear",
                        dawlab::gainToDb(outRms / inRms));
        }
        std::printf("  -> Linear interpolation is a time-varying FIR: |H|^2 = (1-D)^2 + D^2 + 2D(1-D)cos(2*pi*f/fs).\n"
                    "     At 12 kHz = fs/4 the cos term is 0, and D sweeps uniformly over 0..1 -> mean power = integral of (1-D)^2+D^2 dD = 2/3\n"
                    "     -> theory %.2f dB; cubic loses far less.\n",
                    10.0 * std::log10(2.0 / 3.0));
    }
    return 0;
}
