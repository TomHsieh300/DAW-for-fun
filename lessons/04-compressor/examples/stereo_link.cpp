// 範例 4：stereo link —— 有 link 與沒 link 對立體聲像的影響。
//
// 示範什麼：
//   - 立體聲訊號左聲道很大聲（-6 dBFS）、右聲道很小聲（-30 dBFS）
//   - 沒有 stereo link：左右各自壓，只有左邊被壓 → 左右音量差縮小 → 聲像往右「漂」
//   - 有 stereo link：兩邊共用同一個（由最大 level 決定的）gain → 左右音量差保持不變
//   - 也示範外部 sidechain：用 mono sidechain 控制 stereo 主訊號
// 怎麼執行：直接執行 l04_stereo_link。
// 輸出檔案：dawlab_output/l04_stereo_unlinked.wav、l04_stereo_linked.wav。
// 該聽到什麼：unlinked 版本的聲音會往右偏、聲像變窄；linked 版本只是整體變小聲。

#include <dawlab/Common.h>
#include <dawlab/dsp/Compressor.h>

#include <cstdio>

using namespace dawlab;
using dawlab::dsp::Compressor;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

AudioBuffer processBlocks(const AudioBuffer& input, Compressor& comp) {
    AudioBuffer out = input;
    AudioBlock whole(out);
    for (int start = 0; start < out.getNumSamples(); start += kBlockSize)
        comp.process(whole.getSubBlock(start, std::min(kBlockSize, out.getNumSamples() - start)));
    return out;
}

// 只量後半段（穩態）
float steadyLevelDb(const AudioBuffer& b, int ch) {
    const int n = b.getNumSamples();
    return gainToDb(peakAbs(b.getReadPointer(ch) + n / 2, n - n / 2));
}

} // namespace

int main() {
    std::printf("第 04 章 範例 4：stereo link\n");

    // 左 -6 dBFS、右 -30 dBFS 的 1 kHz 正弦波，1 秒
    AudioBuffer input = makeSine(1000.0, kSampleRate, 1.0, 1.0f, 2);
    for (int i = 0; i < input.getNumSamples(); ++i) {
        input.setSample(0, i, input.getSample(0, i) * dbToGain(-6.0f));
        input.setSample(1, i, input.getSample(1, i) * dbToGain(-30.0f));
    }
    std::printf("  輸入：L %.2f dBFS, R %.2f dBFS，L-R 差 %.2f dB\n", steadyLevelDb(input, 0), steadyLevelDb(input, 1),
                steadyLevelDb(input, 0) - steadyLevelDb(input, 1));
    std::printf("  Compressor：T -20 dB, R 4:1, hard knee, attack 5 ms, release 50 ms\n");

    auto makeComp = [](bool link) {
        Compressor c;
        c.prepare(kSampleRate, kBlockSize, 2);
        c.setThresholdDb(-20.0f);
        c.setRatio(4.0f);
        c.setKneeDb(0.0f);
        c.setAttackMs(5.0f);
        c.setReleaseMs(50.0f);
        c.setStereoLink(link);
        return c;
    };

    for (bool link : {false, true}) {
        Compressor comp = makeComp(link);
        AudioBuffer out = processBlocks(input, comp);
        const float l = steadyLevelDb(out, 0), r = steadyLevelDb(out, 1);
        std::printf("\n  stereo link = %s\n", link ? "ON" : "OFF");
        std::printf("    輸出：L %.2f dBFS, R %.2f dBFS，L-R 差 %.2f dB（輸入是 24.00 dB）\n", l, r, l - r);
        std::printf("    GR：%.2f dB（L 的靜態 GR 應為 (−6 − (−20))·(1 − 1/4) = 10.5 dB）\n", comp.getGainReductionDb());
        std::printf("    → %s\n", link ? "兩邊都被壓 10.5 dB，左右差維持 24 dB，聲像位置不變。"
                                       : "只有 L 被壓，R 完全沒動，左右差從 24 dB 縮成 13.5 dB，聲像往右漂。");
        writeWav(outputPath(link ? "l04_stereo_linked.wav" : "l04_stereo_unlinked.wav"), out, kSampleRate);
    }

    // 外部 sidechain：用一個 mono 的 -10 dBFS 正弦波當 sidechain 去壓 stereo 主訊號
    {
        Compressor comp = makeComp(true);
        AudioBuffer sidechain = makeSine(1000.0, kSampleRate, 1.0, dbToGain(-10.0f), 1);
        AudioBuffer out = input;
        AudioBlock whole(out), sc(sidechain);
        for (int start = 0; start < out.getNumSamples(); start += kBlockSize) {
            const int len = std::min(kBlockSize, out.getNumSamples() - start);
            comp.process(whole.getSubBlock(start, len), sc.getSubBlock(start, len));
        }
        std::printf("\n  外部 sidechain（mono -10 dBFS）控制 stereo 主訊號：\n");
        std::printf("    GR = %.2f dB（由 sidechain 決定：(−10 − (−20))·0.75 = 7.5 dB），輸出 L %.2f / R %.2f dBFS\n",
                    comp.getGainReductionDb(), steadyLevelDb(out, 0), steadyLevelDb(out, 1));
    }
    std::printf("\n  已輸出 dawlab_output/l04_stereo_unlinked.wav、l04_stereo_linked.wav\n");
    return 0;
}
