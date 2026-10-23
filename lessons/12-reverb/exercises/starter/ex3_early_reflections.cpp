// Exercise 3 starter: early reflections -- compute first-order reflections with the image-source method
// and feed them into a TappedDelay
//
// Goals:
//   1. Complete firstOrderReflections(): compute one image source for each of the 6 walls of a shoebox room,
//      convert each to a TappedDelay::Tap (delay relative to the direct sound, gain relative to the direct
//      sound), and sort by delay.
//   2. Complete EarlyReflections::process(): process sample by sample with TappedDelay.
//   3. Chain the ER with Reverb (the late part) to process a pluck sequence and write WAVs.
//
// Formulas (README section 2.9):
//   image: mirror the source across the wall, e.g. wall x = 0 -> (-sx, sy, sz), wall x = Lx -> (2Lx - sx, sy, sz)
//   delay = (|image - listener| - |source - listener|) / c, with c = 343 m/s
//   gain  = sqrt(1 - alpha) * |source - listener| / |image - listener|
//
// At run time the self-checks print FAIL; after you complete the TODOs they should all PASS.

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
    double absorption; // alpha
};

double distance(Vec3 a, Vec3 b) {
    const double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

std::vector<TappedDelay::Tap> firstOrderReflections(const Room& room, Vec3 source, Vec3 listener, double sampleRate) {
    std::vector<TappedDelay::Tap> taps;
    // TODO: compute the 6 image sources and each one's delay (in samples) and gain, push them into taps,
    //       and finally sort by delay
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
        // TODO: for each channel call delay_[ch].processSample() sample by sample (the output contains only
        //       the reflections, not the direct sound)
        (void)block;
    }

private:
    TappedDelay delay_[2];
    std::vector<TappedDelay::Tap> taps_[2];
    int maxDelay_ = 1;
};

} // namespace

int main() {
    std::printf("=== Exercise 3: early reflections (starter) ===\n");

    const Room room{{6.0, 4.0, 3.0}, 0.3};
    const Vec3 source{2.0, 1.5, 1.6};
    const Vec3 listener{4.3, 2.6, 1.2};

    const auto taps = firstOrderReflections(room, source, listener, kSampleRate);
    for (const auto& t : taps)
        std::printf("  delay %5d samples (%.3f ms), gain %.4f\n", t.delaySamples, samplesToMs(t.delaySamples, kSampleRate),
                    t.gain);

    DAWLAB_CHECK(taps.size() == 6);
    if (taps.size() == 6) {
        DAWLAB_CHECK(taps[0].delaySamples == 169); // floor reflection (worked by hand in solutions/README.md)
        DAWLAB_CHECK_NEAR(taps[0].gain, 0.570, 0.005);
        DAWLAB_CHECK(taps[5].delaySamples == 536); // the wall at x = 0
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

    // TODO: process makePluckSequence with ER + Reverb (pre-delay = the latest tap), and write
    //       dawlab_output/l12_ex3_er_only.wav and l12_ex3_er_late.wav

    return dawlab::test::summary("l12_ex3_starter");
}
