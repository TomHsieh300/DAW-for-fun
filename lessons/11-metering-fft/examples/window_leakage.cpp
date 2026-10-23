// 範例 2：window function與 spectral leakage 對照
//
// 示範什麼：
//   - 同一個 1 kHz 正弦（N = 2048、fs = 48 kHz，bin 寬 23.4375 Hz，1000 Hz 落在 bin 42.67，不在中心）
//     用四種 window（Rectangular / Hann / Hamming / Blackman-Harris）做 FFT，印出 ASCII 頻譜圖。
//   - 訊號裡還藏了一個 -60 dB 的 3.3 kHz 弱音：rectangular window 的 leakage 會把它淹沒，
//     Hann / Blackman-Harris 則看得到。
//   - 再用「正好落在 bin 中心」的 1007.8125 Hz（bin 43）示範「看不到 leakage」的特例。
//   - 最後用 16 倍 zero-padding 量每個 window 的 -3 dB main lobe 寬度、最高 side lobe 與 scalloping loss，
//     並和 Harris（1978）的理論值比對（DAWLAB_CHECK）。
//
// 怎麼執行：bin/Debug/l11_window_leakage.exe（不需要參數）
// 輸出檔案：dawlab_output/l11_window_test_tone.wav（1 kHz + 微弱 3.3 kHz，耳朵幾乎聽不到第二個音）

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

// 對訊號加 window、做 FFT，回傳每個 bin 的 dBFS（振幅 1.0 的正弦在 bin 中心 → 0 dB）
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

// ASCII 頻譜：線性頻率軸 0 .. maxHz，每一欄取該範圍內 bin 的最大值
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

