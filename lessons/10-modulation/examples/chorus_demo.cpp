// 範例 2：Chorus —— 被 LFO 拉長縮短的短延遲 + 原音
//
// 示範什麼：
//   1. 把撥弦琶音丟進 Chorus（2 voice / 3 voice），輸出 WAV 試聽。
//   2. Doppler 效應的數字驗證：延遲 d(t) = d0 + D·sin(2πrt) 的讀取速度不等於寫入速度，
//      所以音高會漂移，比例 = 1 − d'(t) = 1 − 2πrD·cos(2πrt)。
//      用 1 kHz 正弦（1 voice、mix = 1）量瞬時頻率的最大 / 最小值，和 1000·(1 ± 2πrD) 對照。
//   3. stereo spread：左右聲道的 LFO 相差最多 90°，左右走音不同步 → 左右相關係數下降（聲像變寬）。
//      同時比較白雜訊與撥弦琶音：寬頻素材只要左右延遲差一點點，濕訊號就幾乎完全去相關。
//   4. 插值品質：同一個 chorus 用 linear 和 cubic 插值讀 12 kHz 正弦，比較高頻損失。
//
// 怎麼執行：l10_chorus（不需要參數；可選 argv[1] = 輸入 WAV 路徑）
//
// 輸出檔案（dawlab_output/）：
//   l10_chorus_dry.wav      原始撥弦琶音
//   l10_chorus_wet.wav      2 voice、rate 0.8 Hz、depth 3 ms、centre 15 ms、mix 0.5、spread 1
//   l10_chorus_3voice.wav   3 voice、depth 4 ms、rate 0.5 Hz（更厚、更「合奏」）
//   l10_chorus_mono.wav     spread 0（左右完全相同，拿來和 wet 比較寬度）
//
// 該聽到什麼：wet 比 dry 厚、寬，像兩把吉他一起彈但沒有完全對準；3voice 更像一群人；
//            mono 版厚度一樣但聲像縮在中間。

#include <dawlab/Common.h>
#include <dawlab/dsp/Chorus.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using dawlab::dsp::Chorus;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

template <typename Effect>
void processInBlocks(Effect& fx, dawlab::AudioBuffer& buffer) {
    dawlab::AudioBlock whole(buffer);
    for (int start = 0; start < buffer.getNumSamples(); start += kBlockSize) {
        const int n = std::min(kBlockSize, buffer.getNumSamples() - start);
        fx.process(whole.getSubBlock(start, n));
    }
}

// 左右聲道的相關係數（Pearson）：1 = 完全相同，0 = 不相關，-1 = 反相
double stereoCorrelation(const dawlab::AudioBuffer& b, int skipSamples) {
    const float* l = b.getReadPointer(0);
    const float* r = b.getReadPointer(1);
    double sll = 0.0, srr = 0.0, slr = 0.0;
    for (int i = skipSamples; i < b.getNumSamples(); ++i) {
        sll += double(l[i]) * l[i];
        srr += double(r[i]) * r[i];
        slr += double(l[i]) * r[i];
    }
    return (sll > 0.0 && srr > 0.0) ? slr / std::sqrt(sll * srr) : 0.0;
}

// 用「相鄰兩個正向零交叉的間距」估計瞬時頻率，回傳 skip 之後的最小與最大值
void instantaneousFrequencyRange(const float* x, int n, int skip, double& fMin, double& fMax) {
    fMin = 1.0e9;
    fMax = 0.0;
    double lastCross = -1.0;
    for (int i = std::max(1, skip); i < n; ++i) {
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) {
            // 線性插值找出精確的交叉位置
            const double cross = (i - 1) + double(-x[i - 1]) / (double(x[i]) - x[i - 1]);
            if (lastCross >= 0.0) {
                const double f = kSampleRate / (cross - lastCross);
                fMin = std::min(fMin, f);
                fMax = std::max(fMax, f);
            }
            lastCross = cross;
        }
    }
}

