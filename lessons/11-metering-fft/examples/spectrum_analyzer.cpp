// Example 5: SpectrumAnalyzer on the pluck sequence, as an ASCII spectrum (logarithmic frequency axis)
//
// What it demonstrates:
//   - Simulating the audio callback: pushBlock() is called once every 512 samples (the audio-thread side).
//   - Simulating the UI timer: computeSpectrum() is called after every block (the UI-thread side); it returns true
//     only once a hop's worth of new samples (N/2 = 2048, N = 4096) has accumulated, and false otherwise.
//   - At a few points in time it prints an ASCII spectrum on a 20 Hz .. 20 kHz logarithmic frequency axis and finds
//     the strongest bin. The pitches of the A-minor arpeggio are 110, 164.8, 220, 261.6, 329.6 Hz ...; the harmonic
//     distribution of a Karplus-Strong pluck is determined by the initial noise, so the strongest bin is usually
//     some harmonic (an integer multiple of the fundamental), not necessarily the fundamental itself.
//   - Compares smoothing = 0 with 0.8 (smoothing steadies the display but reacts slowly).
//   - Calibration: a sine of amplitude 0.5 exactly on a bin centre reads -6.02 dBFS (coherent-gain normalization);
//     off the bin centre it reads lower by the scalloping loss.
//   - Finally it spawns a real std::thread as the audio thread, with the main thread as the UI thread, to verify
//     that the lock-free FIFO never yields a corrupted frame.
//   The important numbers all have DAWLAB_CHECK, and [PASS] / [FAIL] is printed at the end (ctest runs this program too).
//
// How to run: bin/Debug/l11_spectrum_analyzer.exe (no arguments needed; argv[1] may name an input WAV)
// Output file: dawlab_output/l11_spectrum_plucks.wav
//
// In the real DAW (chapter 14), pushBlock runs on the audio thread and computeSpectrum on a UI timer (e.g. 30 Hz);
// the two sides communicate through a lock-free ring buffer. For deterministic output, the first half here is
// simulated sequentially on a single thread.

#include <dawlab/Common.h>
#include <dawlab/dsp/SpectrumAnalyzer.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using namespace dawlab;
using dawlab::dsp::SpectrumAnalyzer;
using dawlab::dsp::WindowType;

namespace {

constexpr int kBlockSize = 512;

// Logarithmic frequency axis: `columns` columns from fLow to fHigh, each taking the maximum of the bins in its range
void plotLogSpectrum(const SpectrumAnalyzer& analyzer, const std::vector<float>& db, double fLow, double fHigh,
                     int columns, int rows, float topDb, float bottomDb) {
    std::vector<float> col(static_cast<size_t>(columns), -200.0f);
    const double logLow = std::log(fLow), logRange = std::log(fHigh) - logLow;
    for (int k = 1; k < analyzer.getNumBins(); ++k) {
        const double f = analyzer.getBinFrequency(k);
        if (f < fLow || f > fHigh)
            continue;
        const int c = std::clamp(static_cast<int>((std::log(f) - logLow) / logRange * columns), 0, columns - 1);
        col[static_cast<size_t>(c)] = std::max(col[static_cast<size_t>(c)], db[static_cast<size_t>(k)]);
    }
    // A low-frequency column may contain no bin at all (the bin is wider than the column): fill it with the nearest value to the left
    for (int c = 1; c < columns; ++c)
        if (col[static_cast<size_t>(c)] <= -200.0f)
            col[static_cast<size_t>(c)] = col[static_cast<size_t>(c - 1)];
    const float stepDb = (topDb - bottomDb) / static_cast<float>(rows);
    for (int r = 0; r < rows; ++r) {
        const float levelDb = topDb - stepDb * static_cast<float>(r);
        std::string line;
        for (int c = 0; c < columns; ++c)
            line += col[static_cast<size_t>(c)] >= levelDb ? '#' : ' ';
        std::printf("%6.0f dB |%s|\n", levelDb, line.c_str());
    }
    std::printf("%9s +%s+\n", "", std::string(static_cast<size_t>(columns), '-').c_str());
    std::printf("%9s  20   50  100   200   500   1k    2k    5k   10k  20k Hz\n", "");
}

// Find the strongest bin, searching only from 30 Hz upward: the initial noise of a Karplus-Strong pluck has a DC
// component that leaks into bins 1 and 2 through the window's main lobe, and that is not the pitch we want to see.
int peakBin(const SpectrumAnalyzer& analyzer, const std::vector<float>& db) {
    int best = -1;
    for (int k = 1; k < static_cast<int>(db.size()); ++k) {
        if (analyzer.getBinFrequency(k) < 30.0)
            continue;
        if (best < 0 || db[static_cast<size_t>(k)] > db[static_cast<size_t>(best)])
            best = k;
    }
    return std::max(best, 1);
}

} // namespace

