// 範例 3：Oversampler 的內部——halfband filter 的頻率響應、latency、passthrough 誤差、CPU 成本
//
// 示範什麼：
//   1. 印出 2x / 4x / 8x 各級 halfband FIR 的設計參數（tap 數、Kaiser β、group delay）。
//   2. 直接從係數算第 1 級 filter 的頻率響應（以 96 kHz 計）：passband 平坦到哪裡、
//      stopband 從哪裡開始、衰減多少 dB。這些數字決定了 oversampling「能擋掉多少 aliasing」。
//   3. 用 impulse 量測「升頻 → 什麼都不做 → 降頻」的延遲，驗證 getLatencySamples() 回報的數字。
//   4. 用正弦波量測 passthrough 的增益誤差（理想上 0 dB）、升頻後 image 被壓到多低。
//   5. 計時：10 秒 noise 在 1x / 2x / 4x / 8x 下升降頻的耗時（純 filter 成本，不含非線性）。
//
// 怎麼執行：l06_oversampler_response（不需要參數）
//
// 輸出檔案：無（這支只印數字）。
//
// 該看到：
//   - 第 1 級：passband 到約 20 kHz 內誤差 < 0.01 dB，28 kHz 以上衰減 ≥ 80 dB。
//   - 量到的 latency 與 getLatencySamples() 一致（2x = 31、4x = 36、8x = 38 samples @ 48 kHz）。
//   - 1 kHz 正弦 passthrough 增益誤差 < 0.01 dB；升頻後 95 kHz 的 image 低於 -80 dB。
//   若 latency 不一致，程式回傳 1（ctest 會判定失敗）。

#include <dawlab/Common.h>
#include <dawlab/dsp/Oversampler.h>

#include <chrono>
#include <complex>
#include <cstdio>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

// FIR 的頻率響應：H(f) = Σ_n h[n] e^{-j 2π f n / rate}
double firMagnitudeDb(const std::vector<double>& h, double freq, double rate) {
    std::complex<double> acc(0.0, 0.0);
    for (size_t n = 0; n < h.size(); ++n) {
        const double w = -dawlab::kTwoPi * freq * static_cast<double>(n) / rate;
        acc += h[n] * std::complex<double>(std::cos(w), std::sin(w));
    }
    return dawlab::gainToDb(std::abs(acc));
}

// 「升頻 → 不做事 → 降頻」一整段 buffer（模擬 audio callback，一次 512 個 sample）
void roundTrip(dawlab::dsp::Oversampler& os, dawlab::AudioBuffer& buffer) {
    dawlab::AudioBlock whole(buffer);
    for (int start = 0; start < buffer.getNumSamples(); start += kBlockSize) {
        const int len = std::min(kBlockSize, buffer.getNumSamples() - start);
        dawlab::AudioBlock block = whole.getSubBlock(start, len);
        os.upsample(block);
        os.downsample(block);
    }
}

} // namespace

