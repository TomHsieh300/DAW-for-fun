// 範例 3：convolution reverb —— uniform partitioned FFT convolution vs 直接卷積
//
// 示範什麼：
//   1. 用 ConvolutionReverb::makeSyntheticImpulseResponse() 合成一個 RT60 = 0.3 s 的 stereo IR，
//      對一段 pluck 訊號做（a）直接卷積 y[n] = Σ h[k]·x[n-k]、（b）partitioned FFT convolution，
//      比較兩者的誤差（應該只有 float 精度等級）與執行時間。
//   2. 驗證 latency = partition size：partitioned 的輸出比直接卷積晚 P 個 sample。
//   3. 印出 partition 數、FFT size、頻譜記憶體用量，與「每個輸出 sample 要幾次乘加」的理論比較。
//   4. 用 2 s 的 IR 處理整段 pluck sequence，輸出 WAV（這個長度直接卷積會太慢，只跑 partitioned）。
//
// 怎麼執行：不帶參數直接執行（ctest 也會跑）。
// 輸出檔案（build/dawlab_output/）：
//   l12_conv_ir.wav         合成的 2 s stereo impulse response（聽起來像一聲「噗」接著的雜訊尾巴）
//   l12_conv_dry.wav        原始 pluck sequence
//   l12_conv_wet.wav        convolution reverb 處理後（mix = 0.35）
// 該聽到什麼：pluck 之間被平滑的尾巴連起來，像在一個中型房間裡彈；尾巴沒有 metallic ringing，
//   因為合成 IR 是「雜訊」而不是「規律的 echo」。

#include <dawlab/Common.h>
#include <dawlab/dsp/ConvolutionReverb.h>

#include <chrono>
#include <cstdio>
#include <vector>

using namespace dawlab;
using namespace dawlab::dsp;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

// 直接卷積（教學用、O(N·M)）：y[n] = Σ_{k=0}^{M-1} h[k]·x[n-k]，只算前 numOut 個輸出
void directConvolve(const float* x, int n, const float* h, int m, float* y, int numOut) {
    for (int i = 0; i < numOut; ++i) {
        double acc = 0.0;
        const int kMax = std::min(m - 1, i);
        for (int k = 0; k <= kMax; ++k)
            if (i - k < n)
                acc += static_cast<double>(h[k]) * x[i - k];
        y[i] = static_cast<float>(acc);
    }
}

double elapsedMs(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

// 逐 block 跑 ConvolutionReverb（mix = 1 → 純 wet）
AudioBuffer runPartitioned(ConvolutionReverb& conv, const AudioBuffer& in) {
    AudioBuffer out = in;
    conv.reset();
    for (int start = 0; start < out.getNumSamples(); start += kBlockSize) {
        const int len = std::min(kBlockSize, out.getNumSamples() - start);
        conv.process(out.toBlock().getSubBlock(start, len));
    }
    return out;
}

} // namespace

