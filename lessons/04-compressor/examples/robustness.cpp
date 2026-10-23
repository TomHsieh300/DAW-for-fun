// 範例 5：壞輸入與 stereo link 切換——compressor 在 audio thread 上的兩個「隱形」陷阱。
//
// 示範什麼：
//   (A) 壞輸入：在 1 kHz 正弦波中間塞一個 +Inf、NaN 或極大值（2e19，平方後會溢位），
//       比較「乾淨輸入」與「被污染輸入」在 1 秒後的輸出 peak 與 GR。
//       遞迴的 attack / release 平滑器只要吃進一個 NaN，狀態就會永久卡死：
//       Peak 模式會把輸出永久靜音，RMS 模式會永久不壓縮，而且 GR meter 看不出異常。
//       本章的 AttackReleaseFilter / EnvelopeFollower / Compressor 都會把 non-finite 擋掉，
//       所以這裡應該看到：壞 sample 過去之後，compressor 很快回到和乾淨輸入一樣的狀態。
//   (B) stereo link 切換：先 unlinked、R 聲道很大聲（GR 約 15 dB）、L 很小聲（GR 0），
//       再切成 linked、再切回 unlinked。逐 sample 量 gain，確認切換瞬間沒有 gain 跳變（click）：
//       切成 linked 之後，L 要以 attack 的速度跟上 R 的 GR，而不是瞬間被拉下 15 dB。
// 怎麼執行：直接執行 l04_robustness（不帶參數）。
// 輸出檔案：dawlab_output/l04_link_toggle.wav（(B) 的輸出，切換點在 1.0 s、1.1 s、2.1 s）。
// 該聽到什麼：l04_link_toggle.wav 是直流（DC）測試訊號，耳朵聽不到內容，重點是看數字；用波形編輯器
//             打開，切換點附近的波形高度應該是平滑變化，沒有突然的階梯。
// 本範例最後用 DAWLAB_CHECK 自我驗證，全部通過才回傳 0。

#include <dawlab/Common.h>
#include <dawlab/dsp/Compressor.h>

#include <cmath>
#include <cstdio>
#include <limits>

using namespace dawlab;
using dawlab::dsp::Compressor;
using dawlab::dsp::DetectorMode;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

void processBlocks(AudioBuffer& buffer, Compressor& comp) {
    AudioBlock whole(buffer);
    for (int start = 0; start < buffer.getNumSamples(); start += kBlockSize)
        comp.process(whole.getSubBlock(start, std::min(kBlockSize, buffer.getNumSamples() - start)));
}

// 最後 100 ms 的 peak（dBFS）
float tailPeakDb(const AudioBuffer& b, int ch) {
    const int n = static_cast<int>(0.1 * kSampleRate);
    return gainToDb(peakAbs(b.getReadPointer(ch) + b.getNumSamples() - n, n));
}

// (A)：回傳 {尾段 peak dB, 最後一個 block 的 GR}
struct Result {
    float tailDb;
    float grDb;
};

Result runWithBadSample(DetectorMode mode, bool inject, float badValue) {
    Compressor comp; // 預設：T -20、4:1、knee 6、attack 10 ms、release 100 ms
    comp.prepare(kSampleRate, kBlockSize, 1);
    comp.setDetectorMode(mode);
    AudioBuffer x = makeSine(1000.0, kSampleRate, 1.0, 0.5f, 1); // -6 dBFS，1 秒
    if (inject)
        x.setSample(0, 100, badValue);
    processBlocks(x, comp);
    return {tailPeakDb(x, 0), comp.getGainReductionDb()};
}

} // namespace

