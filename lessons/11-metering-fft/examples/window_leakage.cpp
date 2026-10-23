// Example 2: window functions and spectral leakage compared
//
// What it demonstrates:
//   - The same 1 kHz sine (N = 2048, fs = 48 kHz, bin width 23.4375 Hz, 1000 Hz sits at bin 42.67, off-centre)
//     is run through the FFT with four windows (Rectangular / Hann / Hamming / Blackman-Harris) and an ASCII
//     spectrum is printed for each.
//   - The signal also hides a weak -60 dB tone at 3.3 kHz: the leakage of the rectangular window swamps it,
//     while Hann / Blackman-Harris can still see it.
//   - Then a sine at 1007.8125 Hz (exactly bin 43) shows the special case where the leakage is "invisible".
//   - Finally, 16x zero-padding is used to measure each window's -3 dB main-lobe width, highest side lobe and
//     scalloping loss, and the results are compared with Harris's (1978) theoretical values (DAWLAB_CHECK).
//
// How to run: bin/Debug/l11_window_leakage.exe (no arguments needed)
// Output file: dawlab_output/l11_window_test_tone.wav (1 kHz + a faint 3.3 kHz; the second tone is almost inaudible)

#include <dawlab/Common.h>
#include <dawlab/dsp/Fft.h>

#include <algorithm>
#include <complex>
#include <cstdio>
#include <string>
#include <vector>

using namespace dawlab;
using dawlab::dsp::Fft;
using dawlab::dsp::WindowType;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kOrder = 11;
constexpr int kN = 1 << kOrder; // 2048
constexpr int kBins = kN / 2 + 1;

// Apply a window to the signal, run the FFT, and return the dBFS of every bin (a sine of amplitude 1.0 on a bin centre -> 0 dB)
std::vector<float> spectrumDb(const float* x, WindowType type) {
    static Fft fft(kOrder);
    std::vector<float> window;
    Fft::makeWindow(window, kN, type);
    const double cg = Fft::coherentGain(window.data(), kN);
    std::vector<float> frame(static_cast<size_t>(kN));
    for (int i = 0; i < kN; ++i)
        frame[static_cast<size_t>(i)] = x[i] * window[static_cast<size_t>(i)];
    std::vector<std::complex<float>> spec(static_cast<size_t>(kBins));
    fft.forwardReal(frame.data(), spec.data());
    std::vector<float> db(static_cast<size_t>(kBins));
    for (int k = 0; k < kBins; ++k) {
        const double scale = (k == 0 || k == kBins - 1) ? 1.0 : 2.0;
        db[static_cast<size_t>(k)] = gainToDb(static_cast<float>(scale * std::abs(spec[static_cast<size_t>(k)]) / (kN * cg)),
                                              -200.0f);
    }
    return db;
}

// ASCII spectrum: linear frequency axis 0 .. maxHz, each column takes the maximum of the bins in its range
void plotAscii(const std::vector<float>& db, double maxHz, int columns, int rows, float topDb, float bottomDb) {
    const double binHz = kSampleRate / kN;
    const int maxBin = std::min(kBins - 1, static_cast<int>(maxHz / binHz));
    std::vector<float> col(static_cast<size_t>(columns), -200.0f);
    for (int k = 0; k <= maxBin; ++k) {
        const int c = std::min(columns - 1, static_cast<int>(static_cast<double>(k) / (maxBin + 1) * columns));
        col[static_cast<size_t>(c)] = std::max(col[static_cast<size_t>(c)], db[static_cast<size_t>(k)]);
    }
    const float stepDb = (topDb - bottomDb) / static_cast<float>(rows);
    for (int r = 0; r < rows; ++r) {
        const float levelDb = topDb - stepDb * static_cast<float>(r);
        std::string line;
        for (int c = 0; c < columns; ++c)
            line += col[static_cast<size_t>(c)] >= levelDb ? '#' : (r == rows - 1 ? '.' : ' ');
        std::printf("%7.0f dB |%s|\n", levelDb, line.c_str());
    }
    std::printf("%10s +%s+\n", "", std::string(static_cast<size_t>(columns), '-').c_str());
    std::printf("%10s  0 Hz%*s%.0f Hz\n", "", columns - 10, "", maxHz);
}

