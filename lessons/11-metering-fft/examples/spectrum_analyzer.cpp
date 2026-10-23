// 範例 5：SpectrumAnalyzer 對 pluck sequence 的 ASCII 頻譜（對數頻率軸）
//
// 示範什麼：
//   - 模擬 audio callback：每 512 個 sample 呼叫一次 pushBlock()（audio thread 那一側）。
//   - 模擬 UI timer：每個 block 之後呼叫 computeSpectrum()（UI thread 那一側）；
//     只有累積了 hop（N/2 = 2048，N = 4096）個新 sample 才會回傳 true，否則 false。
//   - 在幾個時間點印出 20 Hz .. 20 kHz 的對數頻率軸 ASCII 頻譜，並找出最強的 bin。
//     A 小調琶音的音高是 110、164.8、220、261.6、329.6 Hz …；Karplus-Strong 撥弦的諧波分布由
//     初始雜訊決定，所以最強的 bin 通常是某個諧波（基頻的整數倍），不一定是基頻本身。
//   - 比較 smoothing = 0 與 0.8 的差異（平滑讓畫面穩定但反應慢）。
//   - 校準：振幅 0.5、正好落在 bin 中心的正弦讀 -6.02 dBFS（coherent gain 正規化）；不在 bin 中心時少
//     scalloping loss。
//   - 最後真的開一個 std::thread 當 audio thread，主 thread 當 UI thread，驗證 lock-free FIFO 沒有讀到壞 frame。
//   重要的數字都有 DAWLAB_CHECK，最後印出 [PASS] / [FAIL]（ctest 也會跑這支）。
//
// 怎麼執行：bin/Debug/l11_spectrum_analyzer.exe（不需要參數；可給 argv[1] 指定輸入 WAV）
// 輸出檔案：dawlab_output/l11_spectrum_plucks.wav
//
// 實際的 DAW（第 14 章）裡，pushBlock 在 audio thread、computeSpectrum 在 UI timer（例如 30 Hz）；
// 兩邊靠 lock-free ring buffer 溝通，前半段為了輸出 deterministic 用單一 thread 依序模擬。

#include <dawlab/Common.h>
#include <dawlab/dsp/SpectrumAnalyzer.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using namespace dawlab;
using dawlab::dsp::SpectrumAnalyzer;
using dawlab::dsp::WindowType;

namespace {

constexpr int kBlockSize = 512;

// 對數頻率軸：columns 欄，從 fLow 到 fHigh，每欄取範圍內 bin 的最大值
void plotLogSpectrum(const SpectrumAnalyzer& analyzer, const std::vector<float>& db, double fLow, double fHigh,
                     int columns, int rows, float topDb, float bottomDb) {
    std::vector<float> col(static_cast<size_t>(columns), -200.0f);
    const double logLow = std::log(fLow), logRange = std::log(fHigh) - logLow;
    for (int k = 1; k < analyzer.getNumBins(); ++k) {
        const double f = analyzer.getBinFrequency(k);
        if (f < fLow || f > fHigh)
            continue;
        const int c = std::clamp(static_cast<int>((std::log(f) - logLow) / logRange * columns), 0, columns - 1);
        col[static_cast<size_t>(c)] = std::max(col[static_cast<size_t>(c)], db[static_cast<size_t>(k)]);
    }
    // 低頻的欄位可能沒有任何 bin（bin 比欄寬）：用左邊最近的值補
    for (int c = 1; c < columns; ++c)
        if (col[static_cast<size_t>(c)] <= -200.0f)
            col[static_cast<size_t>(c)] = col[static_cast<size_t>(c - 1)];
    const float stepDb = (topDb - bottomDb) / static_cast<float>(rows);
    for (int r = 0; r < rows; ++r) {
        const float levelDb = topDb - stepDb * static_cast<float>(r);
        std::string line;
        for (int c = 0; c < columns; ++c)
            line += col[static_cast<size_t>(c)] >= levelDb ? '#' : ' ';
        std::printf("%6.0f dB |%s|\n", levelDb, line.c_str());
    }
    std::printf("%9s +%s+\n", "", std::string(static_cast<size_t>(columns), '-').c_str());
    std::printf("%9s  20   50  100   200   500   1k    2k    5k   10k  20k Hz\n", "");
}

// 找最強的 bin。從 30 Hz 以上開始找：Karplus-Strong 撥弦的初始雜訊有 DC 成分，會經由 window 的
// main lobe 漏到 bin 1、2，不是我們要看的音高。
int peakBin(const SpectrumAnalyzer& analyzer, const std::vector<float>& db) {
    int best = -1;
    for (int k = 1; k < static_cast<int>(db.size()); ++k) {
        if (analyzer.getBinFrequency(k) < 30.0)
            continue;
        if (best < 0 || db[static_cast<size_t>(k)] > db[static_cast<size_t>(best)])
            best = k;
    }
    return std::max(best, 1);
}

} // namespace

