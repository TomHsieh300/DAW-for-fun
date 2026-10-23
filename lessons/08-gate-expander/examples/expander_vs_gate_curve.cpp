// 範例 2：動態處理家族的 transfer curve 對照表，以及 expander / gate 的實測
//
// 示範什麼：
//   - 用靜態 gain computer 印出 compressor（4:1）、limiter（∞:1）、expander（1:2）、gate 四條 transfer curve
//     （輸入 -80..0 dB，每 5 dB 一列），看它們「對 threshold 以上 / 以下做什麼」的差別
//   - expander 的 soft knee（knee 0 / 6 / 12 dB）在 threshold 附近的差異
//   - 把一串不同音量的正弦波段落（tone bursts）餵給 Expander 與 Gate，量每一段穩定後的輸出位準，
//     驗證它們真的落在靜態曲線上（這也是解答常用的驗證手法）
// 怎麼執行：l08_expander_vs_gate_curve（不吃參數）。
// 輸出檔案：dawlab_output/l08_bursts_input.wav、l08_bursts_expander.wav、l08_bursts_gate.wav。
// 該聽到什麼：bursts 由 -20 / -40 / -50 / -60 / -20 dBFS 五段組成。expander 版本安靜的段落更安靜但還聽得到；
//            gate 版本 -40 dB 以下的段落直接消失，而且每段開頭有 1 ms 的 fade-in、結尾有 release 的 fade-out。

#include <dawlab/Common.h>
#include <dawlab/dsp/Compressor.h>
#include <dawlab/dsp/Expander.h>
#include <dawlab/dsp/Gate.h>

#include <cstdio>
#include <vector>

using namespace dawlab;
using dawlab::dsp::Compressor;
using dawlab::dsp::Expander;
using dawlab::dsp::Gate;

namespace {

constexpr int kBlockSize = 512;

// gate 的靜態曲線：threshold 以上不動，以下衰減 |range|（實際上還有 hysteresis 與時間行為）
float gateOutputDb(float inputDb, float thresholdDb, float rangeDb) {
    return inputDb >= thresholdDb ? inputDb : inputDb + rangeDb;
}

template <typename Processor>
AudioBuffer runBlocks(const AudioBuffer& input, Processor& p) {
    AudioBuffer out = input;
    AudioBlock whole(out);
    const int n = out.getNumSamples();
    for (int start = 0; start < n; start += kBlockSize)
        p.process(whole.getSubBlock(start, std::min(kBlockSize, n - start)));
    return out;
}

// 量某一段落「後半段」（穩定後）的正弦振幅 → dB
double segmentLevelDb(const AudioBuffer& b, int segment, int segLen, double freq, double sampleRate) {
    const int start = segment * segLen + segLen / 2;
    const int n = segLen / 2;
    return gainToDb(goertzelAmplitude(b.getReadPointer(0) + start, n, freq, sampleRate));
}

} // namespace

