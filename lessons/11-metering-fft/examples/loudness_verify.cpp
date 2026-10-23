// 範例 4：LoudnessMeter（ITU-R BS.1770-4 / EBU R128）對已知訊號的驗證
//
// 示範什麼：
//   1. K-weighting：48 kHz 的標準係數 vs 從類比原型用 bilinear transform 重新設計的係數（應幾乎相同），
//      以及 44.1 kHz / 96 kHz 的頻率響應（曲線一致）。係數放進第 02 章的 Biquad 執行。
//   2. BS.1770-4 的驗證條件：-20 dBFS 的 997 Hz 正弦送進單一聲道 → -23.01 LUFS（0 dBFS 時 -3.01）。
//   3. 同一訊號送進左右兩聲道 → 功率加倍 → +3.01 LU → -20.0 LUFS。
//   4. gating：-20 dBFS 3 s 之後接 3 s 靜音與 3 s 的 -50 dBFS 微弱段落，integrated ≈ -23.23 LUFS
//      （靜音被 absolute gate 擋掉；微弱段落被 relative gate 擋掉；只有跨邊界的 3 個 block 把平均
//      拉低 0.22 LU）。不 gating 的平均是 -27.78 LUFS。
//   每一段都有 DAWLAB_CHECK 以數字驗證，最後印出 [PASS] / [FAIL]（ctest 也會跑這支）。
//   5. drum loop 與 pluck sequence 的 momentary / short-term / integrated 時間軸。
//
// 怎麼執行：bin/Debug/l11_loudness.exe（不需要參數；可給 argv[1] 指定輸入 WAV，會印出它的 LUFS）
// 輸出檔案：dawlab_output/l11_loudness_997hz_mono.wav、l11_loudness_997hz_stereo.wav、l11_loudness_gated.wav

#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>
#include <dawlab/dsp/LoudnessMeter.h>

#include <cstdio>
#include <string>

using namespace dawlab;
using dawlab::dsp::BiquadCoefficients;
using dawlab::dsp::LoudnessMeter;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

// 一塊一塊餵；printEverySeconds > 0 時印時間軸
void feed(LoudnessMeter& meter, AudioBuffer& audio, double printEverySeconds = 0.0) {
    const int printEvery = printEverySeconds > 0.0 ? secondsToSamples(printEverySeconds, kSampleRate) : 0;
    int nextPrint = printEvery;
    if (printEvery > 0)
        std::printf("%8s %12s %12s %12s\n", "time(s)", "momentary", "short-term", "integrated");
    for (int start = 0; start < audio.getNumSamples(); start += kBlockSize) {
        const int len = std::min(kBlockSize, audio.getNumSamples() - start);
        meter.process(AudioBlock(audio).getSubBlock(start, len));
        if (printEvery > 0 && start + len >= nextPrint) {
            std::printf("%8.2f %9.2f LUFS %9.2f LUFS %9.2f LUFS\n", (start + len) / kSampleRate,
                        meter.getMomentaryLufs(), meter.getShortTermLufs(), meter.getIntegratedLufs());
            nextPrint += printEvery;
        }
    }
}

double kWeightingDb(double freq, double sr) {
    return LoudnessMeter::makeKWeightingStage1(sr).magnitudeDb(freq, sr) +
           LoudnessMeter::makeKWeightingStage2(sr).magnitudeDb(freq, sr);
}

} // namespace

