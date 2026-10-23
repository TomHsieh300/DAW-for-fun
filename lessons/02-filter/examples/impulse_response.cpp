// l02_impulse_response：印出幾種 filter 的 impulse response（IR）。
//
// 示範什麼：
//   - 送一個單位脈衝 δ[n]（第 0 個 sample = 1，其餘 0）進 filter，輸出就是 h[n]。
//     h[n] 完整描述一個 LTI（線性非時變）系統：任何輸入的輸出 = x 與 h 的 convolution。
//   - FIR（moving average）：IR 有限長，N 個 sample 後就是 0。
//   - IIR（one-pole）：IR 無限長，h[n] = (1 - a) a^n，呈指數衰減——跟理論值逐項比對。
//   - biquad lowpass：Q 越高，IR 振盪（ringing）越久；Q = 0.707 時幾乎不振盪。
//   - 用 IR 直接算「頻率響應」：H(e^{jω}) = Σ h[n] e^{-jωn}（DTFT），和 magnitudeDb() 對照。
//
// 怎麼執行：不帶參數直接執行。輸出 WAV：
//   dawlab_output/l02_ir_biquad_q0707.wav、l02_ir_biquad_q8.wav（放大後的 IR，可以聽到 Q=8 的 "ping"）
#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>
#include <dawlab/dsp/OnePole.h>

#include <complex>
#include <cstdio>
#include <vector>

using namespace dawlab;
using dawlab::dsp::Biquad;
using dawlab::dsp::BiquadCoefficients;
using dawlab::dsp::OnePole;

constexpr double kSampleRate = 48000.0;

