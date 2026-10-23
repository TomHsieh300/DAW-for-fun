// 範例 1：feedback comb filter 的 RT60 驗證
//
// 示範什麼：
//   1. 用公式 g = 10^(-3·D / (RT60·fs)) 算 comb 的回授量，餵一個 impulse，
//      量「衰減到 -60 dB」實際花多久，與設定的 RT60 比較。
//   2. comb 為什麼叫 comb：頻率響應在 fs/D 的整數倍有 peak（1/(1-g)）、半整數倍有 notch（1/(1+g)）。
//   3. Schroeder allpass 的振幅響應是平的（0 dB）。
//   4. damping（迴路裡的 lowpass）讓高頻的 RT60 比低頻短。
//
// 怎麼執行：不帶參數直接執行（ctest 也會跑）。
// 輸出檔案（build/dawlab_output/）：
//   l12_comb_ir.wav         RT60 = 2 s 的 comb impulse response（每 32 ms 一個越來越小的 click）
//   l12_comb_damped_ir.wav  同上但加 damping 0.5（click 越來越悶）
// 該聽到什麼：一連串等間隔、指數衰減的 click（像在走廊拍手的 flutter echo），damped 版本高頻先消失。

#include <dawlab/Common.h>
#include <dawlab/dsp/ReverbBuildingBlocks.h>

#include <cstdio>
#include <vector>

using namespace dawlab;
using namespace dawlab::dsp;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr int kDelay = 1557; // Freeverb 的其中一條 comb 長度（32.4 ms @ 48 kHz）

// 逐 block 餵 impulse，回傳整段 impulse response（mono）
AudioBuffer impulseResponse(CombFilter& comb, int numSamples) {
    AudioBuffer out = makeImpulse(numSamples, 1);
    comb.reset();
    for (int start = 0; start < numSamples; start += kBlockSize) {
        const int len = std::min(kBlockSize, numSamples - start);
        float* d = out.getWritePointer(0) + start;
        for (int i = 0; i < len; ++i)
            d[i] = comb.processSample(d[i]);
    }
    return out;
}

// 某個頻率成分在兩段時間窗的振幅比 → 換算成該頻率的 RT60
double rt60AtFrequency(const AudioBuffer& ir, double freq, double t1, double t2, double windowSec) {
    const int w = secondsToSamples(windowSec, kSampleRate);
    const double a1 = goertzelAmplitude(ir.getReadPointer(0) + secondsToSamples(t1, kSampleRate), w, freq, kSampleRate);
    const double a2 = goertzelAmplitude(ir.getReadPointer(0) + secondsToSamples(t2, kSampleRate), w, freq, kSampleRate);
    const double slope = 20.0 * std::log10(a2 / a1) / (t2 - t1); // dB / s
    return slope < 0.0 ? -60.0 / slope : 0.0;
}

} // namespace

