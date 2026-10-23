// Example 2: impulse response of an algorithmic reverb (8-line FDN)
//
// What it demonstrates:
//   1. Feed an impulse into dawlab::dsp::Reverb (mix = 1, damping off), record the impulse response,
//      print an ASCII decay curve (RMS envelope in dB, one row per 100 ms), and measure the RT60 with
//      measureRt60() to compare it with the value we set.
//   2. Print the lengths of the 8 delay lines (in samples, all prime) and each line's decay gain
//      g_i = 10^(-3*L_i/(RT60*fs)).
//   3. The effect of damping: with damping on (4 kHz) the broadband RT60 gets shorter, and 8 kHz decays
//      much faster than 250 Hz.
//   4. Freeze: once frozen, the tail no longer decays (the RMS barely changes over 1 second).
//   5. Echo density: how many "non-zero samples" the FDN impulse response has in the first 100 ms,
//      which shows the effect of diffusion.
//
// How to run: run with no arguments (ctest runs it too).
// Output files (build/dawlab_output/):
//   l12_fdn_ir_2s.wav        stereo impulse response with RT60 = 2 s and no damping
//   l12_fdn_ir_damped.wav    impulse response with RT60 = 2 s and damping at 4 kHz (the tail gets darker and darker)
//   l12_fdn_ir_frozen.wav    impulse response frozen after 0.2 s (the tail never dies away)
// What you should hear: a "pop" followed by a smooth tail with no obvious flutter echo; the damped version
// sounds like a carpeted room; the frozen version turns into a pad that never dissipates.

#include <dawlab/Common.h>
#include <dawlab/dsp/Reverb.h>
#include <dawlab/dsp/ReverbBuildingBlocks.h>

#include <cstdio>
#include <string>
#include <vector>

using namespace dawlab;
using namespace dawlab::dsp;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

// Feed an impulse block by block (simulating an audio callback) and return the stereo impulse response
AudioBuffer impulseResponse(Reverb& reverb, double seconds, double freezeAtSeconds = -1.0) {
    const int n = secondsToSamples(seconds, kSampleRate);
    AudioBuffer out = makeImpulse(n, 2);
    reverb.reset();
    reverb.setFreeze(false);
    const int freezeAt = freezeAtSeconds >= 0.0 ? secondsToSamples(freezeAtSeconds, kSampleRate) : -1;
    for (int start = 0; start < n; start += kBlockSize) {
        if (freezeAt >= 0 && start >= freezeAt)
            reverb.setFreeze(true);
        const int len = std::min(kBlockSize, n - start);
        reverb.process(out.toBlock().getSubBlock(start, len));
    }
    reverb.setFreeze(false);
    return out;
}

// Band energy: run Goertzel at 25 frequency points between freq*(1 +/- 12%) and sum the energies.
// The tail of an FDN is a dense set of modes (resonances); a single frequency point may land exactly on a
// mode or between modes, so the measured decay would jump around. Summing the energy of a whole band is
// much smoother (just like octave-band analysis).
double bandEnergy(const float* x, int w, double freq) {
    double e = 0.0;
    for (int k = -12; k <= 12; ++k) {
        const double a = goertzelAmplitude(x, w, freq * (1.0 + 0.01 * k), kSampleRate);
        e += a * a;
    }
    return e;
}

// Energy ratio of one band across two time windows -> RT60 of that band
double rt60AtFrequency(const AudioBuffer& ir, double freq, double t1, double t2, double windowSec) {
    const int w = secondsToSamples(windowSec, kSampleRate);
    const float* x = ir.getReadPointer(0);
    const double e1 = bandEnergy(x + secondsToSamples(t1, kSampleRate), w, freq);
    const double e2 = bandEnergy(x + secondsToSamples(t2, kSampleRate), w, freq);
    const double slope = 10.0 * std::log10(e2 / e1) / (t2 - t1); // energy uses 10*log10
    return slope < 0.0 ? -60.0 / slope : 0.0;
}

// ASCII decay curve: one row per stepSec, printing the RMS (dB, relative to the loudest row) and a bar
void printDecayCurve(const AudioBuffer& ir, double stepSec, double totalSec) {
    const int w = secondsToSamples(stepSec, kSampleRate);
    const int rows = static_cast<int>(totalSec / stepSec);
    double ref = -200.0;
    std::vector<double> levels;
    for (int r = 0; r < rows; ++r) {
        const float v = rms(ir.getReadPointer(0) + r * w, w);
        const double db = v > 0.0f ? 20.0 * std::log10(v) : -200.0;
        levels.push_back(db);
        ref = std::max(ref, db);
    }
    std::printf("   t(s)   dB   |-60 dB                                   0 dB|\n");
    for (int r = 0; r < rows; ++r) {
        const double rel = levels[static_cast<size_t>(r)] - ref;
        const int bars = std::clamp(static_cast<int>((rel + 60.0) / 60.0 * 50.0), 0, 50);
        std::printf("  %5.2f %6.1f  |%s%s|\n", r * stepSec, rel, std::string(static_cast<size_t>(bars), '#').c_str(),
                    std::string(static_cast<size_t>(50 - bars), ' ').c_str());
    }
}

} // namespace