// 把 impulse 丟進任何 processSample 形式的系統，收集前 n 個輸出
template <typename Fn>
static std::vector<double> impulseResponse(Fn&& processSample, int n) {
    std::vector<double> h(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        h[static_cast<size_t>(i)] = processSample(i == 0 ? 1.0f : 0.0f);
    return h;
}

// DTFT：H(e^{jω}) = Σ h[n] e^{-jωn}
static double dtftDb(const std::vector<double>& h, double f) {
    std::complex<double> sum = 0.0;
    for (size_t n = 0; n < h.size(); ++n)
        sum += h[n] * std::polar(1.0, -kTwoPi * f * static_cast<double>(n) / kSampleRate);
    return gainToDb(std::abs(sum));
}

// ringing 長度：|h[n]| 最後一次超過「IR 峰值的 1%」（-40 dB）的位置
static int ringLength(const std::vector<double>& h) {
    double peak = 0.0;
    for (double v : h)
        peak = std::max(peak, std::abs(v));
    int last = 0;
    for (size_t n = 0; n < h.size(); ++n)
        if (std::abs(h[n]) > 0.01 * peak)
            last = static_cast<int>(n);
    return last;
}

static void printRow(const char* name, const std::vector<double>& h, int count) {
    std::printf("    %-26s", name);
    for (int i = 0; i < count; ++i)
        std::printf(" %7.4f", h[static_cast<size_t>(i)]);
    std::printf("\n");
}

int main() {
    std::printf("=== 第 02 章：impulse response（fs = %.0f Hz）===\n\n", kSampleRate);
    constexpr int kShow = 12;
    std::printf("    %-26s", "n =");
    for (int i = 0; i < kShow; ++i)
        std::printf(" %7d", i);
    std::printf("\n");

    // ---- FIR：4 點 moving average ----------------------------------------------------
    // y[n] = (x[n] + x[n-1] + x[n-2] + x[n-3]) / 4  →  h = {0.25, 0.25, 0.25, 0.25, 0, 0, ...}
    {
        float d[3] = {0.0f, 0.0f, 0.0f};
        auto ma = [&](float x) {
            const float y = 0.25f * (x + d[0] + d[1] + d[2]);
            d[2] = d[1];
            d[1] = d[0];
            d[0] = x;
            return y;
        };
        const auto h = impulseResponse(ma, 64);
        printRow("FIR moving avg N=4", h, kShow);
        DAWLAB_CHECK_NEAR(h[3], 0.25, 1e-9);
        DAWLAB_CHECK_NEAR(h[4], 0.0, 1e-9); // 第 N 個 sample 之後全是 0：有限長
        // 頻率響應：fs/N = 12 kHz 處完全消失（null），DC 處 0 dB
        std::printf("      → DTFT: DC = %.2f dB, 12 kHz = %.1f dB（N 點平均的第一個 null 在 fs/N）\n",
                    dtftDb(h, 0.0), dtftDb(h, 12000.0));
        DAWLAB_CHECK(dtftDb(h, 12000.0) < -90.0);
    }

    // ---- IIR：one-pole lowpass ------------------------------------------------------
    {
        OnePole lp;
        lp.prepare(kSampleRate, 512, 1);
        lp.setCutoff(1000.0f);
        const double a = lp.getCoefficient();
        const auto h = impulseResponse([&](float x) { return lp.processSample(0, x); }, 4096);
        printRow("one-pole LP fc=1k", h, kShow);
        // 理論：h[n] = (1 - a) a^n
        double maxErr = 0.0;
        for (int n = 0; n < 64; ++n)
            maxErr = std::max(maxErr, std::abs(h[static_cast<size_t>(n)] - (1.0 - a) * std::pow(a, n)));
        std::printf("      → a = %.6f，h[n] 與 (1-a)a^n 的最大誤差 = %.2e；h[100] = %.2e（永遠不會真的變 0）\n", a,
                    maxErr, h[100]);
        DAWLAB_CHECK(maxErr < 1e-6);
        DAWLAB_CHECK(h[100] > 0.0);
        // IR 的和 = DC gain = 1（lowpass 讓直流完全通過）
        double sum = 0.0;
        for (double v : h)
            sum += v;
        DAWLAB_CHECK_NEAR(sum, 1.0, 1e-6);
        DAWLAB_CHECK_NEAR(dtftDb(h, 1000.0), -3.0, 0.1);
    }

    // ---- biquad lowpass：Q 對 ringing 的影響 ------------------------------------------
    Biquad bq;
    bq.prepare(kSampleRate, 512, 1);
    std::vector<double> hQ0707, hQ8;
    for (double q : {0.7071, 2.0, 8.0}) {
        bq.setCoefficients(BiquadCoefficients::makeLowpass(kSampleRate, 1000.0, q));
        bq.reset();
        const auto h = impulseResponse([&](float x) { return bq.processSample(0, x); }, 48000);
        char name[32];
        std::snprintf(name, sizeof(name), "biquad LP fc=1k Q=%g", q);
        printRow(name, h, kShow);
        // 「|h[n]| 最後一次超過峰值的 1% 是第幾個 sample」= ringing 長度
        const int last = ringLength(h);
        std::printf("      → |h[n]| 降到峰值 1%% 以下的時間：%d samples = %.1f ms；DTFT(1 kHz) = %.2f dB\n", last,
                    samplesToMs(last, kSampleRate), dtftDb(h, 1000.0));
        if (q < 1.0)
            hQ0707 = h;
        if (q > 7.0)
            hQ8 = h;
    }
    // Q=0.707 幾乎不振盪（很快衰減），Q=8 的 ringing 長得多
    {
        DAWLAB_CHECK(ringLength(hQ8) > 5 * ringLength(hQ0707));
        // Q = 8 在 cutoff 處的增益 ≈ 20 log10(Q) = 18.06 dB
        DAWLAB_CHECK_NEAR(dtftDb(hQ8, 1000.0), 20.0 * std::log10(8.0), 0.1);
    }

    // 寫成 WAV（放大 0.5 倍，重複 4 次，讓人聽得到 "click" vs "ping"）
    auto writeIr = [&](const std::vector<double>& h, const char* file) {
        AudioBuffer out(1, 48000);
        for (int rep = 0; rep < 4; ++rep)
            for (int n = 0; n < 12000 && n < static_cast<int>(h.size()); ++n)
                out.setSample(0, rep * 12000 + n, static_cast<float>(0.5 * h[static_cast<size_t>(n)]));
        writeWav(outputPath(file), out, kSampleRate);
    };
    writeIr(hQ0707, "l02_ir_biquad_q0707.wav");
    writeIr(hQ8, "l02_ir_biquad_q8.wav");
    std::printf("\n已輸出 dawlab_output/l02_ir_biquad_q0707.wav（短促的 click）與 l02_ir_biquad_q8.wav（1 kHz 的 ping）\n\n");

    return dawlab::test::summary("l02_impulse_response");
}
