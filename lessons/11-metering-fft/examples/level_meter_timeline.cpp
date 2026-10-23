// 範例 3：LevelMeter 的時間軸（peak / RMS / peak hold）
//
// 示範什麼：
//   1. ballistics 驗證：-6 dBFS 的 1 kHz 正弦響 0.5 s 後靜音。peak 立刻到 -6 dB，
//      靜音後以 24 dB/s 下降（1 s 後約 -30 dB）；hold 停在 -6 dB 滿 1 s 才開始下降；
//      300 ms 視窗的 RMS 讀 -9.01 dB（正弦 RMS = 峰值 / √2）。
//   2. drum loop 的時間軸：每 50 ms 印一行 peak / RMS / hold 與 ASCII bar（左聲道）。
//
// 怎麼執行：bin/Debug/l11_level_meter.exe（不需要參數；可給 argv[1] 指定輸入 WAV）
// 輸出檔案：dawlab_output/l11_level_meter_drums.wav（量測的 drum loop 本身）
// 該聽到什麼：kick 與 snare 讓 peak 瞬間跳高、RMS 跟得慢；hi-hat 的 peak 高但 RMS 幾乎看不到。

#include <dawlab/Common.h>
#include <dawlab/dsp/LevelMeter.h>

#include <cstdio>
#include <string>

using namespace dawlab;
using dawlab::dsp::LevelMeter;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

// 把 dB 畫成 40 格的 bar：'#' = RMS，'P' = peak，'|' = hold（-60 .. 0 dB）
std::string bar(float peakDb, float rmsDb, float holdDb) {
    constexpr int width = 40;
    auto pos = [](float db) { return std::clamp(static_cast<int>((db + 60.0f) / 60.0f * width), -1, width - 1); };
    std::string s(static_cast<size_t>(width), ' ');
    const int r = pos(rmsDb), p = pos(peakDb), h = pos(holdDb);
    for (int i = 0; i <= r; ++i)
        s[static_cast<size_t>(i)] = '#';
    if (p >= 0)
        s[static_cast<size_t>(p)] = 'P';
    if (h >= 0)
        s[static_cast<size_t>(h)] = '|';
    return s;
}

