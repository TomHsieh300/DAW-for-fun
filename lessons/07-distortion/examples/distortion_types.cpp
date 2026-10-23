// 範例 1：五種 DistortionType 的 transfer curve、諧波結構與聽感
//
// 示範什麼：
//   (1) 印出每種曲線的 transfer curve（輸入 u 從 -3 到 +3），看「波形被怎麼折」。
//   (2) 把 220 Hz 正弦（-6 dBFS）送進每一種 Distortion，印出第 1~8 次諧波的 dBFS 與 THD。
//       重點對照：
//         - HardClip 只有奇次諧波，drive 很大時 THD 逼近方波的 48.3%；
//         - Fuzz 不對稱 → 偶次諧波出現；
//         - Rectify（full-wave）基頻消失、能量跑到 2 倍頻 → 高八度；
//         - Bitcrush 的「諧波」其實是量化雜訊，分布在整個頻帶（THD 看起來不大但聽起來很髒）。
//       接著用 self-check 對照理論值：方波的 H1 = 4/π、H3/H1 = 1/3、|sin| 的 H2 = 4/(3π)、H4 = 4/(15π)。
//   (2b) Fuzz 的 gating 門檻：drive 30 dB 時，輸入峰值低於 20·log10(0.1) - 30 = -50 dBFS 就完全沒有輸出。
//   (3) 把 pluck sequence 經過每種 type 輸出 WAV（輸出都用 RMS 對齊到與輸入相同，方便公平比較）。
//   (4) 用 cross-correlation 實際量每種 type 的延遲，確認 oversampled 路徑與 bitcrush 路徑（+ FixedDelay）
//       都等於 getLatencySamples()（4x 時 36），而且 mix 50% 時 dry / wet 對齊、沒有 comb filtering。
//
// 怎麼執行：l07_distortion_types（不需要參數；可選 argv[1] = 輸入 WAV 路徑）
//
// 輸出檔案（在 dawlab_output/）：
//   l07_types_dry.wav       原始 pluck sequence
//   l07_types_hardclip.wav  HardClip  drive 20 dB、tone 6 kHz
//   l07_types_foldback.wav  Foldback  drive 12 dB
//   l07_types_fuzz.wav      Fuzz      drive 30 dB、tone 4 kHz
//   l07_types_rectify.wav   Rectify   drive 6 dB（full-wave，聽起來高八度、像合成器）
//   l07_types_bitcrush.wav  Bitcrush  6 bit、downsample ×4
//
// 該聽到：hard clip 又硬又亮；foldback 像合成器、泛音會隨音量「掃」動；fuzz 粗糙、音尾會突然斷掉（gating）；
//         rectify 音高高了八度而且少了「底」；bitcrush 有沙沙的量化雜訊與金屬味的 aliasing。

#include <dawlab/Common.h>
#include <dawlab/dsp/Distortion.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

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

// 每一種 type 在這個範例裡用的設定
struct Preset {
    dawlab::dsp::DistortionType type;
    float driveDb;
    float toneHz;
    float bits;
    int downsample;
    const char* wavName;
};

const Preset kPresets[] = {
    {dawlab::dsp::DistortionType::HardClip, 20.0f, 6000.0f, 24.0f, 1, "l07_types_hardclip.wav"},
    {dawlab::dsp::DistortionType::Foldback, 12.0f, 20000.0f, 24.0f, 1, "l07_types_foldback.wav"},
    {dawlab::dsp::DistortionType::Fuzz, 30.0f, 4000.0f, 24.0f, 1, "l07_types_fuzz.wav"},
    {dawlab::dsp::DistortionType::Rectify, 6.02f, 20000.0f, 24.0f, 1, "l07_types_rectify.wav"},
    {dawlab::dsp::DistortionType::Bitcrush, 0.0f, 20000.0f, 6.0f, 4, "l07_types_bitcrush.wav"},
};

void configure(dawlab::dsp::Distortion& d, const Preset& p) {
    d.setType(p.type);
    d.setDriveDb(p.driveDb);
    d.setToneHz(p.toneHz);
    d.setBitDepth(p.bits);
    d.setDownsampleFactor(p.downsample);
    d.setMix(1.0f);
    d.setOutputGainDb(0.0f);
}

