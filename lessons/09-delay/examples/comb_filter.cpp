// 範例 4：短 delay + feedback = comb filter（梳狀濾波器）
//
// 示範什麼：
//   1. delay D = 48 sample（1 ms @ 48 kHz）、feedback g = 0.7 的 feedback comb：
//        y[n] = x[n] + g·y[n−D]      H(f) = 1 / (1 − g·e^{−j2πfD/fs})
//      頻率響應每隔 fs/D = 1000 Hz 出現一個峰（|H| = 1/(1−g) = 3.33 → +10.46 dB），
//      峰與峰之間是谷（|H| = 1/(1+g) = 0.588 → −4.61 dB）。
//      本範例掃描 250–9750 Hz，自動找出峰谷並印出間距，驗證「週期 = fs/D」。
//   2. feedforward comb：y[n] = x[n] + g·x[n−D]，峰谷深度剛好相反（+4.61 / −10.46 dB）。
//   3. 用 Delay class（delay 1 ms、feedback 0.7、mix 1）得到一樣的結果：
//      echo 效果器轉到極短的 delay time 就是 comb filter，這也是第 12 章 reverb 的基本積木。
//
// 怎麼執行：不需要參數，直接執行 l09_comb_filter。
// 輸出檔案（dawlab_output/）：
//   l09_comb_noise_dry.wav   白雜訊
//   l09_comb_noise_1ms.wav   白雜訊經過 1 ms / feedback 0.9 comb（聽到明顯的 1 kHz 音高）
//   l09_comb_noise_10ms.wav  白雜訊經過 10 ms / feedback 0.8 comb（100 Hz 的金屬 / 管狀共鳴）
// 該聽到什麼：雜訊「被調音」了——短 delay 加 feedback 會讓輸入帶上 fs/D 的音高；
//             delay 拉長到 10 ms 以上會變成金屬質感，再長就開始像小房間的回聲。

#include <dawlab/Common.h>
#include <dawlab/dsp/Delay.h>
#include <dawlab/dsp/FractionalDelayLine.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using dawlab::dsp::Delay;
using dawlab::dsp::FractionalDelayLine;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

// feedback comb：y[n] = x[n] + g·y[n−D]。先讀（D−1，因為本 sample 還沒 push）再 push。
class FeedbackComb {
public:
    void prepare(int delaySamples, float g) {
        delay_ = delaySamples;
        g_ = g;
        line_.prepare(delaySamples + 2, 1);
    }
    float processSample(float x) noexcept {
        const float y = x + g_ * line_.readInteger(0, delay_ - 1);
        line_.push(0, y);
        return y;
    }

private:
    FractionalDelayLine line_;
    int delay_ = 1;
    float g_ = 0.0f;
};

// feedforward comb：y[n] = x[n] + g·x[n−D]
class FeedforwardComb {
public:
    void prepare(int delaySamples, float g) {
        delay_ = delaySamples;
        g_ = g;
        line_.prepare(delaySamples + 2, 1);
    }
    float processSample(float x) noexcept {
        line_.push(0, x);
        return x + g_ * line_.readInteger(0, delay_);
    }

private:
    FractionalDelayLine line_;
    int delay_ = 1;
    float g_ = 0.0f;
};

template <typename Comb>
std::vector<double> scanResponse(int delaySamples, float g, const std::vector<double>& freqs) {
    std::vector<double> out;
    for (double f : freqs) {
        Comb comb;
        comb.prepare(delaySamples, g);
        out.push_back(dawlab::measureGainDb([&](float x) { return comb.processSample(x); }, f, kSampleRate));
    }
    return out;
}

// 用 Delay class 一塊一塊處理正弦，再用 Goertzel 量增益（Delay 沒有 processSample，所以手動量）
double measureDelayGainDb(Delay& delay, double freq) {
    dawlab::AudioBuffer in = dawlab::makeSine(freq, kSampleRate, 0.7, 0.25f, 1);
    dawlab::AudioBuffer out = in;
    delay.reset();
    dawlab::AudioBlock whole(out);
    for (int start = 0; start < out.getNumSamples(); start += kBlockSize) {
        const int n = std::min(kBlockSize, out.getNumSamples() - start);
        delay.process(whole.getSubBlock(start, n));
    }
    const int skip = dawlab::secondsToSamples(0.2, kSampleRate);
    const int n = out.getNumSamples() - skip;
    const double a = dawlab::goertzelAmplitude(in.getReadPointer(0) + skip, n, freq, kSampleRate);
    const double b = dawlab::goertzelAmplitude(out.getReadPointer(0) + skip, n, freq, kSampleRate);
    return dawlab::gainToDb(b / a);
}

void renderNoiseThroughComb(const dawlab::AudioBuffer& noise, int delaySamples, float g, float outGain,
                            const char* filename) {
    dawlab::AudioBuffer out(noise.getNumChannels(), noise.getNumSamples());
    for (int ch = 0; ch < noise.getNumChannels(); ++ch) {
        FeedbackComb comb;
        comb.prepare(delaySamples, g);
        const float* src = noise.getReadPointer(ch);
        float* dst = out.getWritePointer(ch);
        for (int start = 0; start < noise.getNumSamples(); start += kBlockSize) {
            const int n = std::min(kBlockSize, noise.getNumSamples() - start);
            for (int i = 0; i < n; ++i)
                dst[start + i] = outGain * comb.processSample(src[start + i]);
        }
    }
    dawlab::writeWav(dawlab::outputPath(filename), out, kSampleRate);
    std::printf("  %s: peak %.3f\n", filename, dawlab::peakAbs(out));
}

} // namespace

