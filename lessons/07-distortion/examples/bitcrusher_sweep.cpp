// 範例 3：bitcrusher —— 掃 bit depth 與 downsample factor，量 SNR 並與理論比較
//
// 示範什麼：
//   (1) bit depth 2 → 16：量化一個全幅（0 dBFS）997 Hz 正弦，量 SNR = 20 log10(rms(signal) / rms(error))，
//       與理論 6.02 N + 1.76 dB 比較。每少 1 bit，SNR 少約 6 dB。
//   (2) 同樣的量化，但訊號只有 -20 dBFS：SNR 少 20 dB——量化雜訊是「固定大小」的，小聲的訊號相對更糟。
//       （這也是為什麼真正的 16 bit 錄音要用 dither，第 00 章提過。）
//   (3) downsample factor 1 → 16（sample & hold）：等效取樣率 48k → 3k，SNR 崩掉，
//       而且 5 kHz 的正弦經過 factor 8（等效 Nyquist 3 kHz）後會 alias 到 1 kHz——用 Goertzel 量給你看。
//   (4) 輸出幾組設定的 pluck sequence WAV。
//
// 怎麼執行：l07_bitcrusher_sweep（不需要參數）
//
// 輸出檔案（在 dawlab_output/）：
//   l07_bitcrush_dry.wav     原始
//   l07_bitcrush_12bit.wav   12 bit（幾乎聽不出差別，音尾可能有一點沙沙聲）
//   l07_bitcrush_8bit.wav    8 bit（明顯的量化雜訊，音尾「碎掉」）
//   l07_bitcrush_4bit.wav    4 bit（非常粗糙）
//   l07_bitcrush_ds8.wav     16 bit 但 downsample ×8（等效 6 kHz：悶、而且有金屬味的 aliasing）
//   l07_bitcrush_lofi.wav    8 bit + ×4（經典 lo-fi 取樣機的聲音）
//
// 該聽到：bit 越少雜訊越大，而且雜訊「跟著音符」出現（不是穩定的 hiss）；downsample 越大高頻越少、
//         但同時多出不和諧的 aliasing 泛音。
//
// 注意：bitcrush 不需要 oversampling（aliasing 就是效果本身），這個範例把 oversampling 關掉，latency = 0，
//       所以可以直接逐 sample 比較輸入與輸出。

#include <dawlab/Common.h>
#include <dawlab/dsp/Distortion.h>

#include <cmath>
#include <cstdio>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

template <typename Processor>
void processInBlocks(dawlab::AudioBuffer& buffer, Processor& processor) {
    dawlab::AudioBlock whole(buffer);
    for (int start = 0; start < buffer.getNumSamples(); start += kBlockSize) {
        const int len = std::min(kBlockSize, buffer.getNumSamples() - start);
        processor.process(whole.getSubBlock(start, len));
    }
}

// SNR（dB）= 20 log10( rms(in) / rms(out - in) )，兩者必須對齊（latency = 0）
double measureSnrDb(const dawlab::AudioBuffer& in, const dawlab::AudioBuffer& out) {
    const int n = in.getNumSamples();
    double sig = 0.0, err = 0.0;
    for (int i = 0; i < n; ++i) {
        const double x = in.getSample(0, i);
        const double e = out.getSample(0, i) - x;
        sig += x * x;
        err += e * e;
    }
    return err > 0.0 ? 10.0 * std::log10(sig / err) : 200.0;
}

dawlab::dsp::Distortion makeCrusher(float bits, int downsample, int numChannels) {
    dawlab::dsp::Distortion d;
    d.setOversamplingFactorLog2(0); // bitcrush 不需要 oversampling
    d.prepare(kSampleRate, kBlockSize, numChannels);
    d.setType(dawlab::dsp::DistortionType::Bitcrush);
    d.setDriveDb(0.0f);
    d.setToneHz(20000.0f);
    d.setMix(1.0f);
    d.setBitDepth(bits);
    d.setDownsampleFactor(downsample);
    return d;
}

} // namespace

