// 範例 2：algorithmic reverb（8 線 FDN）的 impulse response
//
// 示範什麼：
//   1. 把一個 impulse 餵進 dawlab::dsp::Reverb（mix = 1、damping 關閉），錄下 impulse response，
//      印出 ASCII 衰減曲線（每 100 ms 一列的 RMS envelope，dB），並用 measureRt60() 量 RT60 與設定值比較。
//   2. 印出 8 條 delay 線的長度（samples，都是質數）與各自的 decay gain g_i = 10^(-3·L_i/(RT60·fs))。
//   3. damping 的效果：打開 damping（4 kHz）後，寬頻 RT60 變短，而且 8 kHz 比 250 Hz 衰減得快很多。
//   4. freeze：凍結之後尾巴不再衰減（1 秒內 RMS 幾乎不變）。
//   5. echo density：FDN 的 impulse response 在前 100 ms 內有多少「非零 sample」，看 diffusion 的效果。
//
// 怎麼執行：不帶參數直接執行（ctest 也會跑）。
// 輸出檔案（build/dawlab_output/）：
//   l12_fdn_ir_2s.wav        RT60 = 2 s、無 damping 的 stereo impulse response
//   l12_fdn_ir_damped.wav    RT60 = 2 s、damping 4 kHz 的 impulse response（尾巴越來越暗）
//   l12_fdn_ir_frozen.wav    0.2 s 後 freeze 的 impulse response（尾巴永遠不消失）
// 該聽到什麼：一聲「啪」之後是平滑、沒有明顯 flutter echo 的尾巴；damped 版本像鋪了地毯的房間；
//   frozen 版本變成一團持續不散的 pad。

#include <dawlab/Common.h>
#include <dawlab/dsp/Reverb.h>
#include <dawlab/dsp/ReverbBuildingBlocks.h>

#include <cstdio>
#include <string>
#include <vector>

using namespace dawlab;
using namespace dawlab::dsp;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

// 逐 block 餵 impulse（模擬 audio callback），回傳 stereo impulse response
AudioBuffer impulseResponse(Reverb& reverb, double seconds, double freezeAtSeconds = -1.0) {
    const int n = secondsToSamples(seconds, kSampleRate);
    AudioBuffer out = makeImpulse(n, 2);
    reverb.reset();
    reverb.setFreeze(false);
    const int freezeAt = freezeAtSeconds >= 0.0 ? secondsToSamples(freezeAtSeconds, kSampleRate) : -1;
    for (int start = 0; start < n; start += kBlockSize) {
        if (freezeAt >= 0 && start >= freezeAt)
            reverb.setFreeze(true);
        const int len = std::min(kBlockSize, n - start);
        reverb.process(out.toBlock().getSubBlock(start, len));
    }
    reverb.setFreeze(false);
    return out;
}

// 頻帶能量：在 freq·(1 ± 12%) 之間取 25 個頻率點做 Goertzel，把能量加總。
// FDN 的尾巴是一堆緊密排列的 mode（共振），單一頻率點可能剛好落在 mode 上或 mode 之間，
// 量到的衰減會忽快忽慢；加總一個頻帶的能量就平滑多了（就像 octave band 分析）。
double bandEnergy(const float* x, int w, double freq) {
    double e = 0.0;
    for (int k = -12; k <= 12; ++k) {
        const double a = goertzelAmplitude(x, w, freq * (1.0 + 0.01 * k), kSampleRate);
        e += a * a;
    }
    return e;
}

// 某個頻帶在兩段時間窗的能量比 → 該頻帶的 RT60
double rt60AtFrequency(const AudioBuffer& ir, double freq, double t1, double t2, double windowSec) {
    const int w = secondsToSamples(windowSec, kSampleRate);
    const float* x = ir.getReadPointer(0);
    const double e1 = bandEnergy(x + secondsToSamples(t1, kSampleRate), w, freq);
    const double e2 = bandEnergy(x + secondsToSamples(t2, kSampleRate), w, freq);
    const double slope = 10.0 * std::log10(e2 / e1) / (t2 - t1); // 能量用 10·log10
    return slope < 0.0 ? -60.0 / slope : 0.0;
}

// ASCII 衰減曲線：每 stepSec 一列，印 RMS（dB，相對第一列）與長條
void printDecayCurve(const AudioBuffer& ir, double stepSec, double totalSec) {
    const int w = secondsToSamples(stepSec, kSampleRate);
    const int rows = static_cast<int>(totalSec / stepSec);
    double ref = -200.0;
    std::vector<double> levels;
    for (int r = 0; r < rows; ++r) {
        const float v = rms(ir.getReadPointer(0) + r * w, w);
        const double db = v > 0.0f ? 20.0 * std::log10(v) : -200.0;
        levels.push_back(db);
        ref = std::max(ref, db);
    }
    std::printf("   t(s)   dB   |-60 dB                                   0 dB|\n");
    for (int r = 0; r < rows; ++r) {
        const double rel = levels[static_cast<size_t>(r)] - ref;
        const int bars = std::clamp(static_cast<int>((rel + 60.0) / 60.0 * 50.0), 0, 50);
        std::printf("  %5.2f %6.1f  |%s%s|\n", r * stepSec, rel, std::string(static_cast<size_t>(bars), '#').c_str(),
                    std::string(static_cast<size_t>(50 - bars), ' ').c_str());
    }
}

} // namespace