// 輸出 RMS 對齊到輸入 RMS（gain staging：失真後音量暴增，不補償就沒辦法公平比較音色）
float rmsMatch(const dawlab::AudioBuffer& in, dawlab::AudioBuffer& out) {
    const float inRms = dawlab::rms(in, 0);
    const float outRms = dawlab::rms(out, 0);
    const float g = outRms > 1.0e-9f ? inRms / outRms : 1.0f;
    out.applyGain(g);
    return dawlab::gainToDb(g);
}

} // namespace

int main(int argc, char** argv) {
    using dawlab::dsp::Distortion;
    using dawlab::dsp::DistortionType;

    // ---- (1) transfer curves ----
    std::printf("(1) transfer curve y = shape(type, u)（Bitcrush 用 8 bit 量化）\n");
    std::printf("%6s", "u");
    for (const Preset& p : kPresets)
        std::printf(" %10s", dawlab::dsp::distortionTypeName(p.type));
    std::printf("\n");
    for (int i = -12; i <= 12; ++i) {
        const float u = 0.25f * static_cast<float>(i);
        std::printf("%6.2f", u);
        for (const Preset& p : kPresets)
            std::printf(" %10.4f", Distortion::shape(p.type, u));
        std::printf("\n");
    }

    // ---- (2) 諧波分析：220 Hz 正弦，-6 dBFS ----
    const double f0 = 220.0;
    constexpr int kNumHarmonics = 8;
    std::printf("\n(2) 220 Hz 正弦（-6 dBFS）經過各 type 的諧波（dBFS）與 THD（算到第 50 次諧波）\n");
    std::printf("%10s %7s", "type", "drive");
    for (int h = 1; h <= kNumHarmonics; ++h)
        std::printf("   H%d", h);
    std::printf("     THD\n");

    // 分析用的設定（同一種 type 看不同 drive：hard clip 逼近方波、fuzz 的偶次諧波隨 drive 消長）
    const Preset analysis[] = {
        {DistortionType::HardClip, 10.0f, 20000.0f, 24.0f, 1, ""},
        {DistortionType::HardClip, 20.0f, 20000.0f, 24.0f, 1, ""},
        {DistortionType::HardClip, 40.0f, 20000.0f, 24.0f, 1, ""},
        {DistortionType::Foldback, 12.0f, 20000.0f, 24.0f, 1, ""},
        {DistortionType::Fuzz, 12.0f, 20000.0f, 24.0f, 1, ""},
        {DistortionType::Fuzz, 30.0f, 20000.0f, 24.0f, 1, ""},
        {DistortionType::Rectify, 6.02f, 20000.0f, 24.0f, 1, ""},
        {DistortionType::Bitcrush, 0.0f, 20000.0f, 6.0f, 1, ""},
    };
    bool ok = true;
    constexpr int kNumAnalysis = static_cast<int>(sizeof(analysis) / sizeof(analysis[0]));
    double levels[kNumAnalysis][kNumHarmonics + 1] = {}; // levels[a][h]：第 a 組設定的第 h 次諧波（dBFS），h = 1..8
    double thd[kNumAnalysis] = {};
    for (int a = 0; a < kNumAnalysis; ++a) {
        const Preset& p = analysis[a];
        dawlab::AudioBuffer sine = dawlab::makeSine(f0, kSampleRate, 1.0, 0.5f, 1);
        Distortion d;
        d.prepare(kSampleRate, kBlockSize, 1);
        configure(d, p); // tone = 20 kHz（bypass）：分析時關掉 tone，才看得到完整的諧波結構
        processInBlocks(sine, d);
        ok = ok && !dawlab::hasNanOrInf(sine);

        // 跳過前 0.2 秒（oversampler latency 與 DC blocker 的 transient），分析後面 0.5 秒
        const int skip = dawlab::secondsToSamples(0.2, kSampleRate);
        const int n = dawlab::secondsToSamples(0.5, kSampleRate);
        const float* x = sine.getReadPointer(0) + skip;

        std::printf("%10s %5.1fdB", dawlab::dsp::distortionTypeName(p.type), p.driveDb);
        for (int h = 1; h <= kNumHarmonics; ++h) {
            levels[a][h] = dawlab::gainToDb(dawlab::goertzelAmplitude(x, n, f0 * h, kSampleRate));
            std::printf(" %5.1f", levels[a][h]);
        }
        if (p.type == DistortionType::Rectify) {
            std::printf("     n/a（基頻消失，THD 相對於 H1 沒有意義）\n");
        } else {
            thd[a] = dawlab::thdPercent(x, n, f0, kSampleRate, 50);
            std::printf(" %6.1f%%\n", thd[a]);
        }
    }
    std::printf("（方波的理論 THD = sqrt(pi^2/8 - 1) = 48.3%%；只算到第 50 次諧波時約 47.3%%。\n"
                "  HardClip 的 drive 越大越接近方波；Fuzz 在中等 drive 時有偶次諧波，drive 極大時兩邊都飽和、\n"
                "  波形變成「DC 偏移的方波」，偶次諧波反而變弱——留下的 DC 由 DC blocker 移除。）\n");

    // ---- self-check：README 2.2～2.5 節的數字 ----
    // analysis[] 的 index：0/1/2 = HardClip 10/20/40 dB、3 = Foldback 12、4/5 = Fuzz 12/30、6 = Rectify、7 = Bitcrush
    {
        const double squareH1 = dawlab::gainToDb(4.0 / dawlab::kPi);         // 方波基頻 4/π → +2.10 dBFS
        const double rectH2 = dawlab::gainToDb(4.0 / (3.0 * dawlab::kPi));    // 峰值 1 的 |sin|：4/(3π) → -7.44 dBFS
        const double rectH4 = dawlab::gainToDb(4.0 / (15.0 * dawlab::kPi));   // 4/(15π) → -21.42 dBFS
        bool evenFree = true; // HardClip / Foldback 是奇函數 → 偶次諧波必須量不到
        for (int a : {0, 1, 2, 3})
            for (int h = 2; h <= kNumHarmonics; h += 2)
                evenFree = evenFree && levels[a][h] < -80.0;
        const bool c1 = thd[2] > 44.0 && thd[2] < 48.3;                              // 40 dB：接近方波的 48.3%
        const bool c2 = std::abs(levels[2][1] - squareH1) < 0.3;                     // H1 → 4/π
        const bool c3 = std::abs((levels[2][3] - levels[2][1]) + 9.54) < 0.5;        // H3/H1 → 1/3（-9.54 dB）
        const bool c4 = levels[3][3] > levels[3][1];                                 // Foldback 12 dB：H3 比 H1 大
        const bool c5 = levels[4][2] > -30.0 && levels[5][2] < levels[4][2] - 10.0;  // Fuzz：偶次諧波在中等 drive 最明顯
        const bool c6 = levels[6][1] < -60.0 && std::abs(levels[6][2] - rectH2) < 0.3 &&
                        std::abs(levels[6][4] - rectH4) < 0.3;                       // Rectify：基頻消失、H2/H4 照 Fourier 級數
        std::printf("self-check：HardClip/Foldback 無偶次諧波 %s；HardClip 40 dB：THD %s、H1 = 4/π（%.2f dB）%s、"
                    "H3 - H1 = -9.54 dB %s；Foldback H3 > H1 %s；Fuzz 偶次諧波 12 dB > 30 dB %s；"
                    "Rectify H1 消失、H2 = %.2f、H4 = %.2f dBFS %s\n",
                    evenFree ? "OK" : "NG", c1 ? "OK" : "NG", squareH1, c2 ? "OK" : "NG", c3 ? "OK" : "NG",
                    c4 ? "OK" : "NG", c5 ? "OK" : "NG", rectH2, rectH4, c6 ? "OK" : "NG");
        ok = ok && evenFree && c1 && c2 && c3 && c4 && c5 && c6;
    }

    // ---- (2b) Fuzz 的 gating 門檻：dead zone d = 0.1、drive G dB → 輸入峰值低於 20 log10(d) - G dBFS 就完全沒聲音 ----
    {
        const float driveDb = 30.0f;
        const double thresholdDb = dawlab::gainToDb(static_cast<double>(Distortion::kFuzzDeadZone)) - driveDb; // -50 dBFS
        std::printf("\n(2b) Fuzz drive %.0f dB 的 gating 門檻（理論 %.1f dBFS）：220 Hz 正弦的輸出 peak\n", driveDb, thresholdDb);
        std::printf("%12s %14s\n", "輸入(dBFS)", "輸出 peak");
        for (float inDb : {-60.0f, -53.0f, -47.0f, -40.0f}) {
            dawlab::AudioBuffer sine = dawlab::makeSine(f0, kSampleRate, 0.5, dawlab::dbToGain(inDb), 1);
            Distortion d;
            d.prepare(kSampleRate, kBlockSize, 1);
            d.setType(DistortionType::Fuzz);
            d.setDriveDb(driveDb);
            processInBlocks(sine, d);
            const int skip = dawlab::secondsToSamples(0.1, kSampleRate); // 跳過 drive 的第一個 ramp block
            const float peak = dawlab::peakAbs(sine.getReadPointer(0) + skip, sine.getNumSamples() - skip);
            std::printf("%12.1f %14.6f%s\n", inDb, peak, peak == 0.0f ? "（完全靜音）" : "");
            // 門檻以下必須完全靜音、門檻以上必須有聲音
            if (inDb < thresholdDb)
                ok = ok && peak == 0.0f;
            else
                ok = ok && peak > 1.0e-3f;
        }
    }

    // ---- (3) pluck sequence → WAV ----
    dawlab::AudioBuffer dry;
    if (argc > 1) {
        dawlab::WavData wav;
        std::string error;
        if (!dawlab::readWav(argv[1], wav, &error)) {
            std::printf("讀取失敗：%s\n", error.c_str());
            return 1;
        }
        dry = wav.buffer;
        std::printf("\n(3) 使用輸入檔 %s（%d ch，%d samples）\n", argv[1], dry.getNumChannels(), dry.getNumSamples());
    } else {
        dry = dawlab::makePluckSequence(kSampleRate, 4.0, 0.4f, 2);
        std::printf("\n(3) pluck sequence（4 秒、2 ch）經過各 type\n");
    }
    dawlab::writeWav(dawlab::outputPath("l07_types_dry.wav"), dry, kSampleRate);
    std::printf("%10s %10s %10s %12s %10s\n", "type", "peak(dBFS)", "rms(dBFS)", "RMS 補償(dB)", "latency");
    std::printf("%10s %10.2f %10.2f %12s %10s\n", "dry", dawlab::gainToDb(dawlab::peakAbs(dry)),
                dawlab::gainToDb(dawlab::rms(dry, 0)), "-", "-");

    for (const Preset& p : kPresets) {
        dawlab::AudioBuffer out = dry;
        Distortion d;
        d.prepare(kSampleRate, kBlockSize, out.getNumChannels());
        configure(d, p);
        processInBlocks(out, d);
        ok = ok && !dawlab::hasNanOrInf(out);
        const float peakBefore = dawlab::peakAbs(out);
        const float rmsBefore = dawlab::rms(out, 0);
        const float compDb = rmsMatch(dry, out);
        std::printf("%10s %10.2f %10.2f %+12.2f %7d smp\n", dawlab::dsp::distortionTypeName(p.type),
                    dawlab::gainToDb(peakBefore), dawlab::gainToDb(rmsBefore), compDb, d.getLatencySamples());
        dawlab::writeWav(dawlab::outputPath(p.wavName), out, kSampleRate);
    }
    std::printf("已輸出 dawlab_output/l07_types_*.wav\n");

    // ---- (4) 實際量 latency：兩條路徑（oversampled shaper / bitcrush + FixedDelay）真的對齊嗎？ ----
    // getLatencySamples() 只是一個「宣稱」；這裡用 cross-correlation 找出輸出相對輸入真正延遲了幾個 sample。
    // drive 0 dB 時 HardClip / Foldback / Bitcrush（24 bit）幾乎是透明的；Fuzz 只是把小訊號吃掉一點；
    // Rectify 改用 half-wave（full-wave 的 |u| 是偶函數，和輸入的相關係數是 0，量不到）。
    std::printf("\n(4) 量測實際 latency（white noise 0.3、drive 0 dB、4x oversampling）與 mix 50%% 的 comb filtering\n");
    std::printf("%10s %12s %12s %16s\n", "type", "宣稱(smp)", "實測(smp)", "mix 50%−100%(dB)");
    constexpr int kExpectedLatency4x = 36; // 2.8 節的公式：(124 + 22) / 4 = 36.5 → 36
    const dawlab::AudioBuffer noise = dawlab::makeWhiteNoise(kSampleRate, 0.5, 0.3f, 1);
    for (const Preset& p : kPresets) {
        auto makeDistortion = [&](float mix) {
            Distortion d;
            d.prepare(kSampleRate, kBlockSize, 1);
            configure(d, p);
            d.setDriveDb(0.0f);
            d.setToneHz(Distortion::kToneBypassHz);
            d.setBitDepth(24.0f);
            d.setDownsampleFactor(1);
            d.setRectifyMode(dawlab::dsp::RectifyMode::HalfWave);
            d.setMix(mix);
            return d;
        };

        // cross-correlation：r(lag) = Σ y[n]·x[n − lag]，最大值所在的 lag 就是延遲
        dawlab::AudioBuffer out = noise;
        Distortion d = makeDistortion(1.0f);
        processInBlocks(out, d);
        const float* x = noise.getReadPointer(0);
        const float* y = out.getReadPointer(0);
        const int n = noise.getNumSamples();
        int bestLag = -1;
        double bestR = 0.0;
        for (int lag = 0; lag < 200; ++lag) {
            double r = 0.0;
            for (int i = 4800; i < n; ++i) // 跳過開頭 0.1 秒（DC blocker 的 transient）
                r += static_cast<double>(y[i]) * static_cast<double>(x[i - lag]);
            if (r > bestR) {
                bestR = r;
                bestLag = lag;
            }
        }

        // mix 50% vs 100%：wet 與 dry 對齊時兩者在 15 kHz 一樣大；錯位的話會 comb filtering（某些頻率互相抵消）
        auto gainAt15k = [&](float mix) {
            dawlab::AudioBuffer s = dawlab::makeSine(15000.0, kSampleRate, 0.5, 0.1f, 1);
            Distortion dm = makeDistortion(mix);
            processInBlocks(s, dm);
            const int skip = 4800;
            return dawlab::gainToDb(dawlab::rms(s.getReadPointer(0) + skip, s.getNumSamples() - skip) /
                                    (0.1f / std::sqrt(2.0f)));
        };
        const bool fuzzOrRectify = p.type == DistortionType::Fuzz || p.type == DistortionType::Rectify;
        const float combDb = fuzzOrRectify ? 0.0f : gainAt15k(0.5f) - gainAt15k(1.0f);

        std::printf("%10s %12d %12d", dawlab::dsp::distortionTypeName(p.type), d.getLatencySamples(), bestLag);
        if (fuzzOrRectify)
            std::printf(" %16s\n", "-（非線性太強）");
        else
            std::printf(" %+16.3f\n", combDb);
        ok = ok && bestLag == d.getLatencySamples() && d.getLatencySamples() == kExpectedLatency4x;
        ok = ok && std::abs(combDb) < 0.1f;
    }
    std::printf("self-check：每種 type 實測延遲 = getLatencySamples() = %d、mix 50%% 與 100%% 在 15 kHz 差 < 0.1 dB\n",
                kExpectedLatency4x);

    std::printf("\n%s\n", ok ? "全部正常。" : "[FAIL] 有項目不符合預期！");
    return ok ? 0 : 1;
}
