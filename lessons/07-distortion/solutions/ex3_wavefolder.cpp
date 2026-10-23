// 練習 3 解答：wavefolder
//
// (a) 三角 folder 的核心是「把實數線捲到一個週期 4T 的三角波上」：
//       t = (u + T) mod 4T ∈ [0, 4T)
//       y = t - T        （t < 2T：上升段，u ∈ [-T, T] 時 y = u）
//       y = 3T - t       （t ≥ 2T：下降段）
//     這樣不管 u 多大都會自動折很多次；threshold = 1 時就是 Distortion::foldback()。
// (b) sin(π u / 2)：|u| ≤ 1 時單調（像很軟的 soft clip），之後開始「折」，而且折點是平滑的
//     （一階導數連續）——所以高次諧波衰減得比三角 folder 快，聲音比較圓。
// (c) folder 的輸出在折點有「角」（三角 folder）→ 無窮多諧波 → 一定要 oversampling。
//
// 執行：l07_ex3_solution

#include <dawlab/Common.h>
#include <dawlab/dsp/Distortion.h>
#include <dawlab/dsp/Oversampler.h>

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

float foldTriangle(float u, float threshold) {
    const float T = threshold;
    const float period = 4.0f * T;
    float t = std::fmod(u + T, period); // (-4T, 4T)
    if (t < 0.0f)
        t += period;                    // [0, 4T)
    return t < 2.0f * T ? t - T : 3.0f * T - t;
}

float foldSine(float u) {
    return static_cast<float>(std::sin(0.5 * dawlab::kPi * static_cast<double>(u)));
}

class Wavefolder {
public:
    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        oversampler_.prepare(sampleRate, maxBlockSize, numChannels);
        numChannels_ = numChannels;
    }
    void reset() noexcept { oversampler_.reset(); }
    void setDriveDb(float db) noexcept { drive_ = dawlab::dbToGain(db); }
    void setThreshold(float t) noexcept { threshold_ = std::max(0.05f, t); }
    int getLatencySamples() const noexcept { return oversampler_.getLatencySamples(); }

    void process(dawlab::AudioBlock block) noexcept {
        dawlab::AudioBlock up = oversampler_.upsample(block);
        const int nch = std::min(block.getNumChannels(), numChannels_);
        const int n = up.getNumSamples();
        for (int ch = 0; ch < nch; ++ch) {
            float* x = up.getChannelPointer(ch);
            for (int i = 0; i < n; ++i)
                x[i] = foldTriangle(x[i] * drive_, threshold_);
        }
        oversampler_.downsample(block);
    }

private:
    dawlab::dsp::Oversampler oversampler_{2}; // 4x
    int numChannels_ = 0;
    float drive_ = 1.0f;
    float threshold_ = 1.0f;
};

template <typename Processor>
void processInBlocks(dawlab::AudioBuffer& buffer, Processor& processor) {
    dawlab::AudioBlock whole(buffer);
    for (int start = 0; start < buffer.getNumSamples(); start += kBlockSize) {
        const int len = std::min(kBlockSize, buffer.getNumSamples() - start);
        processor.process(whole.getSubBlock(start, len));
    }
}

} // namespace