int main() {
    std::printf("=== 第 12 章 範例 1：comb filter 的 RT60 驗證 ===\n");
    std::printf("fs = %.0f Hz, D = %d samples (%.2f ms), 基頻 fs/D = %.2f Hz\n\n", kSampleRate, kDelay,
                samplesToMs(kDelay, kSampleRate), kSampleRate / kDelay);

    CombFilter comb;
    comb.prepare(kDelay);
    comb.setDelaySamples(kDelay);

    // ---- 1. RT60 設定值 vs 量測值 -------------------------------------------------
    std::printf("--- RT60 驗證（impulse response 的第 k 個 echo = g^(k-1)）---\n");
    std::printf("%8s %10s %12s %14s %14s\n", "RT60(s)", "g", "預期圈數k-1", "衰減-60dB(s)", "回歸估計(s)");
    for (double rt60 : {0.5, 1.0, 2.0, 4.0}) {
        const float g = CombFilter::feedbackForRt60(kDelay, rt60, kSampleRate);
        comb.setFeedback(g);
        comb.setDamping(0.0f);
        const int n = secondsToSamples(rt60 * 1.3 + 0.2, kSampleRate);
        AudioBuffer ir = impulseResponse(comb, n);

        // 第一個 echo 在 n = D（振幅 1），第 k 個 echo 在 n = kD（振幅 g^(k-1)）。
        // 找第一個 |echo| <= 10^(-60/20) 的 k。衰減時間從第一個 echo（0 dB 的參考點）起算：
        // (k-1)·D / fs，也就是繞了 k-1 圈。（第 k 個 echo 的絕對時間是 k·D/fs，多了第一個 echo 本身的 D/fs 傳播延遲）
        int k = 1;
        while (k * kDelay < n && std::abs(ir.getSample(0, k * kDelay)) > 1e-3f)
            ++k;
        const double measured = (k - 1) * static_cast<double>(kDelay) / kSampleRate;
        // 理論：g^(k-1) <= 1e-3 → 圈數 k-1 = ceil(3 / -log10(g)) = ceil(RT60·fs/D)
        const int expectedRounds = static_cast<int>(std::ceil(rt60 * kSampleRate / kDelay));
        // 另一種量法：windowed-RMS envelope 線性回歸（window 要比 D 長才不會看到 0）
        const DecayMeasurement m = measureRt60(ir.getReadPointer(0), n, kSampleRate, 40.0);
        std::printf("%8.2f %10.5f %12d %14.3f %14.3f\n", rt60, g, expectedRounds, measured, m.rt60Seconds);

        if (rt60 == 2.0)
            writeWav(outputPath("l12_comb_ir.wav"), ir, kSampleRate);
    }
    std::printf("（衰減時間從第一個 echo 起算 = 圈數 × D/fs，被量化到 D/fs = %.1f ms 的整數倍，所以略大於設定值）\n\n",
                samplesToMs(kDelay, kSampleRate));

    // ---- 2. 頻率響應：peak 與 notch ---------------------------------------------
    {
        const float g = CombFilter::feedbackForRt60(kDelay, 2.0, kSampleRate);
        comb.setFeedback(g);
        comb.setDamping(0.0f);
        const double f0 = kSampleRate / kDelay;
        std::printf("--- 頻率響應（g = %.4f）---\n", g);
        std::printf("理論 peak = 1/(1-g) = %+.2f dB，notch = 1/(1+g) = %+.2f dB\n", gainToDb(1.0 / (1.0 - g)),
                    gainToDb(1.0 / (1.0 + g)));
        for (double m : {10.0, 10.5, 40.0, 40.5}) {
            comb.reset();
            const double gainDb =
                measureGainDb([&](float x) { return comb.processSample(x); }, m * f0, kSampleRate, 1.0, 3.0);
            std::printf("  f = %4.1f × fs/D = %8.2f Hz : %+7.2f dB  (%s)\n", m, m * f0, gainDb,
                        std::fmod(m, 1.0) == 0.0 ? "peak" : "notch");
        }
        std::printf("\n");
    }

    // ---- 3. allpass：振幅全平 ----------------------------------------------------
    {
        AllpassDiffuser ap;
        ap.prepare(556);
        ap.setDelaySamples(556);
        ap.setGain(0.5f);
        std::printf("--- Schroeder allpass（D = 556, g = 0.5）的振幅響應 ---\n");
        for (double f : {100.0, 1000.0, 5000.0, 12000.0}) {
            ap.reset();
            const double gainDb = measureGainDb([&](float x) { return ap.processSample(x); }, f, kSampleRate, 0.5, 0.5);
            std::printf("  %8.0f Hz : %+6.2f dB\n", f, gainDb);
        }
        std::printf("\n");
    }

    // ---- 4. damping：高頻衰減較快 ------------------------------------------------
    {
        const float g = CombFilter::feedbackForRt60(kDelay, 2.0, kSampleRate);
        const int n = secondsToSamples(2.6, kSampleRate);
        const double f0 = kSampleRate / kDelay;
        std::printf("--- damping 對各頻率 RT60 的影響（量 0.1 s 與 0.9 s 兩個 0.2 s 視窗）---\n");
        std::printf("%10s %12s %12s\n", "freq(Hz)", "damp=0", "damp=0.5");
        comb.setFeedback(g);
        comb.setDamping(0.0f);
        AudioBuffer dry = impulseResponse(comb, n);
        comb.setDamping(0.5f);
        AudioBuffer damped = impulseResponse(comb, n);
        writeWav(outputPath("l12_comb_damped_ir.wav"), damped, kSampleRate);
        for (double target : {250.0, 1000.0, 4000.0, 8000.0}) {
            const double f = std::round(target / f0) * f0; // 取最接近的 comb 諧波，避免落在 notch
            std::printf("%10.1f %12.3f %12.3f\n", f, rt60AtFrequency(dry, f, 0.1, 0.9, 0.2),
                        rt60AtFrequency(damped, f, 0.1, 0.9, 0.2));
        }
        std::printf("（沒有 damping 時每個頻率的 RT60 都一樣；有 damping 時高頻明顯變短）\n");
    }

    return 0;
}
