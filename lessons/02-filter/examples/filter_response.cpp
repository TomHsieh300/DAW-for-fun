// l02_filter_response：理論頻率響應 vs 實測。
//
// 示範什麼：
//   1. one-pole lowpass：exp 版（a = exp(-2π fc/fs)）與 bilinear 版的響應比較，
//      並用二分搜尋找出實際的 -3 dB 點，看 cutoff 越高兩者差多少。
//   2. RBJ biquad 的 8 種 filter：把 BiquadCoefficients::magnitudeDb()（把 z = e^{jω} 代入 H(z)）
//      和 measureGainDb()（真的送正弦波進 filter 量輸出）放在同一張表，兩者應該一致。
//   3. 斜率：LPF 在 cutoff 以上每個 octave 掉多少 dB（biquad ≈ 12 dB/oct，one-pole ≈ 6 dB/oct）。
//   4. Cramping：cutoff 接近 Nyquist 時，bilinear transform 把 analog prototype 的曲線「擠」向 Nyquist。
//
// 怎麼執行：不帶參數直接執行。沒有 WAV 輸出，只印表格；最後用 self-check 確認理論與實測一致。
#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>
#include <dawlab/dsp/OnePole.h>

#include <cstdio>
#include <functional>
#include <limits>
#include <vector>

using namespace dawlab;
using dawlab::dsp::Biquad;
using dawlab::dsp::BiquadCoefficients;
using dawlab::dsp::OnePole;

constexpr double kSampleRate = 48000.0;

// ---- one-pole 的兩種設計 -------------------------------------------------------------
// exp 版：H(z) = (1-a) / (1 - a z^-1)，a = exp(-2π fc/fs)
static double onePoleExpDb(double fc, double f) {
    const double a = OnePole::coefficientForCutoff(fc, kSampleRate);
    const std::complex<double> z1 = std::polar(1.0, -kTwoPi * f / kSampleRate);
    return gainToDb(std::abs((1.0 - a) / (1.0 - a * z1)));
}

// bilinear 版：analog H(s) = 1/(1 + s/ωc)，s = (2/T)(1 - z^-1)/(1 + z^-1)，prewarp 後
//   g = tan(π fc/fs)，H(z) = g(1 + z^-1) / ((1+g) - (1-g) z^-1)
static double onePoleBilinearDb(double fc, double f) {
    const double g = std::tan(kPi * fc / kSampleRate);
    const std::complex<double> z1 = std::polar(1.0, -kTwoPi * f / kSampleRate);
    return gainToDb(std::abs(g * (1.0 + z1) / ((1.0 + g) - (1.0 - g) * z1)));
}

// 二分搜尋 responseDb(f) = targetDb 的頻率（responseDb 在 [lo, hi] 內要單調）
static double findFrequencyAtDb(const std::function<double(double)>& responseDb, double targetDb, double lo,
                                double hi) {
    const bool decreasing = responseDb(lo) > responseDb(hi);
    for (int i = 0; i < 60; ++i) {
        const double mid = std::sqrt(lo * hi); // 幾何平均：在 log 頻率軸上對半切
        const bool above = responseDb(mid) > targetDb;
        if (above == decreasing)
            lo = mid;
        else
            hi = mid;
    }
    return std::sqrt(lo * hi);
}

