// 練習 4（解答）：10-band graphic EQ
//
// Graphic EQ = 固定中心頻率（ISO 266 的 octave 中心：31.5, 63, 125 … 16k Hz）、固定 Q 的 peak 串接，
// 使用者只能推每個 band 的 gain。相鄰 band 相距 1 octave，所以每個 band 的頻寬也取 1 octave，
// 對應 Q = 2^(1/2) / (2^1 − 1) = √2 ≈ 1.414（練習 2 的公式）。
//
// 重點觀察：graphic EQ 的 band 會互相「漏」——把 1 kHz 推 +6 dB，500 Hz 與 2 kHz 也會跟著 +1.1 dB；
// 把所有 band 都推 +6 dB，1 kHz 處不是 +6 而是將近 +9 dB（相鄰 band 的尾巴相加）。
// 這就是為什麼 graphic EQ 的「推桿形狀」跟實際曲線不一樣，也是 proportional-Q / constant-Q 設計
// 想解決的問題之一（README 2.11 節）。

#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>

#include <array>
#include <cstdio>

using namespace dawlab;
using namespace dawlab::dsp;

namespace {

class GraphicEq10 {
public:
    static constexpr int kNumBands = 10;
    static constexpr double kBandwidthOctaves = 1.0;
    // Q = 2^(BW/2) / (2^BW − 1)，BW = 1 → √2
    static constexpr double kQ = 1.41421356237309505;

    static double centreFrequency(int band) noexcept { return 31.25 * std::pow(2.0, band); } // 31.25 … 16000 Hz

    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        sampleRate_ = sampleRate;
        for (auto& f : filters_)
            f.prepare(sampleRate, maxBlockSize, numChannels);
        for (int i = 0; i < kNumBands; ++i)
            updateBand(i);
        reset();
    }

    void reset() noexcept {
        for (auto& f : filters_)
            f.reset();
    }

    // 只能改 gain（這就是 graphic EQ）；在 block 之間呼叫
    void setGainDb(int band, float gainDb) noexcept {
        gainsDb_[static_cast<size_t>(band)] = std::clamp(gainDb, -12.0f, 12.0f);
        updateBand(band);
    }
    float getGainDb(int band) const noexcept { return gainsDb_[static_cast<size_t>(band)]; }

    // 整條曲線 = 每個 band 的 dB 相加（0 dB 的 band 貢獻 0）
    double getMagnitudeDb(double freq) const noexcept {
        double total = 0.0;
        for (const auto& f : filters_)
            total += f.getCoefficients().magnitudeDb(freq, sampleRate_);
        return total;
    }

    float processSample(int ch, float x) noexcept {
        for (auto& f : filters_)
            x = f.processSample(ch, x);
        return x;
    }

    void process(AudioBlock block) noexcept {
        for (auto& f : filters_)
            f.process(block);
    }

private:
    void updateBand(int band) noexcept {
        filters_[static_cast<size_t>(band)].setCoefficients(
            BiquadCoefficients::makePeak(sampleRate_, centreFrequency(band), kQ, gainsDb_[static_cast<size_t>(band)]));
    }

    double sampleRate_ = 48000.0;
    std::array<Biquad, kNumBands> filters_;
    std::array<float, kNumBands> gainsDb_{};
};

} // namespace

