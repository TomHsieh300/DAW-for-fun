// 範例 2：在 pluck sequence 與 drum loop 套用典型的混音 EQ 設定
//
// 示範什麼：
//   - 「貝斯 / 吉他」型 EQ：low cut 去掉次低頻、挖掉 mud 區、提 presence、high shelf 加空氣感。
//   - 「鼓組 bus」型 EQ：low shelf 加 kick 的重量、挖 box 區、high shelf 加 hi-hat 的清脆。
//   - 以 512 sample 為一個 block 模擬 audio callback，一塊一塊處理。
//   - 用 Goertzel 量每個諧波「處理後 / 處理前」的比值，對照 EQ 理論曲線。
// 怎麼執行：l03_eq_presets（不需參數）或 l03_eq_presets input.wav（用自己的音檔，套用吉他型 EQ）
// 輸出（build 目錄/dawlab_output/）：
//   l03_pluck_dry.wav / l03_pluck_eq.wav、l03_drums_dry.wav / l03_drums_eq.wav、
//   （有 argv[1] 時）l03_custom_eq.wav
// 該聽到什麼：pluck 變得較「乾淨」、較不悶、琴弦摩擦感更前面；鼓組 kick 更厚、snare 的紙箱感減少、hi-hat 更亮。

#include <dawlab/Common.h>
#include <dawlab/dsp/ParametricEq.h>

#include <cstdio>
#include <string>

using namespace dawlab;
using namespace dawlab::dsp;

namespace {

constexpr int kBlockSize = 512;

// 模擬 audio callback：一次 512 個 sample
void processInBlocks(ParametricEq& eq, AudioBuffer& buffer) {
    AudioBlock whole(buffer);
    const int n = buffer.getNumSamples();
    for (int start = 0; start < n; start += kBlockSize)
        eq.process(whole.getSubBlock(start, std::min(kBlockSize, n - start)));
}

void printLevels(const char* name, const AudioBuffer& b) {
    std::printf("  %-18s peak %+6.2f dBFS   RMS %+6.2f dBFS\n", name, gainToDb(peakAbs(b)), gainToDb(rms(b, 0)));
}

void printCurve(const ParametricEq& eq) {
    std::printf("  理論曲線：");
    for (double f : {50.0, 100.0, 200.0, 400.0, 800.0, 1600.0, 3200.0, 6400.0, 12800.0})
        std::printf(" %.0fHz:%+.1f", f, eq.getMagnitudeDb(f));
    std::printf(" (dB)\n");
}

// 用 Goertzel 比較「處理後 / 處理前」在幾個頻率的振幅比，應該接近 EQ 曲線
void printMeasuredRatios(const ParametricEq& eq, const AudioBuffer& dry, const AudioBuffer& wet,
                         std::initializer_list<double> freqs, double sr) {
    std::printf("     freq(Hz)   theory(dB)   measured(dB)\n");
    for (double f : freqs) {
        const double a = goertzelAmplitude(dry.getReadPointer(0), dry.getNumSamples(), f, sr);
        const double b = goertzelAmplitude(wet.getReadPointer(0), wet.getNumSamples(), f, sr);
        std::printf("     %7.0f     %+7.2f      %+7.2f\n", f, eq.getMagnitudeDb(f), gainToDb(b / a));
    }
}

void setupGuitarEq(ParametricEq& eq) {
    eq.setBand(0, {EqBandType::LowCut, 70.0f, 0.707f, 0.0f, true});     // 去掉沒有音樂內容的次低頻
    eq.setBand(1, {EqBandType::Peak, 300.0f, 1.2f, -4.0f, true});      // 挖 mud 區（200–400 Hz）
    eq.setBand(2, {EqBandType::Peak, 900.0f, 2.0f, -2.0f, true});      // 稍微減少鼻音 / box 感
    eq.setBand(3, {EqBandType::Peak, 3000.0f, 1.0f, 3.0f, true});      // presence：讓撥弦的「指甲聲」往前
    eq.setBand(4, {EqBandType::HighShelf, 8000.0f, 0.707f, 2.0f, true}); // air
    eq.setBand(5, {EqBandType::HighCut, 16000.0f, 0.707f, 0.0f, false});
}

void setupDrumBusEq(ParametricEq& eq) {
    eq.setBand(0, {EqBandType::LowCut, 30.0f, 0.707f, 0.0f, true});     // 極低頻（subsonic）垃圾
    eq.setSlope(0, EqSlope::Db24);
    eq.setBand(1, {EqBandType::LowShelf, 90.0f, 0.707f, 3.0f, true});  // kick 的重量
    eq.setBand(2, {EqBandType::Peak, 400.0f, 1.5f, -3.0f, true});      // 紙箱感（boxiness）
    eq.setBand(3, {EqBandType::Peak, 2500.0f, 1.2f, 1.5f, true});      // snare 的 crack
    eq.setBand(4, {EqBandType::HighShelf, 7000.0f, 0.707f, 3.0f, true}); // hi-hat 清脆
    eq.setBand(5, {EqBandType::HighCut, 18000.0f, 0.707f, 0.0f, false});
}

} // namespace

