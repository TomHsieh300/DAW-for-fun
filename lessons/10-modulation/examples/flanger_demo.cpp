// 範例 3：Flanger —— 會掃動的 comb filter，notch 頻率的量測與公式對照
//
// 示範什麼：
//   1. 把 LFO 停住（rate = 0）、固定延遲 d，量 flanger 的頻率響應，和公式對照：
//        H(f) = (1 − mix) + mix · e^{−j2πfd} / (1 − g·e^{−j2πfd})        （g = feedback）
//      沒有 feedback、mix = 0.5 時 |H(f)| = |cos(πfd)|：notch 在 f = (2k+1)/(2d)，peak 在 f = k/d。
//      (a) d = 2 ms：掃描 100–4000 Hz 自動找 notch，對照 250、750、1250 … Hz。
//      (b) d = 1 ms：在 peak / notch 頻率上比較 feedback = 0、+0.5、−0.7 三種情況的增益與理論值。
//   2. 撥弦琶音與白雜訊經過 flanger 的 WAV（噴射機聲）。
//   3. through-zero flanging：乾訊號也先延遲一個固定值，濕訊號的延遲掃過它 →
//      兩者延遲相等的瞬間完全抵消（經典磁帶 flanging 的「穿過零點」）。
//
// 怎麼執行：l10_flanger（不需要參數）
//
// 輸出檔案（dawlab_output/）：
//   l10_flanger_dry.wav           撥弦琶音
//   l10_flanger_wet.wav           rate 0.3 Hz、delay 1 ms、depth 2 ms、feedback 0.5、mix 0.5
//   l10_flanger_noise_dry.wav     白雜訊
//   l10_flanger_noise.wav         白雜訊經過 flanger（feedback 0.7）—— 最容易聽出掃動的素材
//   l10_flanger_through_zero.wav  白雜訊的 through-zero flanging（乾訊號固定延遲 2 ms，濕訊號 0.1–3.9 ms 來回掃）
//
// 該聽到什麼：wet 有「咻──」的金屬掃動；noise 版像噴射機起降；through_zero 在每次穿過零點時聲音幾乎消失一瞬。

#include <dawlab/Common.h>
#include <dawlab/dsp/Flanger.h>
#include <dawlab/dsp/FractionalDelayLine.h>
#include <dawlab/dsp/Lfo.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

using dawlab::dsp::Flanger;
using dawlab::dsp::FractionalDelayLine;
using dawlab::dsp::Lfo;
using dawlab::dsp::LfoShape;

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

// 理論頻率響應：H = (1−mix) + mix·z^{−D} / (1 − g·z^{−D})，z^{−D} = e^{−j2πfd}
double theoryGainDb(double fHz, double delaySeconds, double feedback, double mix) {
    const std::complex<double> zD = std::polar(1.0, -dawlab::kTwoPi * fHz * delaySeconds);
    const std::complex<double> h = (1.0 - mix) + mix * zD / (1.0 - feedback * zD);
    return dawlab::gainToDb(std::abs(h));
}

// 把 LFO 停住、延遲固定在 delayMs 的 flanger（單聲道，量測用）
Flanger makeFrozenFlanger(float delayMs, float feedback, float mix) {
    Flanger f;
    f.prepare(kSampleRate, kBlockSize, 1);
    f.setRateHz(0.0f);
    f.setDepthMs(2.0f);
    f.setDelayMs(delayMs);
    f.setFeedback(feedback);
    f.setMix(mix);
    f.setLfoShape(LfoShape::Sine);
    f.setLfoPhase(0.75f); // sin(2π·0.75) = −1 → 延遲停在掃動範圍的最低點 = delayMs
    f.reset();
    return f;
}

double measureFlangerDb(Flanger& f, double freq) {
    f.reset();
    return dawlab::measureGainDb([&](float x) { return f.processSample(0, x); }, freq, kSampleRate, 0.4, 0.1);
}

} // namespace