int main() {
    constexpr double sr = 48000.0;
    std::printf("=== 第 03 章 練習 4：10-band graphic EQ ===\n\n");

    GraphicEq10 eq;
    eq.prepare(sr, 512, 1);
    std::printf("  band 中心頻率：");
    for (int i = 0; i < GraphicEq10::kNumBands; ++i)
        std::printf(" %.5g", GraphicEq10::centreFrequency(i));
    std::printf(" Hz，Q = %.4f（1 octave）\n", GraphicEq10::kQ);

    // ---- 1. 全部 0 dB：是透明的 ----
    const double flat = measureGainDb([&](float x) { return eq.processSample(0, x); }, 1000.0, sr);
    std::printf("\n  全部 0 dB：1 kHz 實測 %+.3f dB\n", flat);
    DAWLAB_CHECK_NEAR(flat, 0.0, 0.01);

    // ---- 2. 只推 1 kHz band +6 dB：中心 +6，相鄰 band 中心約 +1.14 dB（1 octave 外的尾巴）----
    eq.setGainDb(5, 6.0f);
    eq.reset();
    std::printf("  只推 1 kHz +6 dB：\n");
    for (double f : {250.0, 500.0, 707.1, 1000.0, 1414.2, 2000.0, 4000.0}) {
        eq.reset();
        const double m = measureGainDb([&](float x) { return eq.processSample(0, x); }, f, sr);
        std::printf("    %7.1f Hz: 理論 %+6.2f dB, 實測 %+6.2f dB\n", f, eq.getMagnitudeDb(f), m);
        DAWLAB_CHECK_NEAR(m, eq.getMagnitudeDb(f), 0.05);
    }
    DAWLAB_CHECK_NEAR(eq.getMagnitudeDb(1000.0), 6.0, 0.01);
    DAWLAB_CHECK_NEAR(eq.getMagnitudeDb(1414.2), 3.0, 0.1);  // 中點增益邊緣（Q = √2 → ±1/2 octave）
    DAWLAB_CHECK_NEAR(eq.getMagnitudeDb(500.0), 1.14, 0.1);  // 1 octave 外：類比原型算出 +1.137 dB
    DAWLAB_CHECK_NEAR(eq.getMagnitudeDb(2000.0), 1.14, 0.15);

    // ---- 3. 全部 +6 dB：band 互相相加，中心處遠超過 +6 ----
    for (int i = 0; i < GraphicEq10::kNumBands; ++i)
        eq.setGainDb(i, 6.0f);
    eq.reset();
    std::printf("  全部 +6 dB：\n");
    double minDb = 100.0, maxDb = -100.0;
    for (double f = 125.0; f <= 4000.0; f *= std::pow(2.0, 0.25)) {
        const double db = eq.getMagnitudeDb(f);
        minDb = std::min(minDb, db);
        maxDb = std::max(maxDb, db);
    }
    std::printf("    1 kHz 理論 %+.2f dB；125 Hz–4 kHz 之間 曲線在 %+.2f .. %+.2f dB（漣波 %.2f dB）\n",
                eq.getMagnitudeDb(1000.0), minDb, maxDb, maxDb - minDb);
    const double all6 = measureGainDb([&](float x) { return eq.processSample(0, x); }, 1000.0, sr);
    DAWLAB_CHECK_NEAR(all6, eq.getMagnitudeDb(1000.0), 0.05);
    DAWLAB_CHECK(eq.getMagnitudeDb(1000.0) > 8.0); // 不是 6 dB：相鄰 band 的尾巴疊上來（理論約 +8.9）
    DAWLAB_CHECK(maxDb - minDb < 2.5);              // 中頻段約 2 dB 的漣波：band 之間的「縫」沒有完全補平

    // ---- 4. 經典「笑臉」曲線：低高頻提、中頻挖 ----
    const float smile[GraphicEq10::kNumBands] = {6.0f, 4.0f, 2.0f, 0.0f, -3.0f, -3.0f, 0.0f, 2.0f, 4.0f, 6.0f};
    for (int i = 0; i < GraphicEq10::kNumBands; ++i)
        eq.setGainDb(i, smile[i]);
    eq.reset();
    AudioBuffer dry = makeDrumLoop(sr, 2);
    AudioBuffer wet = dry;
    AudioBlock whole(wet);
    for (int start = 0; start < wet.getNumSamples(); start += 512)
        eq.process(whole.getSubBlock(start, std::min(512, wet.getNumSamples() - start)));
    DAWLAB_CHECK(!hasNanOrInf(wet));
    std::printf("  笑臉曲線套在 drum loop：peak %+.2f -> %+.2f dBFS -> dawlab_output/l03_ex4_graphic_smile.wav\n",
                gainToDb(peakAbs(dry)), gainToDb(peakAbs(wet)));
    writeWav(outputPath("l03_ex4_graphic_smile.wav"), wet, sr);

    return dawlab::test::summary("l03_ex4");
}