Chorus makeChorus(int channels, float rate, float depth, float centre, int voices, float mix, float spread) {
    Chorus c;
    c.prepare(kSampleRate, kBlockSize, channels);
    c.setRateHz(rate);
    c.setDepthMs(depth);
    c.setCentreDelayMs(centre);
    c.setVoices(voices);
    c.setMix(mix);
    c.setStereoSpread(spread);
    c.reset(); // 讓平滑器直接貼到目標值
    return c;
}

void renderAndWrite(const dawlab::AudioBuffer& input, Chorus& chorus, const char* filename) {
    dawlab::AudioBuffer out = input;
    processInBlocks(chorus, out);
    dawlab::writeWav(dawlab::outputPath(filename), out, kSampleRate);
    std::printf("  %-24s peak %.3f   L/R 相關係數 %.3f\n", filename, dawlab::peakAbs(out),
                out.getNumChannels() > 1 ? stereoCorrelation(out, 0) : 1.0);
}

} // namespace

int main(int argc, char** argv) {
    std::printf("=== 第 10 章 範例 2：Chorus ===\n\n");

    // ---- 1. 撥弦琶音 → WAV --------------------------------------------------
    dawlab::AudioBuffer input;
    if (argc > 1) {
        dawlab::WavData wav;
        std::string err;
        if (!dawlab::readWav(argv[1], wav, &err)) {
            std::printf("%s\n", err.c_str());
            return 1;
        }
        input = wav.buffer;
        if (input.getNumChannels() == 1) { // 補成立體聲
            dawlab::AudioBuffer st(2, input.getNumSamples());
            st.copyFrom(0, 0, input, 0, 0, input.getNumSamples());
            st.copyFrom(1, 0, input, 0, 0, input.getNumSamples());
            input = st;
        }
    } else {
        input = dawlab::makePluckSequence(kSampleRate, 4.0, 0.4f, 2);
    }
    dawlab::writeWav(dawlab::outputPath("l10_chorus_dry.wav"), input, kSampleRate);
    std::printf("輸出 WAV：\n");
    std::printf("  %-24s peak %.3f   L/R 相關係數 %.3f\n", "l10_chorus_dry.wav", dawlab::peakAbs(input),
                stereoCorrelation(input, 0));
    {
        Chorus c = makeChorus(2, 0.8f, 3.0f, 15.0f, 2, 0.5f, 1.0f);
        renderAndWrite(input, c, "l10_chorus_wet.wav");
    }
    {
        Chorus c = makeChorus(2, 0.5f, 4.0f, 18.0f, 3, 0.5f, 1.0f);
        renderAndWrite(input, c, "l10_chorus_3voice.wav");
    }
    {
        Chorus c = makeChorus(2, 0.8f, 3.0f, 15.0f, 2, 0.5f, 0.0f);
        renderAndWrite(input, c, "l10_chorus_mono.wav");
    }
    std::printf("  -> spread 0 時左右完全相同（相關係數 1.000）；spread 1 時相關係數明顯下降 = 聲像變寬。\n\n");

    // ---- 2. Doppler：瞬時頻率的漂移量 ---------------------------------------
    {
        const float rate = 0.8f, depthMs = 3.0f;
        const double dev = dawlab::kTwoPi * rate * depthMs * 0.001; // 2πrD（D 以秒計）
        dawlab::AudioBuffer tone = dawlab::makeSine(1000.0, kSampleRate, 4.0, 0.5f, 1);
        Chorus c = makeChorus(1, rate, depthMs, 15.0f, 1, 1.0f, 0.0f); // 1 voice、全濕 = 純粹的調變延遲
        processInBlocks(c, tone);
        double fMin, fMax;
        instantaneousFrequencyRange(tone.getReadPointer(0), tone.getNumSamples(),
                                    dawlab::secondsToSamples(1.0, kSampleRate), fMin, fMax);
        std::printf("Doppler 音高漂移（1 kHz 正弦，1 voice，mix 1，rate %.1f Hz，depth %.0f ms）：\n", rate, depthMs);
        std::printf("  理論：比例 1 ± 2πrD = 1 ± %.4f → %.1f .. %.1f Hz（%+.1f / %+.1f cents）\n", dev,
                    1000.0 * (1.0 - dev), 1000.0 * (1.0 + dev), 1200.0 * std::log2(1.0 + dev),
                    1200.0 * std::log2(1.0 - dev));
        std::printf("  量測：瞬時頻率 %.1f .. %.1f Hz\n", fMin, fMax);
        std::printf("  -> depth × rate 決定走音多少：3 ms × 0.8 Hz 大約 ±26 cents（向下略多於向上），再大就會像「醉了」。\n\n");
    }

    // ---- 3. stereo spread 對相關係數的影響 ------------------------------------
    {
        dawlab::AudioBuffer mono = dawlab::makeWhiteNoise(kSampleRate, 3.0, 0.25f, 1, 7u);
        dawlab::AudioBuffer noise(2, mono.getNumSamples());
        noise.copyFrom(0, 0, mono, 0, 0, mono.getNumSamples());
        noise.copyFrom(1, 0, mono, 0, 0, mono.getNumSamples());
        const dawlab::AudioBuffer pluck = dawlab::makePluckSequence(kSampleRate, 4.0, 0.4f, 2);
        const int skip = dawlab::secondsToSamples(0.5, kSampleRate);
        std::printf("stereo spread 與左右相關係數（2 voice，mix 0.5，depth 3 ms）：\n");
        std::printf("   spread   左右 LFO 相差   白雜訊   撥弦琶音\n");
        for (float spread : {0.0f, 0.1f, 0.25f, 0.5f, 1.0f}) {
            dawlab::AudioBuffer a = noise, b = pluck;
            Chorus c1 = makeChorus(2, 0.8f, 3.0f, 15.0f, 2, 0.5f, spread);
            processInBlocks(c1, a);
            Chorus c2 = makeChorus(2, 0.8f, 3.0f, 15.0f, 2, 0.5f, spread);
            processInBlocks(c2, b);
            std::printf("   %5.2f      %5.1f°        %.3f    %.3f\n", spread, 90.0f * spread, stereoCorrelation(a, skip),
                        stereoCorrelation(b, skip));
        }
        std::printf("  -> 白雜訊：左右延遲只要差一點點，濕訊號就完全不相關，所以 spread 一離開 0 就掉到底\n"
                    "     （乾訊號佔一半、左右相同，所以停在約 0.7 而不是 0）。\n"
                    "     撥弦琶音諧波豐富，行為也很接近：spread 只要大於 0.1，相關係數就落在 0.72–0.77。\n"
                    "     所以 spread 對「寬不寬」幾乎是開關；它真正改變的是左右走音錯開的動態（聽起來在轉）。\n\n");
    }

    // ---- 4. linear vs cubic 插值的高頻損失 -----------------------------------
    {
        std::printf("插值品質（12 kHz 正弦，1 voice，mix 1，depth 3 ms，rate 0.8 Hz）：\n");
        const dawlab::AudioBuffer tone = dawlab::makeSine(12000.0, kSampleRate, 3.0, 0.25f, 1);
        const int skip = dawlab::secondsToSamples(0.5, kSampleRate);
        const float inRms = dawlab::rms(tone.getReadPointer(0) + skip, tone.getNumSamples() - skip);
        for (bool cubic : {false, true}) {
            dawlab::AudioBuffer out = tone;
            Chorus c = makeChorus(1, 0.8f, 3.0f, 15.0f, 1, 1.0f, 0.0f);
            c.setUseCubicInterpolation(cubic);
            processInBlocks(c, out);
            const float outRms = dawlab::rms(out.getReadPointer(0) + skip, out.getNumSamples() - skip);
            std::printf("  %-6s 插值：輸出 RMS 相對輸入 %+.2f dB\n", cubic ? "cubic" : "linear",
                        dawlab::gainToDb(outRms / inRms));
        }
        std::printf("  -> linear 插值是一個會變的 FIR：|H|² = (1−Δ)² + Δ² + 2Δ(1−Δ)cos(2πf/fs)。\n"
                    "     12 kHz = fs/4 時 cos 項為 0，Δ 在 0..1 均勻掃過 → 平均功率 = ∫(1−Δ)²+Δ² dΔ = 2/3\n"
                    "     → 理論 %.2f dB；cubic 損失小得多。\n",
                    10.0 * std::log10(2.0 / 3.0));
    }
    return 0;
}