int main() {
    using dawlab::dsp::Distortion;

    // (a)
    DAWLAB_CHECK_NEAR(foldTriangle(0.5f, 1.0f), 0.5, 1e-6);
    DAWLAB_CHECK_NEAR(foldTriangle(1.5f, 1.0f), 0.5, 1e-6);
    DAWLAB_CHECK_NEAR(foldTriangle(2.0f, 1.0f), 0.0, 1e-6);
    DAWLAB_CHECK_NEAR(foldTriangle(3.0f, 1.0f), -1.0, 1e-6);
    DAWLAB_CHECK_NEAR(foldTriangle(-2.5f, 1.0f), 0.5, 1e-6);
    DAWLAB_CHECK_NEAR(foldTriangle(1.0f, 0.5f), 0.0, 1e-6);
    DAWLAB_CHECK_NEAR(foldTriangle(0.75f, 0.5f), 0.25, 1e-6);
    float maxDiff = 0.0f;
    for (int i = -400; i <= 400; ++i) {
        const float u = 0.025f * static_cast<float>(i);
        maxDiff = std::max(maxDiff, std::abs(foldTriangle(u, 1.0f) - Distortion::foldback(u)));
    }
    std::printf("(a) foldTriangle vs Distortion::foldback（u ∈ [-10, 10]）最大差異 = %.2g\n", maxDiff);
    DAWLAB_CHECK(maxDiff < 1e-5f);

    // (b)
    DAWLAB_CHECK_NEAR(foldSine(0.0f), 0.0, 1e-6);
    DAWLAB_CHECK_NEAR(foldSine(1.0f), 1.0, 1e-6);
    DAWLAB_CHECK_NEAR(foldSine(2.0f), 0.0, 1e-6);
    DAWLAB_CHECK_NEAR(foldSine(3.0f), -1.0, 1e-6);
    DAWLAB_CHECK_NEAR(foldSine(0.1f), std::sin(0.1 * dawlab::kPi / 2.0), 1e-6);

    // 補充：兩種 folder 在同樣 drive 下的諧波（static 曲線直接套在正弦上，無 oversampling，只看相對趨勢）
    {
        std::printf("(b) 220 Hz 正弦 × 2.0 經過兩種 folder 的諧波（相對於 H1，dB）\n");
        std::printf("%10s", "folder");
        for (int h = 1; h <= 9; ++h)
            std::printf("   H%d ", h);
        std::printf("\n");
        for (int which = 0; which < 2; ++which) {
            dawlab::AudioBuffer s = dawlab::makeSine(220.0, kSampleRate, 1.0, 2.0f, 1);
            float* x = s.getWritePointer(0);
            for (int i = 0; i < s.getNumSamples(); ++i)
                x[i] = which == 0 ? foldTriangle(x[i], 1.0f) : foldSine(x[i]);
            const auto levels = dawlab::harmonicLevelsDb(x, s.getNumSamples(), 220.0, kSampleRate, 9);
            std::printf("%10s", which == 0 ? "triangle" : "sine");
            for (double l : levels)
                std::printf(" %6.1f", l);
            std::printf("\n");
        }
    }

    // (c)
    {
        Wavefolder wf;
        wf.prepare(kSampleRate, kBlockSize, 1);
        wf.setThreshold(1.0f);
        wf.setDriveDb(0.0f);
        dawlab::AudioBuffer small = dawlab::makeSine(220.0, kSampleRate, 1.0, 0.25f, 1);
        processInBlocks(small, wf);
        const int skip = dawlab::secondsToSamples(0.2, kSampleRate), n = dawlab::secondsToSamples(0.5, kSampleRate);
        const double thdSmall = dawlab::thdPercent(small.getReadPointer(0) + skip, n, 220.0, kSampleRate, 20);
        std::printf("(c) drive 0 dB、0.25 振幅：THD = %.4f%%（應接近 0）\n", thdSmall);
        DAWLAB_CHECK(thdSmall < 0.1);

        wf.reset();
        wf.setDriveDb(18.0f);
        dawlab::AudioBuffer big = dawlab::makeSine(220.0, kSampleRate, 1.0, 0.25f, 1);
        processInBlocks(big, wf);
        const double thdBig = dawlab::thdPercent(big.getReadPointer(0) + skip, n, 220.0, kSampleRate, 20);
        const float peak = dawlab::peakAbs(big);
        std::printf("    drive 18 dB：THD = %.1f%%，peak = %.3f\n", thdBig, peak);
        DAWLAB_CHECK(thdBig > 20.0);
        DAWLAB_CHECK(peak < 1.1f);
        DAWLAB_CHECK(!dawlab::hasNanOrInf(big));
        std::printf("    latency = %d samples\n", wf.getLatencySamples());
        DAWLAB_CHECK(wf.getLatencySamples() > 0);

        // 聽感用：pluck sequence 經過 folder，drive 掃上去
        dawlab::AudioBuffer pluck = dawlab::makePluckSequence(kSampleRate, 4.0, 0.4f, 2);
        Wavefolder wf2;
        wf2.prepare(kSampleRate, kBlockSize, 2);
        wf2.setThreshold(0.5f);
        wf2.setDriveDb(14.0f);
        processInBlocks(pluck, wf2);
        dawlab::writeWav(dawlab::outputPath("l07_ex3_wavefolder.wav"), pluck, kSampleRate);
        std::printf("    已輸出 dawlab_output/l07_ex3_wavefolder.wav\n");
    }

    return dawlab::test::summary("l07_ex3");
}
