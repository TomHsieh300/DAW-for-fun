// Example 4: short delay + feedback = comb filter
//
// What it demonstrates:
//   1. A feedback comb with delay D = 48 samples (1 ms @ 48 kHz) and feedback g = 0.7:
//        y[n] = x[n] + g·y[n−D]      H(f) = 1 / (1 − g·e^{−j2πfD/fs})
//      The frequency response has a peak every fs/D = 1000 Hz (|H| = 1/(1−g) = 3.33 → +10.46 dB),
//      with a notch between neighbouring peaks (|H| = 1/(1+g) = 0.588 → −4.61 dB).
//      This example sweeps 250–9750 Hz, finds the peaks and notches automatically and prints
//      their spacing, verifying "period = fs/D".
//   2. A feedforward comb, y[n] = x[n] + g·x[n−D]; the peak and notch depths are exactly
//      swapped (+4.61 / −10.46 dB).
//   3. The Delay class (delay 1 ms, feedback 0.7, mix 1) gives the same result: an echo
//      effect turned down to a very short delay time is a comb filter, which is also the
//      basic building block of the chapter 12 reverb.
//
// How to run: no arguments needed, just run l09_comb_filter.
// Output files (dawlab_output/):
//   l09_comb_noise_dry.wav   white noise
//   l09_comb_noise_1ms.wav   white noise through a 1 ms / feedback 0.9 comb (a clear 1 kHz pitch)
//   l09_comb_noise_10ms.wav  white noise through a 10 ms / feedback 0.8 comb (100 Hz metallic / tubular resonance)
// What you should hear: the noise is "tuned" -- a short delay plus feedback gives the input a
//             pitch of fs/D; stretching the delay beyond about 10 ms turns it metallic, and
//             longer still it starts to sound like the echo of a small room.

#include <dawlab/Common.h>
#include <dawlab/dsp/Delay.h>
#include <dawlab/dsp/FractionalDelayLine.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using dawlab::dsp::Delay;
using dawlab::dsp::FractionalDelayLine;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

// Feedback comb: y[n] = x[n] + g·y[n−D]. Read first (at D−1, since this sample has not been
// pushed yet), then push.
class FeedbackComb {
public:
    void prepare(int delaySamples, float g) {
        delay_ = delaySamples;
        g_ = g;
        line_.prepare(delaySamples + 2, 1);
    }
    float processSample(float x) noexcept {
        const float y = x + g_ * line_.readInteger(0, delay_ - 1);
        line_.push(0, y);
        return y;
    }

private:
    FractionalDelayLine line_;
    int delay_ = 1;
    float g_ = 0.0f;
};

// Feedforward comb: y[n] = x[n] + g·x[n−D]
class FeedforwardComb {
public:
    void prepare(int delaySamples, float g) {
        delay_ = delaySamples;
        g_ = g;
        line_.prepare(delaySamples + 2, 1);
    }
    float processSample(float x) noexcept {
        line_.push(0, x);
        return x + g_ * line_.readInteger(0, delay_);
    }

private:
    FractionalDelayLine line_;
    int delay_ = 1;
    float g_ = 0.0f;
};

template <typename Comb>
std::vector<double> scanResponse(int delaySamples, float g, const std::vector<double>& freqs) {
    std::vector<double> out;
    for (double f : freqs) {
        Comb comb;
        comb.prepare(delaySamples, g);
        out.push_back(dawlab::measureGainDb([&](float x) { return comb.processSample(x); }, f, kSampleRate));
    }
    return out;
}

// Run a sine through the Delay class block by block, then measure the gain with Goertzel
// (Delay has no processSample, so we measure by hand).
double measureDelayGainDb(Delay& delay, double freq) {
    dawlab::AudioBuffer in = dawlab::makeSine(freq, kSampleRate, 0.7, 0.25f, 1);
    dawlab::AudioBuffer out = in;
    delay.reset();
    dawlab::AudioBlock whole(out);
    for (int start = 0; start < out.getNumSamples(); start += kBlockSize) {
        const int n = std::min(kBlockSize, out.getNumSamples() - start);
        delay.process(whole.getSubBlock(start, n));
    }
    const int skip = dawlab::secondsToSamples(0.2, kSampleRate);
    const int n = out.getNumSamples() - skip;
    const double a = dawlab::goertzelAmplitude(in.getReadPointer(0) + skip, n, freq, kSampleRate);
    const double b = dawlab::goertzelAmplitude(out.getReadPointer(0) + skip, n, freq, kSampleRate);
    return dawlab::gainToDb(b / a);
}

void renderNoiseThroughComb(const dawlab::AudioBuffer& noise, int delaySamples, float g, float outGain,
                            const char* filename) {
    dawlab::AudioBuffer out(noise.getNumChannels(), noise.getNumSamples());
    for (int ch = 0; ch < noise.getNumChannels(); ++ch) {
        FeedbackComb comb;
        comb.prepare(delaySamples, g);
        const float* src = noise.getReadPointer(ch);
        float* dst = out.getWritePointer(ch);
        for (int start = 0; start < noise.getNumSamples(); start += kBlockSize) {
            const int n = std::min(kBlockSize, noise.getNumSamples() - start);
            for (int i = 0; i < n; ++i)
                dst[start + i] = outGain * comb.processSample(src[start + i]);
        }
    }
    dawlab::writeWav(dawlab::outputPath(filename), out, kSampleRate);
    std::printf("  %s: peak %.3f\n", filename, dawlab::peakAbs(out));
}

} // namespace

