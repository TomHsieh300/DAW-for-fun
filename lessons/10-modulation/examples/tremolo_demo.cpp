// 範例 5：Tremolo（振幅調變）與 autopan
//
// 示範什麼：
//   1. 撥弦琶音經過 tremolo：sine 5 Hz、square 4 Hz（平滑過的）、以及 autopan 0.5 Hz 的 WAV。
//   2. AM 的頻譜驗證：gain g(t) = (1 − depth/2) + (depth/2)·sin(2πrt) 乘上 1 kHz 正弦，
//      頻譜會是 1000 Hz（振幅 ×(1 − depth/2)）加上 1000 ± r 兩個 sideband（振幅各 ×depth/4）。
//      用 Goertzel 量 990 / 1000 / 1010 Hz 的振幅和理論對照。
//   3. 對照：ring modulation（直接乘 LFO、沒有 DC 偏移）會把載波（1000 Hz）吃掉，只剩 sideband
//      —— 這是練習 4 的預告。
//   4. autopan：左右聲道反相，gL + gR = 2 − depth 為常數；印出每 0.25 秒左右 RMS 的 ASCII 條。
//
// 怎麼執行：l10_tremolo（不需要參數）
//
// 輸出檔案（dawlab_output/）：
//   l10_tremolo_dry.wav       撥弦琶音
//   l10_tremolo_sine.wav      sine 5 Hz、depth 0.8
//   l10_tremolo_square.wav    square 4 Hz、depth 1、1 ms 平滑（斷音 / 「直升機」感）
//   l10_tremolo_autopan.wav   autopan sine 0.5 Hz、depth 1（聲像每 2 秒左右來回一次）
//
// 該聽到什麼：sine 版音量像波浪起伏；square 版是「噠噠噠」的硬切；autopan 版音量不變但左右跑。

#include <dawlab/Common.h>
#include <dawlab/dsp/Tremolo.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

using dawlab::dsp::LfoShape;
using dawlab::dsp::Tremolo;

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

Tremolo makeTremolo(int channels, float rate, float depth, LfoShape shape, bool autoPan) {
    Tremolo t;
    t.prepare(kSampleRate, kBlockSize, channels);
    t.setRateHz(rate);
    t.setDepth(depth);
    t.setShape(shape);
    t.setAutoPan(autoPan);
    t.setLfoPhase(0.25f); // sine 從 +1 開始：第一拍最大聲
    t.reset();
    return t;
}

void writeProcessed(const dawlab::AudioBuffer& input, Tremolo& t, const char* filename) {
    dawlab::AudioBuffer out = input;
    processInBlocks(t, out);
    dawlab::writeWav(dawlab::outputPath(filename), out, kSampleRate);
    std::printf("  %-26s peak %.3f\n", filename, dawlab::peakAbs(out));
}

} // namespace