int main() {
    std::printf("=== 第 10 章 範例 3：Flanger ===\n\n");

    // ---- 1a. d = 2 ms：掃描找 notch ------------------------------------------
    {
        const float d = 2.0f;
        Flanger f = makeFrozenFlanger(d, 0.0f, 0.5f);
        // 先跑一個 sample 讓 getCurrentDelayMs 更新，確認 LFO 真的停在最低點
        f.processSample(0, 0.0f);
        std::printf("LFO 停住後的實際延遲：%.3f ms（設定 delay %.1f ms、depth 2 ms、LFO 相位 0.75 → 最低點）\n",
                    f.getCurrentDelayMs(), d);

        std::vector<double> freqs, gains;
        for (double fr = 100.0; fr <= 4000.0; fr += 25.0) {
            freqs.push_back(fr);
            gains.push_back(measureFlangerDb(f, fr));
        }
        std::printf("掃描 100–4000 Hz（每 25 Hz）找到的 notch，對照 f_k = (2k+1)/(2d)，d = %.0f ms：\n", d);
        std::printf("   k   量測 notch(Hz)   gain(dB)   理論(Hz)\n");
        int k = 0;
        for (size_t i = 1; i + 1 < gains.size(); ++i) {
            if (gains[i] < gains[i - 1] && gains[i] <= gains[i + 1] && gains[i] < -10.0) {
                std::printf("  %2d   %10.0f       %7.1f   %8.0f\n", k, freqs[i], gains[i],
                            Flanger::notchFrequencyHz(k, d));
                ++k;
            }
        }
        std::printf("  -> notch 等間距、間距 = 1/d = %.0f Hz；peak 在 k/d = 0、500、1000 … Hz。\n\n", 1000.0 / d);
    }

    // ---- 1b. d = 1 ms：feedback 對 peak / notch 的影響 --------------------------
    {
        const float d = 1.0f;
        const double probes[] = {500.0, 1000.0, 1500.0, 2000.0, 2500.0, 3000.0};
        const float feedbacks[] = {0.0f, 0.5f, -0.7f};
        std::printf("d = %.0f ms、mix 0.5：各頻率的增益（量測 / 理論，dB）\n", d);
        std::printf("   freq(Hz)  位置   ");
        for (float g : feedbacks)
            std::printf("     fb %+.1f       ", g);
        std::printf("\n");
        for (double fr : probes) {
            const bool isPeak = std::fmod(fr, 1000.0) < 1.0;
            std::printf("   %7.0f   %-6s", fr, isPeak ? "peak" : "notch");
            for (float g : feedbacks) {
                Flanger f = makeFrozenFlanger(d, g, 0.5f);
                const double meas = measureFlangerDb(f, fr);
                const double theo = theoryGainDb(fr, d * 0.001, g, 0.5);
                std::printf("  %+7.1f / %+7.1f", meas, theo);
            }
            std::printf("\n");
        }
        std::printf("  -> feedback 0：notch 深不見底（理論 −∞）、peak 0 dB。\n"
                    "     feedback +0.5：peak 變成 0.5 + 0.5/(1−g) = 1.5 → +3.5 dB、notch 只剩 0.5 − 0.5/(1+g) → −15.6 dB。\n"
                    "     feedback −0.7：原本 notch 的位置變成 0.5 − 0.5/(1−0.7) = −1.17 → +1.3 dB 的 peak，\n"
                    "     原本的 peak 變成 0.5 + 0.5/1.7 = 0.79 → −2.0 dB —— 整排峰谷對調，聲音「空心」。\n\n");
    }

    // ---- 2. 聽：撥弦琶音與白雜訊 --------------------------------------------
    {
        dawlab::AudioBuffer pluck = dawlab::makePluckSequence(kSampleRate, 4.0, 0.4f, 2);
        dawlab::writeWav(dawlab::outputPath("l10_flanger_dry.wav"), pluck, kSampleRate);
        std::printf("輸出 WAV：\n  l10_flanger_dry.wav          peak %.3f\n", dawlab::peakAbs(pluck));

        Flanger f;
        f.prepare(kSampleRate, kBlockSize, 2);
        f.setRateHz(0.3f);
        f.setDelayMs(1.0f);
        f.setDepthMs(2.0f);
        f.setFeedback(0.5f);
        f.setMix(0.5f);
        f.reset();
        processInBlocks(f, pluck);
        dawlab::writeWav(dawlab::outputPath("l10_flanger_wet.wav"), pluck, kSampleRate);
        std::printf("  l10_flanger_wet.wav          peak %.3f\n", dawlab::peakAbs(pluck));

        dawlab::AudioBuffer noise = dawlab::makeWhiteNoise(kSampleRate, 6.0, 0.2f, 2, 3u);
        dawlab::writeWav(dawlab::outputPath("l10_flanger_noise_dry.wav"), noise, kSampleRate);
        f.setRateHz(0.2f);
        f.setDelayMs(0.5f);
        f.setDepthMs(4.0f);
        f.setFeedback(0.7f);
        f.reset();
        processInBlocks(f, noise);
        dawlab::writeWav(dawlab::outputPath("l10_flanger_noise.wav"), noise, kSampleRate);
        std::printf("  l10_flanger_noise_dry.wav    peak %.3f\n  l10_flanger_noise.wav        peak %.3f\n",
                    0.2f, dawlab::peakAbs(noise));
    }

    // ---- 3. through-zero flanging --------------------------------------------
    {
        // 乾訊號固定延遲 dryMs；濕訊號延遲 = dryMs + sweep·sin(…)，所以會在 dryMs 的兩側來回「穿過」乾訊號。
        // 濕訊號反相：延遲相等的瞬間 x[n−d] − x[n−d] = 0 → 完全抵消；離開零點後是 notch 間距極寬的 comb。
        const float dryMs = 2.0f, sweepMs = 1.9f;
        // 回傳輸出，並順便記錄「最安靜的 block」的 RMS
        auto renderThroughZero = [&](const dawlab::AudioBuffer& in, float& minBlockRms) {
            dawlab::AudioBuffer out(1, in.getNumSamples());
            FractionalDelayLine line;
            line.prepare(dawlab::msToSamples(dryMs + sweepMs, kSampleRate) + 8, 1);
            Lfo lfo;
            lfo.prepare(kSampleRate);
            lfo.setRateHz(0.25f);
            lfo.setShape(LfoShape::Triangle);
            lfo.reset();
            const float* src = in.getReadPointer(0);
            float* dst = out.getWritePointer(0);
            const float toSamples = static_cast<float>(kSampleRate * 0.001);
            minBlockRms = 1.0f;
            for (int start = 0; start < in.getNumSamples(); start += kBlockSize) {
                const int n = std::min(kBlockSize, in.getNumSamples() - start);
                for (int i = 0; i < n; ++i) {
                    line.push(0, src[start + i]);
                    const float wetMs = dryMs + sweepMs * lfo.getNextValue();
                    const float dry = line.readCubic(0, dryMs * toSamples);
                    const float wet = line.readCubic(0, wetMs * toSamples);
                    dst[start + i] = 0.5f * (dry - wet); // 濕訊號反相
                }
                if (start > dawlab::secondsToSamples(0.5, kSampleRate))
                    minBlockRms = std::min(minBlockRms, dawlab::rms(dst + start, n));
            }
            return out;
        };

        float minRms = 0.0f;
        dawlab::AudioBuffer noise = dawlab::makeWhiteNoise(kSampleRate, 6.0, 0.25f, 1, 5u);
        dawlab::AudioBuffer out = renderThroughZero(noise, minRms);
        dawlab::writeWav(dawlab::outputPath("l10_flanger_through_zero.wav"), out, kSampleRate);
        std::printf("  l10_flanger_through_zero.wav peak %.3f\n", dawlab::peakAbs(out));

        // 數字驗證：500 Hz 正弦穿過零點時，延遲差 δ → 0，|H| = 2|sin(πfδ/fs)| → 0
        const dawlab::AudioBuffer tone = dawlab::makeSine(500.0, kSampleRate, 6.0, 0.5f, 1);
        renderThroughZero(tone, minRms);
        const float refRms = 0.5f * dawlab::rms(tone.getReadPointer(0), tone.getNumSamples()); // 乾訊號本身 ×0.5
        std::printf("  through-zero 的 500 Hz 正弦：最安靜的 block 相對乾訊號 %+.1f dB（正常 comb 的 notch 之外不會這麼安靜）\n"
                    "  -> 延遲差穿過 0 的瞬間所有頻率同時抵消；白雜訊版聽起來是「整個聲音被吸走再吐回來」。\n",
                    dawlab::gainToDb(minRms / refRms));
    }
    return 0;
}