int main() {
    using dawlab::dsp::HalfbandStage;
    using dawlab::dsp::Oversampler;
    bool ok = true;

    // ---- 1. 設計參數 ------------------------------------------------------------------------
    std::printf("Halfband 各級設計（Kaiser β = %.1f）：\n", Oversampler::kKaiserBeta);
    std::printf("%-6s %-12s %-10s %-12s %-16s %-14s\n", "級", "倍率", "奇數 tap", "FIR 長度", "group delay", "工作取樣率");
    const int oddTaps[] = {Oversampler::kOddTapsStage1, Oversampler::kOddTapsStage2, Oversampler::kOddTapsStage3};
    for (int stage = 1; stage <= 3; ++stage) {
        const int K = oddTaps[stage - 1] / 2;
        std::printf("%-6d x%-11d %-10d %-12d %-3d samples      %.0f kHz\n", stage, 1 << stage, 2 * K, 4 * K - 1,
                    2 * K - 1, kSampleRate * (1 << stage) / 1000.0);
    }

    // ---- 2. 第 1 級的頻率響應（在 96 kHz 下）---------------------------------------------------
    {
        const std::vector<double> h = HalfbandStage::designHalfband(Oversampler::kOddTapsStage1, Oversampler::kKaiserBeta);
        double sum = 0.0;
        for (double v : h)
            sum += v;
        std::printf("\n第 1 級 halfband：%zu taps，Σh = %.6f（DC gain %.4f dB）\n", h.size(), sum, dawlab::gainToDb(sum));
        std::printf("中心附近的係數 h[0..7]：");
        const size_t centre = h.size() / 2;
        for (size_t i = 0; i < 8; ++i)
            std::printf("%s%.5f", i ? ", " : "", h[centre + i]);
        std::printf("\n（偶數 index 幾乎都是 0 —— 這就是 halfband 省一半乘法的來源）\n\n");

        const double rate = kSampleRate * 2.0;
        std::printf("%-10s %12s\n", "freq (Hz)", "|H| (dB)");
        for (double f : {1000.0, 10000.0, 16000.0, 18000.0, 19000.0, 20000.0, 21000.0, 22000.0, 23000.0, 24000.0,
                         25000.0, 26000.0, 27000.0, 28000.0, 29000.0, 30000.0, 36000.0, 40000.0, 47000.0}) {
            std::printf("%-10.0f %12.3f\n", f, firMagnitudeDb(h, f, rate));
        }
        // 掃描 passband 邊緣（最後一個 |誤差| < 0.01 dB 的頻率）與 stopband 起點（第一個 < -80 dB 的頻率）
        double passEdge = 0.0, stopEdge = 0.0;
        for (double f = 0.0; f <= rate * 0.5; f += 100.0) {
            const double db = firMagnitudeDb(h, f, rate);
            if (std::abs(db) < 0.01)
                passEdge = f;
            if (stopEdge == 0.0 && f > rate * 0.25 && db < -80.0)
                stopEdge = f;
        }
        double worstStop = -200.0;
        for (double f = stopEdge; f <= rate * 0.5; f += 50.0)
            worstStop = std::max(worstStop, firMagnitudeDb(h, f, rate));
        std::printf("\npassband（|誤差| < 0.01 dB）到 %.1f kHz；stopband（< -80 dB）從 %.1f kHz 開始，"
                    "之後最差 %.1f dB\n",
                    passEdge / 1000.0, stopEdge / 1000.0, worstStop);
        std::printf("24 kHz（舊 Nyquist）處正好 -6.02 dB：%.3f dB\n", firMagnitudeDb(h, 24000.0, rate));
    }

    // ---- 3. latency：impulse 來回 --------------------------------------------------------------
    std::printf("\nLatency（impulse 升頻 → 降頻，以 48 kHz 的 sample 計）：\n");
    std::printf("%-8s %-12s %-12s %-10s\n", "倍率", "量測", "getLatency", "一致？");
    for (int log2 = 0; log2 <= Oversampler::kMaxFactorLog2; ++log2) {
        Oversampler os(log2);
        os.prepare(kSampleRate, kBlockSize, 1);
        const int position = 100;
        dawlab::AudioBuffer buffer = dawlab::makeImpulse(4096, 1, position, 1.0f);
        roundTrip(os, buffer);
        int argmax = 0;
        float best = 0.0f;
        const float* d = buffer.getReadPointer(0);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (std::abs(d[i]) > best) {
                best = std::abs(d[i]);
                argmax = i;
            }
        const int measured = argmax - position;
        const bool same = measured == os.getLatencySamples();
        ok = ok && same;
        std::printf("%-8d %-12d %-12d %-10s（peak %.4f）\n", os.getFactor(), measured, os.getLatencySamples(),
                    same ? "yes" : "NO", best);
    }

    // ---- 4. passthrough 誤差 + image 抑制 --------------------------------------------------------
    std::printf("\nPassthrough（正弦波 0.5，升頻 → 降頻 後的增益；理想 0 dB）：\n");
    std::printf("%-10s %10s %10s %10s\n", "freq (Hz)", "2x", "4x", "8x");
    for (double f : {1000.0, 10000.0, 18000.0, 20000.0, 22000.0, 23000.0}) {
        std::printf("%-10.0f", f);
        for (int log2 = 1; log2 <= 3; ++log2) {
            Oversampler os(log2);
            os.prepare(kSampleRate, kBlockSize, 1);
            dawlab::AudioBuffer buffer = dawlab::makeSine(f, kSampleRate, 1.0, 0.5f, 1);
            const dawlab::AudioBuffer input = buffer;
            roundTrip(os, buffer);
            const int skip = 4096; // 跳過開頭的 transient
            const int n = buffer.getNumSamples() - skip;
            const double a = dawlab::goertzelAmplitude(input.getReadPointer(0) + skip, n, f, kSampleRate);
            const double b = dawlab::goertzelAmplitude(buffer.getReadPointer(0) + skip, n, f, kSampleRate);
            const double gainDb = dawlab::gainToDb(b / a);
            std::printf(" %+10.4f", gainDb);
            if (f <= 18000.0)
                ok = ok && std::abs(gainDb) < 0.02;
        }
        std::printf("\n");
    }
    {
        // 1 kHz 升頻 2x 之後，在 96 kHz 下看 1 kHz 與 image（96 - 1 = 95 kHz）
        Oversampler os(1);
        os.prepare(kSampleRate, kBlockSize, 1);
        dawlab::AudioBuffer buffer = dawlab::makeSine(1000.0, kSampleRate, 1.0, 0.5f, 1);
        std::vector<float> up;
        dawlab::AudioBlock whole(buffer);
        for (int start = 0; start < buffer.getNumSamples(); start += kBlockSize) {
            const int len = std::min(kBlockSize, buffer.getNumSamples() - start);
            dawlab::AudioBlock u = os.upsample(whole.getSubBlock(start, len));
            up.insert(up.end(), u.getChannelPointer(0), u.getChannelPointer(0) + u.getNumSamples());
        }
        const double rate = kSampleRate * 2.0;
        const int skip = 8192;
        const int n = static_cast<int>(up.size()) - skip;
        // 插零會在 48 kHz 的整數倍附近長出 image：48 ± 1 → 47 kHz 與 49 kHz（49 kHz 在 96 kHz 下又等於 47 kHz）
        const double a1 = dawlab::goertzelAmplitude(up.data() + skip, n, 1000.0, rate);
        const double a47 = dawlab::goertzelAmplitude(up.data() + skip, n, 47000.0, rate);
        std::printf("\n1 kHz 升頻 2x（96 kHz）：1 kHz = %.4f（輸入 0.5）、image 47 kHz = %.1f dB\n", a1,
                    dawlab::gainToDb(a47 / a1));
        std::printf("（純插零的話 image 會跟原訊號一樣大 = 0 dB；halfband lowpass 把它壓掉了）\n");
        ok = ok && dawlab::gainToDb(a47 / a1) < -75.0;
    }

    // ---- 5. CPU 成本 -------------------------------------------------------------------------
    std::printf("\nCPU：10 秒 stereo noise 升頻 + 降頻（不含非線性）：\n");
    std::printf("%-8s %12s %12s\n", "倍率", "耗時 (ms)", "相對 2x");
    double base = 0.0;
    for (int log2 = 0; log2 <= Oversampler::kMaxFactorLog2; ++log2) {
        Oversampler os(log2);
        os.prepare(kSampleRate, kBlockSize, 2);
        dawlab::AudioBuffer buffer = dawlab::makeWhiteNoise(kSampleRate, 10.0, 0.5f, 2);
        const auto t0 = std::chrono::steady_clock::now();
        roundTrip(os, buffer);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (log2 == 1)
            base = ms;
        std::printf("%-8d %12.1f %12s\n", os.getFactor(), ms, log2 >= 1 ? std::to_string(ms / base).substr(0, 4).c_str() : "-");
        ok = ok && !dawlab::hasNanOrInf(buffer);
    }
#ifdef NDEBUG
    std::printf("（本次是 Release 建置；絕對時間會隨機器變動，看相對倍率）\n");
#else
    std::printf("（本次是 Debug 建置，絕對時間沒有意義，看相對倍率；Release 會快很多）\n");
#endif

    std::printf("\n%s\n", ok ? "latency 與 getLatencySamples() 一致、passthrough 誤差 < 0.02 dB、image < -75 dB。"
                             : "[FAIL] latency / passthrough / image 檢查有項目不符！");
    return ok ? 0 : 1;
}