int main() {
    std::printf("=== 第 12 章 範例 2：FDN reverb 的 impulse response ===\n\n");

    Reverb reverb;
    reverb.prepare(kSampleRate, kBlockSize, 2);
    reverb.setMix(1.0f);
    reverb.setWidth(1.0f);
    reverb.setSize(0.5f);
    reverb.setPreDelayMs(0.0f);
    reverb.setDampingHz(20000.0f); // >= 20 kHz = 不 damping
    reverb.setDecaySeconds(2.0f);

    // ---- 1. delay 線長度與 decay gain -------------------------------------------
    std::printf("--- 8 條 delay 線（size = 0.5）---\n");
    std::printf("%6s %10s %10s %12s\n", "line", "L(samples)", "L(ms)", "g_i(RT60=2s)");
    for (int i = 0; i < Reverb::kNumLines; ++i)
        std::printf("%6d %10.0f %10.2f %12.6f\n", i, reverb.getLineDelaySamples(i),
                    samplesToMs(reverb.getLineDelaySamples(i), kSampleRate), reverb.getLineGain(i));
    std::printf("（長度都是質數，互相沒有公因數；g_i = 10^(-3·L_i / (RT60·fs))）\n\n");

    // ---- 2. RT60 設定值 vs 量測值 -------------------------------------------------
    std::printf("--- RT60 驗證（無 damping；windowed-RMS 回歸，-5 .. -35 dB 區段）---\n");
    std::printf("%10s %12s %10s\n", "設定(s)", "量測(s)", "誤差(%)");
    AudioBuffer ir2s; // RT60 = 2 s 的 IR 留下來，表格印完再畫衰減曲線
    for (float rt60 : {0.5f, 1.0f, 2.0f, 4.0f}) {
        reverb.setDecaySeconds(rt60);
        AudioBuffer ir = impulseResponse(reverb, rt60 * 1.2 + 0.3);
        const DecayMeasurement m = measureRt60(ir.getReadPointer(0), ir.getNumSamples(), kSampleRate, 20.0);
        std::printf("%10.2f %12.3f %10.1f\n", rt60, m.rt60Seconds, (m.rt60Seconds / rt60 - 1.0) * 100.0);
        if (rt60 == 2.0f)
            ir2s = ir;
    }
    writeWav(outputPath("l12_fdn_ir_2s.wav"), ir2s, kSampleRate);
    std::printf("\n  RT60 = 2 s 的衰減曲線（左聲道，每 100 ms 的 RMS）：\n");
    printDecayCurve(ir2s, 0.1, 2.4);
    std::printf("  peak = %.3f, 有 NaN/Inf = %s\n", peakAbs(ir2s), hasNanOrInf(ir2s) ? "yes" : "no");
    std::printf("\n");

    // ---- 3. echo density ----------------------------------------------------------
    {
        reverb.setDecaySeconds(2.0f);
        AudioBuffer ir = impulseResponse(reverb, 0.5);
        std::printf("--- echo density：每 10 ms 內 |h[n]| > 1e-4 的 sample 數（diffusion 讓它快速逼近 480 = 全滿）---\n   ");
        for (int k = 0; k < 10; ++k) {
            const int w = secondsToSamples(0.01, kSampleRate);
            int count = 0;
            for (int i = 0; i < w; ++i)
                if (std::abs(ir.getSample(0, k * w + i)) > 1e-4f)
                    ++count;
            std::printf("%4d", count);
        }
        std::printf("\n\n");
    }

    // ---- 4. damping ---------------------------------------------------------------
    {
        reverb.setDecaySeconds(2.0f);
        reverb.setDampingHz(4000.0f);
        AudioBuffer damped = impulseResponse(reverb, 2.7);
        writeWav(outputPath("l12_fdn_ir_damped.wav"), damped, kSampleRate);
        const DecayMeasurement m = measureRt60(damped.getReadPointer(0), damped.getNumSamples(), kSampleRate, 20.0);
        std::printf("--- damping = 4 kHz（RT60 設定 2 s）---\n");
        std::printf("  寬頻 RT60 量測 = %.3f s（比設定值短：高頻先消失，RMS 曲線變陡）\n", m.rt60Seconds);
        reverb.setDampingHz(20000.0f);
        AudioBuffer plain = impulseResponse(reverb, 2.7);
        std::printf("%10s %14s %14s\n", "freq(Hz)", "無damping(s)", "damping4k(s)");
        for (double f : {250.0, 1000.0, 4000.0, 8000.0})
            std::printf("%10.0f %14.3f %14.3f\n", f, rt60AtFrequency(plain, f, 0.2, 1.0, 0.3),
                        rt60AtFrequency(damped, f, 0.2, 1.0, 0.3));
        std::printf("（真實房間也是這樣：空氣與牆面吸收高頻，所以高頻的 RT60 比低頻短）\n\n");
    }

    // ---- 5. freeze ----------------------------------------------------------------
    {
        reverb.setDecaySeconds(1.0f);
        AudioBuffer frozen = impulseResponse(reverb, 2.0, 0.2);
        writeWav(outputPath("l12_fdn_ir_frozen.wav"), frozen, kSampleRate);
        const int w = secondsToSamples(0.3, kSampleRate);
        const float a = rms(frozen.getReadPointer(0) + secondsToSamples(0.5, kSampleRate), w);
        const float b = rms(frozen.getReadPointer(0) + secondsToSamples(1.6, kSampleRate), w);
        std::printf("--- freeze（0.2 s 時凍結；RT60 原本 1 s）---\n");
        std::printf("  RMS @0.5s = %.5f, RMS @1.6s = %.5f, 差 = %+.2f dB（沒 freeze 的話 1.1 s 會掉 66 dB）\n\n", a, b,
                    gainToDb(b / a));
    }

    std::printf("輸出：dawlab_output/l12_fdn_ir_2s.wav, l12_fdn_ir_damped.wav, l12_fdn_ir_frozen.wav\n");
    return 0;
}
