// Exercise 7 (starter code): Mid/Side stereo width and stereo balance
//
//   M = (L + R) / 2,  S = (L - R) / 2;   L' = M + w*S,  R' = M - w*S
//   balance b: for b > 0 the left channel is multiplied by (1 - b); for b < 0 the right channel is multiplied by (1 + b)
// You are done when every self-check passes after you complete the TODOs. The starter code prints FAIL when it runs; that is expected.

#include <dawlab/Common.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace dawlab;

// In place: channel 0 = L, channel 1 = R
void applyStereoWidth(AudioBlock stereo, float width) {
    if (stereo.getNumChannels() < 2)
        return;
    (void)width;
    // TODO: compute mid / side sample by sample, multiply side by width, then decode back to L / R
}

void applyBalance(AudioBlock stereo, float balance) {
    if (stereo.getNumChannels() < 2)
        return;
    (void)balance;
    // TODO: depending on the sign of balance, multiply one of the channels by (1 - |balance|)
}

namespace {

float maxAbsDiff(const AudioBuffer& a, int chA, const AudioBuffer& b, int chB) {
    float m = 0.0f;
    for (int i = 0; i < a.getNumSamples(); ++i)
        m = std::max(m, std::abs(a.getSample(chA, i) - b.getSample(chB, i)));
    return m;
}

} // namespace

int main() {
    constexpr double sr = 48000.0;

    // Left = sine, right = 0
    AudioBuffer src = makeSine(440.0, sr, 0.5, 0.5f, 2);
    std::fill_n(src.getWritePointer(1), src.getNumSamples(), 0.0f);

    AudioBuffer w0 = src;
    applyStereoWidth(w0, 0.0f);
    DAWLAB_CHECK(maxAbsDiff(w0, 0, w0, 1) < 1e-7f);
    DAWLAB_CHECK_NEAR(peakAbs(w0.getReadPointer(0), w0.getNumSamples()), 0.25, 1e-6);

    AudioBuffer w1 = src;
    applyStereoWidth(w1, 1.0f);
    DAWLAB_CHECK(maxAbsDiff(w1, 0, src, 0) < 1e-7f);
    DAWLAB_CHECK(maxAbsDiff(w1, 1, src, 1) < 1e-7f);

    AudioBuffer w2 = src;
    applyStereoWidth(w2, 2.0f);
    DAWLAB_CHECK_NEAR(peakAbs(w2.getReadPointer(0), w2.getNumSamples()), 0.75, 1e-6);
    DAWLAB_CHECK_NEAR(peakAbs(w2.getReadPointer(1), w2.getNumSamples()), 0.25, 1e-6);
    DAWLAB_CHECK_NEAR(w2.getSample(1, 100), -0.5 * src.getSample(0, 100), 1e-6);

    // Mono compatibility: for any width, (L'+R')/2 equals the original M
    for (const AudioBuffer* b : {&w0, &w1, &w2}) {
        float m = 0.0f;
        for (int i = 0; i < src.getNumSamples(); ++i)
            m = std::max(m, std::abs(0.5f * (b->getSample(0, i) + b->getSample(1, i)) -
                                     0.5f * (src.getSample(0, i) + src.getSample(1, i))));
        DAWLAB_CHECK(m < 1e-6f);
    }

    // Uncorrelated noise: width 2 -> side +6.02 dB, mid unchanged
    AudioBuffer noise = makeWhiteNoise(sr, 1.0, 0.25f, 2, 5);
    auto midSideRms = [](const AudioBuffer& b, float& midRms, float& sideRms) {
        double m2 = 0.0, s2 = 0.0;
        for (int i = 0; i < b.getNumSamples(); ++i) {
            const double mid = 0.5 * (b.getSample(0, i) + b.getSample(1, i));
            const double side = 0.5 * (b.getSample(0, i) - b.getSample(1, i));
            m2 += mid * mid;
            s2 += side * side;
        }
        midRms = static_cast<float>(std::sqrt(m2 / b.getNumSamples()));
        sideRms = static_cast<float>(std::sqrt(s2 / b.getNumSamples()));
    };
    float mid0 = 0.0f, side0 = 0.0f, mid2 = 0.0f, side2 = 0.0f;
    midSideRms(noise, mid0, side0);
    AudioBuffer wide = noise;
    applyStereoWidth(wide, 2.0f);
    midSideRms(wide, mid2, side2);
    DAWLAB_CHECK_NEAR(gainToDb(mid2 / mid0), 0.0, 1e-3);
    DAWLAB_CHECK_NEAR(gainToDb(side2 / side0), 6.0206, 1e-3);

    // balance
    AudioBuffer bal = noise;
    applyBalance(bal, 1.0f);
    DAWLAB_CHECK(peakAbs(bal.getReadPointer(0), bal.getNumSamples()) == 0.0f);
    DAWLAB_CHECK(maxAbsDiff(bal, 1, noise, 1) == 0.0f);
    AudioBuffer half = noise;
    applyBalance(half, 0.5f);
    DAWLAB_CHECK_NEAR(gainToDb(rms(half, 0) / rms(noise, 0)), -6.0206, 1e-3);
    DAWLAB_CHECK_NEAR(gainToDb(rms(half, 1) / rms(noise, 1)), 0.0, 1e-6);

    return dawlab::test::summary("l01_ex7_starter");
}
