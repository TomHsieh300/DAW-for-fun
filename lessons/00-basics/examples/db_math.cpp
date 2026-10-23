// l00_db_math：用數字驗證本章 README 裡的每一個「數值宣稱」。
//
// 示範什麼：
//   1. dB ↔ 線性倍率的對照表（+6 dB ≈ ×2、-20 dB = ×0.1……）。
//   2. 兩個「相同」訊號相加 = +6.02 dB；兩個「不相關」訊號相加 = +3.01 dB。
//   3. crest factor：正弦波 3.01 dB、方波 0 dB、均勻白雜訊 4.77 dB、鼓組 loop 更高。
//   4. aliasing：30 kHz 的正弦波在 48 kHz 取樣後，量到的是 18 kHz。
//      另外輸出一段「掃到 40 kHz」的掃頻，可以親耳聽到頻率「折返」。
//   5. quantization：把訊號量化成 N bit，量 SNR，跟 6.02N + 1.76 dB 比較。
//   6. clipping：振幅 1.5 的正弦波硬切在 ±1.0，數 clip 的 sample 數並量 THD。
//
// 怎麼執行：
//   cd <build 目錄>
//   bin/Debug/l00_db_math.exe
//
// 會輸出：
//   l00_sum_identical.wav      兩個相同的 1 kHz 正弦波相加（-12 dBFS + -12 dBFS = -6 dBFS）
//   l00_sum_uncorrelated.wav   兩段不相關白雜訊相加（-12 dBFS + -12 dBFS ≈ -9 dBFS）
//   l00_aliasing_30k.wav       30 kHz 正弦波 @ 48 kHz —— 聽起來 / 看起來是 18 kHz
//   l00_aliasing_sweep.wav     20 Hz → 40 kHz 掃頻 @ 48 kHz —— 超過 24 kHz 後頻率往回掉
//   l00_quantized_8bit.wav     量化到 8 bit 的 -20 dBFS 正弦波（聽得到嘶嘶聲與失真）
//   l00_quantized_16bit.wav    量化到 16 bit 的同一訊號
//   l00_clipped_sine.wav       硬切的正弦波（刺耳的奇次諧波）
//
// 該聽到什麼：
//   aliasing_sweep 的音高先一路往上，碰到 24 kHz（Nyquist）之後反而往下掉——這就是 aliasing。
//   quantized_8bit 在正弦波底下有明顯的雜訊；16bit 版本聽不出差別。
//   clipped_sine 比乾淨正弦波「亮」而刺耳，因為多出了 3、5、7…次諧波。

#include <dawlab/Common.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

constexpr double kSampleRate = 48000.0;

// 把一個 double 訊號量化成 numBits bit（有號整數，round to nearest），回傳量化後的值。
// N bit 有號整數的合法碼是 -2^(N-1) … 2^(N-1) − 1（16 bit：-32768 … 32767），共 2^N 個刻度。
// round 之後一定要 clamp：否則 x = 1.0 會得到 +2^(N-1)（16 bit 的 +32768），那是整數 PCM 存不下的碼。
// 也因為正端少一階，整數 PCM 能表示的最大正值是 1 − 2^-(N-1)，比 1.0 小一點點。
double quantize(double x, int numBits) {
    const double scale = std::ldexp(1.0, numBits - 1); // 2^(N-1)：例如 16 bit → 32768
    const double code = std::clamp(std::round(x * scale), -scale, scale - 1.0);
    return code / scale;
}

// 量測「把 1 kHz 附近的正弦波量化到 numBits」之後的 SNR（dB），並視需要寫出 WAV
double measureQuantizationSnrDb(int numBits, float amplitudeDb, const char* fileName) {
    using namespace dawlab;
    const int n = secondsToSamples(1.0, kSampleRate);
    const double amp = dbToGain(double(amplitudeDb));
    // 用 997 Hz 而不是 1000 Hz：1000 Hz 跟 48 kHz 成整數比，量化誤差會重複出現而不像雜訊
    const double freq = 997.0;
    AudioBuffer out(1, n);
    double signalPower = 0.0, noisePower = 0.0;
    for (int i = 0; i < n; ++i) {
        const double x = amp * std::sin(kTwoPi * freq * i / kSampleRate);
        const double q = quantize(x, numBits);
        const double e = q - x; // 量化誤差
        signalPower += x * x;
        noisePower += e * e;
        out.setSample(0, i, static_cast<float>(q));
    }
    if (fileName)
        writeWav(outputPath(fileName), out, kSampleRate, 24);
    return 10.0 * std::log10(signalPower / noisePower);
}

} // namespace

