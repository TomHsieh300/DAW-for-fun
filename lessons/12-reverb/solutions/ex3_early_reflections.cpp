// Exercise 3 solution: early reflections -- compute first-order reflections with the image-source method
// and feed them into a TappedDelay
//
// Idea: in a rectangular (shoebox) room, a sound reflected off a wall "appears" to come from a virtual
// source (an image source) at the mirrored position on the other side of the wall. There are 6 first-order
// reflections (one per wall), and for each of them
//   delay = (|image - listener| - |source - listener|) / c      (relative to the direct sound)
//   gain  = r * |source - listener| / |image - listener|         (r = sqrt(1 - alpha): wall reflection coefficient; 1/d distance attenuation)
// Using these 6 (delay, gain) pairs as the taps of a TappedDelay gives the early reflections;
// connecting an FDN after it as the late reverb (with the pre-delay set to the time of the latest tap)
// gives the complete "ER + late" structure.
//
// Output: dawlab_output/l12_ex3_er_only.wav (dry + ER), l12_ex3_er_late.wav (dry + ER + late)

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
constexpr double kSpeedOfSound = 343.0; // m/s (air at 20 degrees C)

struct Vec3 {
    double x, y, z;
};

struct Room {
    Vec3 size;         // length, width, height (m); the walls are at 0 and at size
    double absorption; // wall absorption coefficient alpha (0 = fully reflective, 1 = fully absorptive)
};

double distance(Vec3 a, Vec3 b) {
    const double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// 6 first-order image sources -> 6 taps (sorted by delay)
std::vector<TappedDelay::Tap> firstOrderReflections(const Room& room, Vec3 source, Vec3 listener, double sampleRate) {
    const double dDirect = distance(source, listener);
    const double r = std::sqrt(1.0 - room.absorption); // energy absorption alpha -> amplitude reflection coefficient sqrt(1-alpha)
    const Vec3 images[6] = {
        {-source.x, source.y, source.z},                    // wall x = 0
        {2.0 * room.size.x - source.x, source.y, source.z}, // wall x = Lx
        {source.x, -source.y, source.z},                    // wall y = 0
        {source.x, 2.0 * room.size.y - source.y, source.z}, // wall y = Ly
        {source.x, source.y, -source.z},                    // floor z = 0
        {source.x, source.y, 2.0 * room.size.z - source.z}, // ceiling z = Lz
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

// Stereo early reflections: a separate set of taps for the left and right ear (ear spacing 0.16 m, along the y axis)
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

    // Outputs only the reflections (no direct sound)
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
    std::printf("=== Exercise 3: early reflections (image source -> tapped delay) ===\n");

    const Room room{{6.0, 4.0, 3.0}, 0.3};
    const Vec3 source{2.0, 1.5, 1.6};
    const Vec3 listener{4.3, 2.6, 1.2};

    // ---- 1. The 6 taps for a single listener position (compare with the hand calculation in solutions/README.md) ----
    const auto taps = firstOrderReflections(room, source, listener, kSampleRate);
    std::printf("Direct-sound distance %.4f m; the 6 first-order reflections (relative to the direct sound):\n",
                distance(source, listener));
    std::printf("%8s %10s %8s\n", "delay(sp)", "delay(ms)", "gain");
    for (const auto& t : taps)
        std::printf("%8d %10.3f %8.4f\n", t.delaySamples, samplesToMs(t.delaySamples, kSampleRate), t.gain);

    DAWLAB_CHECK(taps.size() == 6);
    // By hand: floor reflection, image (2, 1.5, -1.6), d = sqrt(2.3^2 + 1.1^2 + 2.8^2) = 3.7868 m,
    // direct 2.5807 m -> delay = 1.2061 / 343 = 3.516 ms = 169 samples; gain = sqrt(0.7)*2.5807/3.7868 = 0.570
    DAWLAB_CHECK(taps[0].delaySamples == 169);
    DAWLAB_CHECK_NEAR(taps[0].gain, 0.570, 0.005);
    // The farthest is the wall at x = 0: image (-2, 1.5, 1.6), d = sqrt(6.3^2 + 1.1^2 + 0.4^2) = 6.4078 m
    // -> delay = 3.8271 / 343 = 11.158 ms = 536 samples; gain = 0.8367*2.5807/6.4078 = 0.337
    DAWLAB_CHECK(taps[5].delaySamples == 536);
    DAWLAB_CHECK_NEAR(taps[5].gain, 0.337, 0.005);
    // Every tap's gain is below 1 (a reflection is always weaker than the direct sound), and the delays never decrease
    for (size_t i = 0; i < taps.size(); ++i) {
        DAWLAB_CHECK(taps[i].gain > 0.0f && taps[i].gain < 1.0f);
        if (i > 0)
            DAWLAB_CHECK(taps[i].delaySamples >= taps[i - 1].delaySamples);
    }

    // ---- 2. The impulse response of the TappedDelay is exactly these taps ------------
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
        DAWLAB_CHECK(ir.getSample(0, 0) == 0.0f); // no direct sound
        for (const auto& t : tapsL)
            DAWLAB_CHECK_NEAR(ir.getSample(0, t.delaySamples), t.gain, 1e-6);
        std::printf("The left-ear impulse response has %d non-zero samples (= the number of taps); positions and gains all match\n", nonZero);
    }

    // ---- 3. ER + late reverb applied to a pluck sequence -----------------------------
    {
        AudioBuffer dry = makePluckSequence(kSampleRate, 4.0, 0.4f, 2);
        const int n = dry.getNumSamples();

        AudioBuffer early = dry;
        er.reset();
        for (int start = 0; start < n; start += kBlockSize)
            er.process(early.toBlock().getSubBlock(start, std::min(kBlockSize, n - start)));

        // Late reverb: set the pre-delay to the latest first-order reflection so the late tail follows the ER
        Reverb late;
        late.prepare(kSampleRate, kBlockSize, 2);
        late.setMix(1.0f);
        late.setDecaySeconds(1.2f);
        late.setDampingHz(5000.0f);
        late.setSize(0.4f);
        late.setPreDelayMs(static_cast<float>(samplesToMs(er.getMaxDelaySamples(), kSampleRate)));
        late.reset(); // all parameters are set: reset puts the smoothed parameters (mix / pre-delay etc.) straight in place instead of gliding from their defaults
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
        // Adding reflections must increase the energy
        DAWLAB_CHECK(rms(erOnly, 0) > rms(dry, 0));
        DAWLAB_CHECK(rms(full, 0) > rms(erOnly, 0));
        std::printf("RMS: dry %.4f, dry+ER %.4f, dry+ER+late %.4f (peak %.3f)\n", rms(dry, 0), rms(erOnly, 0),
                    rms(full, 0), peakAbs(full));
        writeWav(outputPath("l12_ex3_er_only.wav"), erOnly, kSampleRate);
        writeWav(outputPath("l12_ex3_er_late.wav"), full, kSampleRate);
    }

    return dawlab::test::summary("l12_ex3");
}
