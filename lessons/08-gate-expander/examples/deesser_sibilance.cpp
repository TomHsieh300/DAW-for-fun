// 範例 3：de-esser 處理「母音 + 7 kHz sibilance burst」的合成人聲
//
// 示範什麼：
//   - 合成測試訊號：220 Hz 的「母音」（諧波 1..12 次、振幅 1/k 遞減，RMS 約 -19 dBFS）持續 3 秒，
//     在 0.5、1.5、2.5 秒各疊上一段 120 ms 的「齒音」（白雜訊經 7 kHz bandpass，peak 約 -12 dBFS）
//   - 用 DeEsser（frequency 7 kHz、threshold -30 dB、range 12 dB）跑 wideband 與 split-band 兩種模式，
//     逐 block 讀 getGainReductionDb() 印出 gain reduction 時間軸
//   - 用 Goertzel 量齒音期間 220 Hz 母音的位準：wideband 模式母音跟著被壓；split-band 模式母音不受影響
//   - listen 模式：輸出 bandpass 後的偵測訊號，調 frequency / threshold 時就是聽這個
// 怎麼執行：l08_deesser_sibilance（不吃參數）。
// 輸出檔案：dawlab_output/l08_deesser_input.wav、l08_deesser_wideband.wav、l08_deesser_splitband.wav、
//          l08_deesser_listen.wav。
// 該聽到什麼：input 每 1 秒有一聲刺耳的「嘶」；wideband 版嘶聲變小但母音在那一瞬間也跟著縮（吸氣感）；
//            split-band 版只有嘶聲變小、母音不動；listen 版只剩下嘶聲（與一點點母音的高次諧波）。

#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>
#include <dawlab/dsp/DeEsser.h>

#include <cstdio>
#include <vector>

using namespace dawlab;
using dawlab::dsp::Biquad;
using dawlab::dsp::BiquadCoefficients;
using dawlab::dsp::DeEsser;

