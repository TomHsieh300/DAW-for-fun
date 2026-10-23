// Example 3: LevelMeter timeline (peak / RMS / peak hold)
//
// What it demonstrates:
//   1. Ballistics check: a -6 dBFS 1 kHz sine plays for 0.5 s and then goes silent. The peak goes straight to
//      -6 dB, then falls at 24 dB/s after the silence starts (about -30 dB after 1 s); the hold stays at -6 dB
//      for a full 1 s before it starts to fall; the 300 ms-window RMS reads -9.01 dB (sine RMS = peak / √2).
//   2. Timeline of a drum loop: one line every 50 ms with peak / RMS / hold and an ASCII bar (left channel).
//
// How to run: bin/Debug/l11_level_meter.exe (no arguments needed; argv[1] may name an input WAV)
// Output file: dawlab_output/l11_level_meter_drums.wav (the measured drum loop itself)
// What to listen for: kick and snare make the peak jump instantly while the RMS follows slowly; the hi-hat has a
// high peak but barely registers on the RMS.

#include <dawlab/Common.h>
#include <dawlab/dsp/LevelMeter.h>

#include <cstdio>
#include <string>

using namespace dawlab;
using dawlab::dsp::LevelMeter;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

// Draw dB as a 40-cell bar: '#' = RMS, 'P' = peak, '|' = hold (-60 .. 0 dB)
std::string bar(float peakDb, float rmsDb, float holdDb) {
    constexpr int width = 40;
    auto pos = [](float db) { return std::clamp(static_cast<int>((db + 60.0f) / 60.0f * width), -1, width - 1); };
    std::string s(static_cast<size_t>(width), ' ');
    const int r = pos(rmsDb), p = pos(peakDb), h = pos(holdDb);
    for (int i = 0; i <= r; ++i)
        s[static_cast<size_t>(i)] = '#';
    if (p >= 0)
        s[static_cast<size_t>(p)] = 'P';
    if (h >= 0)
        s[static_cast<size_t>(h)] = '|';
    return s;
}

