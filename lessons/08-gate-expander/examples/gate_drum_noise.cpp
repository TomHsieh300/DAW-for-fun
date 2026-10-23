// Example 1: a noise gate cleans up background noise under a drum loop
//
// What it shows:
//   - Adds RMS = -50 dBFS white noise (simulating mic noise floor / bleed) to the makeDrumLoop() drum loop,
//     followed by 1 second of "noise only" to simulate the silence after the drummer stops
//   - Processes it block by block (512 samples) with the Gate: threshold -30 dB, hysteresis 6 dB
//     (closing threshold -36 dB), attack 1 ms, hold 20 ms, release 50 ms, range -80 dB
//   - Measures the RMS of the "silent sections" before and after: the noise drops from -50 dBFS to below the range
//   - Walks the same signal sample by sample using a copy of the Gate and nextGain(), printing the state
//     machine's transition timeline (at which millisecond Closed -> Attack -> Open -> Hold -> Release -> Closed happen)
//   - Compares the number of state transitions with "hysteresis = 0": chattering near the threshold makes the
//     transition count explode
//   - Measures the gate detector's envelope ripple on a steady sine wave (peak-to-peak dB) and compares it with the
//     README 2.4.1 estimate ripple ~= 8.686 * (1 - 2/pi) / (2f * tau_release): low frequencies have large ripple,
//     which is why a kick needs a larger hysteresis
//   - Changes the range while the gate is closed (simulating automation): the gain glides to the new range along the
//     attack / release slope instead of jumping
// How to run: l08_gate_drum_noise [input.wav]; with no argument it uses the synthetic drums + noise.
// Output files: dawlab_output/l08_gate_input.wav (drums + noise), l08_gate_output.wav (after the gate),
//          l08_gate_no_hysteresis.wav (the hysteresis = 0 version, for hearing chattering).
// What to listen for: the input has a layer of hiss in the gaps and at the end; in the output the gaps become
//            completely silent without the drum hits losing their punch; the no_hysteresis version may have a
//            "shattering" sound near the kick tails where the gate opens and closes rapidly.

#include <dawlab/Common.h>
#include <dawlab/dsp/Gate.h>

#include <cstdio>
#include <string>
#include <vector>

using namespace dawlab;
using dawlab::dsp::DetectorMode;
using dawlab::dsp::EnvelopeFollower;
using dawlab::dsp::Gate;
using dawlab::dsp::GateState;
using dawlab::dsp::gateStateName;

namespace {

constexpr int kBlockSize = 512;

// Compute dB directly in double, bypassing the -100 dB floor of gainToDb(), so we can see how low the gate really pushes
double rmsDbOfRange(const AudioBuffer& b, int channel, int start, int end) {
    const float* x = b.getReadPointer(channel);
    double sum = 0.0;
    for (int i = start; i < end; ++i)
        sum += static_cast<double>(x[i]) * x[i];
    const double r = std::sqrt(sum / std::max(1, end - start));
    return r > 1.0e-12 ? 20.0 * std::log10(r) : -240.0;
}

// Process block by block (simulating the audio callback)
AudioBuffer runGate(const AudioBuffer& input, Gate& gate) {
    AudioBuffer out = input;
    AudioBlock whole(out);
    const int n = out.getNumSamples();
    for (int start = 0; start < n; start += kBlockSize)
        gate.process(whole.getSubBlock(start, std::min(kBlockSize, n - start)));
    return out;
}

struct Transition {
    double timeSec;
    GateState from, to;
};

// Walk a gate with the same settings sample by sample and record the time of every state change
// (this is how process() calls nextGain internally)
std::vector<Transition> traceStates(const AudioBuffer& input, Gate gate /* take a copy so the original is untouched */) {
    gate.reset();
    std::vector<Transition> log;
    GateState last = gate.getState();
    for (int i = 0; i < input.getNumSamples(); ++i) {
        float det = 0.0f;
        for (int ch = 0; ch < input.getNumChannels(); ++ch)
            det = std::max(det, std::abs(input.getSample(ch, i)));
        gate.nextGain(det);
        if (gate.getState() != last) {
            log.push_back({i / 48000.0, last, gate.getState()});
            last = gate.getState();
        }
    }
    return log;
}

// Track a steady sine wave with a peak detector configured like the Gate's internal one, and return the
// peak-to-peak value (dB) of the envelope over the second half
double measureRippleDb(double freq, float releaseMs, double sampleRate) {
    EnvelopeFollower env;
    env.prepare(sampleRate);
    env.setMode(DetectorMode::Peak);
    env.setAttackMs(Gate::kDetectorAttackMs);
    env.setReleaseMs(releaseMs);
    const int n = secondsToSamples(1.0, sampleRate);
    float lo = 1.0e9f, hi = 0.0f;
    for (int i = 0; i < n; ++i) {
        const auto x = static_cast<float>(0.5 * std::sin(kTwoPi * freq * i / sampleRate));
        const float e = env.processSample(x);
        if (i >= n / 2) { // Look only at the second half (steady state reached)
            lo = std::min(lo, e);
            hi = std::max(hi, e);
        }
    }
    return 20.0 * std::log10(static_cast<double>(hi) / lo);
}

} // namespace