// 一塊一塊餵給 meter，每 printEveryMs 印一次
void runTimeline(LevelMeter& meter, AudioBuffer& audio, double printEveryMs, double stopSeconds) {
    const int printEvery = msToSamples(printEveryMs, kSampleRate);
    int nextPrint = 0;
    std::printf("%8s %9s %9s %9s  %s\n", "time(s)", "peak dB", "RMS dB", "hold dB", "-60 dB [#=RMS P=peak |=hold] 0 dB");
    for (int start = 0; start < audio.getNumSamples(); start += kBlockSize) {
        const int len = std::min(kBlockSize, audio.getNumSamples() - start);
        meter.process(AudioBlock(audio).getSubBlock(start, len));
        if (start + len >= nextPrint) {
            const double t = (start + len) / kSampleRate;
            if (t > stopSeconds)
                break;
            std::printf("%8.3f %9.2f %9.2f %9.2f  %s\n", t, meter.getPeakDb(0), meter.getRmsDb(0),
                        meter.getPeakHoldDb(0), bar(meter.getPeakDb(0), meter.getRmsDb(0), meter.getPeakHoldDb(0)).c_str());
            nextPrint += printEvery;
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    LevelMeter meter;
    meter.prepare(kSampleRate, 2);
    meter.setPeakHoldMs(1000.0f);
    meter.setDecayDbPerSecond(24.0f);
    meter.setRmsWindowMs(300.0f);

    // ---- 1. ballistics：-6 dBFS 正弦 0.5 s + 靜音 1.5 s --------------------------------------
    {
        AudioBuffer burst = makeToneBursts(1000.0, kSampleRate, {-6.0f, -100.0f, -100.0f, -100.0f}, 0.5, 2);
        std::printf("== 1. ballistics：-6 dBFS 1 kHz 正弦 0.5 s，之後靜音（decay 24 dB/s，hold 1 s，RMS 300 ms）==\n");
        runTimeline(meter, burst, 100.0, 2.0);
        // 數學值用 double 算；LevelMeter 內部存成 float（間距約 6e-8），所以印 8 位小數時最後一位會不同
        std::printf("每 sample 的 decay 係數 g = 10^(-24 / (20·48000)) = %.10f（存成 float 後是 %.8f）\n",
                    std::pow(10.0, -24.0 / (20.0 * kSampleRate)),
                    static_cast<double>(LevelMeter::decayCoefficient(24.0f, kSampleRate)));

        // 用 10 ms（480 sample）的 block 重跑一次，讓讀數的時間點剛好落在 0.5 / 1.0 / 1.5 / 2.0 s，以數字驗證：
        //   0.5 s：peak = hold = -6.00（dbToGain(-6) = 0.501，1 kHz 每週期 48 點會剛好踩到峰值）、
        //          RMS = -6.00 - 3.01 = -9.01（正弦 RMS = 峰值 / √2）
        //   1.0 s：peak 已下降 0.5 s × 24 dB/s = 12 dB → -18.00；hold 還停在 -6.00
        //   1.5 s：peak → -30.00；hold 的 1 s 剛好用完（約 -6.0 ~ -6.1）
        //   2.0 s：peak → -42.00；hold 下降了 0.5 s → -18.00
        meter.reset();
        float readings[4][3] = {};
        for (int b = 0; b < burst.getNumSamples() / 480; ++b) {
            meter.process(AudioBlock(burst).getSubBlock(b * 480, 480));
            if ((b + 1) % 50 == 0 && (b + 1) / 50 <= 4) {
                float* r = readings[(b + 1) / 50 - 1];
                r[0] = meter.getPeakDb(0);
                r[1] = meter.getRmsDb(0);
                r[2] = meter.getPeakHoldDb(0);
            }
        }
        std::printf("%8s %9s %9s %9s   %s\n", "time(s)", "peak dB", "RMS dB", "hold dB", "期望（peak / hold）");
        const float expectedPeak[4] = {-6.0f, -18.0f, -30.0f, -42.0f};
        const float expectedHold[4] = {-6.0f, -6.0f, -6.0f, -18.0f};
        for (int i = 0; i < 4; ++i) {
            std::printf("%8.1f %9.2f %9.2f %9.2f   %.2f / %.2f\n", 0.5 * (i + 1), readings[i][0], readings[i][1],
                        readings[i][2], expectedPeak[i], expectedHold[i]);
            DAWLAB_CHECK_NEAR(readings[i][0], expectedPeak[i], 0.05);
            DAWLAB_CHECK_NEAR(readings[i][2], expectedHold[i], 0.1);
        }
        DAWLAB_CHECK_NEAR(readings[0][1], -9.01, 0.02);
        DAWLAB_CHECK(readings[2][1] <= kMinusInfinityDb); // 靜音超過 300 ms，RMS 視窗全空
        std::printf("\n");
    }

    // ---- 2. drum loop 時間軸 ----------------------------------------------------------------
    AudioBuffer drums;
    if (argc > 1) {
        WavData wav;
        std::string err;
        if (!readWav(argv[1], wav, &err)) {
            std::printf("讀取失敗：%s\n", err.c_str());
            return 1;
        }
        drums = wav.buffer;
    } else {
        drums = makeDrumLoop(kSampleRate, 1, 120.0, 0.8f, 2); // 1 小節 = 2 s
    }
    writeWav(outputPath("l11_level_meter_drums.wav"), drums, kSampleRate);
    meter.reset();
    std::printf("== 2. drum loop（120 BPM，kick 在拍 1、2.5、3，snare 在拍 2、4）==\n");
    runTimeline(meter, drums, 50.0, 2.0);
    const float samplePeakDb = gainToDb(peakAbs(drums));
    const float holdDb = meter.getPeakHoldDb(0);
    std::printf("\n整段 sample peak = %.2f dBFS；結束時 hold 讀到 %.2f dBFS\n", samplePeakDb, holdDb);
    if (argc <= 1)
        std::printf("（整段最大值出現在第一下 kick，它的 1 s hold 早就過期了；現在的 hold 是 1.0 s 那一下 kick 的峰值。）\n");
    DAWLAB_CHECK(holdDb <= samplePeakDb + 0.01f); // hold 不可能超過真正的最大值
    DAWLAB_CHECK(holdDb >= meter.getPeakDb(0));   // hold 永遠不低於 peak

    // UI 點了 meter → resetPeakHold()：下一次 process() 時 hold 立刻掉回目前的 peak
    meter.resetPeakHold();
    AudioBuffer silence(2, kBlockSize);
    meter.process(silence);
    std::printf("resetPeakHold() 之後：hold = %.2f dB，peak = %.2f dB\n", meter.getPeakHoldDb(0), meter.getPeakDb(0));
    DAWLAB_CHECK_NEAR(meter.getPeakHoldDb(0), meter.getPeakDb(0), 0.01);

    std::printf("→ dawlab_output/l11_level_meter_drums.wav\n");
    return dawlab::test::summary("l11_level_meter");
}