// Measure the spectrum (DTFT) of the window itself: the -3 dB width of the main lobe (in units of the original
// bins) and the highest side lobe (dB).
//
// Why can't we just measure "a sine exactly on a bin centre"? Because then the FFT samples only at the centre of the
// main lobe and at the zeros of the side lobes, and there seems to be no leakage at all. To see the shape of the
// lobes we need to take "more points" between the bins: pad the N-point window with 15N zeros and run a 16N-point
// FFT (zero-padding adds no information, it just samples the DTFT more densely).
// scalloping loss: how many dB lower the peak reading is when the sine falls exactly halfway between two bins
// (0.5 bin off) than when it is on a bin centre. With 16x zero-padding, "0.5 bin off" is sample point 8.
void measureLobes(WindowType type, double& width3dB, double& highestSideLobe, double& scallopingLoss) {
    constexpr int kPadOrder = kOrder + 4; // 16x zero-padding -> 16 sample points between every pair of original bins
    constexpr int kPadN = 1 << kPadOrder;
    constexpr int kPadBins = kPadN / 2 + 1;
    static Fft fft(kPadOrder);
    std::vector<float> window;
    Fft::makeWindow(window, kN, type);
    std::vector<float> frame(static_cast<size_t>(kPadN), 0.0f);
    std::copy(window.begin(), window.end(), frame.begin());
    std::vector<std::complex<float>> spec(static_cast<size_t>(kPadBins));
    fft.forwardReal(frame.data(), spec.data());
    // Take bin 0 (= Σw, DC) as 0 dB
    const double peak = std::abs(spec[0]);
    std::vector<double> db(static_cast<size_t>(kPadBins));
    for (int k = 0; k < kPadBins; ++k)
        db[static_cast<size_t>(k)] = gainToDb(std::abs(spec[static_cast<size_t>(k)]) / peak, -300.0);
    // -3 dB point: the first position below -3 dB (linear interpolation), width = 2 x position / 16
    int k = 1;
    while (k < kPadBins && db[static_cast<size_t>(k)] > -3.0)
        ++k;
    const double a = db[static_cast<size_t>(k - 1)], b = db[static_cast<size_t>(k)];
    const double t = (a + 3.0) / std::max(1e-9, a - b);
    width3dB = 2.0 * (static_cast<double>(k - 1) + t) / 16.0;
    scallopingLoss = -db[8];
    // The main lobe ends at the first local minimum (the first zero); the maximum after that is the highest side lobe
    k = 1;
    while (k + 1 < kPadBins && db[static_cast<size_t>(k + 1)] < db[static_cast<size_t>(k)])
        ++k;
    highestSideLobe = -300.0;
    for (int j = k; j < kPadBins; ++j)
        highestSideLobe = std::max(highestSideLobe, db[static_cast<size_t>(j)]);
}

} // namespace

