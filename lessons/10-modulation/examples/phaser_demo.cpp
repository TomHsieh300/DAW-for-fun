// 範例 4：Phaser —— 串接 allpass 的相位偏移與原音相加 → notch
//
// 示範什麼：
//   1. 一階 allpass 的驗證：|H| 在所有頻率都是 0 dB；相位 φ(f) = −2·arctan(tan(πf/fs) / tan(πfc/fs))，
//      在 f = fc 時剛好 −90°。用第 02 章 BiquadCoefficients::response 算出來的相位和公式對照。
//   2. notch 數 = stages / 2：把 LFO 停住（rate 0、depth 0）、centre 1 kHz、mix 0.5、feedback 0，
//      掃描 50 Hz–16 kHz 的頻率響應，自動找 notch 並和理論公式對照（4 / 6 / 8 stages → 2 / 3 / 4 個 notch）。
//      順便看 notch 之間的間距：不是等間距，而是在 tan(πf/fs) 的刻度上等間距。
//   3. feedback 讓 notch 之間的 peak 變尖（共振感）。
//   4. 撥弦琶音與白雜訊經過 phaser 的 WAV。
//
// 怎麼執行：l10_phaser（不需要參數）
//
// 輸出檔案（dawlab_output/）：
//   l10_phaser_dry.wav          撥弦琶音
//   l10_phaser_wet.wav          4 stages、rate 0.4 Hz、centre 800 Hz、depth 1、feedback 0.3、mix 0.5
//   l10_phaser_8stage.wav       8 stages、feedback 0.6（更多 notch、更「水」）
//   l10_phaser_noise.wav        白雜訊經過 6 stages phaser（最容易聽到 notch 在掃）
//
// 該聽到什麼：和 flanger 比，phaser 的掃動比較「柔」、比較「水」，沒有金屬共鳴；
//            stages 越多、feedback 越大，掃動越明顯。

#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>
#include <dawlab/dsp/Phaser.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <string>
#include <vector>

using dawlab::dsp::BiquadCoefficients;
using dawlab::dsp::Phaser;

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

// LFO 停住的 phaser（單聲道、量測用）
Phaser makeFrozenPhaser(int stages, float centreHz, float feedback, float mix) {
    Phaser p;
    p.prepare(kSampleRate, kBlockSize, 1);
    p.setRateHz(0.0f);
    p.setDepth(0.0f);
    p.setCentreHz(centreHz);
    p.setFeedback(feedback);
    p.setStages(stages);
    p.setMix(mix);
    p.reset();
    return p;
}

double measurePhaserDb(Phaser& p, double freq) {
    p.reset();
    return dawlab::measureGainDb([&](float x) { return p.processSample(0, x); }, freq, kSampleRate, 0.3, 0.1);
}

// 理論頻率響應：A = (一階 allpass)^N，feedback 經過 1 個 sample 延遲回到輸入
//   W = X + g·z⁻¹·Y，Y = A·W  ⇒  Y/X = A / (1 − g·A·z⁻¹)
//   H = (1 − mix) + mix · A / (1 − g·A·z⁻¹)
double theoryPhaserDb(double f, int stages, double fc, double feedback, double mix) {
    const std::complex<double> ap = Phaser::makeFirstOrderAllpass(fc, kSampleRate).response(f, kSampleRate);
    const std::complex<double> a = std::pow(ap, stages);
    const std::complex<double> z1 = std::polar(1.0, -dawlab::kTwoPi * f / kSampleRate);
    const std::complex<double> h = (1.0 - mix) + mix * a / (1.0 - feedback * a * z1);
    return dawlab::gainToDb(std::abs(h));
}