// 量 window 本身的頻譜（DTFT）：main lobe 的 -3 dB 寬度（以「原本的 bin」為單位）與最高 side lobe（dB）。
//
// 為什麼不能直接拿「正好在 bin 中心的正弦」來量？因為那時 FFT 剛好只在 main lobe 的中心與 side lobe 的
// 零點上取樣，看起來完全沒有 leakage。要看到 lobe 的形狀，得在 bin 與 bin 之間「多取幾個點」：
// 把 N 點 window 後面補 15N 個 0 再做 16N 點 FFT（zero-padding 不會增加資訊，只是把 DTFT 取樣得更密）。
// scalloping loss：正弦剛好落在兩個 bin 正中間（偏 0.5 bin）時，峰值讀數比在 bin 中心時低多少 dB。
// 16 倍 zero-padding 下，「偏 0.5 bin」就是第 8 個取樣點。
void measureLobes(WindowType type, double& width3dB, double& highestSideLobe, double& scallopingLoss) {
    constexpr int kPadOrder = kOrder + 4; // 16 倍 zero-padding → 每個原始 bin 之間有 16 個取樣點
    constexpr int kPadN = 1 << kPadOrder;
    constexpr int kPadBins = kPadN / 2 + 1;
    static Fft fft(kPadOrder);
    std::vector<float> window;
    Fft::makeWindow(window, kN, type);
    std::vector<float> frame(static_cast<size_t>(kPadN), 0.0f);
    std::copy(window.begin(), window.end(), frame.begin());
    std::vector<std::complex<float>> spec(static_cast<size_t>(kPadBins));
    fft.forwardReal(frame.data(), spec.data());
    // 以 bin 0（= Σw，DC）為 0 dB
    const double peak = std::abs(spec[0]);
    std::vector<double> db(static_cast<size_t>(kPadBins));
    for (int k = 0; k < kPadBins; ++k)
        db[static_cast<size_t>(k)] = gainToDb(std::abs(spec[static_cast<size_t>(k)]) / peak, -300.0);
    // -3 dB 點：第一個低於 -3 dB 的位置（線性內插），寬度 = 2 × 位置 / 16
    int k = 1;
    while (k < kPadBins && db[static_cast<size_t>(k)] > -3.0)
        ++k;
    const double a = db[static_cast<size_t>(k - 1)], b = db[static_cast<size_t>(k)];
    const double t = (a + 3.0) / std::max(1e-9, a - b);
    width3dB = 2.0 * (static_cast<double>(k - 1) + t) / 16.0;
    scallopingLoss = -db[8];
    // main lobe 結束 = 第一個局部最小值（第一個零點）；之後的最大值就是最高 side lobe
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
    std::printf("N = %d，fs = %.0f Hz → bin 寬 = fs/N = %.4f Hz\n", kN, kSampleRate, binHz);
    std::printf("1000 Hz 落在 bin %.2f（不在中心 → leakage）；1007.8125 Hz 正好是 bin 43\n\n", 1000.0 / binHz);

    // ---- 測試訊號：1 kHz（0 dBFS）+ 3.3 kHz（-60 dBFS）-------------------------------------
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
    // 1000 Hz 偏離 bin 43 中心 δ = 1/3 bin 時的理論讀數：rectangular = sinc(δ)、Hann = sinc(δ)/(1−δ²)
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
        std::printf("peak bin %d（%.1f Hz）= %.2f dB；bin±1 = %.1f / %.1f dB；bin±10 = %.1f / %.1f dB；"
                    "3.3 kHz 附近（bin %d）= %.1f dB\n",
                    peakBin, peakBin * binHz, db[static_cast<size_t>(peakBin)], db[static_cast<size_t>(peakBin - 1)],
                    db[static_cast<size_t>(peakBin + 1)], db[static_cast<size_t>(peakBin - 10)],
                    db[static_cast<size_t>(peakBin + 10)], weakBin, db[static_cast<size_t>(weakBin)]);
        DAWLAB_CHECK(peakBin == 43);
        if (type == WindowType::Rectangular) {
            std::printf("（理論：偏離 bin 中心 δ = %.3f bin → sinc(δ) = %.2f dB）\n", delta, theoryRectDb);
            DAWLAB_CHECK_NEAR(db[43], theoryRectDb, 0.1);
            DAWLAB_CHECK(db[static_cast<size_t>(weakBin)] > -55.0f); // -60 dB 的弱音被 leakage 淹沒
        } else if (type == WindowType::Hann) {
            std::printf("（理論：sinc(δ)/(1−δ²) = %.2f dB）\n", theoryHannDb);
            DAWLAB_CHECK_NEAR(db[43], theoryHannDb, 0.1);
            DAWLAB_CHECK_NEAR(db[static_cast<size_t>(weakBin)], -60.0, 1.0); // 看得到 -60 dB 的弱音
        } else if (type == WindowType::BlackmanHarris) {
            DAWLAB_CHECK(db[static_cast<size_t>(peakBin + 10)] < -95.0f);
            DAWLAB_CHECK_NEAR(db[static_cast<size_t>(weakBin)], -60.0, 1.0);
        }
        std::printf("\n");
    }

    // ---- bin 中心的正弦：coherent gain 補償後每種 window 都讀 0 dB，而且鄰近 bin 幾乎是 0 ------
    std::printf("== 正弦正好在 bin 43（%.4f Hz）：沒有 leakage 的情況 ==\n", 43 * binHz);
    std::printf("%-16s %12s %14s %14s %14s\n", "window", "bin 43 (dB)", "bin 44 (dB)", "bin 45 (dB)", "coherent gain");
    AudioBuffer centred = makeSine(43 * binHz, kSampleRate, 1.0, 1.0f, 1);
    for (WindowType type : windows) {
        const std::vector<float> db = spectrumDb(centred.getReadPointer(0), type);
        std::vector<float> w;
        Fft::makeWindow(w, kN, type);
        std::printf("%-16s %12.3f %14.1f %14.1f %14.4f\n", dsp::windowTypeName(type), db[43], db[44], db[45],
                    Fft::coherentGain(w.data(), kN));
        DAWLAB_CHECK_NEAR(db[43], 0.0, 0.01); // coherent gain 補償後，bin 中心的正弦一律讀 0 dB
        if (type == WindowType::Rectangular)
            DAWLAB_CHECK(db[44] < -100.0f);  // 只在 sinc 的零點上取樣
    }
    std::printf("→ 整數個週期剛好填滿 frame 時，FFT 只在 side lobe 的零點上取樣，看起來「完全沒有 leakage」；\n"
                "   Hann 的 bin±1 有 -6 dB 是 main lobe 本身比較寬（±2 bin），不是 leakage。\n\n");

    // ---- window 的 DTFT：main lobe 寬度與最高 side lobe（16 倍 zero-padding）--------------------
    std::printf("== window 本身的頻譜（zero-padding 16 倍取樣 DTFT）==\n");
    std::printf("%-16s %16s %20s %22s\n", "window", "-3 dB 寬 (bins)", "最高 side lobe (dB)", "scalloping loss (dB)");
    // Harris（1978）Table 1 的理論值：-3 dB 寬（bin）、最高 side lobe（dB）、scalloping loss（dB）。
    // Hamming 的 scalloping 表上寫 1.78，直接代公式算：(0.54·sinc(½) + 0.23·(sinc(½) + sinc(1½))) / 0.54
    // = 0.8174 → 1.75 dB，這裡用公式值。
    const double theory[4][3] = {{0.89, -13.3, 3.92}, {1.44, -31.5, 1.42}, {1.30, -42.7, 1.75}, {1.90, -92.0, 0.83}};
    int row = 0;
    for (WindowType type : windows) {
        double width = 0.0, sideLobe = 0.0, scallop = 0.0;
        measureLobes(type, width, sideLobe, scallop);
        std::printf("%-16s %16.2f %20.1f %22.2f\n", dsp::windowTypeName(type), width, sideLobe, scallop);
        DAWLAB_CHECK_NEAR(width, theory[row][0], 0.03);
        DAWLAB_CHECK_NEAR(sideLobe, theory[row][1], 0.5);
        DAWLAB_CHECK_NEAR(scallop, theory[row][2], 0.05);
        ++row;
    }
    std::printf("\n理論值（Harris 1978）：Rectangular 0.89 bin / -13.3 dB / 3.92 dB；Hann 1.44 / -31.5 / 1.42；\n"
                "Hamming 1.30 / -42.7 / 1.78（直接代公式是 1.75）；Blackman-Harris(4 項) 1.90 / -92.0 / 0.83。\n"
                "side lobe 越低，main lobe 越寬（scalloping loss 也越小）。\n");
    return dawlab::test::summary("l11_window_leakage");
}