int main() {
    using dawlab::dsp::Distortion;
    bool ok = true;
    // 997 Hz：跟 48000 沒有整數比，量化誤差不會每個週期重複（重複的誤差會變成諧波而不是雜訊）
    const double f0 = 997.0;

    // ---- (1)(2) bit depth sweep ----
    for (float ampDb : {0.0f, -20.0f}) {
        const float amp = dawlab::dbToGain(ampDb);
        std::printf("%s(%d) 量化 %.0f dBFS 的 %.0f Hz 正弦：step = 2 / 2^N，SNR 理論 = 6.02 N + 1.76 + 20 log10(A)\n",
                    ampDb == 0.0f ? "" : "\n", ampDb == 0.0f ? 1 : 2, ampDb, f0);
        std::printf("%6s %12s %12s %12s %8s\n", "bits", "step", "SNR 量測", "SNR 理論", "差");
        double snr8 = 0.0, snr16 = 0.0;
        for (float bits : {2.0f, 3.0f, 4.0f, 6.0f, 8.0f, 10.0f, 12.0f, 16.0f}) {
            dawlab::AudioBuffer in = dawlab::makeSine(f0, kSampleRate, 1.0, amp, 1);
            dawlab::AudioBuffer out = in;
            Distortion d = makeCrusher(bits, 1, 1);
            processInBlocks(out, d);
            const double snr = measureSnrDb(in, out);
            const double theory = Distortion::theoreticalSnrDb(bits, amp);
            std::printf("%6.0f %12.3g %12.2f %12.2f %+8.2f\n", bits, 2.0 / std::exp2(bits), snr, theory, snr - theory);
            if (bits == 8.0f)
                snr8 = snr;
            if (bits == 16.0f)
                snr16 = snr;
            // 理論公式假設「誤差均勻分布、訊號沒被 clip」。bit 數很少時兩者都不成立：
            //   - mid-tread 量化器的最大正值是 (2^(N-1) - 1)·step = 1 - step，0 dBFS 的正峰會被削掉（4 bit：削掉 0.125！）
            //   - 階數太少（2 bit 只有 4 階）時誤差跟訊號高度相關，不像雜訊
            //   - 訊號比 step/2 還小時整段被量化成 0 → SNR = 0 dB（-20 dBFS 的 2、3 bit 就是這樣：音尾會直接消失）
            if (bits >= 6.0f)
                ok = ok && std::abs(snr - theory) < 2.0;
        }
        std::printf("    8 → 16 bit：每 bit %.2f dB（理論 6.02）\n", (snr16 - snr8) / 8.0);
        ok = ok && std::abs((snr16 - snr8) / 8.0 - 6.02) < 0.3;
    }
    std::printf("（每少 1 bit → SNR 少 6.02 dB；訊號小 20 dB → SNR 也少 20 dB：量化雜訊的大小不隨訊號變。\n"
                "  低 bit 數的偏差：0 dBFS 時正峰被 mid-tread 的上限 1 - step 削掉；-20 dBFS 的 2~3 bit 時訊號 < step/2，整段變 0。）\n");

    // ---- (3) downsample sweep ----
    {
        std::printf("\n(3) sample & hold：16 bit、0 dBFS 的 %.0f Hz 正弦，downsample factor 1 → 16\n", f0);
        std::printf("%8s %14s %12s\n", "factor", "等效取樣率", "SNR 量測");
        double previousSnr = 1.0e9;
        for (int factor : {1, 2, 4, 8, 16}) {
            dawlab::AudioBuffer in = dawlab::makeSine(f0, kSampleRate, 1.0, 1.0f, 1);
            dawlab::AudioBuffer out = in;
            Distortion d = makeCrusher(16.0f, factor, 1);
            processInBlocks(out, d);
            const double snr = measureSnrDb(in, out);
            std::printf("%8d %11.0f Hz %12.2f\n", factor, kSampleRate / factor, snr);
            ok = ok && snr < previousSnr; // factor 越大，S&H 的階梯越粗，誤差越大
            if (factor == 2)
                ok = ok && snr < 30.0;    // 只要 hold 一個 sample，SNR 就從 98 dB 掉到 20 dB 左右
            previousSnr = snr;
        }

        // 5 kHz 正弦、factor 8（等效 fs' = 6 kHz、Nyquist 3 kHz）：5 kHz 會 alias 到 |5000 - 6000| = 1000 Hz
        const double fin = 5000.0;
        const int factor = 8;
        const double fsEff = kSampleRate / factor;
        const double alias = std::abs(fin - fsEff);
        dawlab::AudioBuffer sine = dawlab::makeSine(fin, kSampleRate, 1.0, 0.5f, 1);
        Distortion d = makeCrusher(16.0f, factor, 1);
        processInBlocks(sine, d);
        const int skip = dawlab::secondsToSamples(0.1, kSampleRate);
        const int n = dawlab::secondsToSamples(0.5, kSampleRate);
        const double atIn = dawlab::gainToDb(dawlab::goertzelAmplitude(sine.getReadPointer(0) + skip, n, fin, kSampleRate));
        const double atAlias = dawlab::gainToDb(dawlab::goertzelAmplitude(sine.getReadPointer(0) + skip, n, alias, kSampleRate));
        const double atImage = dawlab::gainToDb(dawlab::goertzelAmplitude(sine.getReadPointer(0) + skip, n, fsEff + fin, kSampleRate));
        // sample & hold（zero-order hold）的頻率響應是 sinc：|H(f)| = |sin(π f / fs') / (π f / fs')|
        auto zohDb = [&](double f) { return dawlab::gainToDb(std::abs(std::sin(dawlab::kPi * f / fsEff) / (dawlab::kPi * f / fsEff))); };
        std::printf("\n    %.0f Hz 正弦（-6 dBFS）、factor %d（fs' = %.0f Hz）：\n", fin, factor, fsEff);
        std::printf("    %6.0f Hz（原頻率）    %6.1f dBFS   （ZOH sinc 理論：-6 %+.1f = %.1f）\n", fin, atIn, zohDb(fin), -6.0 + zohDb(fin));
        std::printf("    %6.0f Hz（alias）      %6.1f dBFS   ← 原本不存在的頻率（理論 -6 %+.1f = %.1f）\n", alias, atAlias, zohDb(alias), -6.0 + zohDb(alias));
        std::printf("    %6.0f Hz（image）      %6.1f dBFS   ← fs' + f\n", fsEff + fin, atImage);
        ok = ok && std::abs(atAlias - (-6.0 + zohDb(alias))) < 1.0; // alias 的大小由 ZOH 的 sinc 決定
        ok = ok && std::abs(atIn - (-6.0 + zohDb(fin))) < 1.0;
    }

    // ---- (4) WAV ----
    {
        dawlab::AudioBuffer dry = dawlab::makePluckSequence(kSampleRate, 4.0, 0.4f, 2);
        dawlab::writeWav(dawlab::outputPath("l07_bitcrush_dry.wav"), dry, kSampleRate);
        struct Setting {
            float bits;
            int factor;
            const char* name;
        };
        const Setting settings[] = {
            {12.0f, 1, "l07_bitcrush_12bit.wav"}, {8.0f, 1, "l07_bitcrush_8bit.wav"}, {4.0f, 1, "l07_bitcrush_4bit.wav"},
            {16.0f, 8, "l07_bitcrush_ds8.wav"},   {8.0f, 4, "l07_bitcrush_lofi.wav"},
        };
        std::printf("\n(4) pluck sequence（peak %.1f dBFS）\n", dawlab::gainToDb(dawlab::peakAbs(dry)));
        std::printf("%24s %6s %8s %12s\n", "file", "bits", "factor", "SNR(dB)");
        for (const Setting& s : settings) {
            dawlab::AudioBuffer out = dry;
            Distortion d = makeCrusher(s.bits, s.factor, 2);
            processInBlocks(out, d);
            ok = ok && !dawlab::hasNanOrInf(out);
            std::printf("%24s %6.0f %8d %12.2f\n", s.name, s.bits, s.factor, measureSnrDb(dry, out));
            dawlab::writeWav(dawlab::outputPath(s.name), out, kSampleRate);
        }
        std::printf("（pluck 的 peak 約 -5 dBFS、RMS 只有約 -20 dBFS，而且音尾越來越小，所以同樣 bit 數的 SNR 比全幅正弦低很多）\n");
    }

    std::printf("\n已輸出 dawlab_output/l07_bitcrush_*.wav\n");
    std::printf("%s\n", ok ? "全部符合預期。" : "[FAIL] 有項目不符合預期！");
    return ok ? 0 : 1;
}