// 在 [lo, hi] 之間用三分搜尋找增益最小的頻率（notch 很尖，格點掃描只能找到附近）
double refineNotch(Phaser& p, double lo, double hi) {
    for (int it = 0; it < 14; ++it) {
        const double m1 = lo + (hi - lo) / 3.0;
        const double m2 = hi - (hi - lo) / 3.0;
        if (measurePhaserDb(p, m1) < measurePhaserDb(p, m2))
            hi = m2;
        else
            lo = m1;
    }
    return 0.5 * (lo + hi);
}

} // namespace

int main() {
    std::printf("=== 第 10 章 範例 4：Phaser ===\n\n");

    // ---- 1. 一階 allpass：振幅 0 dB、相位公式 ----------------------------------
    {
        const double fc = 1000.0;
        const BiquadCoefficients c = Phaser::makeFirstOrderAllpass(fc, kSampleRate);
        std::printf("一階 allpass，fc = %.0f Hz：a = %.5f（b0 = a, b1 = 1, a1 = a）\n", fc, c.b0);
        std::printf("   freq(Hz)   |H|(dB)   相位 Biquad::response   相位公式\n");
        for (double f : {100.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0}) {
            const double degResp = std::arg(c.response(f, kSampleRate)) * 180.0 / dawlab::kPi;
            const double degFormula = Phaser::allpassPhase(f, fc, kSampleRate) * 180.0 / dawlab::kPi;
            std::printf("   %8.0f   %+6.2f     %+8.2f°            %+8.2f°\n", f, c.magnitudeDb(f, kSampleRate), degResp,
                        degFormula);
        }
        std::printf("  -> 振幅處處 0 dB；相位從 0°（DC）經過 −90°（fc）走到 −180°（Nyquist）。\n\n");
    }

    // ---- 2. notch 數 = stages / 2，位置對照公式 --------------------------------
    {
        const float centre = 1000.0f;
        std::vector<double> freqs;
        for (int i = 0; i <= 240; ++i) // 50 Hz .. 16 kHz，對數等距
            freqs.push_back(50.0 * std::pow(16000.0 / 50.0, i / 240.0));

        for (int stages : {4, 6, 8}) {
            Phaser p = makeFrozenPhaser(stages, centre, 0.0f, 0.5f);
            std::vector<double> gains;
            for (double f : freqs)
                gains.push_back(measurePhaserDb(p, f));

            std::printf("%d stages、centre %.0f Hz、mix 0.5、feedback 0：\n", stages, centre);
            std::printf("   k   量測 notch(Hz)   理論(Hz)   與前一個 notch 的比值\n");
            int k = 0;
            double prev = 0.0;
            for (size_t i = 1; i + 1 < gains.size(); ++i) {
                if (gains[i] < gains[i - 1] && gains[i] <= gains[i + 1] && gains[i] < -6.0) {
                    const double notch = refineNotch(p, freqs[i - 1], freqs[i + 1]);
                    const double theory = Phaser::notchFrequencyHz(k, stages, centre, kSampleRate);
                    std::printf("  %2d   %10.1f       %8.1f   %s\n", k, notch, theory,
                                prev > 0.0 ? (std::to_string(notch / prev).substr(0, 5) + " 倍").c_str() : "-");
                    prev = notch;
                    ++k;
                }
            }
            std::printf("  -> 找到 %d 個 notch = stages / 2 = %d。", k, stages / 2);
            if (stages == 6)
                std::printf("中間那個剛好在 fc（%.0f Hz）：6 級 × (−90°) = −540° ≡ −180°，和原音反相。", centre);
            std::printf("\n\n");
        }
        std::printf("  -> notch 既不等差（不像 flanger 的 comb）也不等比：位置由 tan((2k+1)π/(2N)) 決定，\n"
                    "     在低頻（tan(πf/fs) ≈ πf/fs）時對 fc 幾何對稱：f_k · f_(N/2−1−k) ≈ fc²。\n");
        {
            const double f0 = Phaser::notchFrequencyHz(0, 4, centre, kSampleRate);
            const double f1 = Phaser::notchFrequencyHz(1, 4, centre, kSampleRate);
            const double fcSq = double(centre) * centre;
            std::printf("     例：4 stages 的 f0 · f1 = %.4f × %.4f = %.0f ≈ fc² = %.0f（差 %.2f%%，來自 bilinear 的頻率扭曲）\n",
                        f0, f1, f0 * f1, fcSq, 100.0 * (fcSq - f0 * f1) / fcSq);
            const auto tanOf = [](double f) { return std::tan(dawlab::kPi * f / kSampleRate); };
            std::printf("     精確式：tan(πf0/fs) · tan(πf1/fs) = %.10f，tan²(πfc/fs) = %.10f\n\n", tanOf(f0) * tanOf(f1),
                        tanOf(centre) * tanOf(centre));
        }
    }

    // ---- 3. feedback 讓 peak 變尖 --------------------------------------------
    {
        std::printf("feedback 對 4 stages（centre 1 kHz、mix 0.5）的影響（量測 / 理論，dB）：\n");
        std::printf("   freq(Hz)       fb 0.0            fb 0.5            fb 0.8\n");
        for (double f : {415.0, 1000.0, 2400.0, 6000.0}) {
            std::printf("   %8.0f", f);
            for (float fb : {0.0f, 0.5f, 0.8f}) {
                Phaser p = makeFrozenPhaser(4, 1000.0f, fb, 0.5f);
                std::printf("   %+6.1f / %+6.1f", measurePhaserDb(p, f), theoryPhaserDb(f, 4, 1000.0, fb, 0.5));
            }
            std::printf("\n");
        }
        std::printf("  -> feedback 讓 notch 變淺、notch 之間（1000 Hz，A = +1）隆起成 peak：共振感。\n"
                    "     理論值含 feedback 路徑上的 1 個 sample 延遲：H = (1−mix) + mix·A / (1 − g·A·z⁻¹)。\n\n");
    }

    // ---- 4. 聽 ---------------------------------------------------------------
    {
        dawlab::AudioBuffer pluck = dawlab::makePluckSequence(kSampleRate, 4.0, 0.4f, 2);
        dawlab::writeWav(dawlab::outputPath("l10_phaser_dry.wav"), pluck, kSampleRate);
        std::printf("輸出 WAV：\n  l10_phaser_dry.wav      peak %.3f\n", dawlab::peakAbs(pluck));

        Phaser p;
        p.prepare(kSampleRate, kBlockSize, 2);
        p.setRateHz(0.4f);
        p.setCentreHz(800.0f);
        p.setDepth(1.0f);
        p.setFeedback(0.3f);
        p.setStages(4);
        p.setMix(0.5f);
        p.reset();
        dawlab::AudioBuffer wet = pluck;
        processInBlocks(p, wet);
        dawlab::writeWav(dawlab::outputPath("l10_phaser_wet.wav"), wet, kSampleRate);
        std::printf("  l10_phaser_wet.wav      peak %.3f\n", dawlab::peakAbs(wet));

        p.setStages(8);
        p.setFeedback(0.6f);
        p.reset();
        wet = pluck;
        processInBlocks(p, wet);
        dawlab::writeWav(dawlab::outputPath("l10_phaser_8stage.wav"), wet, kSampleRate);
        std::printf("  l10_phaser_8stage.wav   peak %.3f\n", dawlab::peakAbs(wet));

        dawlab::AudioBuffer noise = dawlab::makeWhiteNoise(kSampleRate, 6.0, 0.2f, 2, 11u);
        p.setStages(6);
        p.setFeedback(0.5f);
        p.setRateHz(0.25f);
        p.reset();
        processInBlocks(p, noise);
        dawlab::writeWav(dawlab::outputPath("l10_phaser_noise.wav"), noise, kSampleRate);
        std::printf("  l10_phaser_noise.wav    peak %.3f\n", dawlab::peakAbs(noise));
    }
    return 0;
}