int main() {
    std::printf("=== 第 09 章 範例 4：短 delay + feedback = comb filter ===\n\n");

    const int D = 48;    // 1 ms @ 48 kHz
    const float g = 0.7f;
    const double period = kSampleRate / D; // 峰與峰的間距 = fs/D = 1000 Hz
    std::printf("D = %d samples (%.2f ms), g = %.2f\n", D, dawlab::samplesToMs(D, kSampleRate), g);
    std::printf("理論：峰在 k × %.0f Hz，高度 1/(1−g) = %.2f → %+.2f dB；"
                "谷在 (k+½) × %.0f Hz，深度 1/(1+g) = %.3f → %+.2f dB\n\n",
                period, 1.0 / (1.0 - g), dawlab::gainToDb(1.0 / (1.0 - g)), period, 1.0 / (1.0 + g),
                dawlab::gainToDb(1.0 / (1.0 + g)));

    // ---- 1. 掃描 feedback comb，自動找峰谷 ----------------------------------
    std::vector<double> freqs;
    for (double f = 250.0; f <= 9750.0; f += 50.0)
        freqs.push_back(f);
    const std::vector<double> fb = scanResponse<FeedbackComb>(D, g, freqs);

    std::printf("feedback comb 掃描 250–9750 Hz（每 50 Hz）找到的局部峰 / 谷：\n");
    std::printf("   type     freq(Hz)   gain(dB)   與前一個同類的間距(Hz)\n");
    double lastPeak = -1.0, lastNotch = -1.0;
    for (size_t i = 1; i + 1 < fb.size(); ++i) {
        const bool peak = fb[i] > fb[i - 1] && fb[i] >= fb[i + 1];
        const bool notch = fb[i] < fb[i - 1] && fb[i] <= fb[i + 1];
        if (peak) {
            std::printf("   peak   %8.0f   %+7.2f   %s\n", freqs[i], fb[i],
                        lastPeak > 0 ? std::to_string(static_cast<int>(freqs[i] - lastPeak)).c_str() : "-");
            lastPeak = freqs[i];
        } else if (notch) {
            std::printf("   notch  %8.0f   %+7.2f   %s\n", freqs[i], fb[i],
                        lastNotch > 0 ? std::to_string(static_cast<int>(freqs[i] - lastNotch)).c_str() : "-");
            lastNotch = freqs[i];
        }
    }
    std::printf("  -> 間距 = fs/D = %.0f Hz；峰 ≈ %+.2f dB、谷 ≈ %+.2f dB\n\n", period,
                dawlab::gainToDb(1.0 / (1.0 - g)), dawlab::gainToDb(1.0 / (1.0 + g)));

    // ---- 2. feedforward comb 比較 -------------------------------------------
    {
        const std::vector<double> probe = {500.0, 1000.0, 1500.0, 2000.0, 2500.0, 3000.0};
        const auto ff = scanResponse<FeedforwardComb>(D, g, probe);
        const auto fbp = scanResponse<FeedbackComb>(D, g, probe);
        std::printf("feedforward (y = x + g·x[n−D]) vs feedback (y = x + g·y[n−D])：\n");
        std::printf("   freq(Hz)  feedforward  feedback\n");
        for (size_t i = 0; i < probe.size(); ++i)
            std::printf("   %7.0f   %+8.2f    %+8.2f\n", probe[i], ff[i], fbp[i]);
        std::printf("  -> feedforward：峰 1+g = %.2f (%+.2f dB)、谷 1−g = %.2f (%+.2f dB)；"
                    "feedback 的峰尖而谷淺，feedforward 相反。\n\n",
                    1.0 + g, dawlab::gainToDb(1.0 + g), 1.0 - g, dawlab::gainToDb(1.0 - g));
    }

    // ---- 3. 用 Delay class 做同一件事 --------------------------------------
    {
        Delay delay;
        delay.setMaxDelayMs(100.0f);
        delay.prepare(kSampleRate, kBlockSize, 1);
        delay.setDelayMs(1.0f); // 48 samples
        delay.setFeedback(g);
        delay.setMix(1.0f);
        delay.setLowpassHz(20000.0f); // 關閉 feedback 濾波
        delay.setHighpassHz(20.0f);
        std::printf("Delay class（delayMs = 1, feedback = %.1f, mix = 1）：\n", g);
        std::printf("   1000 Hz: %+.2f dB（理論 %+.2f）   1500 Hz: %+.2f dB（理論 %+.2f）\n\n",
                    measureDelayGainDb(delay, 1000.0), dawlab::gainToDb(1.0 / (1.0 - g)),
                    measureDelayGainDb(delay, 1500.0), dawlab::gainToDb(1.0 / (1.0 + g)));
    }

    // ---- 4. 聽聽看：白雜訊被 comb「調音」--------------------------------------
    {
        dawlab::AudioBuffer noise = dawlab::makeWhiteNoise(kSampleRate, 3.0, 0.2f);
        dawlab::writeWav(dawlab::outputPath("l09_comb_noise_dry.wav"), noise, kSampleRate);
        std::printf("輸出 WAV：\n");
        std::printf("  l09_comb_noise_dry.wav: peak %.3f\n", dawlab::peakAbs(noise));
        renderNoiseThroughComb(noise, 48, 0.9f, 0.15f, "l09_comb_noise_1ms.wav");   // 1 kHz 音高
        renderNoiseThroughComb(noise, 480, 0.8f, 0.25f, "l09_comb_noise_10ms.wav"); // 100 Hz 金屬感
    }
    return 0;
}
