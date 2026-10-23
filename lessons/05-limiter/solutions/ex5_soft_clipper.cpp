// 練習 5 解答：soft clipper，並與 limiter 比較
//
// 兩種「不讓訊號超過上限」的方法：
//   clipper  直接對每個 sample 套一條 waveshaping 曲線（無狀態、零延遲），超過的部分被「折彎」；
//            代價是波形改變 → 產生諧波（失真）。
//   limiter  改變「增益」而不改變「波形」：一段時間內的所有 sample 乘上同一個 gain，
//            穩態的正弦波進去、出來還是正弦波；代價是需要 lookahead（延遲）與 release 的 pumping。
//
// 本題的 soft clipper（threshold t ∈ (0, 1)）：
//   |x| ≤ t：     y = x                                   （線性區，完全不動）
//   |x| > t：     y = sign(x) · [ t + (1 - t) · tanh((|x| - t) / (1 - t)) ]
//   tanh 在 0 的斜率是 1，所以曲線在 t 處連續、斜率也連續（C¹），不會有「折角」造成的高次諧波；
//   tanh ≤ 1 所以 |y| ≤ t + (1 - t) = 1，永遠不會超過 0 dBFS（float 下極大輸入會飽和到剛好 1.0）。
//
// 執行：l05_ex5_solution
// 輸出：dawlab_output/l05_ex5_sine_clipped.wav、l05_ex5_sine_limited.wav、
//       l05_ex5_drums_clipped.wav、l05_ex5_drums_limited.wav

#include <dawlab/Common.h>
#include <dawlab/dsp/Limiter.h>

#include <cmath>
#include <cstdio>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

float softClip(float x, float threshold) noexcept {
    const float t = std::clamp(threshold, 1.0e-3f, 0.999f);
    const float a = std::abs(x);
    if (a <= t)
        return x;
    const float over = (a - t) / (1.0f - t);
    const float y = t + (1.0f - t) * std::tanh(over);
    return std::copysign(y, x);
}

void clipBuffer(dawlab::AudioBuffer& b, float threshold) {
    for (int ch = 0; ch < b.getNumChannels(); ++ch) {
        float* d = b.getWritePointer(ch);
        for (int i = 0; i < b.getNumSamples(); ++i)
            d[i] = softClip(d[i], threshold);
    }
}

template <typename Processor>
void processInBlocks(dawlab::AudioBuffer& buffer, Processor& processor) {
    dawlab::AudioBlock whole(buffer);
    for (int start = 0; start < buffer.getNumSamples(); start += kBlockSize)
        processor.process(whole.getSubBlock(start, std::min(kBlockSize, buffer.getNumSamples() - start)));
}

float peakDb(const dawlab::AudioBuffer& b) { return dawlab::gainToDb(dawlab::peakAbs(b)); }
float rmsDb(const dawlab::AudioBuffer& b) { return dawlab::gainToDb(dawlab::rms(b, 0)); }

} // namespace