int main() {
    std::printf("第 04 章 範例 5：壞輸入與 stereo link 切換\n");

    // ---- (A) 壞輸入 -------------------------------------------------------------
    std::printf("\n(A) 1 kHz -6 dBFS 正弦波，第 100 個 sample 換成壞值，看 1 秒後是否恢復\n");
    std::printf("  模式 | 壞值   | 尾段 peak (dBFS) | 乾淨輸入的尾段 | GR (dB) | 乾淨輸入的 GR\n");
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const struct {
        const char* name;
        float value;
    } bads[] = {{"+Inf", inf}, {"NaN", nan}, {"2e19", 2.0e19f}};

    for (DetectorMode mode : {DetectorMode::Peak, DetectorMode::Rms}) {
        const Result clean = runWithBadSample(mode, false, 0.0f);
        for (const auto& bad : bads) {
            const Result r = runWithBadSample(mode, true, bad.value);
            std::printf("  %-4s | %-6s | %16.2f | %14.2f | %7.2f | %13.2f\n", mode == DetectorMode::Peak ? "Peak" : "RMS",
                        bad.name, r.tailDb, clean.tailDb, r.grDb, clean.grDb);
            // 1 秒後應該和乾淨輸入幾乎一樣（壞 sample 只影響它附近的一小段）
            DAWLAB_CHECK(std::isfinite(r.tailDb) && std::isfinite(r.grDb));
            DAWLAB_CHECK_NEAR(r.tailDb, clean.tailDb, 0.05);
            DAWLAB_CHECK_NEAR(r.grDb, clean.grDb, 0.05);
        }
    }

    // ---- (B) stereo link 切換 ---------------------------------------------------
    // 用直流（DC）當測試訊號：輸入每個 sample 都一樣，gain = 輸出 / 輸入，可以逐 sample 精確量。
    std::printf("\n(B) stereo link 切換：hard knee、T -20、4:1、attack 10 ms、release 100 ms\n");
    std::printf("    0.00–1.00 s unlinked，L = -40 dBFS、R = 0 dBFS（R 的 GR 約 15 dB）\n");
    std::printf("    1.00–1.10 s 切成 linked，輸入不變（L 應以 attack 速度跟上 R 的 GR）\n");
    std::printf("    1.10–2.10 s 切回 unlinked，L、R 都改成 -40 dBFS（各自 release）\n");
    std::printf("    2.10–3.10 s 再切成 linked，輸入不變\n");

    Compressor comp;
    comp.prepare(kSampleRate, kBlockSize, 2);
    comp.setKneeDb(0.0f);
    comp.setStereoLink(false);

    const int seg1 = static_cast<int>(1.0 * kSampleRate);
    const int seg2 = static_cast<int>(0.1 * kSampleRate);
    const int seg3 = static_cast<int>(1.0 * kSampleRate);
    const int seg4 = static_cast<int>(1.0 * kSampleRate);
    const int total = seg1 + seg2 + seg3 + seg4;
    const float quiet = dbToGain(-40.0f);

    AudioBuffer input(2, total);
    for (int i = 0; i < total; ++i) {
        input.setSample(0, i, quiet);
        input.setSample(1, i, i < seg1 + seg2 ? 1.0f : quiet);
    }
    AudioBuffer output = input;
    AudioBlock whole(output);

    // 每一段各自用 512 的 block 處理，段與段之間（block 邊界上）切換 link
    const int segStart[] = {0, seg1, seg1 + seg2, seg1 + seg2 + seg3};
    const int segLen[] = {seg1, seg2, seg3, seg4};
    const bool segLinked[] = {false, true, false, true};
    for (int s = 0; s < 4; ++s) {
        comp.setStereoLink(segLinked[s]);
        for (int off = 0; off < segLen[s]; off += kBlockSize)
            comp.process(whole.getSubBlock(segStart[s] + off, std::min(kBlockSize, segLen[s] - off)));
    }

    // 逐 sample 的 gain（dB）與相鄰 sample 的最大差
    float maxStepDb[2] = {0.0f, 0.0f};
    for (int ch = 0; ch < 2; ++ch) {
        float prevDb = 0.0f;
        for (int i = 0; i < total; ++i) {
            const float gDb = gainToDb(output.getSample(ch, i) / input.getSample(ch, i));
            if (i > 0)
                maxStepDb[ch] = std::max(maxStepDb[ch], std::abs(gDb - prevDb));
            prevDb = gDb;
        }
    }
    auto gainAtDb = [&](int ch, int i) { return gainToDb(output.getSample(ch, i) / input.getSample(ch, i)); };
    for (int s = 1; s < 4; ++s) {
        const int i = segStart[s];
        std::printf("  切換到 %-8s @ %.2f s：R gain %7.2f dB → %7.2f dB；L gain %7.2f dB → %7.2f dB\n",
                    segLinked[s] ? "linked" : "unlinked", i / kSampleRate, gainAtDb(1, i - 1), gainAtDb(1, i),
                    gainAtDb(0, i - 1), gainAtDb(0, i));
    }
    std::printf("  整段相鄰 sample 的最大 gain 變化：L %.3f dB、R %.3f dB（attack 期間每 sample 最多約 0.03 dB）\n",
                maxStepDb[0], maxStepDb[1]);
    // linked 0.1 s（10 個 attack 時間常數）之後，L 的 GR 應該已經追上 R（共用同一個 gain）
    const int endLinked = seg1 + seg2 - 1;
    std::printf("  linked 段結尾：L gain %.2f dB、R gain %.2f dB（應幾乎相同）\n", gainAtDb(0, endLinked),
                gainAtDb(1, endLinked));
    DAWLAB_CHECK_NEAR(gainAtDb(0, endLinked), gainAtDb(1, endLinked), 0.01);
    DAWLAB_CHECK(maxStepDb[0] < 0.1f);
    DAWLAB_CHECK(maxStepDb[1] < 0.1f);

    writeWav(outputPath("l04_link_toggle.wav"), output, kSampleRate);
    std::printf("\n  已輸出 dawlab_output/l04_link_toggle.wav\n");
    return dawlab::test::summary("l04_robustness");
}