int main() {
    std::printf("=== Chapter 09, Example 4: short delay + feedback = comb filter ===\n\n");

    const int D = 48;    // 1 ms @ 48 kHz
    const float g = 0.7f;
    const double period = kSampleRate / D; // peak spacing = fs/D = 1000 Hz
    std::printf("D = %d samples (%.2f ms), g = %.2f\n", D, dawlab::samplesToMs(D, kSampleRate), g);
    std::printf("Theory: peaks at k x %.0f Hz, height 1/(1-g) = %.2f -> %+.2f dB; "
                "notches at (k+1/2) x %.0f Hz, depth 1/(1+g) = %.3f -> %+.2f dB\n\n",
                period, 1.0 / (1.0 - g), dawlab::gainToDb(1.0 / (1.0 - g)), period, 1.0 / (1.0 + g),
                dawlab::gainToDb(1.0 / (1.0 + g)));

    // ---- 1. Sweep the feedback comb and find the peaks and notches automatically ----
    std::vector<double> freqs;
    for (double f = 250.0; f <= 9750.0; f += 50.0)
        freqs.push_back(f);
    const std::vector<double> fb = scanResponse<FeedbackComb>(D, g, freqs);

    std::printf("Local peaks / notches found sweeping the feedback comb over 250-9750 Hz (every 50 Hz):\n");
    std::printf("   type     freq(Hz)   gain(dB)   spacing from previous of same type (Hz)\n");
    double lastPeak = -1.0, lastNotch = -1.0;
    for (size_t i = 1; i + 1 < fb.size(); ++i) {
        const bool peak = fb[i] > fb[i - 1] && fb[i] >= fb[i + 1];
        const bool notch = fb[i] < fb[i - 1] && fb[i] <= fb[i + 1];
        if (peak) {
            std::printf("   peak   %8.0f   %+7.2f   %s\n", freqs[i], fb[i],
                        lastPeak > 0 ? std::to_string(static_cast<int>(freqs[i] - lastPeak)).c_str() : "-");
            lastPeak = freqs[i];
        } else if (notch) {
            std::printf("   notch  %8.0f   %+7.2f   %s\n", freqs[i], fb[i],
                        lastNotch > 0 ? std::to_string(static_cast<int>(freqs[i] - lastNotch)).c_str() : "-");
            lastNotch = freqs[i];
        }
    }
    std::printf("  -> spacing = fs/D = %.0f Hz; peaks ~ %+.2f dB, notches ~ %+.2f dB\n\n", period,
                dawlab::gainToDb(1.0 / (1.0 - g)), dawlab::gainToDb(1.0 / (1.0 + g)));

    // ---- 2. Feedforward comb for comparison ----------------------------------
    {
        const std::vector<double> probe = {500.0, 1000.0, 1500.0, 2000.0, 2500.0, 3000.0};
        const auto ff = scanResponse<FeedforwardComb>(D, g, probe);
        const auto fbp = scanResponse<FeedbackComb>(D, g, probe);
        std::printf("feedforward (y = x + g*x[n-D]) vs feedback (y = x + g*y[n-D]):\n");
        std::printf("   freq(Hz)  feedforward  feedback\n");
        for (size_t i = 0; i < probe.size(); ++i)
            std::printf("   %7.0f   %+8.2f    %+8.2f\n", probe[i], ff[i], fbp[i]);
        std::printf("  -> feedforward: peak 1+g = %.2f (%+.2f dB), notch 1-g = %.2f (%+.2f dB); "
                    "the feedback comb has sharp peaks and shallow notches, the feedforward comb the opposite.\n\n",
                    1.0 + g, dawlab::gainToDb(1.0 + g), 1.0 - g, dawlab::gainToDb(1.0 - g));
    }

    // ---- 3. The same thing with the Delay class --------------------------------
    {
        Delay delay;
        delay.setMaxDelayMs(100.0f);
        delay.prepare(kSampleRate, kBlockSize, 1);
        delay.setDelayMs(1.0f); // 48 samples
        delay.setFeedback(g);
        delay.setMix(1.0f);
        delay.setLowpassHz(20000.0f); // disable the feedback filters
        delay.setHighpassHz(20.0f);
        std::printf("Delay class (delayMs = 1, feedback = %.1f, mix = 1):\n", g);
        std::printf("   1000 Hz: %+.2f dB (theory %+.2f)   1500 Hz: %+.2f dB (theory %+.2f)\n\n",
                    measureDelayGainDb(delay, 1000.0), dawlab::gainToDb(1.0 / (1.0 - g)),
                    measureDelayGainDb(delay, 1500.0), dawlab::gainToDb(1.0 / (1.0 + g)));
    }

    // ---- 4. Listen: white noise "tuned" by the comb --------------------------------
    {
        dawlab::AudioBuffer noise = dawlab::makeWhiteNoise(kSampleRate, 3.0, 0.2f);
        dawlab::writeWav(dawlab::outputPath("l09_comb_noise_dry.wav"), noise, kSampleRate);
        std::printf("Output WAVs:\n");
        std::printf("  l09_comb_noise_dry.wav: peak %.3f\n", dawlab::peakAbs(noise));
        renderNoiseThroughComb(noise, 48, 0.9f, 0.15f, "l09_comb_noise_1ms.wav");   // 1 kHz pitch
        renderNoiseThroughComb(noise, 480, 0.8f, 0.25f, "l09_comb_noise_10ms.wav"); // 100 Hz metallic
    }
    return 0;
}