int main() {
    std::printf("第 08 章 範例 2：transfer curve 對照表與 expander / gate 實測\n");

    // ---- 1. 四種處理器的靜態曲線 ---------------------------------------------
    std::printf("\n[1] transfer curve（輸出 dB）。compressor 4:1 T=-20；limiter ∞:1 T=-10；"
                "expander 1:2 T=-40 range -40；gate T=-40 range -80\n");
    std::printf("    輸入     compressor   limiter   expander      gate\n");
    for (int in = -80; in <= 0; in += 5) {
        const float x = static_cast<float>(in);
        const float comp = Compressor::computeOutputDb(x, -20.0f, 4.0f, 0.0f);
        const float lim = Compressor::computeOutputDb(x, -10.0f, 1.0e9f, 0.0f);
        const float exp = std::max(x - 40.0f, Expander::computeOutputDb(x, -40.0f, 2.0f, 0.0f));
        const float gate = gateOutputDb(x, -40.0f, -80.0f);
        std::printf("    %5d  %10.1f  %9.1f  %9.1f  %9.1f\n", in, comp, lim, exp, gate);
    }

    // ---- 2. expander soft knee ---------------------------------------------
    std::printf("\n[2] expander 1:2、T = -40 dB 的 soft knee：knee 寬度 0 / 6 / 12 dB（輸出 dB）\n");
    std::printf("    輸入    knee 0   knee 6   knee 12\n");
    for (int in = -50; in <= -30; in += 2) {
        const float x = static_cast<float>(in);
        std::printf("    %5d  %7.2f  %7.2f  %7.2f\n", in, Expander::computeOutputDb(x, -40.0f, 2.0f, 0.0f),
                    Expander::computeOutputDb(x, -40.0f, 2.0f, 6.0f), Expander::computeOutputDb(x, -40.0f, 2.0f, 12.0f));
    }
    std::printf("    knee 邊界（T ± W/2）處三條曲線的值與斜率都連續：knee 12 在 -46 dB = %.2f（硬 knee：%.2f）\n",
                Expander::computeOutputDb(-46.0f, -40.0f, 2.0f, 12.0f), Expander::computeOutputDb(-46.0f, -40.0f, 2.0f, 0.0f));

    // ---- 3. 實測：tone bursts ------------------------------------------------
    const double sampleRate = 48000.0;
    const double freq = 1000.0;
    const std::vector<float> levels = {-20.0f, -40.0f, -50.0f, -60.0f, -20.0f};
    const double segSec = 0.5;
    const AudioBuffer input = makeToneBursts(freq, sampleRate, levels, segSec);
    const int segLen = secondsToSamples(segSec, sampleRate);

    Expander expander;
    expander.prepare(sampleRate, kBlockSize, 2);
    expander.setThresholdDb(-40.0f);
    expander.setRatio(2.0f);
    expander.setKneeDb(0.0f);
    expander.setRangeDb(-40.0f);
    expander.setAttackMs(5.0f);
    expander.setReleaseMs(50.0f);

    Gate gate;
    gate.prepare(sampleRate, kBlockSize, 2);
    gate.setThresholdDb(-40.0f);
    gate.setHysteresisDb(4.0f);
    gate.setRangeDb(-80.0f);
    gate.setAttackMs(1.0f);
    gate.setHoldMs(10.0f);
    gate.setReleaseMs(50.0f);

    const AudioBuffer outExp = runBlocks(input, expander);
    const AudioBuffer outGate = runBlocks(input, gate);
    writeWav(outputPath("l08_bursts_input.wav"), input, sampleRate);
    writeWav(outputPath("l08_bursts_expander.wav"), outExp, sampleRate);
    writeWav(outputPath("l08_bursts_gate.wav"), outGate, sampleRate);

    std::printf("\n[3] tone bursts 實測（每段後半段的 1 kHz 振幅，dB）。"
                "正弦波的 peak envelope 比 RMS 高 3 dB，靜態曲線以 peak 計\n");
    std::printf("    段落  輸入    expander 實測 / 理論    gate 實測 / 理論\n");
    for (size_t s = 0; s < levels.size(); ++s) {
        const double in = segmentLevelDb(input, static_cast<int>(s), segLen, freq, sampleRate);
        const double e = segmentLevelDb(outExp, static_cast<int>(s), segLen, freq, sampleRate);
        const double g = segmentLevelDb(outGate, static_cast<int>(s), segLen, freq, sampleRate);
        const float expTheory = std::max(levels[s] - 40.0f, Expander::computeOutputDb(levels[s], -40.0f, 2.0f, 0.0f));
        const float gateTheory = gateOutputDb(levels[s], -40.0f, -80.0f);
        std::printf("    %4zu  %6.1f  %8.1f / %6.1f         %8.1f / %6.1f\n", s + 1, in, e, expTheory, g,
                    gateTheory);
    }
    std::printf("    （gate 的實測值被 gainToDb() 的下限 -100 dB 截住；理論值 -130 / -140 dB 早已低於可聽範圍，只要「遠低於 -60 dB」就算關上了）\n");
    std::printf("\n輸出：dawlab_output/l08_bursts_input.wav、l08_bursts_expander.wav、l08_bursts_gate.wav\n");
    return 0;
}