int main(int argc, char** argv) {
    constexpr double sr = 48000.0;
    std::printf("=== 第 03 章 範例 2：典型 EQ 設定 ===\n");

    // ---- 1. pluck sequence + 吉他型 EQ ----
    {
        AudioBuffer dry = makePluckSequence(sr, 4.0);
        AudioBuffer wet = dry;
        ParametricEq eq;
        eq.prepare(sr, kBlockSize, wet.getNumChannels());
        setupGuitarEq(eq);
        eq.reset(); // 設定好再 reset：平滑器直接跳到目標，開頭不會有 20 ms 的「漸入」
        processInBlocks(eq, wet);

        std::printf("\n[pluck sequence] 吉他型 EQ：LowCut 70 / -4 dB@300 / -2 dB@900 / +3 dB@3k / HighShelf +2 dB@8k\n");
        printCurve(eq);
        printLevels("dry", dry);
        printLevels("eq", wet);
        std::printf("  諧波振幅比（A2 = 110 Hz 的泛音列）：\n");
        printMeasuredRatios(eq, dry, wet, {110.0, 220.0, 330.0, 440.0, 880.0, 1760.0, 3520.0}, sr);
        writeWav(outputPath("l03_pluck_dry.wav"), dry, sr);
        writeWav(outputPath("l03_pluck_eq.wav"), wet, sr);
    }

    // ---- 2. drum loop + 鼓組 bus EQ ----
    {
        AudioBuffer dry = makeDrumLoop(sr, 2);
        AudioBuffer wet = dry;
        ParametricEq eq;
        eq.prepare(sr, kBlockSize, wet.getNumChannels());
        setupDrumBusEq(eq);
        eq.reset();
        processInBlocks(eq, wet);

        std::printf("\n[drum loop] 鼓組 bus EQ：LowCut 30 (24 dB/oct) / LowShelf +3@90 / -3@400 / +1.5@2.5k / HighShelf +3@7k\n");
        printCurve(eq);
        printLevels("dry", dry);
        printLevels("eq", wet);
        writeWav(outputPath("l03_drums_dry.wav"), dry, sr);
        writeWav(outputPath("l03_drums_eq.wav"), wet, sr);
    }

    // ---- 3. 可選：自己的音檔 ----
    if (argc > 1) {
        WavData wav;
        std::string err;
        if (!readWav(argv[1], wav, &err)) {
            std::printf("\n讀取 %s 失敗：%s\n", argv[1], err.c_str());
            return 1;
        }
        ParametricEq eq;
        eq.prepare(wav.sampleRate, kBlockSize, wav.buffer.getNumChannels());
        setupGuitarEq(eq);
        eq.reset();
        processInBlocks(eq, wav.buffer);
        writeWav(outputPath("l03_custom_eq.wav"), wav.buffer, wav.sampleRate);
        std::printf("\n[%s] 套用吉他型 EQ -> dawlab_output/l03_custom_eq.wav\n", argv[1]);
    }

    std::printf("\n輸出：dawlab_output/l03_pluck_dry.wav, l03_pluck_eq.wav, l03_drums_dry.wav, l03_drums_eq.wav\n");
    return 0;
}
