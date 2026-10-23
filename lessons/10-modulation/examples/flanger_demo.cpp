// Example 3: Flanger -- a sweeping comb filter; measuring the notch frequencies against the formula
//
// What it demonstrates:
//   1. Freeze the LFO (rate = 0), fix the delay d, measure the flanger's frequency response, and
//      compare it with the formula:
//        H(f) = (1 − mix) + mix · e^{−j2πfd} / (1 − g·e^{−j2πfd})        (g = feedback)
//      With no feedback and mix = 0.5, |H(f)| = |cos(πfd)|: notches at f = (2k+1)/(2d), peaks at f = k/d.
//      (a) d = 2 ms: sweep 100-4000 Hz, find the notches automatically, and compare with 250, 750, 1250 ... Hz.
//      (b) d = 1 ms: at the peak / notch frequencies, compare the gain with the theoretical value
//          for feedback = 0, +0.5, and −0.7.
//   2. WAVs of a plucked arpeggio and white noise through the flanger (the jet-plane sound).
//   3. Through-zero flanging: the dry signal is also delayed by a fixed amount, and the wet
//      signal's delay sweeps across it -> at the instant the two delays are equal they cancel
//      completely (the "through zero" of classic tape flanging).
//
// How to run: l10_flanger (no arguments)
//
// Output files (dawlab_output/):
//   l10_flanger_dry.wav           plucked arpeggio
//   l10_flanger_wet.wav           rate 0.3 Hz, delay 1 ms, depth 2 ms, feedback 0.5, mix 0.5
//   l10_flanger_noise_dry.wav     white noise
//   l10_flanger_noise.wav         white noise through the flanger (feedback 0.7) -- the easiest material for hearing the sweep
//   l10_flanger_through_zero.wav  through-zero flanging of white noise (dry fixed at 2 ms, wet sweeping back and forth over 0.1-3.9 ms)
//
// What you should hear: wet has a metallic "whoosh" sweep; the noise version sounds like a jet
// taking off and landing; through_zero almost vanishes for an instant each time it passes through zero.

#include <dawlab/Common.h>
#include <dawlab/dsp/Flanger.h>
#include <dawlab/dsp/FractionalDelayLine.h>
#include <dawlab/dsp/Lfo.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

using dawlab::dsp::Flanger;
using dawlab::dsp::FractionalDelayLine;
using dawlab::dsp::Lfo;
using dawlab::dsp::LfoShape;

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

// Theoretical frequency response: H = (1−mix) + mix·z^{−D} / (1 − g·z^{−D}), z^{−D} = e^{−j2πfd}
double theoryGainDb(double fHz, double delaySeconds, double feedback, double mix) {
    const std::complex<double> zD = std::polar(1.0, -dawlab::kTwoPi * fHz * delaySeconds);
    const std::complex<double> h = (1.0 - mix) + mix * zD / (1.0 - feedback * zD);
    return dawlab::gainToDb(std::abs(h));
}

// A flanger with the LFO frozen and the delay fixed at delayMs (mono, for measurement)
Flanger makeFrozenFlanger(float delayMs, float feedback, float mix) {
    Flanger f;
    f.prepare(kSampleRate, kBlockSize, 1);
    f.setRateHz(0.0f);
    f.setDepthMs(2.0f);
    f.setDelayMs(delayMs);
    f.setFeedback(feedback);
    f.setMix(mix);
    f.setLfoShape(LfoShape::Sine);
    f.setLfoPhase(0.75f); // sin(2π·0.75) = −1 -> the delay is parked at the bottom of the sweep range = delayMs
    f.reset();
    return f;
}

double measureFlangerDb(Flanger& f, double freq) {
    f.reset();
    return dawlab::measureGainDb([&](float x) { return f.processSample(0, x); }, freq, kSampleRate, 0.4, 0.1);
}

} // namespace

