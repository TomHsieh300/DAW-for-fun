// 練習 3 解答：early reflections —— image-source 法算一階反射，餵進 TappedDelay
//
// 想法：在一個長方體（shoebox）房間裡，牆面反射的聲音「看起來」像從牆的另一側、鏡像位置的
// 一個虛擬音源（image source）發出來。一階反射有 6 個（6 面牆），每個反射的
//   延遲   = (|image - listener| - |source - listener|) / c      （相對於直達音）
//   增益   = r · |source - listener| / |image - listener|          （r = sqrt(1 - α)：牆面反射係數；1/d 距離衰減）
// 把這 6 個 (delay, gain) 當作 TappedDelay 的 tap，就是 early reflections；
// 之後再接上 FDN 當 late reverb（pre-delay 設成最晚一個 tap 的時間），就是「ER + late」的完整結構。
//
// 輸出：dawlab_output/l12_ex3_er_only.wav（dry + ER）、l12_ex3_er_late.wav（dry + ER + late）

#include <dawlab/Common.h>
#include <dawlab/dsp/Reverb.h>
#include <dawlab/dsp/ReverbBuildingBlocks.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace dawlab;
using namespace dawlab::dsp;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr double kSpeedOfSound = 343.0; // m/s（20 °C 的空氣）

struct Vec3 {
    double x, y, z;
};

struct Room {
    Vec3 size;         // 長、寬、高（m），牆面在 0 與 size
    double absorption; // 牆面吸音係數 α（0 = 全反射，1 = 全吸收）
};

double distance(Vec3 a, Vec3 b) {
    const double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// 6 個一階 image source → 6 個 tap（依 delay 排序）
std::vector<TappedDelay::Tap> firstOrderReflections(const Room& room, Vec3 source, Vec3 listener, double sampleRate) {
    const double dDirect = distance(source, listener);
    const double r = std::sqrt(1.0 - room.absorption); // 能量吸收 α → 振幅反射係數 sqrt(1-α)
    const Vec3 images[6] = {
        {-source.x, source.y, source.z},                   // 牆 x = 0
        {2.0 * room.size.x - source.x, source.y, source.z}, // 牆 x = Lx
        {source.x, -source.y, source.z},                   // 牆 y = 0
        {source.x, 2.0 * room.size.y - source.y, source.z}, // 牆 y = Ly
        {source.x, source.y, -source.z},                   // 地板 z = 0
        {source.x, source.y, 2.0 * room.size.z - source.z}, // 天花板 z = Lz
    };
    std::vector<TappedDelay::Tap> taps;
    for (const Vec3& img : images) {
        const double d = distance(img, listener);
        TappedDelay::Tap t;
        t.delaySamples = static_cast<int>(std::lround((d - dDirect) / kSpeedOfSound * sampleRate));
        t.gain = static_cast<float>(r * dDirect / d);
        taps.push_back(t);
    }
    std::sort(taps.begin(), taps.end(),
              [](const TappedDelay::Tap& a, const TappedDelay::Tap& b) { return a.delaySamples < b.delaySamples; });
    return taps;
}

// stereo early reflections：左右耳各自一組 tap（耳距 0.16 m，沿 y 軸）
class EarlyReflections {
public:
    void prepare(const Room& room, Vec3 source, Vec3 listener, double sampleRate) {
        const Vec3 earL{listener.x, listener.y + 0.08, listener.z};
        const Vec3 earR{listener.x, listener.y - 0.08, listener.z};
        taps_[0] = firstOrderReflections(room, source, earL, sampleRate);
        taps_[1] = firstOrderReflections(room, source, earR, sampleRate);
        int maxDelay = 1;
        for (const auto& set : taps_)
            for (const auto& t : set)
                maxDelay = std::max(maxDelay, t.delaySamples);
        for (int ch = 0; ch < 2; ++ch) {
            delay_[ch].prepare(maxDelay + 1);
            delay_[ch].setTaps(taps_[static_cast<size_t>(ch)]);
        }
        maxDelay_ = maxDelay;
    }
    void reset() noexcept {
        for (auto& d : delay_)
            d.reset();
    }
    int getMaxDelaySamples() const noexcept { return maxDelay_; }
    const std::vector<TappedDelay::Tap>& getTaps(int ch) const noexcept { return taps_[static_cast<size_t>(ch)]; }

    // 只輸出反射（不含直達音）
    void process(AudioBlock block) noexcept {
        for (int ch = 0; ch < std::min(2, block.getNumChannels()); ++ch) {
            float* d = block.getChannelPointer(ch);
            for (int i = 0; i < block.getNumSamples(); ++i)
                d[i] = delay_[ch].processSample(d[i]);
        }
    }

private:
    TappedDelay delay_[2];
    std::vector<TappedDelay::Tap> taps_[2];
    int maxDelay_ = 1;
};

} // namespace

