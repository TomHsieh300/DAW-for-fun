// 範例 2：pan law 表格、pan 掃動、mono pan vs stereo balance
//
// 示範什麼：
//   (A) 印出三種 pan law（Linear / ConstantPower / -4.5 dB）在各 pan 位置的左右 gain、
//       功率和 gL²+gR²（代表「兩個喇叭加起來的能量」）與振幅和 gL+gR（代表「左右相加成 mono」）。
//   (B) 把 mono 的撥弦琶音從全左掃到全右（4 秒），每種 law 各輸出一個立體聲 WAV，
//       並量測掃到中央時左右聲道的 RMS，驗證中央的 -6 / -3 / -4.5 dB。
//   (C) 對「真正的立體聲」訊號（左右不相關的雜訊）套用 Panner，證明它只是 balance：
//       pan 到右邊時左聲道變小，但左聲道的內容並沒有搬到右邊。
//   (D) 端點檢查：pan = ±1 時另一側的 gain 精確為 0；setPan(NaN) 會被忽略（self-check）。
//
// 怎麼執行：
//   cd <build 目錄> && bin/Debug/l01_pan_law.exe
//
// 輸出檔案（在 <build 目錄>/dawlab_output/）：
//   l01_pan_sweep_linear.wav / l01_pan_sweep_constpower.wav / l01_pan_sweep_minus4_5.wav
//
// 該聽到什麼：聲像從左移到右。用耳機聽 linear 版本，經過中央時會覺得「音量掉下去」；
// constant power 版本音量比較穩定（在喇叭上聽），-4.5 dB 介於兩者之間。

#include <dawlab/Common.h>
#include <dawlab/dsp/Panner.h>

#include <cmath>
#include <cstdio>

using namespace dawlab;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

const dsp::PanLaw kLaws[] = {dsp::PanLaw::Linear, dsp::PanLaw::ConstantPower, dsp::PanLaw::Minus4_5dB};
const char* kSweepFiles[] = {"l01_pan_sweep_linear.wav", "l01_pan_sweep_constpower.wav",
                             "l01_pan_sweep_minus4_5.wav"};

void printTable(dsp::PanLaw law) {
    std::printf("--- %s ---\n", dsp::panLawName(law));
    std::printf("  %6s %9s %9s %12s %12s\n", "pan", "gL (dB)", "gR (dB)", "gL²+gR² dB", "gL+gR dB");
    for (int i = -4; i <= 4; ++i) {
        const float pan = 0.25f * static_cast<float>(i);
        float l = 0.0f, r = 0.0f;
        dsp::Panner::computeGains(pan, law, l, r);
        const float powerSum = l * l + r * r;
        std::printf("  %+6.2f %9.2f %9.2f %12.2f %12.2f\n", pan, gainToDb(l), gainToDb(r),
                    10.0f * std::log10(powerSum), gainToDb(l + r));
    }
}

} // namespace