namespace {

constexpr int kBlockSize = 512;
constexpr double kSampleRate = 48000.0;
constexpr double kVowelHz = 220.0;
constexpr double kSibilanceHz = 7000.0;

// 母音：諧波疊加，振幅 1/k（像鋸齒波），用 low-pass 的方式只留到 12 次（2640 Hz）
AudioBuffer makeVowel(double seconds, float amplitude) {
    AudioBuffer b(2, secondsToSamples(seconds, kSampleRate));
    for (int i = 0; i < b.getNumSamples(); ++i) {
        double v = 0.0;
        for (int k = 1; k <= 12; ++k)
            v += std::sin(kTwoPi * kVowelHz * k * i / kSampleRate) / k;
        const auto s = static_cast<float>(amplitude * v / 1.6); // 1.6 ≈ 諧波和的 peak，正規化到 ±amplitude
        b.setSample(0, i, s);
        b.setSample(1, i, s);
    }
    return b;
}

// 齒音：白雜訊 → 7 kHz bandpass（Q = 1.5），套上 10 ms fade in / 30 ms fade out 的 envelope
void addSibilance(AudioBuffer& target, double startSec, double lengthSec, float peakAmplitude) {
    const int start = secondsToSamples(startSec, kSampleRate);
    const int len = secondsToSamples(lengthSec, kSampleRate);
    Biquad bp;
    bp.prepare(kSampleRate, kBlockSize, 1);
    bp.setCoefficients(BiquadCoefficients::makeBandpass(kSampleRate, kSibilanceHz, 1.5));
    NoiseGenerator noise(777);
    std::vector<float> burst(static_cast<size_t>(len));
    float peak = 0.0f;
    for (int i = 0; i < len; ++i) {
        const double t = i / kSampleRate;
        const double env = std::min(1.0, t / 0.010) * std::min(1.0, (lengthSec - t) / 0.030);
        burst[static_cast<size_t>(i)] = static_cast<float>(env * bp.processSample(0, noise.next()));
        peak = std::max(peak, std::abs(burst[static_cast<size_t>(i)]));
    }
    for (int i = 0; i < len && start + i < target.getNumSamples(); ++i)
        for (int ch = 0; ch < target.getNumChannels(); ++ch)
            target.addSample(ch, start + i, burst[static_cast<size_t>(i)] * peakAmplitude / peak);
}

AudioBuffer runDeEsser(const AudioBuffer& input, DeEsser& de, std::vector<float>& grPerBlock) {
    AudioBuffer out = input;
    AudioBlock whole(out);
    const int n = out.getNumSamples();
    grPerBlock.clear();
    for (int start = 0; start < n; start += kBlockSize) {
        de.process(whole.getSubBlock(start, std::min(kBlockSize, n - start)));
        grPerBlock.push_back(de.getGainReductionDb());
    }
    return out;
}

void printTimeline(const char* title, const std::vector<float>& gr, double fromSec, double toSec) {
    std::printf("    %s\n    時間(ms)  GR(dB)  meter（每格 0.5 dB）\n", title);
    const double blockSec = kBlockSize / kSampleRate;
    for (size_t i = 0; i < gr.size(); ++i) {
        const double t = static_cast<double>(i) * blockSec;
        if (t < fromSec || t > toSec)
            continue;
        const int bars = static_cast<int>(std::lround(gr[i] / 0.5f));
        std::printf("    %7.1f   %5.2f   %s\n", t * 1000.0, gr[i], std::string(static_cast<size_t>(bars), '|').c_str());
    }
}

double levelDb(const AudioBuffer& b, double startSec, double lengthSec, double freq) {
    const int start = secondsToSamples(startSec, kSampleRate);
    const int n = secondsToSamples(lengthSec, kSampleRate);
    return gainToDb(goertzelAmplitude(b.getReadPointer(0) + start, n, freq, kSampleRate));
}

// 在 [outside, centre] 之間二分搜尋 magnitudeDb = −3.01 dB 的頻率（bandpass 在中心是 0 dB、往外單調下降）
double findMinus3dbEdge(const BiquadCoefficients& c, double outside, double centre) {
    const double target = -10.0 * std::log10(2.0);
    double a = outside, b = centre;
    for (int it = 0; it < 60; ++it) {
        const double mid = 0.5 * (a + b);
        if (c.magnitudeDb(mid, kSampleRate) < target)
            a = mid;
        else
            b = mid;
    }
    return 0.5 * (a + b);
}

} // namespace

