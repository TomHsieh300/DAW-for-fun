// l02_resonance：Q（resonance）掃描。
//
// 示範什麼：
//   - 二階 lowpass 在 cutoff 處的增益 = Q（線性）= 20 log10(Q) dB：Q = 0.707 → -3 dB，
//     Q = 1 → 0 dB，Q = 4 → +12 dB，Q = 16 → +24 dB。印出理論與實測對照表。
//   - Q 越高，峰越尖、頻寬越窄：印出 -3 dB 頻寬（相對峰值）與 fc/Q 的關係（bandpass 的定義）。
//   - 合成器式的 filter sweep：把 pluck 琶音送進 LPF，cutoff 從 200 Hz 慢慢掃到 8 kHz，
//     每個 block（512 samples）更新一次係數；分別用 Q = 0.707 與 Q = 8 輸出 WAV。
//
// 怎麼執行：不帶參數直接執行。輸出 WAV（dawlab_output/）：
//   l02_resonance_dry.wav、l02_resonance_q0707.wav、l02_resonance_q8.wav
// 該聽到什麼：q0707 版是柔和的「打開音色」；q8 版在 cutoff 經過每個泛音時會「嗚——」地
//   把該泛音突顯出來（經典的 resonant sweep），而且整體音量更大、更刺（注意 peak 數字）。
#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>

#include <cstdio>
#include <vector>

using namespace dawlab;
using dawlab::dsp::Biquad;
using dawlab::dsp::BiquadCoefficients;

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

