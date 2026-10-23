// 範例 1：四種 transfer curve 的諧波分析——哪條曲線產生哪些諧波、多少 THD、有沒有 DC
//
// 示範什麼：
//   1. 印出四條曲線（Tanh / Cubic / Arctan / Asymmetric）的 transfer curve 數值表（x → y）。
//   2. 把 100 Hz 正弦波（振幅 A = 0.25 … 4）直接丟進 Saturation::shape()，用 harmonicLevelsDb()
//      量每一次諧波相對於基頻的 dB、thdPercent() 算 THD、平均值算 DC offset。
//      觀察：對稱曲線只有奇次諧波；Asymmetric 多了偶次諧波與 DC；drive 越大諧波越多。
//      另外印出 Asymmetric 的 DC 與小訊號近似 2A³/(3π) 的比較（README 2.4 節）。
//   3. 把 Cubic（y = x - x³/3）的量測值跟 README 2.2 節的手算結果對照：
//        A = 0.5 → H3 = -33.06 dB、THD = 2.22%；A = 1 → H3 = -19.08 dB、THD = 11.11%。
//   4. 輸出「drive 從 0 dB 掃到 +24 dB」的 220 Hz 正弦波 WAV（經 Saturation、4x oversampling），
//      讓你聽到諧波一點一點長出來。
//
//   為什麼用 100 Hz？因為 100 Hz 的第 240 次諧波才到 Nyquist（24 kHz），這些平滑曲線的高次諧波早就
//   衰減到聽不見，所以不需要 oversampling 就能量到「真正的」諧波（沒有 aliasing 汙染）。
//   第 2 支範例 l06_aliasing_demo 會刻意用 7 kHz 讓 aliasing 發生。
//
// 怎麼執行：l06_harmonic_analysis（不需要參數）
//
// 輸出檔案（在 dawlab_output/）：
//   l06_drive_sweep_tanh.wav / l06_drive_sweep_cubic.wav / l06_drive_sweep_arctan.wav /
//   l06_drive_sweep_asymmetric.wav     220 Hz，4 秒，drive 0 → +24 dB
//
// 該聽到：
//   開頭是乾淨的正弦波，之後越來越「亮」、越來越「扁」。Tanh 與 Cubic 偏「空心、方波感」（奇次），
//   Asymmetric 聽起來比較「厚、有高八度」（偶次）；Cubic 在 drive 很大時聽得出 hard clip 的粗糙感。

#include <dawlab/Common.h>
#include <dawlab/dsp/Saturation.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr double kAnalysisHz = 100.0;

using dawlab::dsp::Saturation;
using dawlab::dsp::SaturationCurve;

const SaturationCurve kCurves[] = {SaturationCurve::Tanh, SaturationCurve::Cubic, SaturationCurve::Arctan,
                                   SaturationCurve::Asymmetric};

double meanOf(const float* x, int n) {
    double s = 0.0;
    for (int i = 0; i < n; ++i)
        s += x[i];
    return n > 0 ? s / n : 0.0;
}

// 把振幅 A 的正弦波丟進曲線，回傳 (H1 振幅, 各次諧波 dB, THD %, DC)
struct Measurement {
    double h1 = 0.0;
    std::vector<double> levelsDb;
    double thd = 0.0;
    double dc = 0.0;
};

Measurement analyse(SaturationCurve curve, float amplitude) {
    dawlab::AudioBuffer b = dawlab::makeSine(kAnalysisHz, kSampleRate, 1.0, amplitude, 1);
    float* d = b.getWritePointer(0);
    const int n = b.getNumSamples();
    for (int i = 0; i < n; ++i)
        d[i] = Saturation::shape(curve, d[i]);
    Measurement m;
    m.h1 = dawlab::goertzelAmplitude(d, n, kAnalysisHz, kSampleRate);
    m.levelsDb = dawlab::harmonicLevelsDb(d, n, kAnalysisHz, kSampleRate, 8);
    m.thd = dawlab::thdPercent(d, n, kAnalysisHz, kSampleRate, 20);
    m.dc = meanOf(d, n);
    return m;
}

} // namespace