int main(int argc, char** argv) {
    std::printf("Chapter 08 Example 1: a noise gate cleans up the noise under a drum loop\n");

    const double sampleRate = 48000.0;
    AudioBuffer input;
    bool synthetic = true;
    if (argc > 1) {
        WavData wav;
        std::string err;
        if (!readWav(argv[1], wav, &err)) {
            std::printf("Read failed: %s\n", err.c_str());
            return 1;
        }
        if (static_cast<int>(wav.sampleRate) != 48000) {
            std::printf("This example assumes 48 kHz; the input file is %.0f Hz\n", wav.sampleRate);
            return 1;
        }
        input = wav.buffer;
        synthetic = false;
    } else {
        // Drums for 2 bars (4 s) + 1 s of silence; white noise at RMS -50 dBFS is added over the whole thing
        const AudioBuffer drums = makeDrumLoop(sampleRate);
        const int tail = secondsToSamples(1.0, sampleRate);
        input.setSize(drums.getNumChannels(), drums.getNumSamples() + tail);
        for (int ch = 0; ch < drums.getNumChannels(); ++ch)
            input.copyFrom(ch, 0, drums, ch, 0, drums.getNumSamples());
        // A uniform distribution on [-A, A) has RMS = A / sqrt(3); for RMS = -50 dBFS, A = 10^(-50/20) * sqrt(3)
        const float noiseAmp = dbToGain(-50.0f) * std::sqrt(3.0f);
        const AudioBuffer noise = makeWhiteNoise(sampleRate, input.getNumSamples() / sampleRate, noiseAmp,
                                                 input.getNumChannels(), 2024);
        for (int ch = 0; ch < input.getNumChannels(); ++ch)
            input.addFrom(ch, 0, noise, ch, 0, input.getNumSamples());
    }
    const double totalSec = input.getNumSamples() / sampleRate;

    Gate gate;
    gate.prepare(sampleRate, kBlockSize, input.getNumChannels());
    gate.setThresholdDb(-30.0f);
    gate.setHysteresisDb(6.0f);
    gate.setRangeDb(-80.0f);
    gate.setAttackMs(1.0f);
    gate.setHoldMs(20.0f);
    gate.setReleaseMs(50.0f);

    std::printf("\nSettings: threshold %.0f dB, hysteresis %.0f dB (closing threshold %.0f dB), attack %.0f ms, hold %.0f ms, "
                "release %.0f ms, range %.0f dB\n",
                gate.getThresholdDb(), gate.getHysteresisDb(), gate.getCloseThresholdDb(), gate.getAttackMs(),
                gate.getHoldMs(), gate.getReleaseMs(), gate.getRangeDb());

    const AudioBuffer output = runGate(input, gate);
    writeWav(outputPath("l08_gate_input.wav"), input, sampleRate);
    writeWav(outputPath("l08_gate_output.wav"), output, sampleRate);

    // ---- 1. RMS improvement in the silent sections -------------------------------------------------
    std::printf("\n[1] RMS of the silent sections (left channel)\n");
    struct Segment {
        const char* name;
        double start, end;
    };
    std::vector<Segment> segments;
    if (synthetic) {
        segments.push_back({"gap at end of bar 1, 1.90-2.00 s", 1.90, 2.00});
        segments.push_back({"trailing silence 4.50-5.00 s    ", 4.50, 5.00});
        segments.push_back({"whole file (with drums) 0-5 s   ", 0.0, totalSec});
    } else {
        segments.push_back({"whole file", 0.0, totalSec});
    }
    for (const auto& s : segments) {
        const int a = secondsToSamples(s.start, sampleRate);
        const int b = std::min(input.getNumSamples(), secondsToSamples(s.end, sampleRate));
        const double before = rmsDbOfRange(input, 0, a, b);
        const double after = rmsDbOfRange(output, 0, a, b);
        std::printf("    %s  before %7.1f dBFS -> after %7.1f dBFS (improvement %.1f dB)\n", s.name, before, after,
                    before - after);
    }
    std::printf("    peak: before %.2f dBFS, after %.2f dBFS (the hits' peaks should not be clipped away)\n",
                gainToDb(peakAbs(input)), gainToDb(peakAbs(output)));

    // ---- 2. State machine transition timeline ---------------------------------------------------
    std::printf("\n[2] State machine transition timeline (first 20)\n");
    const auto log = traceStates(input, gate);
    std::printf("    time(ms)   transition\n");
    for (size_t i = 0; i < log.size() && i < 20; ++i)
        std::printf("    %8.2f   %-7s -> %s\n", log[i].timeSec * 1000.0, gateStateName(log[i].from),
                    gateStateName(log[i].to));
    std::printf("    ... %zu transitions in total (%.1f seconds)\n", log.size(), totalSec);

    // ---- 3. What happens without hysteresis ---------------------------------------------
    Gate noHyst = gate;
    noHyst.setHysteresisDb(0.0f);
    noHyst.setHoldMs(0.0f);
    noHyst.reset();
    const auto logNoHyst = traceStates(input, noHyst);
    const AudioBuffer outNoHyst = runGate(input, noHyst);
    writeWav(outputPath("l08_gate_no_hysteresis.wav"), outNoHyst, sampleRate);
    std::printf("\n[3] With hysteresis = 0 and hold = 0: %zu transitions (with hysteresis + hold: %zu)\n", logNoHyst.size(),
                log.size());
    std::printf("    The more transitions, the more violently the gate flutters around the threshold (chattering).\n");

    // ---- 4. Detector envelope ripple ----------------------------------------------
    std::printf("\n[4] Envelope ripple of the gate detector (peak, attack %.1f ms) on a steady sine wave (peak-to-peak, dB)\n",
                Gate::kDetectorAttackMs);
    std::printf("    Estimate (README 2.4.1): during release the one-pole decays toward |x|, not toward 0; over each half period 1/(2f) it drops by\n"
                "    dE/A ~= (1 - 2/pi) / (2f*tau) -> ripple ~= 8.686 * (1 - 2/pi) / (2f*tau) dB. "
                "If the detector decayed toward 0 (analog diode + RC), it would be 8.686 / (2f*tau) dB\n");
    std::printf("    freq(Hz)   release 20 ms meas. / est. / decay-to-0    release 50 ms meas. / est. / decay-to-0\n");
    const double kOneMinus2OverPi = 1.0 - 2.0 / kPi; // the mean of |sin| is 2/pi
    for (double f : {45.0, 100.0, 185.0, 1000.0}) {
        const double halfPeriodMs = 1000.0 / (2.0 * f);
        std::printf("    %7.0f      %6.2f / %5.2f / %5.2f                  %6.2f / %5.2f / %5.2f\n", f,
                    measureRippleDb(f, 20.0f, sampleRate), 8.686 * kOneMinus2OverPi * halfPeriodMs / 20.0,
                    8.686 * halfPeriodMs / 20.0, measureRippleDb(f, 50.0f, sampleRate),
                    8.686 * kOneMinus2OverPi * halfPeriodMs / 50.0, 8.686 * halfPeriodMs / 50.0);
    }
    std::printf("    -> hysteresis must exceed this ripple: low frequencies (kick) need a larger hysteresis or a longer detector release than high frequencies (hi-hat)\n");

    // ---- 5. Changing the range while the gate is closed: the gain ramps over instead of jumping --------------------------------
    {
        Gate g = gate;
        g.reset(); // Closed, gain = -80 dB
        auto glide = [&](float newRangeDb) {
            g.setRangeDb(newRangeDb);
            float prevDb = gainToDb(g.nextGain(0.0f), -200.0f);
            float maxStep = 0.0f;
            int n = 1;
            for (; n < 48000; ++n) {
                const float db = gainToDb(g.nextGain(0.0f), -200.0f);
                maxStep = std::max(maxStep, std::abs(db - prevDb));
                prevDb = db;
                if (std::abs(db - g.getRangeDb()) < 1.0e-3f)
                    break;
            }
            std::printf("    range -> %4.0f dB: reached in %5d samples (%5.2f ms), maximum change per sample %.3f dB\n", newRangeDb, n + 1,
                        samplesToMs(n + 1, sampleRate), maxStep);
        };
        std::printf("\n[5] With the gate closed, change the range from -80 to -20 and back to -80 (simulating automation)\n");
        glide(-20.0f); // Going up uses the attack slope: 60 dB / (20 dB / 1 ms) = 3 ms
        glide(-80.0f); // Going down uses the release slope: 60 dB / (80 dB / 50 ms) = 37.5 ms
    }

    std::printf("\nOutput: dawlab_output/l08_gate_input.wav, l08_gate_output.wav, l08_gate_no_hysteresis.wav\n");
    return 0;
}
