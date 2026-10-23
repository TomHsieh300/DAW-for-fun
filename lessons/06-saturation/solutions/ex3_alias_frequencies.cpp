// 練習 3 解答：計算 alias 頻率，再用「真的 hard clip」量測驗證
//
// 題目：fs = 44.1 kHz，5 kHz 正弦波經過很重的 hard clip（近似方波，只有奇次諧波，第 k 次振幅 ≈ (4/π)/k）。
//   (a) 第 3、5、7、9、11、13、15 次諧波各在哪裡？哪些會 alias、落到哪裡？
//   (b) 9 次諧波（45 kHz）的 alias 相對於基頻是幾 dB？
//   (c) 改成 2x oversampling（88.2 kHz 下做 hard clip）後，哪些諧波仍會折回 0..22.05 kHz？
//
// alias 公式：f_a = | ((f + fs/2) mod fs) - fs/2 |   （把 f 摺進 0..fs/2）
//   15 kHz → 15 kHz（真正的諧波）
//   25 kHz → 44.1 - 25 = 19.1 kHz        35 kHz → 44.1 - 35 = 9.1 kHz
//   45 kHz → 45 - 44.1 = 0.9 kHz          55 kHz → 55 - 44.1 = 10.9 kHz
//   65 kHz → 65 - 44.1 = 20.9 kHz         75 kHz → 88.2 - 75 = 13.2 kHz
// (b) 方波第 9 次振幅 = 基頻的 1/9 → 20 log10(1/9) = -19.08 dB。這個 900 Hz 的音比基頻低了兩個八度多，
//     而且跟 5 kHz 完全不成整數倍關係——非常刺耳。
// (c) 在 88.2 kHz 下：25、35 kHz 是「真的」諧波（< 44.1k），降頻時被 lowpass 擋掉；
//     45 → 43.2、55 → 33.2、65 → 23.2、75 → 13.2 kHz……只有折返後 < 22.05 kHz 的會原封不動留下來（75 kHz → 13.2 kHz 仍是 alias）。
//     介於 22.05 kHz 與 stopband 起點（約 25.7 kHz）之間的落在 halfband 的 transition band，只被衰減一部分，
//     降頻時再以 22.05 kHz 為鏡子折回 fs - f：65 kHz → 23.2 kHz（只衰減約 15 dB）→ 20.9 kHz；
//     25 kHz 也剛好在 transition band 的尾端，但已被衰減約 45 dB（→ 19.1 kHz），實務上可視為擋掉。
//     程式用本章 Oversampler 第 1 級的係數實際算出這兩個衰減量。
//
// 執行：l06_ex3_solution

#include <dawlab/Common.h>
#include <dawlab/dsp/Oversampler.h>

#include <algorithm>
#include <cstdio>

namespace {

double aliasFrequency(double f, double fs) {
    const double shifted = std::fmod(f + fs * 0.5, fs);
    return std::abs(shifted - fs * 0.5);
}

// FIR h 在頻率 f（sample rate fsHigh）的振幅響應（dB）：|Σ h[n] e^{-jωn}|
double firResponseDb(const std::vector<double>& h, double f, double fsHigh) {
    const double w = dawlab::kTwoPi * f / fsHigh;
    double re = 0.0, im = 0.0;
    for (size_t n = 0; n < h.size(); ++n) {
        re += h[n] * std::cos(w * static_cast<double>(n));
        im -= h[n] * std::sin(w * static_cast<double>(n));
    }
    return dawlab::gainToDb(std::sqrt(re * re + im * im));
}

} // namespace