int main() {
    std::printf("=== 第 02 章：Q / resonance 掃描（LPF fc = 1 kHz）===\n\n");

    // ---- 1. cutoff 處的增益 vs Q ------------------------------------------------------
    std::printf("[1] cutoff 處的增益\n");
    std::printf("    %6s  %12s  %12s  %12s  %14s\n", "Q", "20log10(Q)", "theory dB", "measured dB", "peak freq (Hz)");
    Biquad filter;
    filter.prepare(kSampleRate, kBlockSize, 1);
    for (double q : {0.5, 0.7071, 1.0, 2.0, 4.0, 8.0, 16.0}) {
        const BiquadCoefficients c = BiquadCoefficients::makeLowpass(kSampleRate, 1000.0, q);
        filter.setCoefficients(c);
        filter.reset();
        const double theory = c.magnitudeDb(1000.0, kSampleRate);
        const double meas = measureGainDb([&](float x) { return filter.processSample(0, x); }, 1000.0, kSampleRate,
                                          0.5, 0.5);
        // 真正的峰值頻率：Q > 1/√2 時才有峰，且略低於 fc（analog：fc·sqrt(1 - 1/(2Q²))）
        double peakF = 0.0, peakDb = -1e9;
        for (double f = 20.0; f < 20000.0; f *= 1.002) {
            const double m = c.magnitudeDb(f, kSampleRate);
            if (m > peakDb) {
                peakDb = m;
                peakF = f;
            }
        }
        // 峰值落在掃描起點（20 Hz）= 曲線單調下降、沒有 resonance 峰
        const bool hasPeak = peakF > 25.0;
        char peakText[32];
        if (hasPeak)
            std::snprintf(peakText, sizeof(peakText), "%.0f", peakF);
        else
            std::snprintf(peakText, sizeof(peakText), "(no peak)");
        std::printf("    %6.3f  %12.2f  %12.2f  %12.2f  %14s\n", q, 20.0 * std::log10(q), theory, meas, peakText);
        DAWLAB_CHECK_NEAR(theory, 20.0 * std::log10(q), 0.01);
        DAWLAB_CHECK_NEAR(meas, theory, 0.1);
        // analog 二階 lowpass 的峰值頻率：fc · sqrt(1 - 1/(2Q²))（Q > 1/√2 才有峰）
        if (q > 0.71)
            DAWLAB_CHECK_NEAR(peakF, 1000.0 * std::sqrt(1.0 - 1.0 / (2.0 * q * q)), 6.0);
        else
            DAWLAB_CHECK(!hasPeak);
    }
    std::printf("    → Q ≤ 0.707 沒有峰（最平坦），Q > 0.707 開始出現 resonance 峰，峰值略低於 fc。\n\n");

    // ---- 2. bandpass 頻寬 = fc / Q -----------------------------------------------------
    std::printf("[2] bandpass（0 dB peak）的 -3 dB 頻寬 vs fc/Q\n");
    std::printf("    %6s  %10s  %10s  %12s  %12s\n", "Q", "f_low", "f_high", "BW measured", "fc/Q");
    for (double q : {0.5, 1.0, 2.0, 4.0, 8.0}) {
        const BiquadCoefficients c = BiquadCoefficients::makeBandpass(kSampleRate, 1000.0, q);
        // 從中心往兩邊找 -3.01 dB 點
        double fLow = 1000.0, fHigh = 1000.0;
        while (fLow > 1.0 && c.magnitudeDb(fLow, kSampleRate) > -3.0103)
            fLow /= 1.0005;
        while (fHigh < kSampleRate * 0.499 && c.magnitudeDb(fHigh, kSampleRate) > -3.0103)
            fHigh *= 1.0005;
        const double bw = fHigh - fLow;
        std::printf("    %6.2f  %10.1f  %10.1f  %12.1f  %12.1f\n", q, fLow, fHigh, bw, 1000.0 / q);
        // RBJ 的 bandpass 定義：BW = fc / Q（bilinear 的 prewarp 保證在 fc 附近成立，高 Q 更準）
        DAWLAB_CHECK_NEAR(bw, 1000.0 / q, 1000.0 / q * 0.03);
        // 幾何對稱：f_low · f_high ≈ fc²
        DAWLAB_CHECK_NEAR(std::sqrt(fLow * fHigh), 1000.0, 5.0);
    }
    std::printf("    → Q 的定義：Q = fc / BW，Q 越大，通過的頻帶越窄。\n\n");

    // ---- 3. 合成器式的 resonant sweep ---------------------------------------------------
    std::printf("[3] resonant filter sweep（cutoff 200 Hz → 8 kHz，每 block 更新係數）\n");
    AudioBuffer dry = makePluckSequence(kSampleRate, 6.0, 0.4f, 2);
    writeWav(outputPath("l02_resonance_dry.wav"), dry, kSampleRate);
    std::printf("    dry   peak %.2f dBFS\n", gainToDb(peakAbs(dry)));

    for (double q : {0.7071, 8.0}) {
        AudioBuffer out = dry;
        Biquad sweepFilter;
        sweepFilter.prepare(kSampleRate, kBlockSize, out.getNumChannels());
        AudioBlock whole(out);
        const int total = out.getNumSamples();
        for (int start = 0; start < total; start += kBlockSize) {
            const int len = std::min(kBlockSize, total - start);
            // cutoff 在 log 頻率軸上線性掃：200 · (8000/200)^t
            const double t = static_cast<double>(start) / total;
            const double cutoff = 200.0 * std::pow(40.0, t);
            sweepFilter.setCoefficients(BiquadCoefficients::makeLowpass(kSampleRate, cutoff, q));
            sweepFilter.process(whole.getSubBlock(start, len));
        }
        const char* file = q < 1.0 ? "l02_resonance_q0707.wav" : "l02_resonance_q8.wav";
        std::printf("    Q=%-5g peak %.2f dBFS  RMS %.2f dB  → %s\n", q, gainToDb(peakAbs(out)), gainToDb(rms(out, 0)),
                    file);
        DAWLAB_CHECK(!hasNanOrInf(out));
        writeWav(outputPath(file), out, kSampleRate, 32); // 32-bit float：Q=8 可能超過 0 dBFS，保留 overs
    }
    std::printf("    （Q=8 的版本用 32-bit float 寫出，可能超過 0 dBFS；播放前請先降低音量）\n\n");

    return dawlab::test::summary("l02_resonance");
}