int main(int argc, char** argv) {
    double sr = 48000.0;
    AudioBuffer audio;
    if (argc > 1) {
        WavData wav;
        std::string err;
        if (!readWav(argv[1], wav, &err)) {
            std::printf("Failed to read: %s\n", err.c_str());
            return 1;
        }
        audio = wav.buffer;
        sr = wav.sampleRate;
    } else {
        audio = makePluckSequence(sr, 4.0, 0.4f, 2);
    }
    writeWav(outputPath("l11_spectrum_plucks.wav"), audio, sr);

    SpectrumAnalyzer analyzer;
    analyzer.prepare(sr, 12); // N = 4096 -> bin width 11.7 Hz, enough to separate the low harmonics
    analyzer.setWindowType(WindowType::Hann);
    analyzer.setSmoothing(0.8f);
    analyzer.setOverlap(0.5f);
    std::printf("N = %d, bin width = %.2f Hz, hop = %d samples, window = %s, smoothing = %.1f\n\n",
                analyzer.getFftSize(), analyzer.getBinWidthHz(), analyzer.getHopSize(),
                dsp::windowTypeName(analyzer.getWindowType()), analyzer.getSmoothing());

    const double snapshotTimes[] = {0.15, 1.1, 2.1, 3.1}; // correspond to the 1st, 5th, 9th and 13th notes of the arpeggio
    size_t nextSnapshot = 0;
    std::vector<float> db;
    int frames = 0, polls = 0;

    for (int start = 0; start < audio.getNumSamples(); start += kBlockSize) {
        const int len = std::min(kBlockSize, audio.getNumSamples() - start);
        analyzer.pushBlock(AudioBlock(audio).getSubBlock(start, len)); // <- the audio-thread side

        ++polls;
        if (analyzer.computeSpectrum(db)) // <- the UI-thread side
            ++frames;

        const double t = (start + len) / sr;
        if (nextSnapshot < 4 && t >= snapshotTimes[nextSnapshot]) {
            const int pk = peakBin(analyzer, db);
            std::printf("---- t = %.2f s: strongest bin = %d (%.1f Hz, %.1f dBFS) ----\n", t, pk,
                        analyzer.getBinFrequency(pk), db[static_cast<size_t>(pk)]);
            plotLogSpectrum(analyzer, db, 20.0, 20000.0, 64, 10, 0.0f, -100.0f);
            std::printf("\n");
            ++nextSnapshot;
        }
    }
    std::printf("computeSpectrum was called %d times, %d of which produced a new frame (hop = %d, block = %d -> one frame every %d blocks)\n\n",
                polls, frames, analyzer.getHopSize(), kBlockSize, analyzer.getHopSize() / kBlockSize);
    // The first frame is produced once N samples have accumulated, then one per hop samples
    const int expectedFrames = (audio.getNumSamples() - analyzer.getFftSize()) / analyzer.getHopSize() + 1;
    if (audio.getNumSamples() % kBlockSize == 0 && analyzer.getHopSize() % kBlockSize == 0)
        DAWLAB_CHECK(frames == expectedFrames);

    // No new data -> false
    std::vector<float> again;
    const bool gotAgain = analyzer.computeSpectrum(again);
    std::printf("computeSpectrum returns %s when no new samples were pushed\n", gotAgain ? "true" : "false");
    DAWLAB_CHECK(!gotAgain);

    // ---- Calibration: on a bin centre vs off it -----------------------------------------------------
    {
        SpectrumAnalyzer cal;
        cal.prepare(48000.0, 12);
        cal.setSmoothing(0.0f);
        std::vector<float> calDb;
        const double centred = 100.0 * cal.getBinWidthHz(); // 1171.875 Hz, exactly bin 100
        AudioBuffer s1 = makeSine(centred, 48000.0, 0.2, 0.5f, 1);
        cal.pushSamples(s1.getReadPointer(0), s1.getNumSamples());
        cal.computeSpectrum(calDb);
        const float atCentre = calDb[100];
        cal.prepare(48000.0, 12);
        cal.setSmoothing(0.0f);
        const double between = 100.5 * cal.getBinWidthHz(); // exactly halfway between bins 100 and 101
        AudioBuffer s2 = makeSine(between, 48000.0, 0.2, 0.5f, 1);
        cal.pushSamples(s2.getReadPointer(0), s2.getNumSamples());
        cal.computeSpectrum(calDb);
        const float halfBin = std::max(calDb[100], calDb[101]);
        std::printf("\nCalibration (Hann, N = 4096, amplitude 0.5 = -6.02 dBFS): bin centre %.2f dB; 0.5 bin off %.2f dB"
                    " (%.2f dB lower = Hann's scalloping loss)\n",
                    atCentre, halfBin, atCentre - halfBin);
        DAWLAB_CHECK_NEAR(atCentre, -6.02, 0.02);
        DAWLAB_CHECK_NEAR(atCentre - halfBin, 1.42, 0.05);
    }

    // Smoothing comparison: for the same signal, the spectrum with α = 0 changes instantly when the note changes; with α = 0.8 it takes several frames
    {
        SpectrumAnalyzer fast, slow;
        fast.prepare(sr, 12);
        slow.prepare(sr, 12);
        fast.setSmoothing(0.0f);
        slow.setSmoothing(0.8f);
        AudioBuffer bursts = makeToneBursts(440.0, sr, {-6.0f, -200.0f}, 0.5, 1);
        std::vector<float> dbFast, dbSlow;
        std::printf("\nSmoothing comparison: a 440 Hz sine that suddenly goes silent after 0.5 s; watch the 440 Hz bin\n");
        std::printf("%8s %12s %12s\n", "time(s)", "alpha=0", "alpha=0.8");
        const int bin440 = static_cast<int>(std::lround(440.0 / fast.getBinWidthHz()));
        float prevSlow = 0.0f, lastSlowStep = 0.0f;
        for (int start = 0; start < bursts.getNumSamples(); start += kBlockSize) {
            const int len = std::min(kBlockSize, bursts.getNumSamples() - start);
            fast.pushSamples(bursts.getReadPointer(0) + start, len);
            slow.pushSamples(bursts.getReadPointer(0) + start, len);
            const bool a = fast.computeSpectrum(dbFast);
            const bool b = slow.computeSpectrum(dbSlow);
            const double t = (start + len) / sr;
            if (a && b && t > 0.4 && t < 0.75) {
                std::printf("%8.3f %9.1f dB %9.1f dB\n", t, dbFast[static_cast<size_t>(bin440)],
                            dbSlow[static_cast<size_t>(bin440)]);
                lastSlowStep = prevSlow - dbSlow[static_cast<size_t>(bin440)];
                prevSlow = dbSlow[static_cast<size_t>(bin440)];
            }
        }
        // 440 Hz is 0.45 bin off the bin centre -> Hann's scalloping makes the reading about 1.2 dB below -6.02
        // After the silence with α = 0.8: each frame is multiplied by 0.8 -> 20·log10(0.8) = -1.94 dB / frame
        std::printf("With α = 0.8 each frame after the silence drops %.2f dB (theory: 20·log10(0.8) = %.2f dB)\n", lastSlowStep,
                    -20.0 * std::log10(0.8));
        DAWLAB_CHECK_NEAR(lastSlowStep, -20.0 * std::log10(0.8), 0.05);
        DAWLAB_CHECK(dbFast[static_cast<size_t>(bin440)] <= kMinusInfinityDb);
    }
    // ---- Using two real threads: the audio thread pushes a 1 kHz sine while the UI thread keeps reading ----------
    // No mutex. If the FIFO is wrong (for example a forgotten release/acquire, or overwrites that go undetected), the UI
    // side reads "old and new mixed" frames: the discontinuity at the seam leaks energy over the whole band. So we check
    // two things:
    //   (1) the strongest bin is at 1 kHz; (2) the maximum reading above 5 kHz is < -80 dB (a clean Hann frame is far
    //       below that, while a frame with a seam is tens of dB higher). The number of frames differs on every run
    //       (it depends on thread scheduling).
    {
        SpectrumAnalyzer live;
        live.prepare(sr, 11);
        live.setSmoothing(0.0f);
        std::atomic<bool> done{false};
        std::atomic<int> framesSeen{0};
        int pushedBlocks = 0; // only the audio thread writes it; the main thread reads it only after join()
        std::thread audioThread([&] {
            std::vector<float> block(static_cast<size_t>(kBlockSize));
            double phase = 0.0;
            // Push at full speed (not in real time) until the UI side has 50 frames (capped at 200000 blocks to avoid an infinite loop)
            for (int b = 0; b < 200000 && framesSeen.load(std::memory_order_relaxed) < 50; ++b, ++pushedBlocks) {
                for (float& v : block) {
                    v = static_cast<float>(0.5 * std::sin(phase));
                    phase += kTwoPi * 1000.0 / sr;
                }
                live.pushSamples(block.data(), kBlockSize);
            }
            done.store(true, std::memory_order_release);
        });
        int liveFrames = 0, badFrames = 0;
        float worstHighDb = -200.0f;
        std::vector<float> liveDb;
        const int firstHighBin = static_cast<int>(5000.0 / live.getBinWidthHz());
        while (!done.load(std::memory_order_acquire)) {
            if (!live.computeSpectrum(liveDb))
                continue;
            ++liveFrames;
            framesSeen.store(liveFrames, std::memory_order_relaxed);
            float highDb = -200.0f;
            for (int k = firstHighBin; k < live.getNumBins(); ++k)
                highDb = std::max(highDb, liveDb[static_cast<size_t>(k)]);
            worstHighDb = std::max(worstHighDb, highDb);
            if (std::abs(live.getBinFrequency(peakBin(live, liveDb)) - 1000.0) > live.getBinWidthHz() || highDb > -80.0f)
                ++badFrames;
        }
        audioThread.join();
        std::printf("\nTwo threads running at once: the audio thread pushed %d blocks (%.1f seconds of audio), the UI side computed %d frames, "
                    "%d bad frames (should be 0); the maximum reading above 5 kHz over all frames = %.1f dB\n",
                    pushedBlocks, pushedBlocks * kBlockSize / sr, liveFrames, badFrames, worstHighDb);
        DAWLAB_CHECK(liveFrames > 0);
        DAWLAB_CHECK(badFrames == 0);
    }

    std::printf("\n-> dawlab_output/l11_spectrum_plucks.wav\n");
    return dawlab::test::summary("l11_spectrum_analyzer");
}