int main() {
    std::printf("=== 第 02 章：理論頻率響應 vs 實測（fs = %.0f Hz）===\n\n", kSampleRate);

    // ---- 1. one-pole：exp 版 vs bilinear 版 ---------------------------------------------
    std::printf("[1] one-pole lowpass：exp 版 vs bilinear 版的實際 -3.01 dB 點\n");
    std::printf("    %8s  %12s  %12s  %12s\n", "fc (Hz)", "a=exp(..)", "exp -3dB", "bilinear -3dB");
    for (double fc : {100.0, 1000.0, 5000.0, 10000.0, 15000.0}) {
        const double a = OnePole::coefficientForCutoff(fc, kSampleRate);
        const double f3bil = findFrequencyAtDb([&](double f) { return onePoleBilinearDb(fc, f); }, -3.0103, 1.0,
                                               kSampleRate * 0.4999);
        // exp 版在 Nyquist 的增益是 (1-a)/(1+a)，不是 0：fc 太高時整條曲線都在 -3 dB 以上，-3 dB 點根本不存在
        const double nyquistDb = onePoleExpDb(fc, kSampleRate * 0.5);
        if (nyquistDb < -3.0103) {
            const double f3exp = findFrequencyAtDb([&](double f) { return onePoleExpDb(fc, f); }, -3.0103, 1.0,
                                                   kSampleRate * 0.4999);
            std::printf("    %8.0f  %12.6f  %12.1f  %12.1f\n", fc, a, f3exp, f3bil);
        } else {
            std::printf("    %8.0f  %12.6f  %12s  %12.1f   ← exp 版在 Nyquist 也只有 %.2f dB\n", fc, a, "不存在", f3bil,
                        nyquistDb);
            DAWLAB_CHECK(fc >= 12000.0); // 只有 cutoff 很高時才會發生
        }
        // bilinear 版 -3 dB 點永遠精準落在 fc（prewarp 的目的）
        DAWLAB_CHECK_NEAR(f3bil, fc, fc * 0.001);
    }
    std::printf("    → exp 版在 fc << fs 時很準，fc 越接近 Nyquist 偏得越多；bilinear 版永遠精準。\n\n");
    // 書上的數字：fc = 1 kHz 時 exp 版只偏 0.14%，fc = 10 kHz 時偏約 19%
    DAWLAB_CHECK_NEAR(findFrequencyAtDb([](double f) { return onePoleExpDb(1000.0, f); }, -3.0103, 1.0, 23999.0),
                      1001.4, 0.5);
    DAWLAB_CHECK_NEAR(findFrequencyAtDb([](double f) { return onePoleExpDb(10000.0, f); }, -3.0103, 1.0, 23999.0),
                      11895.0, 5.0);
    // 2.8 節表格的其他兩列：fc = 5 kHz 的 exp 版 -3 dB 點，以及 fc = 15 kHz 時 Nyquist 處只有 -2.45 dB
    DAWLAB_CHECK_NEAR(findFrequencyAtDb([](double f) { return onePoleExpDb(5000.0, f); }, -3.0103, 1.0, 23999.0),
                      5188.9, 0.5);
    DAWLAB_CHECK_NEAR(onePoleExpDb(15000.0, kSampleRate * 0.5), -2.45, 0.01);

    // OnePole class 的實測：1 kHz lowpass 在 1 kHz 應該約 -3 dB，10 kHz 約 -20 dB（6 dB/oct）
    {
        OnePole lp;
        lp.prepare(kSampleRate, 512, 1);
        lp.setType(OnePole::Type::Lowpass);
        lp.setCutoff(1000.0f);
        std::printf("[1b] OnePole class 實測（Lowpass fc = 1 kHz）\n");
        std::printf("    %8s  %10s  %10s\n", "f (Hz)", "theory dB", "measured");
        for (double f : {100.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0}) {
            lp.reset();
            const double theory = onePoleExpDb(1000.0, f);
            const double meas = measureGainDb([&](float x) { return lp.processSample(0, x); }, f, kSampleRate);
            std::printf("    %8.0f  %10.2f  %10.2f\n", f, theory, meas);
            DAWLAB_CHECK_NEAR(meas, theory, 0.05);
        }
        OnePole hp;
        hp.prepare(kSampleRate, 512, 1);
        hp.setType(OnePole::Type::Highpass);
        hp.setCutoff(1000.0f);
        // highpass 理論值：H(z) = h (1 - z^-1) / (1 - a z^-1)，h = (1 + a) / 2
        const auto onePoleHpDb = [](double fcHz, double f) {
            const double ah = OnePole::coefficientForCutoff(fcHz, kSampleRate);
            const double h = 0.5 * (1.0 + ah);
            const std::complex<double> z1 = std::polar(1.0, -kTwoPi * f / kSampleRate);
            return gainToDb(std::abs(h * (1.0 - z1) / (1.0 - ah * z1)));
        };
        std::printf("    Highpass fc = 1 kHz（實測 / 理論）：");
        double hpMeas[3] = {};
        int idx = 0;
        for (double f : {20.0, 1000.0, 20000.0}) {
            hp.reset();
            const double meas = measureGainDb([&](float x) { return hp.processSample(0, x); }, f, kSampleRate);
            const double theory = onePoleHpDb(1000.0, f);
            std::printf("  %.0f Hz = %.2f / %.2f dB", f, meas, theory);
            DAWLAB_CHECK_NEAR(meas, theory, 0.05);
            hpMeas[idx++] = meas;
        }
        std::printf("\n");
        // README 2.9 節引用的數字：20 Hz ≈ 6 dB/oct 外插 20 log10(20/1000) = -33.98 dB、fc 處 -3 dB、高頻 0 dB
        DAWLAB_CHECK_NEAR(hpMeas[0], 20.0 * std::log10(20.0 / 1000.0), 0.1);
        DAWLAB_CHECK_NEAR(hpMeas[1], -3.0, 0.05);
        DAWLAB_CHECK_NEAR(hpMeas[2], 0.0, 0.05);
        DAWLAB_CHECK_NEAR(onePoleHpDb(1000.0, kSampleRate * 0.5), 0.0, 1e-9); // h = (1+a)/2：Nyquist 剛好 0 dB

        // NaN 參數防護（README 第 4 節）：std::clamp / std::max 擋不住 NaN，所以 setter / factory 要先擋
        {
            const double before = hp.getCoefficient();
            hp.setCutoff(std::numeric_limits<float>::quiet_NaN()); // 被忽略，保留上一個有效值
            DAWLAB_CHECK(hp.getCoefficient() == before && std::isfinite(hp.processSample(0, 0.5f)));
            const double qnan = std::numeric_limits<double>::quiet_NaN();
            const BiquadCoefficients bad[] = {BiquadCoefficients::makeLowpass(kSampleRate, qnan, 0.7),
                                              BiquadCoefficients::makeLowpass(kSampleRate, 1000.0, qnan),
                                              BiquadCoefficients::makePeak(kSampleRate, 1000.0, 1.0, qnan)};
            for (const BiquadCoefficients& c : bad)
                DAWLAB_CHECK(std::isfinite(c.b0) && std::isfinite(c.a1) && c.isStable());
        }

        // 手算對照（練習 1）：fc = 1 kHz 的兩種 one-pole 係數，以及 exp 版在 Nyquist 的增益
        const double a = lp.getCoefficient();
        const double nyqDb = gainToDb((1.0 - a) / (1.0 + a));
        const double g = std::tan(kPi * 1000.0 / kSampleRate);
        const double pole = (1.0 - g) / (1.0 + g);
        std::printf("    手算對照：exp 版 a = %.6f，b0 = 1 - a = %.6f，Nyquist 增益 (1-a)/(1+a) = %.2f dB\n", a, 1.0 - a,
                    nyqDb);
        std::printf("              bilinear 版 g = tan(π fc/fs) = %.6f，pole = (1-g)/(1+g) = %.6f，b0 = b1 = g/(1+g) = %.6f\n\n",
                    g, pole, g / (1.0 + g));
        DAWLAB_CHECK_NEAR(a, 0.877306, 1e-6);
        DAWLAB_CHECK_NEAR(nyqDb, -23.69, 0.01);
        DAWLAB_CHECK_NEAR(onePoleExpDb(1000.0, kSampleRate * 0.5), nyqDb, 1e-9);
        DAWLAB_CHECK_NEAR(pole, 0.876976, 1e-6);
        DAWLAB_CHECK(onePoleBilinearDb(1000.0, kSampleRate * 0.4999) < -80.0); // zero 在 z = -1：Nyquist 處趨近 -∞
    }

    // ---- 2. biquad 八種 filter：理論 vs 實測 ------------------------------------------
    struct Case {
        const char* name;
        BiquadCoefficients c;
    };
    const double fc = 1000.0, q = BiquadCoefficients::kButterworthQ;
    const std::vector<Case> cases = {
        {"Lowpass  Q=0.707", BiquadCoefficients::makeLowpass(kSampleRate, fc, q)},
        {"Highpass Q=0.707", BiquadCoefficients::makeHighpass(kSampleRate, fc, q)},
        {"Bandpass Q=2    ", BiquadCoefficients::makeBandpass(kSampleRate, fc, 2.0)},
        {"Notch    Q=2    ", BiquadCoefficients::makeNotch(kSampleRate, fc, 2.0)},
        {"Allpass  Q=0.707", BiquadCoefficients::makeAllpass(kSampleRate, fc, q)},
        {"Peak +6dB Q=1   ", BiquadCoefficients::makePeak(kSampleRate, fc, 1.0, 6.0)},
        {"LowShelf +6dB   ", BiquadCoefficients::makeLowShelf(kSampleRate, fc, q, 6.0)},
        {"HighShelf -6dB  ", BiquadCoefficients::makeHighShelf(kSampleRate, fc, q, -6.0)},
    };
    const double freqs[] = {50.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 16000.0};

    std::printf("[2] RBJ biquad（fc = 1 kHz）：每格「理論 / 實測」dB\n");
    std::printf("    %-17s", "filter");
    for (double f : freqs)
        std::printf("  %13.0f", f);
    std::printf("\n");

    Biquad filter;
    filter.prepare(kSampleRate, 512, 1);
    for (const Case& cs : cases) {
        std::printf("    %-17s", cs.name);
        filter.setCoefficients(cs.c);
        for (double f : freqs) {
            filter.reset();
            const double theory = cs.c.magnitudeDb(f, kSampleRate);
            const double meas =
                measureGainDb([&](float x) { return filter.processSample(0, x); }, f, kSampleRate, 0.5, 0.3);
            std::printf("  %6.2f/%6.2f", theory, meas);
            if (theory > -50.0)
                DAWLAB_CHECK_NEAR(meas, theory, 0.1);
            else
                DAWLAB_CHECK(meas < -50.0); // notch 中心：量測受 window leakage 限制，只要求「非常深」
        }
        std::printf("\n");
        DAWLAB_CHECK(cs.c.isStable());
    }
    std::printf("\n");

    // 幾個「教科書數字」的 self-check
    const BiquadCoefficients lpf = cases[0].c;
    DAWLAB_CHECK_NEAR(lpf.magnitudeDb(1000.0, kSampleRate), -3.01, 0.05);  // Butterworth：cutoff 處 -3 dB
    DAWLAB_CHECK_NEAR(lpf.magnitudeDb(20.0, kSampleRate), 0.0, 0.01);      // 通帶 0 dB
    DAWLAB_CHECK_NEAR(cases[2].c.magnitudeDb(1000.0, kSampleRate), 0.0, 0.01); // BPF 峰值 0 dB
    DAWLAB_CHECK(cases[3].c.magnitudeDb(1000.0, kSampleRate) < -60.0);        // notch 中心幾乎全消
    DAWLAB_CHECK_NEAR(cases[4].c.magnitudeDb(300.0, kSampleRate), 0.0, 1e-9); // allpass 全頻段 0 dB
    DAWLAB_CHECK_NEAR(std::abs(cases[4].c.phase(1000.0, kSampleRate)), kPi, 1e-6); // allpass 在 fc 相位 ±180°
    DAWLAB_CHECK_NEAR(cases[5].c.magnitudeDb(1000.0, kSampleRate), 6.0, 0.01); // peak 在 fc = +6 dB
    DAWLAB_CHECK_NEAR(cases[6].c.magnitudeDb(20.0, kSampleRate), 6.0, 0.05);   // low shelf 低頻 +6 dB
    DAWLAB_CHECK_NEAR(cases[7].c.magnitudeDb(20000.0, kSampleRate), -6.0, 0.05); // high shelf 高頻 -6 dB

    // ---- 3. 斜率：每個 octave 掉幾 dB ------------------------------------------------
    std::printf("[3] 斜率（cutoff 以上，每升一個 octave 的衰減）\n");
    {
        const double d1 = lpf.magnitudeDb(4000.0, kSampleRate) - lpf.magnitudeDb(2000.0, kSampleRate);
        const double d2 = lpf.magnitudeDb(8000.0, kSampleRate) - lpf.magnitudeDb(4000.0, kSampleRate);
        // one-pole 用 fc = 100 Hz 來看：離 Nyquist 夠遠，才看得到乾淨的 -6 dB/oct 漸近線
        const double o1 = onePoleExpDb(100.0, 1600.0) - onePoleExpDb(100.0, 800.0);
        const double o2 = onePoleExpDb(1000.0, 4000.0) - onePoleExpDb(1000.0, 2000.0);
        std::printf("    biquad LPF fc=1k    2k→4k: %.2f dB   4k→8k: %.2f dB   （漸近 -12 dB/oct；8 kHz 已開始 cramping，更陡）\n",
                    d1, d2);
        std::printf("    one-pole LP fc=100  800→1.6k: %.2f dB                  （漸近 -6 dB/oct）\n", o1);
        std::printf("    one-pole LP fc=1k   2k→4k: %.2f dB   （exp 版在 Nyquist 處不是 -∞，fc 高時斜率不到 6 dB/oct）\n", o2);
        DAWLAB_CHECK_NEAR(d1, -12.0, 0.6);
        DAWLAB_CHECK(d2 < d1 - 0.5); // cramping：越接近 Nyquist 越陡
        DAWLAB_CHECK_NEAR(o1, -6.0, 0.3);
        DAWLAB_CHECK(o2 > -6.0 && o2 < -4.5);
    }
    std::printf("\n");

    // ---- 4. Cramping：cutoff 接近 Nyquist ----------------------------------------------
    std::printf("[4] Cramping：analog prototype vs bilinear 後的 digital 響應（LPF Q=0.707）\n");
    std::printf("    %8s  %8s  %12s  %12s\n", "fc", "f", "analog dB", "digital dB");
    for (double cutoff : {1000.0, 10000.0, 18000.0}) {
        const BiquadCoefficients c = BiquadCoefficients::makeLowpass(kSampleRate, cutoff, q);
        for (double ratio : {0.5, 1.0, 1.2}) {
            const double f = cutoff * ratio;
            if (f >= kSampleRate * 0.5)
                continue;
            // analog 二階 Butterworth：|H|² = 1 / (1 + (f/fc)^4)
            const double analog = -10.0 * std::log10(1.0 + std::pow(f / cutoff, 4.0));
            const double digital = c.magnitudeDb(f, kSampleRate);
            std::printf("    %8.0f  %8.0f  %12.2f  %12.2f\n", cutoff, f, analog, digital);
            if (ratio == 1.0)
                DAWLAB_CHECK_NEAR(digital, -3.01, 0.01); // prewarp：cutoff 處永遠 -3.01 dB
            if (ratio == 1.2 && cutoff == 1000.0)
                DAWLAB_CHECK_NEAR(digital, analog, 0.02); // fc 低：幾乎和 analog 一樣
        }
    }
    // 2.14 節表格的數字：cutoff 上方 20% 處 analog 都是 -4.88 dB；digital 在 fc = 10k / 18k 時是 -5.89 / -16.79 dB
    {
        const auto digitalAt = [&](double cutoff, double f) {
            return BiquadCoefficients::makeLowpass(kSampleRate, cutoff, q).magnitudeDb(f, kSampleRate);
        };
        const double analog12 = -10.0 * std::log10(1.0 + std::pow(1.2, 4.0));
        DAWLAB_CHECK_NEAR(analog12, -4.88, 0.01);
        DAWLAB_CHECK_NEAR(digitalAt(10000.0, 12000.0), -5.89, 0.01);
        DAWLAB_CHECK_NEAR(digitalAt(18000.0, 21600.0), -16.79, 0.01);
        DAWLAB_CHECK(digitalAt(18000.0, 21600.0) < analog12 - 10.0); // cramping：多衰減超過 10 dB
    }
    std::printf("    → cutoff 處永遠對齊（prewarp 保證），但 cutoff 以上的曲線被壓向 Nyquist（24 kHz 處 = -∞）。\n");
    std::printf("      fc = 18 kHz 時 21.6 kHz 的衰減遠比 analog 深，這就是 cramping。\n\n");

    return dawlab::test::summary("l02_filter_response");
}