int main(int argc, char** argv) {
    // ---- 1. K-weighting 係數與頻率響應 -------------------------------------------------------
    {
        std::printf("== 1. K-weighting ==\n");
        const BiquadCoefficients d1 = LoudnessMeter::makeKWeightingStage1(48000.0);
        const BiquadCoefficients d2 = LoudnessMeter::makeKWeightingStage2(48000.0);
        const double* t1 = LoudnessMeter::kStage1Table48k;
        const double* t2 = LoudnessMeter::kStage2Table48k;
        std::printf("stage 1（high shelf）BS.1770-4 Table 1: b0=%.11f b1=%.11f b2=%.11f a1=%.11f a2=%.11f\n", t1[0],
                    t1[1], t1[2], t1[3], t1[4]);
        std::printf("stage 1 由類比原型重新設計（48 kHz）:   b0=%.11f b1=%.11f b2=%.11f a1=%.11f a2=%.11f\n", d1.b0,
                    d1.b1, d1.b2, d1.a1, d1.a2);
        std::printf("stage 2（highpass）BS.1770-4 Table 2:   b0=%.11f b1=%.11f b2=%.11f a1=%.11f a2=%.11f\n", t2[0],
                    t2[1], t2[2], t2[3], t2[4]);
        std::printf("stage 2 由類比原型重新設計（48 kHz）:   b0=%.11f b1=%.11f b2=%.11f a1=%.11f a2=%.11f\n", d2.b0,
                    d2.b1, d2.b2, d2.a1, d2.a2);
        const double maxDiff = std::max({std::abs(d1.b0 - t1[0]), std::abs(d1.b1 - t1[1]), std::abs(d1.b2 - t1[2]),
                                         std::abs(d1.a1 - t1[3]), std::abs(d1.a2 - t1[4]), std::abs(d2.b0 - t2[0]),
                                         std::abs(d2.b1 - t2[1]), std::abs(d2.b2 - t2[2]), std::abs(d2.a1 - t2[3]),
                                         std::abs(d2.a2 - t2[4])});
        std::printf("標準係數表與重新設計的最大差異 = %.2e（應 < 1e-10）\n\n", maxDiff);
        DAWLAB_CHECK(maxDiff < 1e-10);

        std::printf("%10s %12s %12s %12s\n", "freq (Hz)", "48 kHz", "44.1 kHz", "96 kHz");
        for (double f : {20.0, 38.0, 100.0, 300.0, 997.0, 1000.0, 2000.0, 5000.0, 10000.0, 16000.0})
            std::printf("%10.0f %9.3f dB %9.3f dB %9.3f dB\n", f, kWeightingDb(f, 48000.0), kWeightingDb(f, 44100.0),
                        kWeightingDb(f, 96000.0));
        DAWLAB_CHECK_NEAR(kWeightingDb(997.0, 48000.0), 0.691, 0.005);
        // 重新設計的 44.1 / 96 kHz 曲線與 48 kHz 相差 < 0.05 dB（bilinear transform 在高頻的頻率扭曲不同，差一點點）
        for (double f : {38.0, 100.0, 997.0, 2000.0, 10000.0}) {
            DAWLAB_CHECK_NEAR(kWeightingDb(f, 44100.0), kWeightingDb(f, 48000.0), 0.05);
            DAWLAB_CHECK_NEAR(kWeightingDb(f, 96000.0), kWeightingDb(f, 48000.0), 0.05);
        }
        // 反例：44.1 kHz 直接拿 48 kHz 的係數表 → 曲線往低頻移 8%（2 kHz 處偏多少？）
        BiquadCoefficients wrong1, wrong2;
        wrong1.b0 = t1[0]; wrong1.b1 = t1[1]; wrong1.b2 = t1[2]; wrong1.a1 = t1[3]; wrong1.a2 = t1[4];
        wrong2.b0 = t2[0]; wrong2.b1 = t2[1]; wrong2.b2 = t2[2]; wrong2.a1 = t2[3]; wrong2.a2 = t2[4];
        const double wrongAt2k = wrong1.magnitudeDb(2000.0, 44100.0) + wrong2.magnitudeDb(2000.0, 44100.0);
        std::printf("\n反例：44.1 kHz 誤用 48 kHz 係數表 → 2 kHz 處 %.3f dB（正確 %.3f dB），997 Hz 處 %.3f dB\n",
                    wrongAt2k, kWeightingDb(2000.0, 44100.0),
                    wrong1.magnitudeDb(997.0, 44100.0) + wrong2.magnitudeDb(997.0, 44100.0));
        DAWLAB_CHECK(std::abs(wrongAt2k - kWeightingDb(2000.0, 44100.0)) > 0.1);
        std::printf("→ 三個取樣率的曲線一致：997 Hz 處 +0.691 dB，這就是 LUFS 公式裡 -0.691 的由來；\n"
                    "   高頻 +4.04 dB（含 stage 2 分子未正規化的 +0.043 dB）、38 Hz 處 -6 dB、更低頻滾降。\n\n");
    }

    // ---- 2. -20 dBFS 997 Hz 單聲道 → -23.01 LUFS -------------------------------------------
    AudioBuffer mono = makeSine(997.0, kSampleRate, 5.0, dbToGain(-20.0f), 1);
    {
        LoudnessMeter meter;
        meter.prepare(kSampleRate, 1);
        feed(meter, mono);
        std::printf("== 2. -20 dBFS 997 Hz 正弦，單一聲道 5 s ==\n");
        std::printf("momentary = %.2f  short-term = %.2f  integrated = %.2f LUFS（規範：-23.01；0 dBFS → -3.01）\n\n",
                    meter.getMomentaryLufs(), meter.getShortTermLufs(), meter.getIntegratedLufs());
        DAWLAB_CHECK_NEAR(meter.getMomentaryLufs(), -23.01, 0.02);
        DAWLAB_CHECK_NEAR(meter.getShortTermLufs(), -23.01, 0.02);
        DAWLAB_CHECK_NEAR(meter.getIntegratedLufs(), -23.01, 0.02);
        writeWav(outputPath("l11_loudness_997hz_mono.wav"), mono, kSampleRate);
    }

    // ---- 3. 同訊號送左右兩聲道 → +3.01 LU -------------------------------------------------
    AudioBuffer stereo = makeSine(997.0, kSampleRate, 5.0, dbToGain(-20.0f), 2);
    {
        LoudnessMeter meter;
        meter.prepare(kSampleRate, 2);
        feed(meter, stereo);
        std::printf("== 3. 同一訊號送 L 與 R ==\n");
        std::printf("integrated = %.2f LUFS（兩聲道功率相加 → +3.01 LU → -20.00）\n\n", meter.getIntegratedLufs());
        DAWLAB_CHECK_NEAR(meter.getIntegratedLufs(), -20.00, 0.02);
        writeWav(outputPath("l11_loudness_997hz_stereo.wav"), stereo, kSampleRate);
    }

    // ---- 4. gating ---------------------------------------------------------------------------
    {
        // 3 s 的 -20 dBFS、3 s 靜音、3 s 的 -50 dBFS
        AudioBuffer gated = makeToneBursts(997.0, kSampleRate, {-20.0f, -200.0f, -50.0f}, 3.0, 1);
        LoudnessMeter meter;
        meter.prepare(kSampleRate, 1);
        std::printf("== 4. gating：3 s @ -20 dBFS → 3 s 靜音 → 3 s @ -50 dBFS ==\n");
        feed(meter, gated, 1.0);
        // 期望值：第一段有 27 個完整的 400 ms block（功率 P）加上跨越「訊號→靜音」邊界的 3 個 block
        // （功率 3P/4、2P/4、P/4，都通過 relative gate），平均 = 28.5P/30 → -23.01 + 10·log10(0.95) = -23.23。
        // 靜音 block（-∞）被 absolute gate 擋掉；-53 LUFS 的 block 低於 relative gate（約 -36）也被擋掉。
        const double p = std::pow(10.0, (-23.01 + 0.691) / 10.0);
        const double expectedGated = LoudnessMeter::powerToLufs(28.5 * p / 30.0);
        // 不 gating 的「平均功率」：(3·P + 0 + 3·P/1000) / 9
        const double ungated = LoudnessMeter::powerToLufs((3.0 * p + 3.0 * p * 1e-3) / 9.0);
        std::printf("integrated = %.2f LUFS（期望 %.2f：靜音被 absolute gate、-53 LUFS 段被 relative gate 擋掉；\n"
                    "   邊界上 3 個「半滿」的 block 通過 gate，把平均拉低 10·log10(28.5/30) = -0.22 LU）\n",
                    meter.getIntegratedLufs(), expectedGated);
        std::printf("若完全不 gating、直接平均 9 s 的功率 → %.2f LUFS（被靜音稀釋了）\n\n", ungated);
        DAWLAB_CHECK_NEAR(meter.getIntegratedLufs(), expectedGated, 0.05);
        DAWLAB_CHECK(meter.getIntegratedLufs() > ungated + 4.0f);
        writeWav(outputPath("l11_loudness_gated.wav"), gated, kSampleRate);
    }

    // ---- 5. 音樂素材 -------------------------------------------------------------------------
    {
        AudioBuffer drums = makeDrumLoop(kSampleRate, 2, 120.0, 0.8f, 2);
        LoudnessMeter meter;
        meter.prepare(kSampleRate, 2);
        std::printf("== 5a. drum loop（4 s）==\n");
        feed(meter, drums, 0.5);
        const float drumsIntegrated = meter.getIntegratedLufs();
        const float drumsPeakDb = gainToDb(peakAbs(drums));
        std::printf("integrated = %.2f LUFS，max momentary = %.2f，max short-term = %.2f，sample peak = %.2f dBFS\n\n",
                    drumsIntegrated, meter.getMaxMomentaryLufs(), meter.getMaxShortTermLufs(),
                    drumsPeakDb);
        // 動態大的素材：max momentary 明顯高於 integrated；integrated 比 sample peak 低很多（crest factor）
        DAWLAB_CHECK(meter.getMaxMomentaryLufs() > drumsIntegrated + 3.0f);
        DAWLAB_CHECK(drumsIntegrated < drumsPeakDb - 10.0f);

        AudioBuffer plucks = makePluckSequence(kSampleRate, 4.0, 0.4f, 2);
        meter.reset();
        std::printf("== 5b. pluck sequence（4 s）==\n");
        feed(meter, plucks, 0.5);
        std::printf("integrated = %.2f LUFS，max momentary = %.2f，sample peak = %.2f dBFS\n",
                    meter.getIntegratedLufs(), meter.getMaxMomentaryLufs(), gainToDb(peakAbs(plucks)));
        std::printf("→ 串流平台 target 約 -14 LUFS（Spotify / YouTube）、-16（Apple）、-23（EBU R128 廣播）：\n"
                    "   要把 drum loop 推到 -14 LUFS 需要加 %.1f dB，sample peak 會變成 %.2f dBFS ——\n"
                    "   已經超過 -1 dBTP 的建議上限，實務上要先用 limiter（第 05 章）壓掉峰值再推音量。\n\n",
                    -14.0f - drumsIntegrated, drumsPeakDb + (-14.0f - drumsIntegrated));
    }

    if (argc > 1) {
        WavData wav;
        std::string err;
        if (!readWav(argv[1], wav, &err)) {
            std::printf("讀取失敗：%s\n", err.c_str());
            return 1;
        }
        LoudnessMeter meter;
        meter.prepare(wav.sampleRate, wav.buffer.getNumChannels());
        for (int start = 0; start < wav.buffer.getNumSamples(); start += kBlockSize) {
            const int len = std::min(kBlockSize, wav.buffer.getNumSamples() - start);
            meter.process(AudioBlock(wav.buffer).getSubBlock(start, len));
        }
        std::printf("%s：integrated = %.2f LUFS，max momentary = %.2f，max short-term = %.2f\n", argv[1],
                    meter.getIntegratedLufs(), meter.getMaxMomentaryLufs(), meter.getMaxShortTermLufs());
    }
    return dawlab::test::summary("l11_loudness");
}
