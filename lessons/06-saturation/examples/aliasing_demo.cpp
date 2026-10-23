// 範例 2：Aliasing 示範——高頻正弦波 + 大 drive，有 / 沒有 oversampling
//
// 示範什麼：
//   7 kHz 正弦波經過 tanh（drive +18 dB，進入曲線的 peak ≈ 4）。tanh 是奇函式，只產生奇次諧波：
//   21、35、49、63、77 kHz……；超過 Nyquist（24 kHz）的那些在 48 kHz 下會折返（alias）：
//       35 → |35 - 48| = 13 kHz、49 → 1 kHz、63 → 15 kHz、77 → 19 kHz、91 → 5 kHz……
//   這些折返的頻率「不是 7 kHz 的整數倍」，聽起來就是不和諧的雜音。
//   開 oversampling 之後，非線性在 2fs / 4fs / 8fs 下進行，Nyquist 變成 48 / 96 / 192 kHz，
//   大部分高次諧波在降頻時被 halfband lowpass 濾掉，而不是折回來。
//
//   這支程式：
//   1. 印出「預測表」：每個諧波在 1x / 2x / 4x / 8x 下會落到哪裡、會不會留在輸出裡。
//   2. 實際量測：基頻 7 kHz、諧波 21 kHz、以及所有 alias 位置（1、3、5、9、11、13、15、17、19、23 kHz）
//      的能量，相對於基頻的 dB；最後算一個總 alias 能量（所有 alias 位置的功率和 / 基頻功率）。
//   3. 輸出四個 WAV 讓你聽。
//
// 怎麼執行：l06_aliasing_demo（不需要參數）
//
// 輸出檔案（在 dawlab_output/）：
//   l06_alias_7k_1x.wav / l06_alias_7k_2x.wav / l06_alias_7k_4x.wav / l06_alias_7k_8x.wav
//
// 該聽到：
//   1x 版本除了 7 kHz 之外明顯聽得到低很多的音（1 kHz、5 kHz……），像有另一個音在裡面；
//   2x 後大部分消失，4x / 8x 幾乎只剩乾淨的 7 kHz（＋ 21 kHz，多數人聽不到）。
//   程式在 alias 總能量沒有隨倍率下降時回傳 1。

#include <dawlab/Common.h>
#include <dawlab/dsp/Saturation.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr double kFundamental = 7000.0;
constexpr float kDriveDb = 18.0f;

// 頻率 f 在取樣率 rate 下會被記錄成哪個頻率（折返公式）：
//   f_alias = | ((f + rate/2) mod rate) - rate/2 |
double aliasFrequency(double f, double rate) {
    const double shifted = std::fmod(f + rate * 0.5, rate);
    return std::abs(shifted - rate * 0.5);
}

template <typename Processor>
void processInBlocks(dawlab::AudioBuffer& buffer, Processor& processor) {
    dawlab::AudioBlock whole(buffer);
    for (int start = 0; start < buffer.getNumSamples(); start += kBlockSize) {
        const int len = std::min(kBlockSize, buffer.getNumSamples() - start);
        processor.process(whole.getSubBlock(start, len));
    }
}

} // namespace