int main() {
    // ---- (1) 曲線本身的性質 ----
    {
        const float t = 0.5f;
        DAWLAB_CHECK(softClip(0.3f, t) == 0.3f);   // 線性區不動
        DAWLAB_CHECK(softClip(-0.5f, t) == -0.5f); // 邊界也不動
        DAWLAB_CHECK(std::abs(softClip(100.0f, t)) <= 1.0f); // 永遠不超過 1（float 下 tanh 會飽和到剛好 1.0）
        DAWLAB_CHECK(std::abs(softClip(1.0e9f, t)) <= 1.0f);
        DAWLAB_CHECK_NEAR(softClip(1.0e9f, t), 1.0, 1.0e-5); // 但會逼近 1
        DAWLAB_CHECK(softClip(-2.0f, t) == -softClip(2.0f, t)); // 奇對稱
        // 單調遞增 + 在 t 處斜率連續（數值微分 ≈ 1）
        bool monotonic = true;
        float prev = softClip(-3.0f, t);
        for (float x = -3.0f + 0.001f; x <= 3.0f; x += 0.001f) {
            const float y = softClip(x, t);
            monotonic = monotonic && (y >= prev);
            prev = y;
        }
        DAWLAB_CHECK(monotonic);
        const float h = 1.0e-3f;
        const float slopeBelow = (softClip(t, t) - softClip(t - h, t)) / h;
        const float slopeAbove = (softClip(t + h, t) - softClip(t, t)) / h;
        DAWLAB_CHECK_NEAR(slopeBelow, 1.0, 1.0e-3);
        DAWLAB_CHECK_NEAR(slopeAbove, 1.0, 1.0e-2);
        std::printf("softClip(x, 0.5)：x=0.6 → %.4f，x=1.0 → %.4f，x=2.0 → %.4f，x=10 → %.4f\n", softClip(0.6f, t),
                    softClip(1.0f, t), softClip(2.0f, t), softClip(10.0f, t));
    }

    // ---- (2) 正弦波 +6 dB 過 0 dBFS：clipper 產生諧波、limiter 不會 ----
    {
        const double freq = 1000.0;
        dawlab::AudioBuffer sine = dawlab::makeSine(freq, kSampleRate, 1.0, dawlab::dbToGain(6.0f), 1); // 峰值 2.0

        dawlab::AudioBuffer clipped = sine;
        clipBuffer(clipped, 0.5f);

        dawlab::AudioBuffer limited = sine;
        dawlab::dsp::Limiter limiter;
        limiter.prepare(kSampleRate, kBlockSize, 1);
        limiter.setCeilingDb(-0.5f); // 跟 clipper 的輸出 peak 差不多
        limiter.setLookaheadMs(5.0f);
        limiter.setReleaseMs(100.0f);
        processInBlocks(limited, limiter);

        // 跳過前 0.2 秒（limiter 的 attack / 穩定期），量後面 0.5 秒的 THD
        const int skip = dawlab::secondsToSamples(0.2, kSampleRate);
        const int n = dawlab::secondsToSamples(0.5, kSampleRate);
        const double thdClip = dawlab::thdPercent(clipped.getReadPointer(0) + skip, n, freq, kSampleRate);
        const double thdLim = dawlab::thdPercent(limited.getReadPointer(0) + skip, n, freq, kSampleRate);
        const auto harmonics = dawlab::harmonicLevelsDb(clipped.getReadPointer(0) + skip, n, freq, kSampleRate, 5);

        std::printf("\n1 kHz sine +6 dBFS（peak 2.0）：\n");
        std::printf("  soft clip (t=0.5)：peak %6.2f dBFS，THD %6.2f %%，諧波 H2 %.1f / H3 %.1f / H4 %.1f / H5 %.1f dB\n",
                    peakDb(clipped), thdClip, harmonics[1], harmonics[2], harmonics[3], harmonics[4]);
        std::printf("  limiter (-0.5 dB)：peak %6.2f dBFS，THD %6.4f %%\n", peakDb(limited), thdLim);
        DAWLAB_CHECK(dawlab::peakAbs(clipped) <= 1.0f);
        DAWLAB_CHECK(dawlab::peakAbs(limited) <= dawlab::dbToGain(-0.5f));
        DAWLAB_CHECK(thdClip > 5.0);          // 明顯失真
        DAWLAB_CHECK(thdLim < 0.1);           // 穩態下 gain 固定 → 波形不變
        DAWLAB_CHECK(harmonics[1] < -40.0);   // 奇對稱曲線：偶次諧波（H2）幾乎為 0
        DAWLAB_CHECK(harmonics[2] > -30.0);   // 奇次諧波（H3）明顯
        dawlab::writeWav(dawlab::outputPath("l05_ex5_sine_clipped.wav"), clipped, kSampleRate);
        dawlab::writeWav(dawlab::outputPath("l05_ex5_sine_limited.wav"), limited, kSampleRate);
    }

    // ---- (3) drum loop +12 dB：兩者都能把 peak 壓住，聽感與 RMS 不同 ----
    {
        dawlab::AudioBuffer dry = dawlab::makeDrumLoop(kSampleRate, 2);
        dawlab::AudioBuffer hot = dry;
        hot.applyGain(dawlab::dbToGain(12.0f));

        dawlab::AudioBuffer clipped = hot;
        clipBuffer(clipped, 0.6f);

        dawlab::AudioBuffer limited = dry;
        dawlab::dsp::Limiter limiter;
        limiter.prepare(kSampleRate, kBlockSize, 2);
        limiter.setCeilingDb(-0.1f);
        limiter.setInputGainDb(12.0f);
        limiter.setLookaheadMs(5.0f);
        limiter.setReleaseMs(80.0f);
        limiter.reset();
        processInBlocks(limited, limiter);

        std::printf("\ndrum loop +12 dB（輸入 peak %.2f dBFS）：\n", peakDb(hot));
        std::printf("  %-20s peak %6.2f dBFS  RMS %6.2f dBFS  crest %5.2f dB\n", "dry", peakDb(dry), rmsDb(dry),
                    peakDb(dry) - rmsDb(dry));
        std::printf("  %-20s peak %6.2f dBFS  RMS %6.2f dBFS  crest %5.2f dB\n", "soft clip (t=0.6)", peakDb(clipped),
                    rmsDb(clipped), peakDb(clipped) - rmsDb(clipped));
        std::printf("  %-20s peak %6.2f dBFS  RMS %6.2f dBFS  crest %5.2f dB\n", "limiter (-0.1 dB)", peakDb(limited),
                    rmsDb(limited), peakDb(limited) - rmsDb(limited));
        DAWLAB_CHECK(dawlab::peakAbs(clipped) <= 1.0f);
        DAWLAB_CHECK(dawlab::peakAbs(limited) <= dawlab::dbToGain(-0.1f));
        DAWLAB_CHECK(rmsDb(clipped) > rmsDb(dry) + 6.0f); // 兩者都明顯變大聲
        DAWLAB_CHECK(rmsDb(limited) > rmsDb(dry) + 6.0f);
        dawlab::writeWav(dawlab::outputPath("l05_ex5_drums_clipped.wav"), clipped, kSampleRate);
        dawlab::writeWav(dawlab::outputPath("l05_ex5_drums_limited.wav"), limited, kSampleRate);
        std::printf("  聽聽看：clipped 的 kick / snare 多了「沙沙」的諧波；limited 比較乾淨但 transient 被壓鈍、會輕微 pumping\n");
    }

    return dawlab::test::summary("l05_ex5");
}
