// 練習 4（起始碼）：10-band graphic EQ
//
// 目標：用 10 顆 peak Biquad 做一個 graphic EQ：
//   - 中心頻率 31.25 × 2^k Hz（k = 0..9），Q 由「1 octave 頻寬」換算（練習 2 的公式）。
//   - setGainDb(band, dB) 重算那顆 band 的係數；getMagnitudeDb 把每顆的 dB 相加。
//   - 觀察 band 之間的互相影響。
// 執行時 self-check 會印出 FAIL，直到你把 TODO 補完。

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
    static constexpr double kQ = 1.0; // TODO: 由 1 octave 頻寬換算成 Q（提示：2^(BW/2) / (2^BW − 1)）

    static double centreFrequency(int band) noexcept { return 31.25 * std::pow(2.0, band); }

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

    void setGainDb(int band, float gainDb) noexcept {
        gainsDb_[static_cast<size_t>(band)] = std::clamp(gainDb, -12.0f, 12.0f);
        updateBand(band);
    }
    float getGainDb(int band) const noexcept { return gainsDb_[static_cast<size_t>(band)]; }

    double getMagnitudeDb(double freq) const noexcept {
        (void)freq;
        return 0.0; // TODO: 每顆 filter 的 getCoefficients().magnitudeDb(freq, sampleRate_) 相加
    }

    float processSample(int ch, float x) noexcept {
        (void)ch;
        return x; // TODO: 依序通過 10 顆 filter
    }

    void process(AudioBlock block) noexcept {
        (void)block; // TODO
    }

private:
    void updateBand(int band) noexcept {
        (void)band; // TODO: filters_[band].setCoefficients(BiquadCoefficients::makePeak(...))
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

    const double flat = measureGainDb([&](float x) { return eq.processSample(0, x); }, 1000.0, sr);
    std::printf("  全部 0 dB：1 kHz 實測 %+.3f dB\n", flat);
    DAWLAB_CHECK_NEAR(flat, 0.0, 0.01);

    eq.setGainDb(5, 6.0f);
    eq.reset();
    for (double f : {500.0, 1000.0, 1414.2, 2000.0}) {
        eq.reset();
        const double m = measureGainDb([&](float x) { return eq.processSample(0, x); }, f, sr);
        std::printf("    %7.1f Hz: 理論 %+6.2f dB, 實測 %+6.2f dB\n", f, eq.getMagnitudeDb(f), m);
        DAWLAB_CHECK_NEAR(m, eq.getMagnitudeDb(f), 0.05);
    }
    DAWLAB_CHECK_NEAR(eq.getMagnitudeDb(1000.0), 6.0, 0.01);
    DAWLAB_CHECK_NEAR(eq.getMagnitudeDb(1414.2), 3.0, 0.1);
    DAWLAB_CHECK_NEAR(eq.getMagnitudeDb(500.0), 1.14, 0.1);

    for (int i = 0; i < GraphicEq10::kNumBands; ++i)
        eq.setGainDb(i, 6.0f);
    eq.reset();
    std::printf("  全部 +6 dB：1 kHz 理論 %+.2f dB\n", eq.getMagnitudeDb(1000.0));
    DAWLAB_CHECK(eq.getMagnitudeDb(1000.0) > 8.0); // 相鄰 band 的尾巴會疊上來

    // TODO（第 4 小題，試聽）：把「笑臉曲線」套在 drum loop 上
    //   const float smile[GraphicEq10::kNumBands] = {6, 4, 2, 0, -3, -3, 0, 2, 4, 6};
    //   AudioBuffer wet = makeDrumLoop(sr, 2);  用 eq.prepare(sr, 512, wet.getNumChannels()) 重新 prepare、
    //   設好 10 個 gain，以 512 sample 為一個 block 呼叫 eq.process(...)，印出處理前後的 peak（dBFS），
    //   最後 writeWav(outputPath("l03_ex4_graphic_smile.wav"), wet, sr)。

    return dawlab::test::summary("l03_ex4_starter");
}
