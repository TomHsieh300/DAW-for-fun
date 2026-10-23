// 練習 7 解答（計算 + 觀念題的數字驗證）：comb filter 的峰谷、負 feedback、用 comb 「調音」
//
// 答案與推導寫在 solutions/README.md；這支程式把每個數字量一次：
//   1. D = 240 sample（5 ms @ 48 kHz）、g = 0.8 的 feedback comb：
//        峰在 k·fs/D = k·200 Hz，|H| = 1/(1−g) = 5 → +13.98 dB；
//        谷在 (k+½)·200 Hz，|H| = 1/(1+g) = 0.556 → −5.11 dB。
//   2. g = −0.8：峰谷對調位置——峰移到 (k+½)·fs/D = 100、300、500 Hz……（「少一半」的音高 + 只剩奇次諧波）。
//   3. 想讓 comb 的音高是 A4 = 440 Hz（fs = 44.1 kHz）：D = 44100/440 = 100.227 sample，不是整數。
//        D 取整數 100 → 峰在 441.0 Hz（+3.9 cents）；取 101 → 436.63 Hz（−13.3 cents）；
//        cents 用理論頻率 fs/D 計算；掃描量到的峰只有 0.1 Hz 解析度（436.6 Hz），只拿來驗證峰的位置。
//        用 cubic fractional delay → 峰就在 440 Hz。
//   4. 觀念：feedback comb 的 impulse response 是 g^k 每 D 個 sample 一次 → |g| ≥ 1 時不會衰減。
//   5. 練習 2 第 2 小題的數字：迴路裡放 one-pole LP（g = 0.99）仍然穩定；
//      放 Q = 4 的 biquad LP（cutoff 處約 +12 dB）時，g = 0.5 也會爆。
//
// 執行：l09_ex7_solution（不需要參數，不輸出 WAV）。

#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>
#include <dawlab/dsp/FractionalDelayLine.h>
#include <dawlab/dsp/OnePole.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

using dawlab::dsp::FractionalDelayLine;

namespace {

// y[n] = x[n] + g·y[n − D]，D 可以是小數（cubic 插值）。
// 先讀再 push：讀的時候本 sample 還沒寫入，所以讀 D − 1（同 Delay.h 的慣例）。
class FractionalComb {
public:
    void prepare(float delaySamples, float g) {
        delay_ = delaySamples;
        g_ = g;
        line_.prepare(static_cast<int>(delaySamples) + 4, 1);
    }
    float processSample(float x) noexcept {
        const float y = x + g_ * line_.readCubic(0, delay_ - 1.0f);
        line_.push(0, y);
        return y;
    }

private:
    FractionalDelayLine line_;
    float delay_ = 2.0f;
    float g_ = 0.0f;
};

double combGainDb(float delaySamples, float g, double freq, double sampleRate) {
    FractionalComb comb;
    comb.prepare(delaySamples, g);
    // 迴路 g = 0.8 時 echo 衰減到 −60 dB 需要 3D/(−log10 0.8) ≈ 31 圈，D ≤ 240 時 < 0.16 s，
    // 所以 measureGainDb 預設的 0.2 s 暖身足夠讓 transient 消失
    return dawlab::measureGainDb([&](float x) { return comb.processSample(x); }, freq, sampleRate);
}

// 在 [f0, f1] 以 step 掃描，回傳增益最大的頻率
double findPeak(float delaySamples, float g, double f0, double f1, double step, double sampleRate) {
    double bestF = f0, bestG = -1.0e9;
    for (double f = f0; f <= f1 + 1.0e-9; f += step) {
        const double gdb = combGainDb(delaySamples, g, f, sampleRate);
        if (gdb > bestG) {
            bestG = gdb;
            bestF = f;
        }
    }
    return bestF;
}

double cents(double f, double ref) { return 1200.0 * std::log2(f / ref); }

} // namespace