int main() {
    std::printf("=== 第 01 章 範例 2：pan law ===\n\n");

    // ---- (A) 表格 ------------------------------------------------------------
    std::printf("(A) 三種 pan law 的左右 gain（θ = (pan+1)/2）\n");
    for (dsp::PanLaw law : kLaws)
        printTable(law);
    std::printf("\n");

    // ---- (B) pan 掃動 ----------------------------------------------------------
    std::printf("(B) mono 撥弦琶音從左掃到右（4 s），量測掃到中央時左右聲道的 RMS：\n");
    const AudioBuffer mono = makePluckSequence(kSampleRate, 4.0, 0.4f, 1);
    const int total = mono.getNumSamples();
    for (int lawIndex = 0; lawIndex < 3; ++lawIndex) {
        dsp::Panner panner;
        panner.setPanLaw(kLaws[lawIndex]);
        panner.setRampSeconds(0.0); // 掃動本身已經是連續的，這裡用 0 讓量測位置準確
        panner.prepare(kSampleRate, kBlockSize, 2);
        panner.setPan(-1.0f);

        AudioBuffer stereo(2, total);
        AudioBlock out(stereo);
        for (int start = 0; start < total; start += kBlockSize) {
            const int len = std::min(kBlockSize, total - start);
            const float pan = -1.0f + 2.0f * static_cast<float>(start) / static_cast<float>(total);
            panner.setPan(pan);
            panner.processMonoToStereo(mono.getReadPointer(0) + start, out.getSubBlock(start, len));
        }
        writeWav(outputPath(kSweepFiles[lawIndex]), stereo, kSampleRate);

        // 在中央附近（2.0 s）取 0.1 s 量 RMS，與原始 mono 比較
        const int c = total / 2;
        const int n = secondsToSamples(0.1, kSampleRate);
        const float rmsMono = rms(mono.getReadPointer(0) + c, n);
        const float rmsL = rms(stereo.getReadPointer(0) + c, n);
        const float rmsR = rms(stereo.getReadPointer(1) + c, n);
        std::printf("  %-14s 中央：L %.2f dB, R %.2f dB（相對於 mono 原始訊號）→ %s\n", dsp::panLawName(kLaws[lawIndex]),
                    gainToDb(rmsL / rmsMono), gainToDb(rmsR / rmsMono), kSweepFiles[lawIndex]);
    }
    std::printf("\n");

    // ---- (C) 真正的立體聲訊號：Panner 其實是 balance -------------------------
    std::printf("(C) 立體聲（左右不相關的白雜訊）經過 Panner（ConstantPower, pan = +0.5）：\n");
    AudioBuffer noise = makeWhiteNoise(kSampleRate, 1.0, 0.25f, 2);
    const float inL = rms(noise, 0), inR = rms(noise, 1);
    dsp::Panner balance;
    balance.setRampSeconds(0.0);
    balance.prepare(kSampleRate, kBlockSize, 2);
    balance.setPan(0.5f);
    AudioBlock nb(noise);
    for (int start = 0; start < noise.getNumSamples(); start += kBlockSize)
        balance.process(nb.getSubBlock(start, std::min(kBlockSize, noise.getNumSamples() - start)));
    float gL = 0.0f, gR = 0.0f;
    dsp::Panner::computeGains(0.5f, dsp::PanLaw::ConstantPower, gL, gR);
    std::printf("  L：%.2f dB → %.2f dB（變化 %.2f dB，= gL %.2f dB）\n", gainToDb(inL), gainToDb(rms(noise, 0)),
                gainToDb(rms(noise, 0) / inL), gainToDb(gL));
    std::printf("  R：%.2f dB → %.2f dB（變化 %.2f dB，= gR %.2f dB）\n", gainToDb(inR), gainToDb(rms(noise, 1)),
                gainToDb(rms(noise, 1) / inR), gainToDb(gR));
    std::printf("  左聲道只是變小 %.2f dB，內容沒有搬到右邊：這是 balance，不是 mono pan。\n",
                -gainToDb(rms(noise, 0) / inL));

    // ---- (D) 端點與非法輸入：全左 / 全右時另一側必須「精確」是 0，NaN 不可流進音訊 ----
    std::printf("\n(D) 端點檢查：pan = ±1 時另一側的 gain\n");
    bool endpointsExact = true;
    for (dsp::PanLaw law : {dsp::PanLaw::Linear, dsp::PanLaw::ConstantPower, dsp::PanLaw::Minus4_5dB}) {
        float l1 = 0.0f, r1 = 0.0f, l2 = 0.0f, r2 = 0.0f;
        dsp::Panner::computeGains(+1.0f, law, l1, r1); // 全右：左聲道必須 == 0
        dsp::Panner::computeGains(-1.0f, law, l2, r2); // 全左：右聲道必須 == 0
        std::printf("  %-14s 全右 gL = %.9g、全左 gR = %.9g\n", dsp::panLawName(law), l1, r2);
        endpointsExact = endpointsExact && l1 == 0.0f && r2 == 0.0f && r1 > 0.999f && l2 > 0.999f;
    }
    dsp::Panner bad;
    bad.prepare(kSampleRate, kBlockSize, 2);
    bad.setPan(0.25f);
    bad.setPan(std::nanf("")); // 非法：應該被忽略、保持 0.25
    float nanL = 0.0f, nanR = 0.0f;
    dsp::Panner::computeGains(std::nanf(""), dsp::PanLaw::ConstantPower, nanL, nanR); // 視為中央
    std::printf("  setPan(NaN) 之後 pan = %.2f；computeGains(NaN) = %.4f / %.4f（視為中央）\n", bad.getPan(), nanL, nanR);
    DAWLAB_CHECK(endpointsExact);
    DAWLAB_CHECK(bad.getPan() == 0.25f);
    DAWLAB_CHECK(std::isfinite(nanL) && std::isfinite(nanR));

    std::printf("\n輸出：dawlab_output/%s, %s, %s\n", kSweepFiles[0], kSweepFiles[1], kSweepFiles[2]);
    return dawlab::test::summary("l01_pan_law");
}
