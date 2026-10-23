// 範例 4：hard clip 到底需不需要 oversampling？量 aliasing 給你看
//
// 示範什麼：
//   (1) 5 kHz 正弦、drive 20 dB 的 hard clip，在 48 kHz 下產生的奇次諧波：15k、25k、35k、45k、55k……
//       只有 15 kHz 在 Nyquist（24 kHz）以內；其餘會「折返」：
//         25k → 48k - 25k = 23k、35k → 13k、45k → 3k、55k → 55k - 48k = 7k、65k → 17k……
//       這些頻率跟 5 kHz 沒有諧波關係，聽起來是不和諧的「金屬味」。
//       表格比較 1x / 2x / 4x / 8x oversampling 在 3 kHz、7 kHz、13 kHz、23 kHz 的 alias 大小（dBFS），
//       再算出每種倍率下「最低第幾次諧波」會折回 3 kHz / 7 kHz，解釋為什麼 2x 與 4x 幾乎一樣、8x 才大幅改善。
//       self-check：latency = 0 / 31 / 36 / 38 samples（由 halfband 長度推得）、4x 比 1x 低 20 dB 以上、
//       8x 比 4x 低 20 dB 以上、2x 與 4x 差不到 6 dB、真正的諧波不受影響。
//   (2) 對照組：同樣的訊號經過 Bitcrush（8 bit、無 downsample）——量化雜訊本來就分布在整個頻帶，
//       oversampling 幫不上忙（而且 S&H 的 aliasing 本來就是效果的一部分）。
//   (3) 把 log sweep（100 Hz → 10 kHz）hard clip 後輸出 1x 與 4x 兩個版本，聽「往下掃」的 alias 鳥叫聲。
//
// 怎麼執行：l07_oversampling_aliasing（不需要參數）
//
// 輸出檔案（在 dawlab_output/）：
//   l07_alias_sweep_1x.wav   不 oversampling：sweep 往上時會聽到好幾條「往下掃」的雜音
//   l07_alias_sweep_4x.wav   4x oversampling：乾淨很多
//
// 該聽到：1x 版本在 sweep 超過 2~3 kHz 之後，背景出現往下掉的哨音；4x 版本幾乎沒有。

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

double levelDb(const dawlab::AudioBuffer& b, double freq) {
    const int skip = dawlab::secondsToSamples(0.2, kSampleRate);
    const int n = dawlab::secondsToSamples(0.5, kSampleRate);
    return dawlab::gainToDb(dawlab::goertzelAmplitude(b.getReadPointer(0) + skip, n, freq, kSampleRate));
}

} // namespace