// Feed the meter block by block and print once every printEveryMs
void runTimeline(LevelMeter& meter, AudioBuffer& audio, double printEveryMs, double stopSeconds) {
    const int printEvery = msToSamples(printEveryMs, kSampleRate);
    int nextPrint = 0;
    std::printf("%8s %9s %9s %9s  %s\n", "time(s)", "peak dB", "RMS dB", "hold dB", "-60 dB [#=RMS P=peak |=hold] 0 dB");
    for (int start = 0; start < audio.getNumSamples(); start += kBlockSize) {
        const int len = std::min(kBlockSize, audio.getNumSamples() - start);
        meter.process(AudioBlock(audio).getSubBlock(start, len));
        if (start + len >= nextPrint) {
            const double t = (start + len) / kSampleRate;
            if (t > stopSeconds)
                break;
            std::printf("%8.3f %9.2f %9.2f %9.2f  %s\n", t, meter.getPeakDb(0), meter.getRmsDb(0),
                        meter.getPeakHoldDb(0), bar(meter.getPeakDb(0), meter.getRmsDb(0), meter.getPeakHoldDb(0)).c_str());
            nextPrint += printEvery;
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    LevelMeter meter;
    meter.prepare(kSampleRate, 2);
    meter.setPeakHoldMs(1000.0f);
    meter.setDecayDbPerSecond(24.0f);
    meter.setRmsWindowMs(300.0f);

    // ---- 1. Ballistics: -6 dBFS sine for 0.5 s + 1.5 s of silence ---------------------------
    {
        AudioBuffer burst = makeToneBursts(1000.0, kSampleRate, {-6.0f, -100.0f, -100.0f, -100.0f}, 0.5, 2);
        std::printf("== 1. Ballistics: -6 dBFS 1 kHz sine for 0.5 s, then silence (decay 24 dB/s, hold 1 s, RMS 300 ms) ==\n");
        runTimeline(meter, burst, 100.0, 2.0);
        // The mathematical value is computed in double; LevelMeter stores it as float (spacing about 6e-8), so the last
        // digit differs when 8 decimals are printed
        std::printf("Per-sample decay coefficient g = 10^(-24 / (20·48000)) = %.10f (%.8f once stored as float)\n",
                    std::pow(10.0, -24.0 / (20.0 * kSampleRate)),
                    static_cast<double>(LevelMeter::decayCoefficient(24.0f, kSampleRate)));

        // Re-run with 10 ms (480-sample) blocks so the readings land exactly on 0.5 / 1.0 / 1.5 / 2.0 s, and check numerically:
        //   0.5 s: peak = hold = -6.00 (dbToGain(-6) = 0.501, and a 1 kHz sine with 48 points per period lands exactly on the peak),
        //          RMS = -6.00 - 3.01 = -9.01 (sine RMS = peak / √2)
        //   1.0 s: peak has fallen 0.5 s × 24 dB/s = 12 dB -> -18.00; hold is still at -6.00
        //   1.5 s: peak -> -30.00; the hold's 1 s has just run out (about -6.0 ~ -6.1)
        //   2.0 s: peak -> -42.00; hold has fallen for 0.5 s -> -18.00
        meter.reset();
        float readings[4][3] = {};
        for (int b = 0; b < burst.getNumSamples() / 480; ++b) {
            meter.process(AudioBlock(burst).getSubBlock(b * 480, 480));
            if ((b + 1) % 50 == 0 && (b + 1) / 50 <= 4) {
                float* r = readings[(b + 1) / 50 - 1];
                r[0] = meter.getPeakDb(0);
                r[1] = meter.getRmsDb(0);
                r[2] = meter.getPeakHoldDb(0);
            }
        }
        std::printf("%8s %9s %9s %9s   %s\n", "time(s)", "peak dB", "RMS dB", "hold dB", "expected (peak / hold)");
        const float expectedPeak[4] = {-6.0f, -18.0f, -30.0f, -42.0f};
        const float expectedHold[4] = {-6.0f, -6.0f, -6.0f, -18.0f};
        for (int i = 0; i < 4; ++i) {
            std::printf("%8.1f %9.2f %9.2f %9.2f   %.2f / %.2f\n", 0.5 * (i + 1), readings[i][0], readings[i][1],
                        readings[i][2], expectedPeak[i], expectedHold[i]);
            DAWLAB_CHECK_NEAR(readings[i][0], expectedPeak[i], 0.05);
            DAWLAB_CHECK_NEAR(readings[i][2], expectedHold[i], 0.1);
        }
        DAWLAB_CHECK_NEAR(readings[0][1], -9.01, 0.02);
        DAWLAB_CHECK(readings[2][1] <= kMinusInfinityDb); // more than 300 ms of silence, so the RMS window is entirely empty
        std::printf("\n");
    }

    // ---- 2. Drum loop timeline --------------------------------------------------------------
    AudioBuffer drums;
    if (argc > 1) {
        WavData wav;
        std::string err;
        if (!readWav(argv[1], wav, &err)) {
            std::printf("Failed to read: %s\n", err.c_str());
            return 1;
        }
        drums = wav.buffer;
    } else {
        drums = makeDrumLoop(kSampleRate, 1, 120.0, 0.8f, 2); // 1 bar = 2 s
    }
    writeWav(outputPath("l11_level_meter_drums.wav"), drums, kSampleRate);
    meter.reset();
    std::printf("== 2. Drum loop (120 BPM, kick on beats 1, 2.5 and 3, snare on beats 2 and 4) ==\n");
    runTimeline(meter, drums, 50.0, 2.0);
    const float samplePeakDb = gainToDb(peakAbs(drums));
    const float holdDb = meter.getPeakHoldDb(0);
    std::printf("\nOverall sample peak = %.2f dBFS; the hold reads %.2f dBFS at the end\n", samplePeakDb, holdDb);
    if (argc <= 1)
        std::printf("(The overall maximum occurs on the first kick, whose 1 s hold expired long ago; the current hold is the peak of the kick at 1.0 s.)\n");
    DAWLAB_CHECK(holdDb <= samplePeakDb + 0.01f); // the hold cannot exceed the true maximum
    DAWLAB_CHECK(holdDb >= meter.getPeakDb(0));   // the hold is never below the peak

    // The UI clicked the meter -> resetPeakHold(): on the next process() the hold drops straight back to the current peak
    meter.resetPeakHold();
    AudioBuffer silence(2, kBlockSize);
    meter.process(silence);
    std::printf("After resetPeakHold(): hold = %.2f dB, peak = %.2f dB\n", meter.getPeakHoldDb(0), meter.getPeakDb(0));
    DAWLAB_CHECK_NEAR(meter.getPeakHoldDb(0), meter.getPeakDb(0), 0.01);

    std::printf("-> dawlab_output/l11_level_meter_drums.wav\n");
    return dawlab::test::summary("l11_level_meter");
}