int main() {
    std::printf("=== 第 10 章 範例 5：Tremolo / autopan ===\n\n");

    // ---- 1. 聽 ---------------------------------------------------------------
    {
        dawlab::AudioBuffer pluck = dawlab::makePluckSequence(kSampleRate, 4.0, 0.4f, 2);
        dawlab::writeWav(dawlab::outputPath("l10_tremolo_dry.wav"), pluck, kSampleRate);
        std::printf("輸出 WAV：\n  %-26s peak %.3f\n", "l10_tremolo_dry.wav", dawlab::peakAbs(pluck));
        Tremolo a = makeTremolo(2, 5.0f, 0.8f, LfoShape::Sine, false);
        writeProcessed(pluck, a, "l10_tremolo_sine.wav");
        Tremolo b = makeTremolo(2, 4.0f, 1.0f, LfoShape::Square, false);
        b.setSmoothingMs(1.0f);
        writeProcessed(pluck, b, "l10_tremolo_square.wav");
        Tremolo c = makeTremolo(2, 0.5f, 1.0f, LfoShape::Sine, true);
        writeProcessed(pluck, c, "l10_tremolo_autopan.wav");
        std::printf("\n");
    }

    // ---- 2. AM 頻譜 -------------------------------------------------------------
    {
        const double fc = 1000.0, r = 10.0;
        const float A = 0.5f, depth = 1.0f;
        dawlab::AudioBuffer tone = dawlab::makeSine(fc, kSampleRate, 3.0, A, 1);
        Tremolo t = makeTremolo(1, static_cast<float>(r), depth, LfoShape::Sine, false);
        processInBlocks(t, tone);
        const int skip = dawlab::secondsToSamples(0.5, kSampleRate);
        const float* x = tone.getReadPointer(0) + skip;
        const int n = tone.getNumSamples() - skip;
        std::printf("AM 頻譜：%.0f Hz 正弦（振幅 %.2f）× sine tremolo %.0f Hz、depth %.1f\n", fc, A, r, depth);
        std::printf("   freq(Hz)   量測振幅   理論振幅\n");
        std::printf("   %8.0f   %8.4f   %8.4f   （載波 ×(1 − depth/2)）\n", fc,
                    dawlab::goertzelAmplitude(x, n, fc, kSampleRate), A * (1.0 - depth / 2.0));
        for (double f : {fc - r, fc + r})
            std::printf("   %8.0f   %8.4f   %8.4f   （sideband ×depth/4）\n", f,
                        dawlab::goertzelAmplitude(x, n, f, kSampleRate), A * depth / 4.0);
        std::printf("   %8.0f   %8.4f   %8.4f   （LFO 本身不會出現在輸出裡）\n", r,
                    dawlab::goertzelAmplitude(x, n, r, kSampleRate), 0.0);
        std::printf("  -> tremolo = AM：頻譜是載波 + 兩個 sideband。LFO 太快（> 20 Hz）時 sideband 會分得開到聽成新的音。\n\n");

        // ---- 3. 對照：ring modulation ---------------------------------------
        dawlab::AudioBuffer ring = dawlab::makeSine(fc, kSampleRate, 3.0, A, 1);
        float* d = ring.getWritePointer(0);
        for (int i = 0; i < ring.getNumSamples(); ++i)
            d[i] *= static_cast<float>(std::sin(dawlab::kTwoPi * r * i / kSampleRate));
        const float* y = ring.getReadPointer(0) + skip;
        std::printf("對照 ring modulation（直接 × sin(2π·%.0f·t)，沒有 DC 偏移）：\n", r);
        std::printf("   %8.0f Hz 振幅 %.4f（載波消失）   %8.0f / %8.0f Hz 振幅 %.4f / %.4f（理論 A/2 = %.4f）\n", fc,
                    dawlab::goertzelAmplitude(y, n, fc, kSampleRate), fc - r, fc + r,
                    dawlab::goertzelAmplitude(y, n, fc - r, kSampleRate),
                    dawlab::goertzelAmplitude(y, n, fc + r, kSampleRate), A / 2.0);
        std::printf("  -> sin·sin = ½[cos(差) − cos(和)]：只剩和頻與差頻。練習 4 會自己做一個 ring modulator。\n\n");
    }

    // ---- 4. autopan：gL + gR 為常數、左右 RMS 的 ASCII 條 --------------------------
    {
        dawlab::AudioBuffer tone = dawlab::makeSine(440.0, kSampleRate, 4.0, 0.5f, 2);
        Tremolo t = makeTremolo(2, 0.5f, 1.0f, LfoShape::Sine, true);
        // 逐 sample 檢查 gL + gR = 2 − depth
        float maxSumError = 0.0f;
        dawlab::AudioBlock whole(tone);
        for (int start = 0; start < tone.getNumSamples(); start += kBlockSize) {
            const int n = std::min(kBlockSize, tone.getNumSamples() - start);
            t.process(whole.getSubBlock(start, n));
            maxSumError = std::max(maxSumError, std::abs(t.getCurrentGain(0) + t.getCurrentGain(1) - (2.0f - 1.0f)));
        }
        std::printf("autopan（sine 0.5 Hz、depth 1）：每個 block 結尾 |gL + gR − (2 − depth)| 最大 = %.2e\n", maxSumError);
        std::printf("   時間(s)   L RMS   R RMS\n");
        const int seg = dawlab::secondsToSamples(0.25, kSampleRate);
        for (int start = 0; start + seg <= tone.getNumSamples(); start += seg) {
            const float l = dawlab::rms(tone.getReadPointer(0) + start, seg);
            const float r = dawlab::rms(tone.getReadPointer(1) + start, seg);
            std::printf("   %5.2f   ", start / kSampleRate);
            for (int i = 0; i < 20; ++i)
                std::putchar(i < static_cast<int>(std::lround(l / 0.3536f * 20.0f)) ? '#' : '.');
            std::printf("  ");
            for (int i = 0; i < 20; ++i)
                std::putchar(i < static_cast<int>(std::lround(r / 0.3536f * 20.0f)) ? '#' : '.');
            std::printf("   %.3f  %.3f\n", l, r);
        }
        std::printf("  -> 左右此消彼長，一個週期 2 秒。gL + gR 固定是 linear pan law，\n"
                    "     若要 constant power 可以改用 sin/cos（第 01 章 Panner）。\n");
    }
    return 0;
}