int main() {
    std::printf("=== 第 12 章 範例 3：convolution reverb（partitioned FFT）vs 直接卷積 ===\n\n");

    // ---- 1. 短 IR：直接卷積 vs partitioned，比誤差與速度 ------------------------------
    {
        const double rt60 = 0.3;
        AudioBuffer ir = ConvolutionReverb::makeSyntheticImpulseResponse(kSampleRate, rt60, 2, 7);
        AudioBuffer dry = makePluckSequence(kSampleRate, 1.0, 0.4f, 2);
        const int n = dry.getNumSamples();
        const int m = ir.getNumSamples();

        ConvolutionReverb conv;
        conv.prepare(kSampleRate, kBlockSize, 2);
        conv.setImpulseResponse(ir);
        conv.setMix(1.0f);
        const int P = conv.getLatencySamples();

        std::printf("--- 短 IR（RT60 = %.1f s → %d samples，%d 個 partition）處理 %.1f s 的 pluck ---\n", rt60, m,
                    conv.getNumPartitions(), n / kSampleRate);
        std::printf("partition size P = %d, FFT size N = 2P = %d, latency = %d samples (%.2f ms)\n", P,
                    conv.getFftSize(), conv.getLatencySamples(), samplesToMs(conv.getLatencySamples(), kSampleRate));

        // 直接卷積：多算 P 個 sample，才能和（晚 P 個 sample 的）partitioned 輸出對齊
        AudioBuffer direct(2, n + P);
        auto t0 = std::chrono::steady_clock::now();
        for (int ch = 0; ch < 2; ++ch)
            directConvolve(dry.getReadPointer(ch), n, ir.getReadPointer(ch), m, direct.getWritePointer(ch), n + P);
        const double directMs = elapsedMs(t0);

        // partitioned：輸入補 P 個零，讓最後 P 個 sample 的輸出也吐出來
        AudioBuffer padded(2, n + P);
        for (int ch = 0; ch < 2; ++ch)
            padded.copyFrom(ch, 0, dry, ch, 0, n);
        t0 = std::chrono::steady_clock::now();
        AudioBuffer part = runPartitioned(conv, padded);
        const double partMs = elapsedMs(t0);

        // 誤差：part[i + P] 應該等於 direct[i]
        double maxErr = 0.0, sigPeak = 0.0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i) {
                maxErr = std::max(maxErr, static_cast<double>(std::abs(part.getSample(ch, i + P) - direct.getSample(ch, i))));
                sigPeak = std::max(sigPeak, static_cast<double>(std::abs(direct.getSample(ch, i))));
            }
        // 前 P 個 sample 必須是 0（latency）
        const float leading = peakAbs(part.getReadPointer(0), P);

        std::printf("直接卷積      : %8.1f ms（%.2e 次乘加）\n", directMs, 2.0 * n * static_cast<double>(m));
        std::printf("partitioned   : %8.1f ms（約 %.2e 次 complex 乘加 + %d 次 FFT）\n", partMs,
                    2.0 * (n + P) / P * conv.getNumPartitions() * (conv.getFftSize() / 2 + 1),
                    2 * 2 * ((n + P) / P));
        std::printf("加速比        : %.1f x\n", directMs / std::max(partMs, 1e-3));
        std::printf("最大誤差      : %.3e（訊號 peak %.3f → %.1f dB 以下）\n", maxErr, sigPeak,
                    gainToDb(maxErr / sigPeak));
        std::printf("前 %d 個輸出的 peak = %.3e（latency 期間輸出必須是 0）\n\n", P, leading);
        std::printf("理論成本（每個輸出 sample）：直接 = M = %d 次乘加；partitioned ≈ K·(N/2+1)/P = %.0f 次 complex 乘加 "
                    "≈ %.0f 次實數乘加，再加上每 P 個 sample 兩次 N 點 FFT\n\n",
                    m, static_cast<double>(conv.getNumPartitions()) * (conv.getFftSize() / 2 + 1) / P,
                    4.0 * conv.getNumPartitions() * (conv.getFftSize() / 2 + 1) / P);
    }

    // ---- 2. 長 IR：只跑 partitioned，輸出 WAV ------------------------------------------
    {
        const double rt60 = 2.0;
        AudioBuffer ir = ConvolutionReverb::makeSyntheticImpulseResponse(kSampleRate, rt60, 2, 3);
        AudioBuffer dry = makePluckSequence(kSampleRate, 4.0, 0.4f, 2);

        ConvolutionReverb conv;
        conv.prepare(kSampleRate, kBlockSize, 2);
        conv.setImpulseResponse(ir);
        conv.setMix(0.35f);

        std::printf("--- 長 IR（RT60 = %.1f s → %d samples，%d 個 partition）---\n", rt60, ir.getNumSamples(),
                    conv.getNumPartitions());
        std::printf("頻譜記憶體：IR 頻譜 + FDL = %.2f MiB（每個 partition %d 個 complex<float> bin）\n",
                    static_cast<double>(conv.getSpectraMemoryBytes()) / (1024.0 * 1024.0), conv.getFftSize() / 2 + 1);
        std::printf("IR 能量 Σh² = %.4f（合成 IR 正規化成 1）, IR peak = %.3f\n",
                    static_cast<double>(rms(ir, 0)) * rms(ir, 0) * ir.getNumSamples(), peakAbs(ir));

        auto t0 = std::chrono::steady_clock::now();
        AudioBuffer wet = runPartitioned(conv, dry);
        const double ms = elapsedMs(t0);
        std::printf("處理 %.1f s 音訊花 %.1f ms（即時倍率 %.0f x）\n", dry.getNumSamples() / kSampleRate, ms,
                    dry.getNumSamples() / kSampleRate * 1000.0 / std::max(ms, 1e-3));
        std::printf("dry RMS = %.4f, wet(mix 0.35) RMS = %.4f, peak = %.3f, NaN/Inf = %s\n", rms(dry, 0), rms(wet, 0),
                    peakAbs(wet), hasNanOrInf(wet) ? "yes" : "no");

        writeWav(outputPath("l12_conv_ir.wav"), ir, kSampleRate);
        writeWav(outputPath("l12_conv_dry.wav"), dry, kSampleRate);
        writeWav(outputPath("l12_conv_wet.wav"), wet, kSampleRate);
        std::printf("\n輸出：dawlab_output/l12_conv_ir.wav, l12_conv_dry.wav, l12_conv_wet.wav\n");
    }
    return 0;
}