int main() {
    constexpr double fs = 44100.0;
    constexpr double f0 = 5000.0;

    // ---- (a) 表格 ----------------------------------------------------------------------------
    // 第 1 級 halfband 的 stopband 從 28 kHz（fs = 48 kHz 時）起算，也就是原始 Nyquist 的 28/24 倍；
    // 44.1 kHz 時約 25.7 kHz。折到 22.05～25.7 kHz 之間的頻率落在 transition band：只被衰減一部分，
    // 降頻時再以 fs/2 為鏡子折回 fs - a2。
    const double stopbandEdge = fs * 0.5 * (28.0 / 24.0);
    // 第 1 級 halfband（降頻 88.2k → 44.1k 用的就是它）
    const std::vector<double> halfband = dawlab::dsp::HalfbandStage::designHalfband(
        dawlab::dsp::Oversampler::kOddTapsStage1, dawlab::dsp::Oversampler::kKaiserBeta);
    std::printf("%-6s %-10s %-14s %-20s\n", "k", "k·f0", "1x (44.1k)", "2x (88.2k) 先折到");
    for (int k = 3; k <= 15; k += 2) {
        const double f = k * f0;
        const double a1 = aliasFrequency(f, fs);
        const double a2 = aliasFrequency(f, 2 * fs);
        char note[96];
        if (a2 < fs / 2)
            std::snprintf(note, sizeof(note), "%s", f < fs / 2 ? "（諧波）" : "（仍會留下）");
        else if (a2 < stopbandEdge)
            std::snprintf(note, sizeof(note), "（transition band：衰減 %.1f dB 後折到 %.1f kHz）",
                          -firResponseDb(halfband, a2, 2 * fs), (fs - a2) / 1000.0);
        else
            std::snprintf(note, sizeof(note), "%s", "（降頻時濾掉）");
        std::printf("%-6d %-10.0f %-8.1f kHz%s %-8.1f kHz%s\n", k, f, a1 / 1000.0, f < fs / 2 ? "（諧波）" : " ALIAS ",
                    a2 / 1000.0, note);
    }
    DAWLAB_CHECK_NEAR(aliasFrequency(25000, fs), 19100, 1e-6);
    DAWLAB_CHECK_NEAR(aliasFrequency(35000, fs), 9100, 1e-6);
    DAWLAB_CHECK_NEAR(aliasFrequency(45000, fs), 900, 1e-6);
    DAWLAB_CHECK_NEAR(aliasFrequency(55000, fs), 10900, 1e-6);
    DAWLAB_CHECK_NEAR(aliasFrequency(65000, fs), 20900, 1e-6);
    DAWLAB_CHECK_NEAR(aliasFrequency(75000, fs), 13200, 1e-6);
    DAWLAB_CHECK_NEAR(aliasFrequency(75000, 2 * fs), 13200, 1e-6);
    DAWLAB_CHECK_NEAR(aliasFrequency(65000, 2 * fs), 23200, 1e-6);
    DAWLAB_CHECK(aliasFrequency(65000, 2 * fs) > fs / 2 && aliasFrequency(65000, 2 * fs) < stopbandEdge); // transition band
    DAWLAB_CHECK_NEAR(fs - aliasFrequency(65000, 2 * fs), 20900, 1e-6);                                // 降頻後折到 20.9 kHz
    // 23.2 kHz 只被衰減 10～20 dB（留下來的 alias 仍然很明顯）；25 kHz 已被衰減 40 dB 以上
    const double att232 = -firResponseDb(halfband, 23200.0, 2 * fs);
    const double att25 = -firResponseDb(halfband, 25000.0, 2 * fs);
    DAWLAB_CHECK(att232 > 10.0 && att232 < 20.0);
    DAWLAB_CHECK(att25 > 40.0);

    // ---- (b) 量測：sine × 100 再 hard clip 到 ±1 ≈ 方波 ----------------------------------------
    dawlab::AudioBuffer b = dawlab::makeSine(f0, fs, 1.0, 1.0f, 1);
    float* d = b.getWritePointer(0);
    const int n = b.getNumSamples();
    for (int i = 0; i < n; ++i)
        d[i] = std::clamp(100.0f * d[i], -1.0f, 1.0f);

    const double h1 = dawlab::goertzelAmplitude(d, n, f0, fs);
    const double a900 = dawlab::goertzelAmplitude(d, n, 900.0, fs);
    const double a9100 = dawlab::goertzelAmplitude(d, n, 9100.0, fs);
    std::printf("\n量測：基頻振幅 %.4f（理論 4/π = %.4f）\n", h1, 4.0 / dawlab::kPi);
    std::printf("      900 Hz alias = %.2f dB（理論 9 次：%.2f dB）\n", dawlab::gainToDb(a900 / h1),
                dawlab::gainToDb(1.0 / 9.0));
    std::printf("      9.1 kHz alias = %.2f dB（理論 7 次：%.2f dB）\n", dawlab::gainToDb(a9100 / h1),
                dawlab::gainToDb(1.0 / 7.0));
    DAWLAB_CHECK_NEAR(h1, 4.0 / dawlab::kPi, 0.02);
    // 其他更高次的諧波（例如 873、891 次）也會折到 900 Hz，但振幅只有 1/9 的 1% 左右 → 誤差 < 0.5 dB
    DAWLAB_CHECK_NEAR(dawlab::gainToDb(a900 / h1), dawlab::gainToDb(1.0 / 9.0), 0.5);
    DAWLAB_CHECK_NEAR(dawlab::gainToDb(a9100 / h1), dawlab::gainToDb(1.0 / 7.0), 0.5);
    // 對照組：800 Hz 不是任何諧波的 alias 位置（gcd(5000, 44100) = 100，但 800 Hz 要 k·5000 ≡ ±800 (mod 44100)
    // 的 k 都很大，振幅 < 1/400），應該很安靜
    const double a800 = dawlab::goertzelAmplitude(d, n, 800.0, fs);
    std::printf("      對照 800 Hz = %.2f dB（只有很高次的諧波會折到這裡）\n", dawlab::gainToDb(a800 / h1));
    DAWLAB_CHECK(dawlab::gainToDb(a800 / h1) < -40.0);

    return dawlab::test::summary("l06_ex3");
}
