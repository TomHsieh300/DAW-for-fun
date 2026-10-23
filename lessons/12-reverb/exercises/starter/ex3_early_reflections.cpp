// 練習 3 起始碼：early reflections —— image-source 法算一階反射，餵進 TappedDelay
//
// 目標：
//   1. 完成 firstOrderReflections()：對 shoebox 房間的 6 面牆各算一個 image source，
//      轉成 TappedDelay::Tap（delay 相對直達音、gain 相對直達音），依 delay 排序。
//   2. 完成 EarlyReflections::process()：用 TappedDelay 逐 sample 處理。
//   3. 把 ER 與 Reverb（late）串起來處理 pluck sequence，輸出 WAV。
//
// 公式（README 2.9 節）：
//   image：把 source 對牆面鏡射，例如牆 x = 0 → (-sx, sy, sz)，牆 x = Lx → (2Lx - sx, sy, sz)
//   delay = (|image - listener| - |source - listener|) / c，c = 343 m/s
//   gain  = sqrt(1 - α) · |source - listener| / |image - listener|
//
// 執行時 self-check 會印 FAIL，完成 TODO 後應該全部 PASS。

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
constexpr double kSpeedOfSound = 343.0;

struct Vec3 {
    double x, y, z;
};

struct Room {
    Vec3 size;
    double absorption; // α
};

double distance(Vec3 a, Vec3 b) {
    const double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

std::vector<TappedDelay::Tap> firstOrderReflections(const Room& room, Vec3 source, Vec3 listener, double sampleRate) {
    std::vector<TappedDelay::Tap> taps;
    // TODO: 算 6 個 image source、每個的 delay（samples）與 gain，push 進 taps，最後依 delay 排序
    (void)room;
    (void)source;
    (void)listener;
    (void)sampleRate;
    return taps;
}

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

    void process(AudioBlock block) noexcept {
        // TODO: 每個 channel 逐 sample 呼叫 delay_[ch].processSample()（輸出只有反射、沒有直達音）
        (void)block;
    }

private:
    TappedDelay delay_[2];
    std::vector<TappedDelay::Tap> taps_[2];
    int maxDelay_ = 1;
};

} // namespace

int main() {
    std::printf("=== 練習 3：early reflections（起始碼）===\n");

    const Room room{{6.0, 4.0, 3.0}, 0.3};
    const Vec3 source{2.0, 1.5, 1.6};
    const Vec3 listener{4.3, 2.6, 1.2};

    const auto taps = firstOrderReflections(room, source, listener, kSampleRate);
    for (const auto& t : taps)
        std::printf("  delay %5d samples (%.3f ms), gain %.4f\n", t.delaySamples, samplesToMs(t.delaySamples, kSampleRate),
                    t.gain);

    DAWLAB_CHECK(taps.size() == 6);
    if (taps.size() == 6) {
        DAWLAB_CHECK(taps[0].delaySamples == 169); // 地板反射（手算見 solutions/README.md）
        DAWLAB_CHECK_NEAR(taps[0].gain, 0.570, 0.005);
        DAWLAB_CHECK(taps[5].delaySamples == 536); // x = 0 那面牆
        DAWLAB_CHECK_NEAR(taps[5].gain, 0.337, 0.005);
    }

    EarlyReflections er;
    er.prepare(room, source, listener, kSampleRate);
    {
        AudioBuffer ir = makeImpulse(er.getMaxDelaySamples() + 16, 2);
        er.process(ir);
        int nonZero = 0;
        for (int i = 0; i < ir.getNumSamples(); ++i)
            if (std::abs(ir.getSample(0, i)) > 0.0f)
                ++nonZero;
        DAWLAB_CHECK(nonZero == static_cast<int>(er.getTaps(0).size()) && nonZero > 0);
        DAWLAB_CHECK(ir.getSample(0, 0) == 0.0f);
    }

    // TODO: 用 ER + Reverb（pre-delay = 最晚的 tap）處理 makePluckSequence，輸出
    //       dawlab_output/l12_ex3_er_only.wav 與 l12_ex3_er_late.wav

    return dawlab::test::summary("l12_ex3_starter");
}