int main() {
    std::printf("=== Chapter 12 Example 2: impulse response of an FDN reverb ===\n\n");

    Reverb reverb;
    reverb.prepare(kSampleRate, kBlockSize, 2);
    reverb.setMix(1.0f);
    reverb.setWidth(1.0f);
    reverb.setSize(0.5f);
    reverb.setPreDelayMs(0.0f);
    reverb.setDampingHz(20000.0f); // >= 20 kHz = no damping
    reverb.setDecaySeconds(2.0f);

    // ---- 1. Delay-line lengths and decay gains -------------------------------------
    std::printf("--- The 8 delay lines (size = 0.5) ---\n");
    std::printf("%6s %10s %10s %12s\n", "line", "L(samples)", "L(ms)", "g_i(RT60=2s)");
    for (int i = 0; i < Reverb::kNumLines; ++i)
        std::printf("%6d %10.0f %10.2f %12.6f\n", i, reverb.getLineDelaySamples(i),
                    samplesToMs(reverb.getLineDelaySamples(i), kSampleRate), reverb.getLineGain(i));
    std::printf("(All lengths are prime, so they share no common factors; g_i = 10^(-3*L_i / (RT60*fs)))\n\n");

    // ---- 2. Requested RT60 vs measured value ---------------------------------------
    std::printf("--- RT60 verification (no damping; windowed-RMS regression over the -5 .. -35 dB range) ---\n");
    std::printf("%10s %12s %10s\n", "set(s)", "measured(s)", "error(%)");
    AudioBuffer ir2s; // keep the RT60 = 2 s IR so we can draw its decay curve after the table is printed
    for (float rt60 : {0.5f, 1.0f, 2.0f, 4.0f}) {
        reverb.setDecaySeconds(rt60);
        AudioBuffer ir = impulseResponse(reverb, rt60 * 1.2 + 0.3);
        const DecayMeasurement m = measureRt60(ir.getReadPointer(0), ir.getNumSamples(), kSampleRate, 20.0);
        std::printf("%10.2f %12.3f %10.1f\n", rt60, m.rt60Seconds, (m.rt60Seconds / rt60 - 1.0) * 100.0);
        if (rt60 == 2.0f)
            ir2s = ir;
    }
    writeWav(outputPath("l12_fdn_ir_2s.wav"), ir2s, kSampleRate);
    std::printf("\n  Decay curve for RT60 = 2 s (left channel, RMS every 100 ms):\n");
    printDecayCurve(ir2s, 0.1, 2.4);
    std::printf("  peak = %.3f, contains NaN/Inf = %s\n", peakAbs(ir2s), hasNanOrInf(ir2s) ? "yes" : "no");
    std::printf("\n");

    // ---- 3. Echo density -----------------------------------------------------------
    {
        reverb.setDecaySeconds(2.0f);
        AudioBuffer ir = impulseResponse(reverb, 0.5);
        std::printf("--- Echo density: number of samples with |h[n]| > 1e-4 in each 10 ms "
                    "(diffusion quickly drives it toward 480 = completely full) ---\n   ");
        for (int k = 0; k < 10; ++k) {
            const int w = secondsToSamples(0.01, kSampleRate);
            int count = 0;
            for (int i = 0; i < w; ++i)
                if (std::abs(ir.getSample(0, k * w + i)) > 1e-4f)
                    ++count;
            std::printf("%4d", count);
        }
        std::printf("\n\n");
    }

    // ---- 4. Damping ----------------------------------------------------------------
    {
        reverb.setDecaySeconds(2.0f);
        reverb.setDampingHz(4000.0f);
        AudioBuffer damped = impulseResponse(reverb, 2.7);
        writeWav(outputPath("l12_fdn_ir_damped.wav"), damped, kSampleRate);
        const DecayMeasurement m = measureRt60(damped.getReadPointer(0), damped.getNumSamples(), kSampleRate, 20.0);
        std::printf("--- damping = 4 kHz (RT60 set to 2 s) ---\n");
        std::printf("  Measured broadband RT60 = %.3f s (shorter than the set value: the highs vanish first, so the RMS curve gets steeper)\n",
                    m.rt60Seconds);
        reverb.setDampingHz(20000.0f);
        AudioBuffer plain = impulseResponse(reverb, 2.7);
        std::printf("%10s %14s %14s\n", "freq(Hz)", "no damping(s)", "damping4k(s)");
        for (double f : {250.0, 1000.0, 4000.0, 8000.0})
            std::printf("%10.0f %14.3f %14.3f\n", f, rt60AtFrequency(plain, f, 0.2, 1.0, 0.3),
                        rt60AtFrequency(damped, f, 0.2, 1.0, 0.3));
        std::printf("(Real rooms behave the same way: air and walls absorb high frequencies, so the RT60 of the highs is shorter than that of the lows)\n\n");
    }

    // ---- 5. Freeze -----------------------------------------------------------------
    {
        reverb.setDecaySeconds(1.0f);
        AudioBuffer frozen = impulseResponse(reverb, 2.0, 0.2);
        writeWav(outputPath("l12_fdn_ir_frozen.wav"), frozen, kSampleRate);
        const int w = secondsToSamples(0.3, kSampleRate);
        const float a = rms(frozen.getReadPointer(0) + secondsToSamples(0.5, kSampleRate), w);
        const float b = rms(frozen.getReadPointer(0) + secondsToSamples(1.6, kSampleRate), w);
        std::printf("--- Freeze (frozen at 0.2 s; the RT60 was originally 1 s) ---\n");
        std::printf("  RMS @0.5s = %.5f, RMS @1.6s = %.5f, difference = %+.2f dB (without freeze it would drop 66 dB over 1.1 s)\n\n",
                    a, b, gainToDb(b / a));
    }

    std::printf("Output: dawlab_output/l12_fdn_ir_2s.wav, l12_fdn_ir_damped.wav, l12_fdn_ir_frozen.wav\n");
    return 0;
}