int main() {
    const WindowType windows[] = {WindowType::Rectangular, WindowType::Hann, WindowType::Hamming,
                                  WindowType::BlackmanHarris};
    const double binHz = kSampleRate / kN;
    std::printf("N = %d, fs = %.0f Hz -> bin width = fs/N = %.4f Hz\n", kN, kSampleRate, binHz);
    std::printf("1000 Hz falls at bin %.2f (off-centre -> leakage); 1007.8125 Hz is exactly bin 43\n\n", 1000.0 / binHz);

    // ---- Test signal: 1 kHz (0 dBFS) + 3.3 kHz (-60 dBFS) ------------------------------------
    AudioBuffer tone = makeSine(1000.0, kSampleRate, 1.0, 1.0f, 1);
    AudioBuffer weak = makeSine(3300.0, kSampleRate, 1.0, dbToGain(-60.0f), 1);
    tone.addFrom(0, 0, weak, 0, 0, tone.getNumSamples());
    {
        AudioBuffer stereo(2, tone.getNumSamples());
        stereo.copyFrom(0, 0, tone, 0, 0, tone.getNumSamples());
        stereo.copyFrom(1, 0, tone, 0, 0, tone.getNumSamples());
        stereo.applyGain(0.5f);
        writeWav(outputPath("l11_window_test_tone.wav"), stereo, kSampleRate);
    }

    const int weakBin = static_cast<int>(std::lround(3300.0 / binHz)); // 140.8 → 141
    // Theoretical reading when 1000 Hz is δ = 1/3 bin off the centre of bin 43: rectangular = sinc(δ), Hann = sinc(δ)/(1−δ²)
    const double delta = 43.0 - 1000.0 / binHz;
    const double sincDelta = std::sin(kPi * delta) / (kPi * delta);
    const double theoryRectDb = gainToDb(sincDelta);
    const double theoryHannDb = gainToDb(sincDelta / (1.0 - delta * delta));
    for (WindowType type : windows) {
        const std::vector<float> db = spectrumDb(tone.getReadPointer(0), type);
        int peakBin = 0;
        for (int k = 1; k < kBins; ++k)
            if (db[static_cast<size_t>(k)] > db[static_cast<size_t>(peakBin)])
                peakBin = k;
        std::printf("---- %s ----\n", dsp::windowTypeName(type));
        plotAscii(db, 5000.0, 64, 12, 0.0f, -120.0f);
        std::printf("peak bin %d (%.1f Hz) = %.2f dB; bin+-1 = %.1f / %.1f dB; bin+-10 = %.1f / %.1f dB; "
                    "near 3.3 kHz (bin %d) = %.1f dB\n",
                    peakBin, peakBin * binHz, db[static_cast<size_t>(peakBin)], db[static_cast<size_t>(peakBin - 1)],
                    db[static_cast<size_t>(peakBin + 1)], db[static_cast<size_t>(peakBin - 10)],
                    db[static_cast<size_t>(peakBin + 10)], weakBin, db[static_cast<size_t>(weakBin)]);
        DAWLAB_CHECK(peakBin == 43);
        if (type == WindowType::Rectangular) {
            std::printf("(Theory: offset from the bin centre by δ = %.3f bin -> sinc(δ) = %.2f dB)\n", delta, theoryRectDb);
            DAWLAB_CHECK_NEAR(db[43], theoryRectDb, 0.1);
            DAWLAB_CHECK(db[static_cast<size_t>(weakBin)] > -55.0f); // the -60 dB tone is swamped by leakage
        } else if (type == WindowType::Hann) {
            std::printf("(Theory: sinc(δ)/(1−δ²) = %.2f dB)\n", theoryHannDb);
            DAWLAB_CHECK_NEAR(db[43], theoryHannDb, 0.1);
            DAWLAB_CHECK_NEAR(db[static_cast<size_t>(weakBin)], -60.0, 1.0); // the -60 dB tone is visible
        } else if (type == WindowType::BlackmanHarris) {
            DAWLAB_CHECK(db[static_cast<size_t>(peakBin + 10)] < -95.0f);
            DAWLAB_CHECK_NEAR(db[static_cast<size_t>(weakBin)], -60.0, 1.0);
        }
        std::printf("\n");
    }

    // ---- Sine on a bin centre: after coherent-gain compensation every window reads 0 dB, and the neighbouring bins are almost 0 ----
    std::printf("== Sine exactly on bin 43 (%.4f Hz): the no-leakage case ==\n", 43 * binHz);
    std::printf("%-16s %12s %14s %14s %14s\n", "window", "bin 43 (dB)", "bin 44 (dB)", "bin 45 (dB)", "coherent gain");
    AudioBuffer centred = makeSine(43 * binHz, kSampleRate, 1.0, 1.0f, 1);
    for (WindowType type : windows) {
        const std::vector<float> db = spectrumDb(centred.getReadPointer(0), type);
        std::vector<float> w;
        Fft::makeWindow(w, kN, type);
        std::printf("%-16s %12.3f %14.1f %14.1f %14.4f\n", dsp::windowTypeName(type), db[43], db[44], db[45],
                    Fft::coherentGain(w.data(), kN));
        DAWLAB_CHECK_NEAR(db[43], 0.0, 0.01); // after coherent-gain compensation, a sine on a bin centre always reads 0 dB
        if (type == WindowType::Rectangular)
            DAWLAB_CHECK(db[44] < -100.0f);  // sampled only at the zeros of the sinc
    }
    std::printf("-> When a whole number of periods exactly fills the frame, the FFT samples only at the zeros of the side lobes, so there seems to be \"no leakage at all\";\n"
                "   the -6 dB at Hann's bin+-1 is the main lobe itself being wider (+-2 bins), not leakage.\n\n");

    // ---- DTFT of the window: main-lobe width and highest side lobe (16x zero-padding) --------
    std::printf("== Spectrum of the window itself (DTFT sampled with 16x zero-padding) ==\n");
    std::printf("%-16s %18s %24s %22s\n", "window", "-3 dB width (bins)", "highest side lobe (dB)", "scalloping loss (dB)");
    // Theoretical values from Table 1 of Harris (1978): -3 dB width (bins), highest side lobe (dB), scalloping loss (dB).
    // The table gives 1.78 for Hamming's scalloping; computing it directly from the formula:
    // (0.54·sinc(½) + 0.23·(sinc(½) + sinc(1½))) / 0.54
    // = 0.8174 -> 1.75 dB, so the formula value is used here.
    const double theory[4][3] = {{0.89, -13.3, 3.92}, {1.44, -31.5, 1.42}, {1.30, -42.7, 1.75}, {1.90, -92.0, 0.83}};
    int row = 0;
    for (WindowType type : windows) {
        double width = 0.0, sideLobe = 0.0, scallop = 0.0;
        measureLobes(type, width, sideLobe, scallop);
        std::printf("%-16s %18.2f %24.1f %22.2f\n", dsp::windowTypeName(type), width, sideLobe, scallop);
        DAWLAB_CHECK_NEAR(width, theory[row][0], 0.03);
        DAWLAB_CHECK_NEAR(sideLobe, theory[row][1], 0.5);
        DAWLAB_CHECK_NEAR(scallop, theory[row][2], 0.05);
        ++row;
    }
    std::printf("\nTheoretical values (Harris 1978): Rectangular 0.89 bin / -13.3 dB / 3.92 dB; Hann 1.44 / -31.5 / 1.42;\n"
                "Hamming 1.30 / -42.7 / 1.78 (1.75 when computed directly from the formula); Blackman-Harris (4-term) 1.90 / -92.0 / 0.83.\n"
                "The lower the side lobes, the wider the main lobe (and the smaller the scalloping loss).\n");
    return dawlab::test::summary("l11_window_leakage");
}