int main() {
    using namespace dawlab;
    const int oneSecond = secondsToSamples(1.0, kSampleRate);

    // ---- 1. dB 對照表 -----------------------------------------------------
    std::printf("== dB ↔ 線性倍率（gain = 10^(dB/20)，dB = 20·log10(gain)）==\n");
    const float dbTable[] = {12.0f, 6.0f, 3.0f, 0.0f, -3.0f, -6.0f, -12.0f, -20.0f, -40.0f, -60.0f};
    for (float db : dbTable)
        std::printf("  %+6.1f dB  →  × %.4f\n", db, dbToGain(db));
    std::printf("  × 2 = %+.2f dB，× 0.5 = %+.2f dB，× √2 = %+.2f dB\n", gainToDb(2.0f), gainToDb(0.5f),
                gainToDb(static_cast<float>(std::sqrt(2.0))));
    DAWLAB_CHECK_NEAR(gainToDb(2.0f), 6.0206, 0.001);
    DAWLAB_CHECK_NEAR(dbToGain(-20.0f), 0.1, 1e-6);

    // ---- 2. 相同 vs 不相關訊號相加 -----------------------------------------
    std::printf("\n== 兩個 -12 dBFS 的訊號相加 ==\n");
    AudioBuffer sineA = makeSine(1000.0, kSampleRate, 1.0, dbToGain(-12.0f), 1);
    AudioBuffer sumIdentical = sineA;
    sumIdentical.addFrom(0, 0, sineA, 0, 0, oneSecond); // 相同訊號：振幅直接相加
    const float identicalDb = gainToDb(rms(sumIdentical, 0)) - gainToDb(rms(sineA, 0));
    std::printf("  相同的正弦波：RMS 增加 %+.2f dB（振幅 ×2）\n", identicalDb);
    DAWLAB_CHECK_NEAR(identicalDb, 6.02, 0.02);

    AudioBuffer noiseA = makeWhiteNoise(kSampleRate, 1.0, 1.0f, 1, 1);
    AudioBuffer noiseB = makeWhiteNoise(kSampleRate, 1.0, 1.0f, 1, 777);
    // 先各自 normalize 到 -12 dBFS RMS，再相加
    noiseA.applyGain(dbToGain(-12.0f) / rms(noiseA, 0));
    noiseB.applyGain(dbToGain(-12.0f) / rms(noiseB, 0));
    AudioBuffer sumUncorrelated = noiseA;
    sumUncorrelated.addFrom(0, 0, noiseB, 0, 0, oneSecond); // 不相關：功率相加，振幅只增加 √2
    const float uncorrelatedDb = gainToDb(rms(sumUncorrelated, 0)) - gainToDb(rms(noiseA, 0));
    std::printf("  不相關的白雜訊：RMS 增加 %+.2f dB（功率 ×2，振幅 ×√2）\n", uncorrelatedDb);
    DAWLAB_CHECK_NEAR(uncorrelatedDb, 3.01, 0.1);
    writeWav(outputPath("l00_sum_identical.wav"), sumIdentical, kSampleRate);
    writeWav(outputPath("l00_sum_uncorrelated.wav"), sumUncorrelated, kSampleRate);

    // ---- 3. crest factor ---------------------------------------------------
    std::printf("\n== crest factor = peak / RMS ==\n");
    auto crestDb = [](const AudioBuffer& b) { return gainToDb(peakAbs(b)) - gainToDb(rms(b, 0)); };
    AudioBuffer square(1, oneSecond);
    for (int i = 0; i < oneSecond; ++i)
        square.setSample(0, i, (i / 24) % 2 == 0 ? 0.5f : -0.5f); // 1 kHz 方波（每 24 samples 換號）
    AudioBuffer sineFull = makeSine(1000.0, kSampleRate, 1.0, 0.5f, 1);
    AudioBuffer noiseFull = makeWhiteNoise(kSampleRate, 1.0, 0.5f, 1);
    AudioBuffer drums = makeDrumLoop(kSampleRate);
    std::printf("  方波       %5.2f dB（理論 0）\n", crestDb(square));
    std::printf("  正弦波     %5.2f dB（理論 20·log10(√2) = 3.01）\n", crestDb(sineFull));
    std::printf("  均勻白雜訊 %5.2f dB（理論 20·log10(√3) = 4.77）\n", crestDb(noiseFull));
    std::printf("  鼓組 loop  %5.2f dB（打擊樂的 peak 遠高於平均音量）\n", crestDb(drums));
    DAWLAB_CHECK_NEAR(crestDb(square), 0.0, 0.01);
    DAWLAB_CHECK_NEAR(crestDb(sineFull), 3.01, 0.02);
    DAWLAB_CHECK_NEAR(crestDb(noiseFull), 4.77, 0.1);
    DAWLAB_CHECK(crestDb(drums) > 8.0f);

    // ---- 4. aliasing -------------------------------------------------------
    std::printf("\n== aliasing：f_in = 30 kHz，fs = 48 kHz ==\n");
    AudioBuffer sine30k = makeSine(30000.0, kSampleRate, 1.0, 0.5f, 1);
    const double at30k = goertzelAmplitude(sine30k.getReadPointer(0), oneSecond, 30000.0, kSampleRate);
    const double at18k = goertzelAmplitude(sine30k.getReadPointer(0), oneSecond, 18000.0, kSampleRate);
    std::printf("  產生振幅 0.5 的 30 kHz 正弦波，量測結果：\n");
    std::printf("    在 18 kHz（|30000 − 48000|）量到振幅 %.4f  ← 全部能量都「折」到這裡\n", at18k);
    std::printf("    （在 30 kHz 量到 %.4f：Goertzel 在 30 kHz 與 18 kHz 看到的是同一組 sample）\n", at30k);
    DAWLAB_CHECK_NEAR(at18k, 0.5, 0.01);
    writeWav(outputPath("l00_aliasing_30k.wav"), sine30k, kSampleRate);
    // 掃到 40 kHz 的掃頻：makeLogSweep 只是算 sin(phase)，不會阻止我們超過 Nyquist
    writeWav(outputPath("l00_aliasing_sweep.wav"), makeLogSweep(20.0, 40000.0, kSampleRate, 6.0, 0.5f, 1),
             kSampleRate);

    // ---- 5. quantization ---------------------------------------------------
    std::printf("\n== quantization SNR（full-scale 正弦波）vs 6.02·N + 1.76 dB ==\n");
    std::printf("  %4s %12s %12s\n", "bits", "measured", "formula");
    for (int bits : {8, 12, 16, 20, 24}) {
        const double measured = measureQuantizationSnrDb(bits, 0.0f, nullptr);
        const double formula = 6.02 * bits + 1.76;
        std::printf("  %4d %9.2f dB %9.2f dB\n", bits, measured, formula);
        DAWLAB_CHECK_NEAR(measured, formula, 1.0);
    }
    const double snr8at20 = measureQuantizationSnrDb(8, -20.0f, "l00_quantized_8bit.wav");
    const double snr16at20 = measureQuantizationSnrDb(16, -20.0f, "l00_quantized_16bit.wav");
    std::printf("  訊號降到 -20 dBFS 時，SNR 也跟著少 20 dB：8 bit → %.2f dB，16 bit → %.2f dB\n", snr8at20,
                snr16at20);
    DAWLAB_CHECK_NEAR(snr8at20, 6.02 * 8 + 1.76 - 20.0, 1.5);

    // ---- 6. clipping -------------------------------------------------------
    // 注意：1 kHz 跟 48 kHz 成整數比（48 倍），所以超過 Nyquist 的奇次諧波 alias 回來時
    // 剛好落在 |k·1000 − 48000| = 另一個「奇次」諧波的位置上（例如 H25 → 23 kHz = H23），
    // 不會汙染偶次諧波的 bin。這是刻意選的巧合，讓 clip 的 sample 數是整齊的 26000（練習 4）；
    // 一般的頻率（例如 1.1 kHz）alias 會落在不是基頻整數倍的位置，變成不和諧的雜音。
    std::printf("\n== clipping：振幅 1.5 的 1 kHz 正弦波硬切在 ±1.0 ==\n");
    AudioBuffer loud = makeSine(1000.0, kSampleRate, 1.0, 1.5f, 1);
    int clipped = 0;
    for (int i = 0; i < oneSecond; ++i) {
        const float x = loud.getSample(0, i);
        if (std::abs(x) >= 1.0f) {
            ++clipped;
            loud.setSample(0, i, x > 0.0f ? 1.0f : -1.0f);
        }
    }
    const double thd = thdPercent(loud.getReadPointer(0), oneSecond, 1000.0, kSampleRate);
    const std::vector<double> harmonics = harmonicLevelsDb(loud.getReadPointer(0), oneSecond, 1000.0, kSampleRate, 5);
    std::printf("  clip 的 sample 數 = %d / %d（%.1f%%；理論 |sin| ≥ 2/3 的比例 ≈ 54%%）\n", clipped, oneSecond,
                100.0 * clipped / oneSecond);
    std::printf("  THD = %.2f%%；諧波相對基頻：", thd);
    for (size_t h = 0; h < harmonics.size(); ++h)
        std::printf("H%zu %+.1f dB  ", h + 1, harmonics[h]);
    std::printf("\n  （偶次諧波 H2、H4 接近 -100 dB = 不存在：對稱的 clip 只產生奇次諧波）\n");
    DAWLAB_CHECK(clipped == 26000);
    DAWLAB_CHECK(thd > 5.0);
    writeWav(outputPath("l00_clipped_sine.wav"), loud, kSampleRate);

    std::printf("\n");
    return dawlab::test::summary("l00_db_math");
}