int main(int argc, char** argv) {
    double sr = 48000.0;
    AudioBuffer audio;
    if (argc > 1) {
        WavData wav;
        std::string err;
        if (!readWav(argv[1], wav, &err)) {
            std::printf("讀取失敗：%s\n", err.c_str());
            return 1;
        }
        audio = wav.buffer;
        sr = wav.sampleRate;
    } else {
        audio = makePluckSequence(sr, 4.0, 0.4f, 2);
    }
    writeWav(outputPath("l11_spectrum_plucks.wav"), audio, sr);

    SpectrumAnalyzer analyzer;
    analyzer.prepare(sr, 12); // N = 4096 → bin 寬 11.7 Hz，才分得開低音的諧波
    analyzer.setWindowType(WindowType::Hann);
    analyzer.setSmoothing(0.8f);
    analyzer.setOverlap(0.5f);
    std::printf("N = %d，bin 寬 = %.2f Hz，hop = %d samples，window = %s，smoothing = %.1f\n\n",
                analyzer.getFftSize(), analyzer.getBinWidthHz(), analyzer.getHopSize(),
                dsp::windowTypeName(analyzer.getWindowType()), analyzer.getSmoothing());

    const double snapshotTimes[] = {0.15, 1.1, 2.1, 3.1}; // 對應琶音第 1、5、9、13 個音
    size_t nextSnapshot = 0;
    std::vector<float> db;
    int frames = 0, polls = 0;

    for (int start = 0; start < audio.getNumSamples(); start += kBlockSize) {
        const int len = std::min(kBlockSize, audio.getNumSamples() - start);
        analyzer.pushBlock(AudioBlock(audio).getSubBlock(start, len)); // ← audio thread 那一側

        ++polls;
        if (analyzer.computeSpectrum(db)) // ← UI thread 那一側
            ++frames;

        const double t = (start + len) / sr;
        if (nextSnapshot < 4 && t >= snapshotTimes[nextSnapshot]) {
            const int pk = peakBin(analyzer, db);
            std::printf("---- t = %.2f s：最大 bin = %d（%.1f Hz，%.1f dBFS）----\n", t, pk,
                        analyzer.getBinFrequency(pk), db[static_cast<size_t>(pk)]);
            plotLogSpectrum(analyzer, db, 20.0, 20000.0, 64, 10, 0.0f, -100.0f);
            std::printf("\n");
            ++nextSnapshot;
        }
    }
    std::printf("computeSpectrum 被呼叫 %d 次，其中 %d 次有新 frame（hop = %d，block = %d → 每 %d 次 block 一個 frame）\n\n",
                polls, frames, analyzer.getHopSize(), kBlockSize, analyzer.getHopSize() / kBlockSize);
    // 第一個 frame 在累積滿 N 個 sample 時產生，之後每 hop 個 sample 一個
    const int expectedFrames = (audio.getNumSamples() - analyzer.getFftSize()) / analyzer.getHopSize() + 1;
    if (audio.getNumSamples() % kBlockSize == 0 && analyzer.getHopSize() % kBlockSize == 0)
        DAWLAB_CHECK(frames == expectedFrames);

    // 沒有新資料 → false
    std::vector<float> again;
    const bool gotAgain = analyzer.computeSpectrum(again);
    std::printf("沒有推入新 sample 時 computeSpectrum 回傳 %s\n", gotAgain ? "true" : "false");
    DAWLAB_CHECK(!gotAgain);

    // ---- 校準：bin 中心 vs 不在 bin 中心 --------------------------------------------------------------
    {
        SpectrumAnalyzer cal;
        cal.prepare(48000.0, 12);
        cal.setSmoothing(0.0f);
        std::vector<float> calDb;
        const double centred = 100.0 * cal.getBinWidthHz(); // 1171.875 Hz，正好是 bin 100
        AudioBuffer s1 = makeSine(centred, 48000.0, 0.2, 0.5f, 1);
        cal.pushSamples(s1.getReadPointer(0), s1.getNumSamples());
        cal.computeSpectrum(calDb);
        const float atCentre = calDb[100];
        cal.prepare(48000.0, 12);
        cal.setSmoothing(0.0f);
        const double between = 100.5 * cal.getBinWidthHz(); // 正好在 bin 100 與 101 中間
        AudioBuffer s2 = makeSine(between, 48000.0, 0.2, 0.5f, 1);
        cal.pushSamples(s2.getReadPointer(0), s2.getNumSamples());
        cal.computeSpectrum(calDb);
        const float halfBin = std::max(calDb[100], calDb[101]);
        std::printf("\n校準（Hann、N = 4096、振幅 0.5 = -6.02 dBFS）：bin 中心 %.2f dB；偏 0.5 bin %.2f dB"
                    "（少 %.2f dB = Hann 的 scalloping loss）\n",
                    atCentre, halfBin, atCentre - halfBin);
        DAWLAB_CHECK_NEAR(atCentre, -6.02, 0.02);
        DAWLAB_CHECK_NEAR(atCentre - halfBin, 1.42, 0.05);
    }

    // smoothing 對比：同一段訊號，α = 0 的頻譜在音符切換時立刻改變；α = 0.8 需要幾個 frame
    {
        SpectrumAnalyzer fast, slow;
        fast.prepare(sr, 12);
        slow.prepare(sr, 12);
        fast.setSmoothing(0.0f);
        slow.setSmoothing(0.8f);
        AudioBuffer bursts = makeToneBursts(440.0, sr, {-6.0f, -200.0f}, 0.5, 1);
        std::vector<float> dbFast, dbSlow;
        std::printf("\nsmoothing 對比：440 Hz 正弦 0.5 s 後突然靜音，觀察 440 Hz bin 的讀數\n");
        std::printf("%8s %12s %12s\n", "time(s)", "alpha=0", "alpha=0.8");
        const int bin440 = static_cast<int>(std::lround(440.0 / fast.getBinWidthHz()));
        float prevSlow = 0.0f, lastSlowStep = 0.0f;
        for (int start = 0; start < bursts.getNumSamples(); start += kBlockSize) {
            const int len = std::min(kBlockSize, bursts.getNumSamples() - start);
            fast.pushSamples(bursts.getReadPointer(0) + start, len);
            slow.pushSamples(bursts.getReadPointer(0) + start, len);
            const bool a = fast.computeSpectrum(dbFast);
            const bool b = slow.computeSpectrum(dbSlow);
            const double t = (start + len) / sr;
            if (a && b && t > 0.4 && t < 0.75) {
                std::printf("%8.3f %9.1f dB %9.1f dB\n", t, dbFast[static_cast<size_t>(bin440)],
                            dbSlow[static_cast<size_t>(bin440)]);
                lastSlowStep = prevSlow - dbSlow[static_cast<size_t>(bin440)];
                prevSlow = dbSlow[static_cast<size_t>(bin440)];
            }
        }
        // 440 Hz 偏離 bin 中心 0.45 bin → Hann 的 scalloping 讓讀數比 -6.02 低約 1.2 dB
        // 靜音之後 α = 0.8：每個 frame 乘 0.8 → 20·log10(0.8) = -1.94 dB / frame
        std::printf("α = 0.8 時靜音後每個 frame 下降 %.2f dB（理論 20·log10(0.8) = %.2f dB）\n", lastSlowStep,
                    -20.0 * std::log10(0.8));
        DAWLAB_CHECK_NEAR(lastSlowStep, -20.0 * std::log10(0.8), 0.05);
        DAWLAB_CHECK(dbFast[static_cast<size_t>(bin440)] <= kMinusInfinityDb);
    }
    // ---- 真的用兩個 thread：audio thread 推 1 kHz 正弦，UI thread 一直讀 -----------------------------
    // 沒有 mutex。若 FIFO 寫錯（例如忘了 release/acquire，或沒偵測到被覆寫），UI 端會讀到「新舊混雜」
    // 的 frame：接縫處的不連續會讓能量漏到整個頻帶。所以我們檢查兩件事：
    //   (1) 最強的 bin 在 1 kHz；(2) 5 kHz 以上的最大讀數 < -80 dB（乾淨的 Hann frame 遠低於此，
    //       有接縫的 frame 會高出好幾十 dB）。frame 的數量每次執行都不一樣（取決於 thread 排程）。
    {
        SpectrumAnalyzer live;
        live.prepare(sr, 11);
        live.setSmoothing(0.0f);
        std::atomic<bool> done{false};
        std::atomic<int> framesSeen{0};
        int pushedBlocks = 0; // 只有 audio thread 寫；join() 之後主 thread 才讀
        std::thread audioThread([&] {
            std::vector<float> block(static_cast<size_t>(kBlockSize));
            double phase = 0.0;
            // 全速推（不照真實時間），直到 UI 端拿到 50 個 frame 為止（上限 20 萬個 block，避免無窮迴圈）
            for (int b = 0; b < 200000 && framesSeen.load(std::memory_order_relaxed) < 50; ++b, ++pushedBlocks) {
                for (float& v : block) {
                    v = static_cast<float>(0.5 * std::sin(phase));
                    phase += kTwoPi * 1000.0 / sr;
                }
                live.pushSamples(block.data(), kBlockSize);
            }
            done.store(true, std::memory_order_release);
        });
        int liveFrames = 0, badFrames = 0;
        float worstHighDb = -200.0f;
        std::vector<float> liveDb;
        const int firstHighBin = static_cast<int>(5000.0 / live.getBinWidthHz());
        while (!done.load(std::memory_order_acquire)) {
            if (!live.computeSpectrum(liveDb))
                continue;
            ++liveFrames;
            framesSeen.store(liveFrames, std::memory_order_relaxed);
            float highDb = -200.0f;
            for (int k = firstHighBin; k < live.getNumBins(); ++k)
                highDb = std::max(highDb, liveDb[static_cast<size_t>(k)]);
            worstHighDb = std::max(worstHighDb, highDb);
            if (std::abs(live.getBinFrequency(peakBin(live, liveDb)) - 1000.0) > live.getBinWidthHz() || highDb > -80.0f)
                ++badFrames;
        }
        audioThread.join();
        std::printf("\n兩個 thread 同時跑：audio thread 推了 %d 個 block（%.1f 秒的音訊），UI 端算了 %d 個 frame，"
                    "壞 frame %d 個（應為 0）；所有 frame 在 5 kHz 以上的最大讀數 = %.1f dB\n",
                    pushedBlocks, pushedBlocks * kBlockSize / sr, liveFrames, badFrames, worstHighDb);
        DAWLAB_CHECK(liveFrames > 0);
        DAWLAB_CHECK(badFrames == 0);
    }

    std::printf("\n→ dawlab_output/l11_spectrum_plucks.wav\n");
    return dawlab::test::summary("l11_spectrum_analyzer");
}