int main() {
    std::printf("=== 第 09 章 練習 7：comb filter 的計算 ===\n\n");

    // ---- 1. D = 240、g = 0.8 -----------------------------------------------------
    {
        const double fs = 48000.0;
        const float D = 240.0f, g = 0.8f;
        const double peakDb = dawlab::gainToDb(1.0 / (1.0 - g));
        const double notchDb = dawlab::gainToDb(1.0 / (1.0 + g));
        std::printf("D = 240（5 ms）、g = 0.8：理論 峰 %+.2f dB @ k·200 Hz、谷 %+.2f dB @ (k+½)·200 Hz\n", peakDb,
                    notchDb);
        std::printf("   freq(Hz)   量到(dB)   理論(dB)\n");
        for (double f : {200.0, 300.0, 400.0, 500.0, 1000.0, 1100.0, 5000.0, 5100.0}) {
            const bool isPeak = std::fmod(f, 200.0) == 0.0;
            const double expect = isPeak ? peakDb : notchDb;
            const double got = combGainDb(D, g, f, fs);
            std::printf("   %7.0f    %+7.2f    %+7.2f\n", f, got, expect);
            DAWLAB_CHECK_NEAR(got, expect, 0.05);
        }
        DAWLAB_CHECK_NEAR(peakDb, 13.98, 0.01);
        DAWLAB_CHECK_NEAR(notchDb, -5.11, 0.01);
        std::printf("\n");
    }

    // ---- 2. 負 feedback：峰移到奇數倍的 fs/(2D) ------------------------------------
    {
        const double fs = 48000.0;
        const float D = 240.0f, g = -0.8f;
        std::printf("g = −0.8：峰應該在 100、300、500 Hz（奇數 × fs/2D），谷在 200、400 Hz\n");
        std::printf("   freq(Hz)   量到(dB)\n");
        for (double f : {100.0, 200.0, 300.0, 400.0, 500.0}) {
            const double got = combGainDb(D, g, f, fs);
            std::printf("   %7.0f    %+7.2f\n", f, got);
            const bool oddMultiple = static_cast<int>(f / 100.0) % 2 == 1;
            DAWLAB_CHECK_NEAR(got, oddMultiple ? 13.98 : -5.11, 0.05);
        }
        std::printf("\n");
    }

    // ---- 3. 把 comb 調到 A4 = 440 Hz（fs = 44.1 kHz）-------------------------------
    {
        const double fs = 44100.0;
        const float g = 0.8f;
        const float exact = static_cast<float>(fs / 440.0); // 100.227
        const double pFrac = findPeak(exact, g, 435.0, 445.0, 0.1, fs);
        const double p100 = findPeak(100.0f, g, 435.0, 445.0, 0.1, fs);
        const double p101 = findPeak(101.0f, g, 435.0, 445.0, 0.1, fs);
        // 理論音高 fs/D 與 cents；掃描（0.1 Hz 一格）量到的峰只用來驗證位置——
        // 用四捨五入過的 436.6 Hz 算 cents 會得到 −13.4，比真正的 −13.3 多了捨入誤差。
        const double f100 = fs / 100.0, f101 = fs / 101.0;
        std::printf("fs = 44.1 kHz、想要 440 Hz 的 comb：D = %.3f sample\n", exact);
        std::printf("  D = 100（整數）     → 理論 fs/D = %.2f Hz（%+.1f cents），量到峰 %.1f Hz\n", f100,
                    cents(f100, 440.0), p100);
        std::printf("  D = 101（整數）     → 理論 fs/D = %.2f Hz（%+.1f cents），量到峰 %.1f Hz\n", f101,
                    cents(f101, 440.0), p101);
        std::printf("  D = %.3f（cubic）→ 量到峰 %.1f Hz\n", exact, pFrac);
        DAWLAB_CHECK_NEAR(cents(f100, 440.0), 3.93, 0.01);
        DAWLAB_CHECK_NEAR(cents(f101, 440.0), -13.30, 0.01);
        DAWLAB_CHECK_NEAR(p100, f100, 0.15);
        DAWLAB_CHECK_NEAR(p101, f101, 0.15);
        DAWLAB_CHECK_NEAR(pFrac, 440.0, 0.15);
        std::printf("  -> 整數 delay 只能做出 fs/整數 這些離散音高；要準確調音就需要 fractional delay。\n\n");
    }

    // ---- 4. impulse response：g^k 每 D 個 sample ------------------------------------
    {
        const float D = 10.0f;
        for (float g : {0.9f, 1.0f, 1.1f}) {
            FractionalComb comb;
            comb.prepare(D, g);
            float y = 0.0f;
            for (int n = 0; n <= 100 * 10; ++n)
                y = comb.processSample(n == 0 ? 1.0f : 0.0f); // 最後一個 n = 1000 = 第 100 個 echo
            std::printf("g = %.1f：第 100 個 echo 的振幅 %.4g（理論 g^100 = %.4g）\n", g, y, std::pow(g, 100.0f));
            DAWLAB_CHECK_NEAR(y / std::pow(g, 100.0f), 1.0, 1.0e-3);
        }
        std::printf("  -> |g| < 1 衰減、= 1 永遠不衰減、> 1 指數爆炸（g = 1.1 時 100 圈放大 %.0f 倍）\n",
                    std::pow(1.1, 100.0));
    }

    // ---- 5. 練習 2.2：迴路裡的 filter 增益 > 1 時，|g| < 1 不再保證穩定 ----------------
    {
        const double fs = 48000.0, fc = 1000.0;
        const int D = 240; // comb 的峰在 k·200 Hz，其中 1000 Hz 剛好落在 biquad 的共振上
        const auto lp = dawlab::dsp::BiquadCoefficients::makeLowpass(fs, fc, 4.0);
        double maxDb = -1.0e9, maxF = 0.0;
        for (double f = 500.0; f <= 1500.0; f += 1.0) {
            const double m = lp.magnitudeDb(f, fs);
            if (m > maxDb) {
                maxDb = m;
                maxF = f;
            }
        }
        std::printf("\nQ = 4 的 biquad lowpass（fc = 1 kHz）：fc 處 %+.2f dB，最大 %+.2f dB @ %.0f Hz\n",
                    lp.magnitudeDb(fc, fs), maxDb, maxF);
        std::printf("  g = 0.5 時迴路增益最大 = 0.5 × %.2f = %.2f > 1\n", dawlab::dbToGain(maxDb),
                    0.5 * dawlab::dbToGain(maxDb));
        DAWLAB_CHECK_NEAR(lp.magnitudeDb(fc, fs), 12.04, 0.05); // RBJ lowpass 在 fc 的增益 = Q
        DAWLAB_CHECK(0.5 * dawlab::dbToGain(maxDb) > 1.0);

        // 實際跑：y[n] = x[n] + g·F(y[n − D])，impulse 進去，看 2.5–3.0 s 的 peak
        auto runLoop = [&](auto&& filter, float g) {
            FractionalDelayLine line;
            line.prepare(D + 2, 1);
            const int total = dawlab::secondsToSamples(3.0, fs);
            const int tail = dawlab::secondsToSamples(2.5, fs);
            float tailPeak = 0.0f;
            for (int n = 0; n < total; ++n) {
                const float y = (n == 0 ? 1.0f : 0.0f) + g * filter(line.readInteger(0, D - 1));
                line.push(0, y);
                if (!std::isfinite(y)) // 發散到 float 溢位（inf / NaN）；std::max 遇到 NaN 會回傳 0，所以要特別處理
                    return std::numeric_limits<float>::infinity();
                if (n >= tail)
                    tailPeak = std::max(tailPeak, std::abs(y));
            }
            return tailPeak;
        };
        dawlab::dsp::OnePole onePole;
        onePole.setType(dawlab::dsp::OnePole::Type::Lowpass);
        onePole.prepare(fs, 512, 1);
        onePole.setCutoff(static_cast<float>(fc));
        const float tailOnePole = runLoop([&](float v) { return onePole.processSample(0, v); }, 0.99f);
        dawlab::dsp::Biquad biquad;
        biquad.prepare(fs, 512, 1);
        biquad.setCoefficients(lp);
        const float tailBiquad = runLoop([&](float v) { return biquad.processSample(0, v); }, 0.5f);
        std::printf("  impulse 進去，2.5–3.0 s 的 peak：one-pole LP、g = 0.99 → %.3g（衰減）；"
                    "Q = 4 biquad、g = 0.5 → %.3g（發散）\n",
                    tailOnePole, tailBiquad);
        DAWLAB_CHECK(tailOnePole < 0.1f);
        DAWLAB_CHECK(tailBiquad > 100.0f);
    }

    return dawlab::test::summary("l09_ex7");
}