int main() {
    std::printf("=== Chapter 10 Example 3: Flanger ===\n\n");

    // ---- 1a. d = 2 ms: sweep to find the notches -------------------------------
    {
        const float d = 2.0f;
        Flanger f = makeFrozenFlanger(d, 0.0f, 0.5f);
        // Run one sample first so getCurrentDelayMs updates, confirming the LFO really is parked at the bottom
        f.processSample(0, 0.0f);
        std::printf("Actual delay with the LFO frozen: %.3f ms (delay set to %.1f ms, depth 2 ms, LFO phase 0.75 -> the bottom)\n",
                    f.getCurrentDelayMs(), d);

        std::vector<double> freqs, gains;
        for (double fr = 100.0; fr <= 4000.0; fr += 25.0) {
            freqs.push_back(fr);
            gains.push_back(measureFlangerDb(f, fr));
        }
        std::printf("Notches found sweeping 100-4000 Hz (every 25 Hz), vs f_k = (2k+1)/(2d), d = %.0f ms:\n", d);
        std::printf("   k   measured notch(Hz)   gain(dB)   theory(Hz)\n");
        int k = 0;
        for (size_t i = 1; i + 1 < gains.size(); ++i) {
            if (gains[i] < gains[i - 1] && gains[i] <= gains[i + 1] && gains[i] < -10.0) {
                std::printf("  %2d   %14.0f       %7.1f   %8.0f\n", k, freqs[i], gains[i],
                            Flanger::notchFrequencyHz(k, d));
                ++k;
            }
        }
        std::printf("  -> The notches are evenly spaced, with spacing 1/d = %.0f Hz; the peaks are at k/d = 0, 500, 1000 ... Hz.\n\n", 1000.0 / d);
    }

    // ---- 1b. d = 1 ms: effect of feedback on the peaks / notches ----------------
    {
        const float d = 1.0f;
        const double probes[] = {500.0, 1000.0, 1500.0, 2000.0, 2500.0, 3000.0};
        const float feedbacks[] = {0.0f, 0.5f, -0.7f};
        std::printf("d = %.0f ms, mix 0.5: gain at each frequency (measured / theory, dB)\n", d);
        std::printf("   freq(Hz)  type   ");
        for (float g : feedbacks)
            std::printf("     fb %+.1f       ", g);
        std::printf("\n");
        for (double fr : probes) {
            const bool isPeak = std::fmod(fr, 1000.0) < 1.0;
            std::printf("   %7.0f   %-6s", fr, isPeak ? "peak" : "notch");
            for (float g : feedbacks) {
                Flanger f = makeFrozenFlanger(d, g, 0.5f);
                const double meas = measureFlangerDb(f, fr);
                const double theo = theoryGainDb(fr, d * 0.001, g, 0.5);
                std::printf("  %+7.1f / %+7.1f", meas, theo);
            }
            std::printf("\n");
        }
        std::printf("  -> feedback 0: the notches are bottomless (theory −inf) and the peaks are 0 dB.\n"
                    "     feedback +0.5: the peak becomes 0.5 + 0.5/(1-g) = 1.5 -> +3.5 dB, and the notch is only 0.5 - 0.5/(1+g) -> -15.6 dB.\n"
                    "     feedback −0.7: the old notch position becomes 0.5 - 0.5/(1-0.7) = -1.17 -> a +1.3 dB peak,\n"
                    "     and the old peak becomes 0.5 + 0.5/1.7 = 0.79 -> -2.0 dB -- the whole row of peaks and valleys swaps, and the sound is \"hollow\".\n\n");
    }

    // ---- 2. Listening: plucked arpeggio and white noise ------------------------
    {
        dawlab::AudioBuffer pluck = dawlab::makePluckSequence(kSampleRate, 4.0, 0.4f, 2);
        dawlab::writeWav(dawlab::outputPath("l10_flanger_dry.wav"), pluck, kSampleRate);
        std::printf("Output WAVs:\n  l10_flanger_dry.wav          peak %.3f\n", dawlab::peakAbs(pluck));

        Flanger f;
        f.prepare(kSampleRate, kBlockSize, 2);
        f.setRateHz(0.3f);
        f.setDelayMs(1.0f);
        f.setDepthMs(2.0f);
        f.setFeedback(0.5f);
        f.setMix(0.5f);
        f.reset();
        processInBlocks(f, pluck);
        dawlab::writeWav(dawlab::outputPath("l10_flanger_wet.wav"), pluck, kSampleRate);
        std::printf("  l10_flanger_wet.wav          peak %.3f\n", dawlab::peakAbs(pluck));

        dawlab::AudioBuffer noise = dawlab::makeWhiteNoise(kSampleRate, 6.0, 0.2f, 2, 3u);
        dawlab::writeWav(dawlab::outputPath("l10_flanger_noise_dry.wav"), noise, kSampleRate);
        f.setRateHz(0.2f);
        f.setDelayMs(0.5f);
        f.setDepthMs(4.0f);
        f.setFeedback(0.7f);
        f.reset();
        processInBlocks(f, noise);
        dawlab::writeWav(dawlab::outputPath("l10_flanger_noise.wav"), noise, kSampleRate);
        std::printf("  l10_flanger_noise_dry.wav    peak %.3f\n  l10_flanger_noise.wav        peak %.3f\n",
                    0.2f, dawlab::peakAbs(noise));
    }

    // ---- 3. through-zero flanging --------------------------------------------
    {
        // The dry signal has a fixed delay dryMs; the wet delay = dryMs + sweep·sin(...), so it
        // sweeps back and forth across the dry signal on both sides of dryMs.
        // The wet signal is inverted: when the delays are equal, x[n−d] − x[n−d] = 0 -> complete
        // cancellation; away from the zero point it is a comb with very widely spaced notches.
        const float dryMs = 2.0f, sweepMs = 1.9f;
        // Returns the output, and also records the RMS of the quietest block
        auto renderThroughZero = [&](const dawlab::AudioBuffer& in, float& minBlockRms) {
            dawlab::AudioBuffer out(1, in.getNumSamples());
            FractionalDelayLine line;
            line.prepare(dawlab::msToSamples(dryMs + sweepMs, kSampleRate) + 8, 1);
            Lfo lfo;
            lfo.prepare(kSampleRate);
            lfo.setRateHz(0.25f);
            lfo.setShape(LfoShape::Triangle);
            lfo.reset();
            const float* src = in.getReadPointer(0);
            float* dst = out.getWritePointer(0);
            const float toSamples = static_cast<float>(kSampleRate * 0.001);
            minBlockRms = 1.0f;
            for (int start = 0; start < in.getNumSamples(); start += kBlockSize) {
                const int n = std::min(kBlockSize, in.getNumSamples() - start);
                for (int i = 0; i < n; ++i) {
                    line.push(0, src[start + i]);
                    const float wetMs = dryMs + sweepMs * lfo.getNextValue();
                    const float dry = line.readCubic(0, dryMs * toSamples);
                    const float wet = line.readCubic(0, wetMs * toSamples);
                    dst[start + i] = 0.5f * (dry - wet); // the wet signal is inverted
                }
                if (start > dawlab::secondsToSamples(0.5, kSampleRate))
                    minBlockRms = std::min(minBlockRms, dawlab::rms(dst + start, n));
            }
            return out;
        };

        float minRms = 0.0f;
        dawlab::AudioBuffer noise = dawlab::makeWhiteNoise(kSampleRate, 6.0, 0.25f, 1, 5u);
        dawlab::AudioBuffer out = renderThroughZero(noise, minRms);
        dawlab::writeWav(dawlab::outputPath("l10_flanger_through_zero.wav"), out, kSampleRate);
        std::printf("  l10_flanger_through_zero.wav peak %.3f\n", dawlab::peakAbs(out));

        // Numerical check: when a 500 Hz sine passes through zero, the delay difference δ -> 0 and |H| = 2|sin(πfδ/fs)| -> 0
        const dawlab::AudioBuffer tone = dawlab::makeSine(500.0, kSampleRate, 6.0, 0.5f, 1);
        renderThroughZero(tone, minRms);
        const float refRms = 0.5f * dawlab::rms(tone.getReadPointer(0), tone.getNumSamples()); // the dry signal itself x 0.5
        std::printf("  Through-zero, 500 Hz sine: the quietest block is %+.1f dB relative to the dry signal (a normal comb is never this quiet outside its notches)\n"
                    "  -> At the instant the delay difference passes through 0 every frequency cancels at once; the white-noise version sounds like \"the whole sound being sucked away and spat back\".\n",
                    dawlab::gainToDb(minRms / refRms));
    }
    return 0;
}
