// 練習 3（起始碼）：實作 wavefolder
//
// 目標：
//   (a) 寫出 foldTriangle(u, threshold)：超過 ±threshold 的部分「折回來」，可以折很多次（週期 4·threshold 的三角波）。
//       threshold = 1 時必須與 dawlab::dsp::Distortion::foldback() 完全一致。
//   (b) 寫出 foldSine(u)：y = sin(π u / 2)。|u| ≤ 1 時像 soft clip，之後平滑地折返（Buchla 風格 folder 的簡化版）。
//   (c) 把 (a) 包成 Wavefolder class（標準介面 + setDriveDb + setThreshold），在 4x oversampling 下處理 block。
//
// 執行：l07_ex3_starter → 目前會因為 TODO 而 FAIL，寫完後應該全部 PASS。

#include <dawlab/Common.h>
#include <dawlab/dsp/Distortion.h>
#include <dawlab/dsp/Oversampler.h>

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

// (a) 三角 folder：把 u 映射到週期 4T 的三角波，|u| ≤ T 時 y = u
float foldTriangle(float u, float threshold) {
    // TODO: 提示——先把 u + T 對 4T 取餘數（std::fmod；負數要補回正），得到 t ∈ [0, 4T)，
    //       t < 2T 時 y = t - T，否則 y = 3T - t。
    (void)threshold;
    return u;
}

// (b) 正弦 folder：y = sin(π u / 2)
float foldSine(float u) {
    // TODO
    return u;
}

// (c) Wavefolder：drive → oversample → foldTriangle → downsample
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
        // TODO: 1. upsample  2. 每個 sample y = foldTriangle(x * drive_, threshold_)  3. downsample
        (void)block;
    }

private:
    dawlab::dsp::Oversampler oversampler_{2};
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

    // (a) threshold = 1 時與 Distortion::foldback 一致；幾個手算的點
    DAWLAB_CHECK_NEAR(foldTriangle(0.5f, 1.0f), 0.5, 1e-6);
    DAWLAB_CHECK_NEAR(foldTriangle(1.5f, 1.0f), 0.5, 1e-6);
    DAWLAB_CHECK_NEAR(foldTriangle(2.0f, 1.0f), 0.0, 1e-6);
    DAWLAB_CHECK_NEAR(foldTriangle(3.0f, 1.0f), -1.0, 1e-6);
    DAWLAB_CHECK_NEAR(foldTriangle(-2.5f, 1.0f), 0.5, 1e-6);
    DAWLAB_CHECK_NEAR(foldTriangle(1.0f, 0.5f), 0.0, 1e-6);  // threshold 0.5：u = 1 剛好折到 0
    DAWLAB_CHECK_NEAR(foldTriangle(0.75f, 0.5f), 0.25, 1e-6);
    float maxDiff = 0.0f;
    for (int i = -400; i <= 400; ++i) {
        const float u = 0.025f * static_cast<float>(i);
        maxDiff = std::max(maxDiff, std::abs(foldTriangle(u, 1.0f) - Distortion::foldback(u)));
    }
    DAWLAB_CHECK(maxDiff < 1e-5f);

    // (b) sine folder
    DAWLAB_CHECK_NEAR(foldSine(0.0f), 0.0, 1e-6);
    DAWLAB_CHECK_NEAR(foldSine(1.0f), 1.0, 1e-6);
    DAWLAB_CHECK_NEAR(foldSine(2.0f), 0.0, 1e-6);
    DAWLAB_CHECK_NEAR(foldSine(3.0f), -1.0, 1e-6);
    DAWLAB_CHECK_NEAR(foldSine(0.1f), std::sin(0.1 * dawlab::kPi / 2.0), 1e-6);

    // (c) Wavefolder：小訊號透明、大訊號折返且輸出不超過 threshold
    {
        Wavefolder wf;
        wf.prepare(kSampleRate, kBlockSize, 1);
        wf.setThreshold(1.0f);
        wf.setDriveDb(0.0f);
        dawlab::AudioBuffer small = dawlab::makeSine(220.0, kSampleRate, 1.0, 0.25f, 1);
        processInBlocks(small, wf);
        const int skip = dawlab::secondsToSamples(0.2, kSampleRate), n = dawlab::secondsToSamples(0.5, kSampleRate);
        const double thdSmall = dawlab::thdPercent(small.getReadPointer(0) + skip, n, 220.0, kSampleRate, 20);
        std::printf("drive 0 dB、0.25 振幅：THD = %.4f%%（應接近 0）\n", thdSmall);
        DAWLAB_CHECK(thdSmall < 0.1);

        wf.reset();
        wf.setDriveDb(18.0f); // 0.25 × 7.94 ≈ 2：會折返一次
        dawlab::AudioBuffer big = dawlab::makeSine(220.0, kSampleRate, 1.0, 0.25f, 1);
        processInBlocks(big, wf);
        const double thdBig = dawlab::thdPercent(big.getReadPointer(0) + skip, n, 220.0, kSampleRate, 20);
        const float peak = dawlab::peakAbs(big);
        std::printf("drive 18 dB：THD = %.1f%%，peak = %.3f（≤ 1 + 一點點 filter overshoot）\n", thdBig, peak);
        DAWLAB_CHECK(thdBig > 20.0);
        DAWLAB_CHECK(peak < 1.1f);
        DAWLAB_CHECK(!dawlab::hasNanOrInf(big));
        std::printf("latency = %d samples\n", wf.getLatencySamples());
        DAWLAB_CHECK(wf.getLatencySamples() > 0);
    }

    return dawlab::test::summary("l07_ex3");
}