int main() {
    std::printf("=== 練習 3：early reflections（image-source → tapped delay）===\n");

    const Room room{{6.0, 4.0, 3.0}, 0.3};
    const Vec3 source{2.0, 1.5, 1.6};
    const Vec3 listener{4.3, 2.6, 1.2};

    // ---- 1. 單一聽者位置的 6 個 tap（與 solutions/README.md 的手算對照）-------------
    const auto taps = firstOrderReflections(room, source, listener, kSampleRate);
    std::printf("直達音距離 %.4f m；6 個一階反射（相對直達音）：\n", distance(source, listener));
    std::printf("%8s %10s %8s\n", "delay(sp)", "delay(ms)", "gain");
    for (const auto& t : taps)
        std::printf("%8d %10.3f %8.4f\n", t.delaySamples, samplesToMs(t.delaySamples, kSampleRate), t.gain);

    DAWLAB_CHECK(taps.size() == 6);
    // 手算：地板反射 image (2, 1.5, -1.6)，d = sqrt(2.3² + 1.1² + 2.8²) = 3.7868 m，
    // 直達 2.5807 m → delay = 1.2061 / 343 = 3.516 ms = 169 samples；gain = sqrt(0.7)·2.5807/3.7868 = 0.570
    DAWLAB_CHECK(taps[0].delaySamples == 169);
    DAWLAB_CHECK_NEAR(taps[0].gain, 0.570, 0.005);
    // 最遠的是 x = 0 那面牆：image (-2, 1.5, 1.6)，d = sqrt(6.3² + 1.1² + 0.4²) = 6.4078 m
    // → delay = 3.8271 / 343 = 11.158 ms = 536 samples；gain = 0.8367·2.5807/6.4078 = 0.337
    DAWLAB_CHECK(taps[5].delaySamples == 536);
    DAWLAB_CHECK_NEAR(taps[5].gain, 0.337, 0.005);
    // 每個 tap 的 gain 都小於 1（反射一定比直達音小），delay 嚴格遞增
    for (size_t i = 0; i < taps.size(); ++i) {
        DAWLAB_CHECK(taps[i].gain > 0.0f && taps[i].gain < 1.0f);
        if (i > 0)
            DAWLAB_CHECK(taps[i].delaySamples >= taps[i - 1].delaySamples);
    }

    // ---- 2. TappedDelay 的 impulse response 就是這些 tap ----------------------------
    EarlyReflections er;
    er.prepare(room, source, listener, kSampleRate);
    {
        AudioBuffer ir = makeImpulse(er.getMaxDelaySamples() + 16, 2);
        er.process(ir);
        const auto& tapsL = er.getTaps(0);
        int nonZero = 0;
        for (int i = 0; i < ir.getNumSamples(); ++i)
            if (std::abs(ir.getSample(0, i)) > 0.0f)
                ++nonZero;
        DAWLAB_CHECK(nonZero == static_cast<int>(tapsL.size()));
        DAWLAB_CHECK(ir.getSample(0, 0) == 0.0f); // 沒有直達音
        for (const auto& t : tapsL)
            DAWLAB_CHECK_NEAR(ir.getSample(0, t.delaySamples), t.gain, 1e-6);
        std::printf("左耳 impulse response 有 %d 個非零 sample（= tap 數），位置與增益都符合\n", nonZero);
    }

    // ---- 3. ER + late reverb 處理 pluck sequence ------------------------------------
    {
        AudioBuffer dry = makePluckSequence(kSampleRate, 4.0, 0.4f, 2);
        const int n = dry.getNumSamples();

        AudioBuffer early = dry;
        er.reset();
        for (int start = 0; start < n; start += kBlockSize)
            er.process(early.toBlock().getSubBlock(start, std::min(kBlockSize, n - start)));

        // late reverb：pre-delay 設成最晚的一階反射，讓 late tail 接在 ER 之後
        Reverb late;
        late.prepare(kSampleRate, kBlockSize, 2);
        late.setMix(1.0f);
        late.setDecaySeconds(1.2f);
        late.setDampingHz(5000.0f);
        late.setSize(0.4f);
        late.setPreDelayMs(static_cast<float>(samplesToMs(er.getMaxDelaySamples(), kSampleRate)));
        late.reset(); // 參數都設好了：reset 讓 mix / pre-delay 等平滑參數直接就位，不要從預設值滑過去
        AudioBuffer lateOut = dry;
        for (int start = 0; start < n; start += kBlockSize)
            late.process(lateOut.toBlock().getSubBlock(start, std::min(kBlockSize, n - start)));

        AudioBuffer erOnly = dry, full = dry;
        for (int ch = 0; ch < 2; ++ch) {
            erOnly.addFrom(ch, 0, early, ch, 0, n, 0.8f);
            full.addFrom(ch, 0, early, ch, 0, n, 0.8f);
            full.addFrom(ch, 0, lateOut, ch, 0, n, 0.5f);
        }
        DAWLAB_CHECK(!hasNanOrInf(full));
        DAWLAB_CHECK(peakAbs(full) < 1.0f);
        // 反射加上去之後能量一定變多
        DAWLAB_CHECK(rms(erOnly, 0) > rms(dry, 0));
        DAWLAB_CHECK(rms(full, 0) > rms(erOnly, 0));
        std::printf("RMS: dry %.4f, dry+ER %.4f, dry+ER+late %.4f（peak %.3f）\n", rms(dry, 0), rms(erOnly, 0),
                    rms(full, 0), peakAbs(full));
        writeWav(outputPath("l12_ex3_er_only.wav"), erOnly, kSampleRate);
        writeWav(outputPath("l12_ex3_er_late.wav"), full, kSampleRate);
    }

    return dawlab::test::summary("l12_ex3");
}