int main() {
    // ---- 1. transfer curve 數值表 ------------------------------------------------------------
    std::printf("Transfer curves（y = shape(curve, x)）：\n");
    std::printf("%8s %10s %10s %10s %12s\n", "x", "Tanh", "Cubic", "Arctan", "Asymmetric");
    for (double x = -3.0; x <= 3.0001; x += 0.5) {
        std::printf("%8.2f", x);
        for (SaturationCurve c : kCurves)
            std::printf(" %10.4f", Saturation::shape(c, static_cast<float>(x)));
        std::printf("\n");
    }
    std::printf("（Cubic 在 |x| ≥ 1 時固定在 ±2/3；Asymmetric 的負半波提早在 -0.5 飽和）\n\n");

    // ---- 2. 諧波分析表 -----------------------------------------------------------------------
    const float amplitudes[] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f};
    for (SaturationCurve c : kCurves) {
        std::printf("=== %s ===  輸入 %.0f Hz 正弦波，振幅 A（= 進入曲線前的 peak）\n", dawlab::dsp::saturationCurveName(c),
                    kAnalysisHz);
        std::printf("%6s %8s %8s %8s %8s %8s %8s %8s %8s %8s %9s\n", "A", "H1 amp", "H2", "H3", "H4", "H5", "H6", "H7",
                    "H8", "THD%", "DC");
        for (float a : amplitudes) {
            const Measurement m = analyse(c, a);
            std::printf("%6.2f %8.4f", a, m.h1);
            for (size_t h = 1; h < 8; ++h) {
                const double db = h < m.levelsDb.size() ? m.levelsDb[h] : dawlab::kMinusInfinityDb;
                if (db <= -99.0)
                    std::printf(" %8s", "<-99");
                else
                    std::printf(" %8.1f", db);
            }
            std::printf(" %8.2f %+9.4f\n", m.thd, m.dc);
        }
        std::printf("\n");
    }
    std::printf("（dB 都是相對於基頻 H1；Tanh / Cubic / Arctan 的偶次諧波與 DC 應該是 0（印成 <-99）；\n"
                "  Asymmetric 的 H2、H4 與 DC 都不是 0 —— 這就是「類 tube」的偶次諧波。）\n\n");

    // Asymmetric 的偶部 g(x) = (f(x) + f(-x))/2 = t³ / (2(1 + t²))，t = tanh|x|（README 2.4 節）。
    // g ≥ 0，所以 DC 一定是正的；小訊號時 g ≈ |x|³/2，而 |sin θ|³ 的平均是 4/(3π) → DC ≈ 2A³/(3π)。
    std::printf("Asymmetric 的 DC：小訊號近似 2A³/(3π) vs 量測\n");
    std::printf("%6s %12s %12s\n", "A", "近似", "量測");
    for (float a : {0.1f, 0.25f, 0.5f}) {
        const double approx = 2.0 * a * a * a / (3.0 * dawlab::kPi);
        std::printf("%6.2f %12.5f %12.5f\n", a, approx, analyse(SaturationCurve::Asymmetric, a).dc);
    }
    std::printf("（A 越小越準；A 變大後 tanh 的高次項讓 DC 比近似值小）\n\n");

    // ---- 3. Cubic 的理論值對照 ---------------------------------------------------------------
    std::printf("Cubic（y = x - x³/3）手算 vs 量測：\n");
    std::printf("  x = A sin θ → y = (A - A³/4) sin θ + (A³/12) sin 3θ\n");
    std::printf("%6s %12s %12s %12s %12s\n", "A", "理論 H3 dB", "量測 H3 dB", "理論 THD%", "量測 THD%");
    double worst = 0.0;
    for (float a : {0.25f, 0.5f, 1.0f}) {
        const double A = a;
        const double h1 = A - A * A * A / 4.0;
        const double h3 = A * A * A / 12.0;
        const double theoryDb = dawlab::gainToDb(h3 / h1);
        const double theoryThd = 100.0 * h3 / h1;
        const Measurement m = analyse(SaturationCurve::Cubic, a);
        std::printf("%6.2f %12.2f %12.2f %12.2f %12.2f\n", A, theoryDb, m.levelsDb[2], theoryThd, m.thd);
        worst = std::max(worst, std::abs(theoryDb - m.levelsDb[2]));
    }
    std::printf("（|x| ≤ 1 時 Cubic 只有 3 次諧波，所以 THD 就是 H3/H1；A > 1 進入截斷區才會長出 5、7 次……）\n\n");

    // ---- 4. drive sweep WAV ------------------------------------------------------------------
    const double seconds = 4.0;
    for (SaturationCurve c : kCurves) {
        dawlab::AudioBuffer buffer = dawlab::makeSine(220.0, kSampleRate, seconds, 0.5f, 2);
        Saturation sat;
        sat.setOversamplingFactorLog2(2); // 4x：220 Hz 的高次諧波很多，避免 aliasing
        sat.prepare(kSampleRate, kBlockSize, 2);
        sat.setCurve(c);
        sat.setMix(1.0f);
        sat.setOutputGainDb(-3.0f);
        dawlab::AudioBlock whole(buffer);
        for (int start = 0; start < buffer.getNumSamples(); start += kBlockSize) {
            const int len = std::min(kBlockSize, buffer.getNumSamples() - start);
            // 進入曲線前的振幅 0.5 × drive；drive 從 0 dB 線性掃到 +24 dB（peak 0.5 → 7.9）
            sat.setDriveDb(static_cast<float>(24.0 * start / buffer.getNumSamples()));
            sat.process(whole.getSubBlock(start, len));
        }
        std::string name = dawlab::dsp::saturationCurveName(c);
        for (char& ch : name)
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        dawlab::writeWav(dawlab::outputPath("l06_drive_sweep_" + name + ".wav"), buffer, kSampleRate);
        std::printf("已輸出 dawlab_output/l06_drive_sweep_%s.wav（peak %.3f）\n", name.c_str(), dawlab::peakAbs(buffer));
    }

    std::printf("\nCubic 理論 vs 量測最大差距 %.3f dB%s\n", worst, worst < 0.1 ? "（一致）" : "（[FAIL] 不一致！）");
    return worst < 0.1 ? 0 : 1;
}