int main() {
    using dawlab::dsp::Saturation;
    using dawlab::dsp::SaturationCurve;

    // ---- 1. 預測表 ---------------------------------------------------------------------------
    std::printf("%.0f Hz 正弦波 → tanh（drive %+.0f dB）。奇次諧波在各倍率下的去向（fs = %.0f kHz）：\n", kFundamental,
                kDriveDb, kSampleRate / 1000.0);
    std::printf("%-6s %-10s | %-14s | %-22s | %-22s | %-22s\n", "諧波", "頻率(kHz)", "1x → 落在", "2x（Nyq 48k）",
                "4x（Nyq 96k）", "8x（Nyq 192k）");
    for (int k = 3; k <= 29; k += 2) {
        const double f = kFundamental * k;
        std::printf("%-6d %-10.0f |", k, f / 1000.0);
        for (int log2 = 0; log2 <= 3; ++log2) {
            const double rate = kSampleRate * (1 << log2);
            const double fa = aliasFrequency(f, rate);
            // 留在輸出裡的條件：折返後的頻率 < 原始 Nyquist（24 kHz）；否則被 downsample 的 lowpass 擋掉
            const bool survives = fa < kSampleRate * 0.5;
            const bool isHarmonic = std::abs(fa - f) < 1.0; // 根本沒折返（本來就在 Nyquist 以下）
            char text[64];
            if (isHarmonic)
                std::snprintf(text, sizeof(text), "%.0f kHz（諧波）", fa / 1000.0);
            else if (survives)
                std::snprintf(text, sizeof(text), "%.0f kHz ALIAS", fa / 1000.0);
            else
                std::snprintf(text, sizeof(text), "%.0f kHz → 濾掉", fa / 1000.0);
            std::printf(" %-20s |", text);
        }
        std::printf("\n");
    }
    std::printf("（「濾掉」= 折返後仍高於 24 kHz，降頻時的 halfband lowpass 會把它擋掉；ALIAS = 留在可聽範圍內）\n\n");

    // ---- 2. 實際量測 -------------------------------------------------------------------------
    // 所有 alias 都落在奇數 kHz（因為 gcd(7000, 48000) = 1000 且只有奇次諧波），排除 7 與 21（真正的諧波）
    const std::vector<double> aliasHz = {1000.0, 3000.0, 5000.0, 9000.0, 11000.0, 13000.0, 15000.0, 17000.0, 19000.0,
                                         23000.0};
    std::vector<double> totalAliasDb;
    std::printf("量測（相對於基頻 7 kHz 的 dB；輸入振幅 0.5，drive %+.0f dB → 進入 tanh 的 peak ≈ %.2f）：\n", kDriveDb,
                0.5 * dawlab::dbToGain(kDriveDb));
    std::printf("%-12s %10s %10s %10s %10s\n", "頻率", "1x", "2x", "4x", "8x");

    std::vector<std::vector<double>> table(aliasHz.size() + 2, std::vector<double>(4, 0.0));
    for (int log2 = 0; log2 <= 3; ++log2) {
        dawlab::AudioBuffer buffer = dawlab::makeSine(kFundamental, kSampleRate, 2.0, 0.5f, 1);
        Saturation sat;
        sat.setOversamplingFactorLog2(log2);
        sat.prepare(kSampleRate, kBlockSize, 1);
        sat.setCurve(SaturationCurve::Tanh);
        sat.setDriveDb(kDriveDb);
        sat.setOutputGainDb(-6.0f); // tanh 輸出 peak ≈ 1，降 6 dB 給 WAV 留 headroom
        sat.setMix(1.0f);
        sat.reset(); // 讓參數 ramp 直接就定位
        processInBlocks(buffer, sat);

        const int skip = 4800; // 跳過開頭 0.1 秒（latency + 暫態）
        const float* d = buffer.getReadPointer(0) + skip;
        const int n = buffer.getNumSamples() - skip;
        const double h1 = dawlab::goertzelAmplitude(d, n, kFundamental, kSampleRate);
        table[0][static_cast<size_t>(log2)] = 0.0;
        table[1][static_cast<size_t>(log2)] =
            dawlab::gainToDb(dawlab::goertzelAmplitude(d, n, kFundamental * 3.0, kSampleRate) / h1);
        double aliasPower = 0.0;
        for (size_t i = 0; i < aliasHz.size(); ++i) {
            const double a = dawlab::goertzelAmplitude(d, n, aliasHz[i], kSampleRate);
            table[i + 2][static_cast<size_t>(log2)] = dawlab::gainToDb(a / h1);
            aliasPower += a * a;
        }
        totalAliasDb.push_back(10.0 * std::log10(std::max(aliasPower / (h1 * h1), 1.0e-12)));

        dawlab::writeWav(dawlab::outputPath("l06_alias_7k_" + std::to_string(1 << log2) + "x.wav"), buffer, kSampleRate);
    }
    auto printRow = [&](const char* label, const std::vector<double>& row) {
        std::printf("%-12s", label);
        for (double v : row)
            std::printf(" %10.1f", std::max(v, -99.9));
        std::printf("\n");
    };
    printRow("7 kHz (H1)", table[0]);
    printRow("21 kHz (H3)", table[1]);
    for (size_t i = 0; i < aliasHz.size(); ++i) {
        char label[32];
        std::snprintf(label, sizeof(label), "%2.0f kHz alias", aliasHz[i] / 1000.0);
        printRow(label, table[i + 2]);
    }
    printRow("alias 總和", totalAliasDb);
    std::printf("（alias 總和 = 10·log10(Σ alias 功率 / 基頻功率)；越負越乾淨。-96 dB 大約是 16-bit 的量化底噪。）\n");
    std::printf("\n已輸出 dawlab_output/l06_alias_7k_{1x,2x,4x,8x}.wav\n");

    const bool ok = totalAliasDb[1] < totalAliasDb[0] - 6.0 && totalAliasDb[2] < totalAliasDb[1] - 6.0 &&
                    totalAliasDb[3] <= totalAliasDb[2] + 1.0;
    std::printf("%s\n", ok ? "alias 總能量隨 oversampling 倍率下降。" : "[FAIL] alias 總能量沒有如預期下降！");
    return ok ? 0 : 1;
}