int main() {
    using dawlab::dsp::Distortion;
    using dawlab::dsp::DistortionType;
    bool ok = true;

    const double f0 = 5000.0;
    const double aliasFreqs[] = {3000.0, 7000.0, 13000.0, 23000.0}; // 45k→3k、55k→7k、35k→13k、25k→23k
    const double harmonicFreqs[] = {5000.0, 15000.0};

    std::printf("(1) %.0f Hz 正弦（-6 dBFS）、HardClip drive 20 dB：諧波與 alias 的 dBFS\n", f0);
    std::printf("%6s %8s |", "OS", "latency");
    for (double f : harmonicFreqs)
        std::printf(" H@%5.0fk", f / 1000.0);
    std::printf(" |");
    for (double f : aliasFreqs)
        std::printf(" A@%5.0fk", f / 1000.0);
    std::printf("\n");

    double alias3k[4] = {0, 0, 0, 0};
    double alias7k[4] = {0, 0, 0, 0};
    double h5k[4] = {0, 0, 0, 0}, h15k[4] = {0, 0, 0, 0};
    int latency[4] = {0, 0, 0, 0};
    for (int log2 = 0; log2 <= 3; ++log2) {
        dawlab::AudioBuffer sine = dawlab::makeSine(f0, kSampleRate, 1.0, 0.5f, 1);
        Distortion d;
        d.setOversamplingFactorLog2(log2);
        d.prepare(kSampleRate, kBlockSize, 1);
        d.setType(DistortionType::HardClip);
        d.setDriveDb(20.0f);
        d.setToneHz(20000.0f);
        processInBlocks(sine, d);

        std::printf("%5dx %5d smp |", 1 << log2, d.getLatencySamples());
        for (double f : harmonicFreqs)
            std::printf(" %8.1f", levelDb(sine, f));
        std::printf(" |");
        for (double f : aliasFreqs)
            std::printf(" %8.1f", levelDb(sine, f));
        std::printf("\n");
        alias3k[log2] = levelDb(sine, 3000.0);
        alias7k[log2] = levelDb(sine, 7000.0);
        h5k[log2] = levelDb(sine, 5000.0);
        h15k[log2] = levelDb(sine, 15000.0);
        latency[log2] = d.getLatencySamples();
    }
    std::printf("（H = 真正的諧波，A = alias。1x 時 3 kHz 的 alias 來自第 9 次諧波 45 kHz：|45k - 48k| = 3k）\n"
                "（oversampling 只是「減少」而不是「消滅」alias：hard clip 的諧波無窮多、只以約 1/n 衰減，\n"
                "  在 4x（192 kHz）時第 39 次諧波 195 kHz 仍會折回 3 kHz，只是小很多。\n"
                "  23 kHz 那一欄幾乎不降：25 kHz 的諧波落在 halfband filter 的過渡帶（以 24 kHz 為中心）裡，\n"
                "  只被衰減一點點，而且折回到的 23 kHz 本來就聽不到。）\n");
    // 為什麼 2x 與 4x 在 3 kHz / 7 kHz 幾乎一樣？在高取樣率 L·fs 裡，第 n 次諧波會折到 |n·f0 - k·L·fs|。
    // 2x（96 kHz）與 4x（192 kHz）有共同的「折返點」192 kHz：第 39 次諧波 195 kHz → 3 kHz、第 37 次 185 kHz → 7 kHz，
    // 兩種倍率都逃不掉；8x（384 kHz）要到第 77 次（385 kHz → 1 kHz）、第 153 次（765 kHz → 3 kHz）才折回來，小得多。
    std::printf("\n為什麼 2x ≈ 4x？找出在每種倍率下「最低次」會折回 3 kHz / 7 kHz 的諧波：\n");
    std::printf("%6s %18s %18s\n", "OS", "→ 3 kHz", "→ 7 kHz");
    for (int log2 = 0; log2 <= 3; ++log2) {
        const int fsHigh = 48000 << log2;
        int lowest[2] = {0, 0};
        const int targets[2] = {3000, 7000};
        for (int t = 0; t < 2; ++t) {
            for (int h = 3; h < 400 && lowest[t] == 0; h += 2) { // hard clip 只有奇次諧波
                const int f = 5000 * h;
                const int folded = std::abs(f - fsHigh * ((f + fsHigh / 2) / fsHigh)); // |f - k·fsHigh|，k = round(f / fsHigh)
                if (folded == targets[t])
                    lowest[t] = h;
            }
        }
        std::printf("%5dx %10d 次（%3d kHz） %8d 次（%3d kHz）\n", 1 << log2, lowest[0], lowest[0] * 5, lowest[1], lowest[1] * 5);
    }

    // latency 的公式（第 06 章 Oversampler）：Σ_i 2(2K_i - 1)·2^(S-i) 個「最高取樣率的 sample」，除以 2^S 後取整數
    //   K_i = 各級奇數 tap 數 / 2 = 16、6、4 → 2x：62/2 = 31；4x：(124 + 22)/4 = 36.5 → 36；8x：(248 + 44 + 14)/8 = 38.25 → 38
    {
        using dawlab::dsp::Oversampler;
        const int oddTaps[3] = {Oversampler::kOddTapsStage1, Oversampler::kOddTapsStage2, Oversampler::kOddTapsStage3};
        bool latencyOk = latency[0] == 0;
        for (int S = 1; S <= 3; ++S) {
            int units = 0;
            for (int i = 1; i <= S; ++i)
                units += 2 * (oddTaps[i - 1] - 1) * (1 << (S - i)); // 2(2K - 1)，2K = 奇數 tap 數
            latencyOk = latencyOk && latency[S] == units / (1 << S);
        }
        std::printf("self-check：latency = 0 / 31 / 36 / 38（由 halfband 長度推得）%s\n", latencyOk ? "OK" : "NG");
        ok = ok && latencyOk;
    }

    // 4x 一定要比 1x 乾淨很多；8x 又比 4x 乾淨很多；2x 與 4x 在 3 kHz 差不到 6 dB（同樣的諧波折回來）；
    // 真正的諧波（5 kHz、15 kHz）不受 oversampling 影響
    {
        const bool c1 = (alias3k[0] - alias3k[2]) > 20.0 && (alias7k[0] - alias7k[2]) > 15.0;
        const bool c2 = (alias3k[2] - alias3k[3]) > 20.0 && (alias7k[2] - alias7k[3]) > 20.0;
        const bool c3 = std::abs(alias3k[1] - alias3k[2]) < 6.0 && std::abs(alias7k[1] - alias7k[2]) < 6.0;
        bool c4 = true;
        for (int i = 1; i <= 3; ++i)
            c4 = c4 && std::abs(h5k[i] - h5k[0]) < 0.2 && std::abs(h15k[i] - h15k[0]) < 0.5;
        std::printf("self-check：4x 比 1x 低 > 20 dB %s、8x 比 4x 低 > 20 dB %s、2x ≈ 4x（< 6 dB）%s、諧波不變 %s\n",
                    c1 ? "OK" : "NG", c2 ? "OK" : "NG", c3 ? "OK" : "NG", c4 ? "OK" : "NG");
        ok = ok && c1 && c2 && c3 && c4;
    }

    // ---- (2) Bitcrush 對照組 ----
    {
        std::printf("\n(2) 同樣的正弦經過 Bitcrush 8 bit（drive 0 dB、無 downsample）：量化雜訊在 3 kHz / 7 kHz 的大小\n");
        std::printf("%6s %10s %10s\n", "OS", "@3k", "@7k");
        double crush7k[2] = {0.0, 0.0};
        for (int log2 : {0, 2}) {
            dawlab::AudioBuffer sine = dawlab::makeSine(f0, kSampleRate, 1.0, 0.5f, 1);
            Distortion d;
            d.setOversamplingFactorLog2(log2);
            d.prepare(kSampleRate, kBlockSize, 1);
            d.setType(DistortionType::Bitcrush);
            d.setBitDepth(8.0f);
            d.setDriveDb(0.0f);
            d.setToneHz(20000.0f);
            processInBlocks(sine, d);
            std::printf("%5dx %10.1f %10.1f\n", 1 << log2, levelDb(sine, 3000.0), levelDb(sine, 7000.0));
            crush7k[log2 == 0 ? 0 : 1] = levelDb(sine, 7000.0);
        }
        ok = ok && std::abs(crush7k[0] - crush7k[1]) < 0.01;
        std::printf("（兩列一樣：Bitcrush 路徑本來就不經過 oversampler，量化雜訊也不是 oversampling 能處理的）\n");
    }

    // ---- (3) sweep WAV ----
    {
        for (int log2 : {0, 2}) {
            dawlab::AudioBuffer sweep = dawlab::makeLogSweep(100.0, 10000.0, kSampleRate, 4.0, 0.5f, 2);
            Distortion d;
            d.setOversamplingFactorLog2(log2);
            d.prepare(kSampleRate, kBlockSize, 2);
            d.setType(DistortionType::HardClip);
            d.setDriveDb(20.0f);
            d.setToneHz(20000.0f);
            d.setOutputGainDb(-6.0f);
            processInBlocks(sweep, d);
            ok = ok && !dawlab::hasNanOrInf(sweep);
            const char* name = log2 == 0 ? "l07_alias_sweep_1x.wav" : "l07_alias_sweep_4x.wav";
            dawlab::writeWav(dawlab::outputPath(name), sweep, kSampleRate);
        }
        std::printf("\n(3) 已輸出 dawlab_output/l07_alias_sweep_1x.wav 與 l07_alias_sweep_4x.wav\n");
    }

    std::printf("%s\n", ok ? "全部符合預期。" : "[FAIL] 有項目不符合預期！");
    return ok ? 0 : 1;
}
