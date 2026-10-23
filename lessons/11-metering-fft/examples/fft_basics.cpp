// 範例 1：FFT 的正確性與效能
//
// 示範什麼：
//   1. 自己寫的 radix-2 FFT 跟「照定義算」的 naive DFT 比對（N = 16 .. 4096），誤差應在 1e-4 以下
//   2. inverse(forward(x)) 還原原訊號；forwardReal / inverseReal 與複數版一致
//   3. 用 Goertzel（Analysis.h）交叉驗證單一 bin 的振幅
//   4. 計時：FFT vs naive DFT，N = 256 .. 16384（naive 只跑到 4096，否則太慢）
//   5. 把 pluck 訊號切成 2048 的 block，每塊 forwardReal → inverseReal 再寫出 WAV（round-trip）
//   每一項都有 DAWLAB_CHECK 以數字驗證，最後印出 [PASS] / [FAIL]（ctest 也會跑這支）。
//
// 怎麼執行：build 後在 build 目錄執行 bin/Debug/l11_fft_basics.exe（不需要參數）
// 輸出檔案：dawlab_output/l11_fft_roundtrip.wav —— 聽起來應該跟原本的 pluck 完全一樣
//
// 提醒：Debug 組態下的計時只能看「趨勢」（O(N log N) vs O(N^2)），絕對值會比 Release 慢很多。

#include <dawlab/Common.h>
#include <dawlab/dsp/Fft.h>

#include <chrono>
#include <complex>
#include <cstdio>
#include <vector>

using namespace dawlab;
using dawlab::dsp::Fft;