int main() {
    std::printf("第 08 章 範例 3：de-esser（母音 + 7 kHz sibilance burst）\n");

    AudioBuffer input = makeVowel(3.0, dbToGain(-14.0f));
    for (double t : {0.5, 1.5, 2.5})
        addSibilance(input, t, 0.120, dbToGain(-12.0f));
    writeWav(outputPath("l08_deesser_input.wav"), input, kSampleRate);
    const int vowelStart = secondsToSamples(1.0, kSampleRate);
    const int vowelLen = secondsToSamples(0.4, kSampleRate);
    std::printf("\n輸入：純母音段（1.0–1.4 s）RMS %.1f dBFS；整段 RMS %.1f dBFS、peak %.1f dBFS；"
                "齒音 burst 在 0.5 / 1.5 / 2.5 s，各 120 ms\n",
                gainToDb(rms(input.getReadPointer(0) + vowelStart, vowelLen)), gainToDb(rms(input, 0)),
                gainToDb(peakAbs(input)));

    DeEsser de;
    de.prepare(kSampleRate, kBlockSize, 2);
    de.setFrequency(7000.0f);
    de.setThresholdDb(-30.0f);
    de.setRangeDb(12.0f);
    std::printf("設定：frequency %.0f Hz、threshold %.0f dB、range %.0f dB、分頻點 %.0f Hz（= %.4f f）\n",
                de.getFrequency(), de.getThresholdDb(), de.getRangeDb(), de.getCrossoverHz(), DeEsser::kCrossoverRatio);
    // 偵測 bandpass 的 −3 dB 邊界：analog prototype 公式 vs 數位 RBJ bandpass 實測（README 2.5.2）
    const auto bp = BiquadCoefficients::makeBandpass(kSampleRate, de.getFrequency(), DeEsser::kDetectorQ);
    const double edgeLow = findMinus3dbEdge(bp, 100.0, de.getFrequency());
    const double edgeHigh = findMinus3dbEdge(bp, kSampleRate * 0.5 - 1.0, de.getFrequency());
    std::printf("偵測頻帶（−3 dB）：analog 公式 %.0f–%.0f Hz（%.3f oct）；數位 bandpass 實測 %.0f–%.0f Hz（%.3f oct）\n",
                de.getFrequency() * DeEsser::kCrossoverRatio,
                de.getFrequency() * (DeEsser::kCrossoverRatio + 1.0f / DeEsser::kDetectorQ),
                std::log2(1.0 + 1.0 / (DeEsser::kCrossoverRatio * DeEsser::kDetectorQ)), edgeLow, edgeHigh,
                std::log2(edgeHigh / edgeLow));

    std::vector<float> grWide, grSplit, grListen;
    de.setSplitBand(false);
    const AudioBuffer wide = runDeEsser(input, de, grWide);
    de.reset();
    de.setSplitBand(true);
    const AudioBuffer split = runDeEsser(input, de, grSplit);
    de.reset();
    de.setListen(true);
    const AudioBuffer listen = runDeEsser(input, de, grListen);
    writeWav(outputPath("l08_deesser_wideband.wav"), wide, kSampleRate);
    writeWav(outputPath("l08_deesser_splitband.wav"), split, kSampleRate);
    writeWav(outputPath("l08_deesser_listen.wav"), listen, kSampleRate);

    std::printf("\n[1] gain reduction 時間軸（第一個 burst 附近，每個 block = %.1f ms）\n", kBlockSize * 1000.0 / kSampleRate);
    printTimeline("wideband（split-band 的 GR 時間軸完全相同，差別只在 gain 套在哪個頻段）", grWide, 0.45, 0.72);
    float maxGr = 0.0f;
    for (float g : grWide)
        maxGr = std::max(maxGr, g);
    std::printf("    最大 GR = %.2f dB（上限 = range %.0f dB）\n", maxGr, de.getRangeDb());

    std::printf("\n[2] 母音（220 Hz）與齒音（7 kHz）的位準，dB。齒音期間取 0.52–0.60 s，純母音取 1.0–1.4 s\n");
    std::printf("    訊號         母音@純母音  母音@齒音期間  7 kHz@齒音期間\n");
    const auto row = [&](const char* name, const AudioBuffer& b) {
        std::printf("    %-12s %10.2f  %12.2f  %13.2f\n", name, levelDb(b, 1.0, 0.4, kVowelHz),
                    levelDb(b, 0.52, 0.08, kVowelHz), levelDb(b, 0.52, 0.08, kSibilanceHz));
    };
    row("input", input);
    row("wideband", wide);
    row("split-band", split);
    row("listen", listen);
    std::printf("    → wideband 把母音一起壓下去（第二欄與第一欄不同）；split-band 的母音幾乎不動，只有 7 kHz 被壓。\n");

    std::printf("\n[3] split-band 在沒有齒音時（1.0–1.4 s）輸出 = LP + HP ≈ 輸入（LR4 相加為 allpass）：\n");
    std::printf("    220 Hz：輸入 %.2f dB / 輸出 %.2f dB；1760 Hz（8 次諧波）：輸入 %.2f dB / 輸出 %.2f dB\n",
                levelDb(input, 1.0, 0.4, kVowelHz), levelDb(split, 1.0, 0.4, kVowelHz),
                levelDb(input, 1.0, 0.4, kVowelHz * 8), levelDb(split, 1.0, 0.4, kVowelHz * 8));

    std::printf("\n輸出：dawlab_output/l08_deesser_input.wav、l08_deesser_wideband.wav、l08_deesser_splitband.wav、"
                "l08_deesser_listen.wav\n");
    return 0;
}
