#pragma once
// check.h - the self-check of Step 3 (clips). Defines main(); included at the END of
// starter/step03.cpp and solution/step03.cpp.
//
// Tests only what Step 3 adds: sample-accurate clip placement for any block size, trimming,
// clip gain, mono/stereo sources, fades and complementary crossfades. There is no mixer yet, so
// a clip sample of 0.5 comes out as exactly 0.5 on both channels.

#include <dawlab/Common.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

namespace step03_check {

using namespace dawlab;
using namespace minidaw;

constexpr double kSampleRate = 48000.0;

inline std::shared_ptr<const AudioBuffer> dc(float left, float right, int n) {
    auto b = std::make_shared<AudioBuffer>(right == left ? 1 : 2, n);
    for (int i = 0; i < n; ++i) {
        b->setSample(0, i, left);
        if (b->getNumChannels() > 1)
            b->setSample(1, i, right);
    }
    return b;
}

// Renders `total` samples of `clips` on track 1 with the given block size.
inline AudioBuffer render(const std::vector<Clip>& clips, int total, int blockSize) {
    Engine engine;
    engine.prepare(kSampleRate, 512);
    engine.setTrackClips(0, clips);
    engine.getTransport().play();
    AudioBuffer out(kNumChannels, total);
    for (int pos = 0; pos < total; pos += blockSize)
        engine.processBlock(out.toBlock().getSubBlock(pos, std::min(blockSize, total - pos)));
    return out;
}

inline int run() {
    std::printf("Step 3: clips (build-along)\n");

    // 1) An impulse clip at timeline 12345 lands at output 12345 - and nowhere else - for any block size.
    auto impulse = std::make_shared<const AudioBuffer>(makeImpulse(1, 1, 0, 1.0f));
    for (int bs : {512, 64, 1, 1000}) {
        const AudioBuffer out = render({Clip::fromBuffer(impulse, 12345)}, 20000, bs);
        int found = -1, nonZero = 0;
        for (int i = 0; i < out.getNumSamples(); ++i)
            if (out.getSample(0, i) != 0.0f) {
                ++nonZero;
                found = i;
            }
        std::printf("  impulse clip at 12345, block size %4d: found at %d, %d non-zero samples\n", bs, found, nonZero);
        DAWLAB_CHECK(found == 12345 && nonZero == 1);
        DAWLAB_CHECK(out.getSample(1, 12345) == 1.0f); // mono source -> both channels
    }

    // 2) Trimming + clip gain: ramp source (sample i = i / 1000), sourceOffset 100, length 300,
    //    clip at 1000, -6.0206 dB = x0.5.
    auto ramp = std::make_shared<AudioBuffer>(1, 1000);
    for (int i = 0; i < 1000; ++i)
        ramp->setSample(0, i, static_cast<float>(i) / 1000.0f);
    Clip trimmed = Clip::fromBuffer(ramp, 1000);
    trimmed.sourceOffset = 100;
    trimmed.length = 300;
    trimmed.gainDb = -6.0206f;
    const AudioBuffer t = render({trimmed}, 2000, 512);
    std::printf("  trimmed clip: out[999] %.4f, out[1000] %.4f, out[1299] %.4f, out[1300] %.4f "
                "(expected 0, 0.0500, 0.1995, 0)\n",
                t.getSample(0, 999), t.getSample(0, 1000), t.getSample(0, 1299), t.getSample(0, 1300));
    DAWLAB_CHECK(t.getSample(0, 999) == 0.0f);
    DAWLAB_CHECK_NEAR(t.getSample(0, 1000), 0.100 * 0.5, 1e-5);
    DAWLAB_CHECK_NEAR(t.getSample(0, 1299), 0.399 * 0.5, 1e-5);
    DAWLAB_CHECK(t.getSample(0, 1300) == 0.0f);

    // 3) A stereo clip keeps its channels; a clip longer than its source is silent beyond it.
    Clip stereo = Clip::fromBuffer(dc(0.3f, -0.2f, 100), 0);
    stereo.length = 200;
    const AudioBuffer s = render({stereo}, 300, 64);
    DAWLAB_CHECK(s.getSample(0, 50) == 0.3f && s.getSample(1, 50) == -0.2f);
    DAWLAB_CHECK(s.getSample(0, 150) == 0.0f && s.getSample(1, 150) == 0.0f);

    // 4) Overlapping clips add; a muted clip adds nothing.
    Clip a = Clip::fromBuffer(dc(0.25f, 0.25f, 100), 0);
    Clip b = Clip::fromBuffer(dc(0.5f, 0.5f, 100), 50);
    Clip m = Clip::fromBuffer(dc(0.125f, 0.125f, 100), 0);
    m.muted = true;
    const AudioBuffer o = render({a, b, m}, 200, 64);
    DAWLAB_CHECK(o.getSample(0, 25) == 0.25f && o.getSample(0, 75) == 0.75f && o.getSample(0, 125) == 0.5f);

    // 5) Fades: a 4-sample linear fade-in is 0.2, 0.4, 0.6, 0.8, then 1; the fade-out mirrors it.
    Clip f = Clip::fromBuffer(dc(1.0f, 1.0f, 20), 0);
    f.fadeInSamples = 4;
    f.fadeOutSamples = 4;
    const AudioBuffer fo = render({f}, 20, 512);
    std::printf("  fade-in  : %.2f %.2f %.2f %.2f %.2f (expected 0.20 0.40 0.60 0.80 1.00)\n", fo.getSample(0, 0),
                fo.getSample(0, 1), fo.getSample(0, 2), fo.getSample(0, 3), fo.getSample(0, 4));
    std::printf("  fade-out : %.2f %.2f %.2f %.2f (expected 0.80 0.60 0.40 0.20)\n", fo.getSample(0, 16),
                fo.getSample(0, 17), fo.getSample(0, 18), fo.getSample(0, 19));
    const float expectedIn[] = {0.2f, 0.4f, 0.6f, 0.8f, 1.0f};
    for (int i = 0; i < 5; ++i)
        DAWLAB_CHECK_NEAR(fo.getSample(0, i), expectedIn[i], 1e-6);
    for (int i = 0; i < 4; ++i)
        DAWLAB_CHECK_NEAR(fo.getSample(0, 16 + i), expectedIn[3 - i], 1e-6);

    // 6) Complementary crossfades: clip A fades out over F samples while clip B fades in over the
    //    same F samples. Linear gains sum to 1, equal-power squares sum to 1.
    const int F = 100;
    double worstLinear = 0.0, worstPower = 0.0;
    for (FadeShape shape : {FadeShape::Linear, FadeShape::EqualPower}) {
        Clip out = Clip::fromBuffer(dc(1.0f, 1.0f, 1000), 0);
        out.fadeOutSamples = F;
        out.fadeShape = shape;
        Clip in = Clip::fromBuffer(dc(1.0f, 1.0f, 1000), 1000 - F);
        in.fadeInSamples = F;
        in.fadeShape = shape;
        for (int k = 0; k < F; ++k) {
            const float gOut = clipFadeAt(out, out.length - F + k);
            const float gIn = clipFadeAt(in, k);
            if (shape == FadeShape::Linear)
                worstLinear = std::max(worstLinear, std::abs(gOut + gIn - 1.0));
            else
                worstPower = std::max(worstPower, std::abs(gOut * gOut + gIn * gIn - 1.0));
        }
    }
    std::printf("  crossfade over %d samples: max |gOut + gIn - 1| (linear) %.2g, max |gOut^2 + gIn^2 - 1| "
                "(equal power) %.2g\n",
                F, worstLinear, worstPower);
    DAWLAB_CHECK(worstLinear < 1e-6);
    DAWLAB_CHECK(worstPower < 1e-5);
    DAWLAB_CHECK_NEAR(clipFadeAt(Clip::fromBuffer(dc(1.0f, 1.0f, 10), 0), 5), 1.0, 0.0); // no fades = 1

    return dawlab::test::summary("l14_step03");
}

} // namespace step03_check

int main() { return step03_check::run(); }