namespace {

using Clock = std::chrono::steady_clock;

double millisecondsSince(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// 亂數複數訊號（deterministic）
std::vector<std::complex<float>> makeRandomComplex(int n, uint32_t seed) {
    NoiseGenerator noise(seed);
    std::vector<std::complex<float>> v(static_cast<size_t>(n));
    for (auto& c : v)
        c = std::complex<float>(noise.next(), noise.next());
    return v;
}

float maxAbsError(const std::vector<std::complex<float>>& a, const std::vector<std::complex<float>>& b) {
    float e = 0.0f;
    for (size_t i = 0; i < a.size(); ++i)
        e = std::max(e, std::abs(a[i] - b[i]));
    return e;
}

} // namespace

int main() {
    constexpr double sr = 48000.0;

    // ---- 1. 與 naive DFT 比對 ------------------------------------------------------------
    std::printf("== FFT vs naive DFT（最大絕對誤差，輸入 |x| <= 1 的亂數）==\n");
    std::printf("%8s %14s %14s\n", "N", "forward err", "inverse err");
    for (int order = 4; order <= 12; ++order) {
        const int n = 1 << order;
        Fft fft(order);
        const auto x = makeRandomComplex(n, 1000u + static_cast<uint32_t>(order));
        std::vector<std::complex<float>> reference(static_cast<size_t>(n)), got = x;
        dsp::naiveDft(x.data(), reference.data(), n);
        fft.forward(got.data());
        const float fwdErr = maxAbsError(got, reference);

        // 用 FFT 的輸出做 IFFT 應該回到 x；naive IDFT 也一樣
        std::vector<std::complex<float>> back = got;
        fft.inverse(back.data());
        const float invErr = maxAbsError(back, x);
        // forward 的輸出量級約 √N，float 的相對誤差約 1e-7 → 絕對誤差隨 N 緩慢增加
        DAWLAB_CHECK(fwdErr < 1e-4f);
        DAWLAB_CHECK(invErr < 1e-5f);
        std::printf("%8d %14.3e %14.3e\n", n, fwdErr, invErr);
    }

    // ---- 2. forwardReal / inverseReal 與複數版一致 ----------------------------------------
    {
        constexpr int order = 10, n = 1 << order;
        Fft fft(order);
        AudioBuffer sine = makeSine(1000.0, sr, static_cast<double>(n) / sr, 0.5f, 1);
        std::vector<std::complex<float>> full(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i)
            full[static_cast<size_t>(i)] = std::complex<float>(sine.getSample(0, i), 0.0f);
        fft.forward(full.data());

        std::vector<std::complex<float>> half(static_cast<size_t>(fft.getNumRealBins()));
        fft.forwardReal(sine.getReadPointer(0), half.data());
        float err = 0.0f;
        for (int k = 0; k < fft.getNumRealBins(); ++k)
            err = std::max(err, std::abs(half[static_cast<size_t>(k)] - full[static_cast<size_t>(k)]));
        // 共軛對稱：X[N-k] == conj(X[k])
        float symErr = 0.0f;
        for (int k = 1; k < n / 2; ++k)
            symErr = std::max(symErr, std::abs(full[static_cast<size_t>(n - k)] - std::conj(full[static_cast<size_t>(k)])));

        std::vector<float> restored(static_cast<size_t>(n));
        fft.inverseReal(half.data(), restored.data());
        float rtErr = 0.0f;
        for (int i = 0; i < n; ++i)
            rtErr = std::max(rtErr, std::abs(restored[static_cast<size_t>(i)] - sine.getSample(0, i)));
        std::printf("\n== 實數 FFT（N = %d）==\n", n);
        std::printf("forwardReal vs forward 誤差 = %.3e，共軛對稱誤差 = %.3e，inverseReal round-trip 誤差 = %.3e\n",
                    err, symErr, rtErr);
        DAWLAB_CHECK(err < 1e-6f);
        DAWLAB_CHECK(symErr < 1e-4f);
        DAWLAB_CHECK(rtErr < 1e-5f);

        // ---- 3. 用 Goertzel 交叉驗證：1 kHz 正弦落在哪個 bin？振幅是多少？-------------------
        // bin 寬 = fs/N = 46.875 Hz，1000 Hz 落在 bin 21.33 之間 → 不在 bin 中心，所以會 leakage
        int peakBin = 0;
        for (int k = 1; k < fft.getNumRealBins(); ++k)
            if (std::abs(half[static_cast<size_t>(k)]) > std::abs(half[static_cast<size_t>(peakBin)]))
                peakBin = k;
        const double binHz = sr / n;
        const double ampFromFft = 2.0 * std::abs(half[static_cast<size_t>(peakBin)]) / n; // rectangular window，CG = 1
        const double ampGoertzel = goertzelAmplitude(sine.getReadPointer(0), n, 1000.0, sr);
        std::printf("bin 寬 = %.3f Hz，最大 bin = %d（%.1f Hz），FFT 估的振幅 = %.4f，Goertzel = %.4f（真值 0.5）\n",
                    binHz, peakBin, peakBin * binHz, ampFromFft, ampGoertzel);
        std::printf("→ 1000 Hz 不在 bin 中心（1000 / %.3f = %.2f），能量漏到鄰近 bin，所以 FFT 的單一 bin 讀數偏低。\n",
                    binHz, 1000.0 / binHz);
        // 理論：rectangular window、偏離 bin 中心 δ = 1/3 bin → 振幅乘 sinc(δ) = sin(π/3)/(π/3) = 0.827
        const double sincDelta = std::sin(kPi / 3.0) / (kPi / 3.0);
        std::printf("理論值（rectangular 的 scalloping）：0.5 × sinc(1/3) = %.4f\n", 0.5 * sincDelta);
        DAWLAB_CHECK(peakBin == 21);
        DAWLAB_CHECK_NEAR(ampGoertzel, 0.5, 1e-3);
        DAWLAB_CHECK_NEAR(ampFromFft, 0.5 * sincDelta, 0.005);
    }

    // ---- 4. 計時 ---------------------------------------------------------------------------
    std::printf("\n== 計時（每個 N 重複多次取平均；Debug 組態只看趨勢）==\n");
    std::printf("%8s %14s %14s %10s\n", "N", "FFT (ms)", "naive DFT (ms)", "ratio");
    double ratioAt4096 = 0.0;
    for (int order = 8; order <= 14; ++order) {
        const int n = 1 << order;
        Fft fft(order);
        auto x = makeRandomComplex(n, 7u);
        std::vector<std::complex<float>> work = x;

        const int reps = std::max(1, 20000 / n);
        auto t0 = Clock::now();
        for (int r = 0; r < reps; ++r) {
            work = x;
            fft.forward(work.data());
        }
        const double fftMs = millisecondsSince(t0) / reps;

        double naiveMs = -1.0;
        if (n <= 4096) {
            std::vector<std::complex<float>> out(static_cast<size_t>(n));
            t0 = Clock::now();
            dsp::naiveDft(x.data(), out.data(), n);
            naiveMs = millisecondsSince(t0);
        }
        if (n == 4096)
            ratioAt4096 = naiveMs / std::max(fftMs, 1e-9);
        if (naiveMs >= 0.0)
            std::printf("%8d %14.4f %14.3f %10.0f\n", n, fftMs, naiveMs, naiveMs / std::max(fftMs, 1e-9));
        else
            std::printf("%8d %14.4f %14s %10s\n", n, fftMs, "(略過)", "-");
    }
    std::printf("理論：naive DFT 是 O(N^2)，FFT 是 O(N log2 N)；N = 4096 時 N / log2(N) = %.0f 倍。\n", 4096.0 / 12.0);
    // 計時會隨機器與負載浮動，只檢查「數量級」：N = 4096 時 FFT 至少快 20 倍（實測通常數百倍）
    DAWLAB_CHECK(ratioAt4096 > 20.0);

    // ---- 5. 以 block 為單位做 round-trip，寫出 WAV -----------------------------------------
    {
        constexpr int order = 11, n = 1 << order; // 2048
        Fft fft(order);
        AudioBuffer plucks = makePluckSequence(sr, 3.0, 0.4f, 2);
        AudioBuffer out(plucks.getNumChannels(), plucks.getNumSamples());
        std::vector<std::complex<float>> spec(static_cast<size_t>(fft.getNumRealBins()));
        std::vector<float> block(static_cast<size_t>(n));
        float maxErr = 0.0f;
        for (int ch = 0; ch < plucks.getNumChannels(); ++ch) {
            for (int start = 0; start + n <= plucks.getNumSamples(); start += n) {
                fft.forwardReal(plucks.getReadPointer(ch) + start, spec.data());
                fft.inverseReal(spec.data(), block.data());
                for (int i = 0; i < n; ++i) {
                    out.setSample(ch, start + i, block[static_cast<size_t>(i)]);
                    maxErr = std::max(maxErr, std::abs(block[static_cast<size_t>(i)] - plucks.getSample(ch, start + i)));
                }
            }
        }
        writeWav(outputPath("l11_fft_roundtrip.wav"), out, sr);
        std::printf("\nround-trip（2048 點 block）最大誤差 = %.3e → dawlab_output/l11_fft_roundtrip.wav\n", maxErr);
        DAWLAB_CHECK(maxErr < 1e-5f);
    }
    return dawlab::test::summary("l11_fft_basics");
}
