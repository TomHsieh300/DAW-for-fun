// Exercise 6 solution: linear vs equal-power fade in / fade out, and crossfade.
//
// Run: bin/Debug/l00_ex6_solution.exe (ctest label: solution)
// Output: dawlab_output/l00_ex6_crossfade_linear.wav, l00_ex6_crossfade_equal_power.wav

#include <dawlab/Common.h>

#include <cmath>
#include <cstdio>

namespace {

enum class FadeShape { Linear, EqualPower };

// Fade-in gain at position t ∈ [0, 1]:
//   Linear      g(t) = t                 -> midpoint 0.5   (-6.02 dB)
//   EqualPower  g(t) = sin(t · π/2)      -> midpoint 0.707 (-3.01 dB)
// A fade-out substitutes 1 - t, so the equal-power fade-out is cos(t · π/2), and sin² + cos² = 1 -> power is conserved.
float fadeGain(float t, FadeShape shape) {
    t = std::clamp(t, 0.0f, 1.0f);
    switch (shape) {
    case FadeShape::Linear:
        return t;
    case FadeShape::EqualPower:
        return std::sin(t * static_cast<float>(dawlab::kPi) * 0.5f);
    }
    return t;
}

// Applies a fade-in over the first numSamples samples of the buffer (gain rises from 0 to 1)
void applyFadeIn(dawlab::AudioBuffer& buffer, int numSamples, FadeShape shape) {
    const int n = std::min(numSamples, buffer.getNumSamples());
    if (n <= 1)
        return;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        float* x = buffer.getWritePointer(ch);
        for (int i = 0; i < n; ++i)
            x[i] *= fadeGain(static_cast<float>(i) / static_cast<float>(n - 1), shape);
    }
}

// Applies a fade-out over the last numSamples samples of the buffer (gain falls from 1 to 0)
void applyFadeOut(dawlab::AudioBuffer& buffer, int numSamples, FadeShape shape) {
    const int total = buffer.getNumSamples();
    const int n = std::min(numSamples, total);
    if (n <= 1)
        return;
    const int start = total - n;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        float* x = buffer.getWritePointer(ch);
        for (int i = 0; i < n; ++i)
            x[start + i] *= fadeGain(1.0f - static_cast<float>(i) / static_cast<float>(n - 1), shape);
    }
}

// Fade out all of a, fade in all of b, then add them = crossfade
dawlab::AudioBuffer crossfade(dawlab::AudioBuffer a, dawlab::AudioBuffer b, FadeShape shape) {
    const int n = std::min(a.getNumSamples(), b.getNumSamples());
    applyFadeOut(a, n, shape);
    applyFadeIn(b, n, shape);
    a.addFrom(0, 0, b, 0, 0, n);
    if (a.getNumChannels() > 1)
        a.addFrom(1, 0, b, std::min(1, b.getNumChannels() - 1), 0, n);
    return a;
}

// Measures the RMS of the middle 10% of the buffer (channel 0)
float middleRmsDb(const dawlab::AudioBuffer& b) {
    const int n = b.getNumSamples();
    const int len = n / 10;
    return dawlab::gainToDb(dawlab::rms(b.getReadPointer(0) + (n - len) / 2, len));
}

} // namespace

int main() {
    using namespace dawlab;
    constexpr double sr = 48000.0;

    // 1. The gain curves themselves: midpoint and endpoints
    std::printf("fade-in curve  t=0 / 0.5 / 1: linear %.3f / %.3f / %.3f, equal-power %.3f / %.3f / %.3f\n",
                fadeGain(0.0f, FadeShape::Linear), fadeGain(0.5f, FadeShape::Linear), fadeGain(1.0f, FadeShape::Linear),
                fadeGain(0.0f, FadeShape::EqualPower), fadeGain(0.5f, FadeShape::EqualPower),
                fadeGain(1.0f, FadeShape::EqualPower));
    DAWLAB_CHECK_NEAR(fadeGain(0.5f, FadeShape::Linear), 0.5, 1e-6);
    DAWLAB_CHECK_NEAR(fadeGain(0.5f, FadeShape::EqualPower), std::sqrt(0.5), 1e-6);
    DAWLAB_CHECK_NEAR(fadeGain(1.0f, FadeShape::EqualPower), 1.0, 1e-6);
    // fade-in² + fade-out² is 1 at every position (power conserved); linear gives only 0.5 at the midpoint
    for (float t : {0.1f, 0.3f, 0.5f, 0.8f}) {
        const float gi = fadeGain(t, FadeShape::EqualPower), go = fadeGain(1.0f - t, FadeShape::EqualPower);
        DAWLAB_CHECK_NEAR(gi * gi + go * go, 1.0, 1e-5);
    }
    {
        const float gi = fadeGain(0.5f, FadeShape::Linear), go = fadeGain(0.5f, FadeShape::Linear);
        DAWLAB_CHECK_NEAR(gi * gi + go * go, 0.5, 1e-6);
    }

    // 2. Apply fade-in / fade-out to a constant signal (DC = 1.0) and read the samples to see the curve
    const int fadeLen = 1001; // odd length, so the midpoint is exactly index 500
    AudioBuffer dc(1, 4000);
    for (int i = 0; i < 4000; ++i)
        dc.setSample(0, i, 1.0f);
    AudioBuffer dcIn = dc, dcOut = dc;
    applyFadeIn(dcIn, fadeLen, FadeShape::Linear);
    applyFadeOut(dcOut, fadeLen, FadeShape::EqualPower);
    DAWLAB_CHECK_NEAR(dcIn.getSample(0, 0), 0.0, 1e-6);
    DAWLAB_CHECK_NEAR(dcIn.getSample(0, 500), 0.5, 1e-6);
    DAWLAB_CHECK_NEAR(dcIn.getSample(0, 1000), 1.0, 1e-6);
    DAWLAB_CHECK_NEAR(dcIn.getSample(0, 1001), 1.0, 1e-6); // untouched outside the fade range
    DAWLAB_CHECK_NEAR(dcOut.getSample(0, 4000 - fadeLen - 1), 1.0, 1e-6); // untouched before the fade range
    DAWLAB_CHECK_NEAR(dcOut.getSample(0, 4000 - fadeLen), 1.0, 1e-6);
    DAWLAB_CHECK_NEAR(dcOut.getSample(0, 4000 - fadeLen + 500), std::sqrt(0.5), 1e-6);
    DAWLAB_CHECK_NEAR(dcOut.getSample(0, 3999), 0.0, 1e-6);

    // 3. Crossfade two "uncorrelated" white-noise signals: equal-power keeps the middle level, linear dips 3 dB in the middle
    AudioBuffer n1 = makeWhiteNoise(sr, 2.0, 0.3f, 2, 1);
    AudioBuffer n2 = makeWhiteNoise(sr, 2.0, 0.3f, 2, 999);
    const float refDb = middleRmsDb(n1);
    AudioBuffer xfLinear = crossfade(n1, n2, FadeShape::Linear);
    AudioBuffer xfEqual = crossfade(n1, n2, FadeShape::EqualPower);
    const float linDip = middleRmsDb(xfLinear) - refDb;
    const float eqDip = middleRmsDb(xfEqual) - refDb;
    std::printf("Uncorrelated material, crossfade mid RMS change: linear %+.2f dB, equal-power %+.2f dB\n", linDip, eqDip);
    DAWLAB_CHECK_NEAR(linDip, -3.01, 0.25);
    DAWLAB_CHECK_NEAR(eqDip, 0.0, 0.25);

    // 4. Crossfade two "identical" signals: linear stays exactly unchanged (0.5 + 0.5 = 1), equal-power actually gains 3 dB
    AudioBuffer s1 = makeSine(440.0, sr, 2.0, 0.3f, 2);
    const float sineRef = middleRmsDb(s1);
    const float linSame = middleRmsDb(crossfade(s1, s1, FadeShape::Linear)) - sineRef;
    const float eqSame = middleRmsDb(crossfade(s1, s1, FadeShape::EqualPower)) - sineRef;
    std::printf("Identical material, crossfade mid RMS change: linear %+.2f dB, equal-power %+.2f dB\n", linSame, eqSame);
    DAWLAB_CHECK_NEAR(linSame, 0.0, 0.05);
    DAWLAB_CHECK_NEAR(eqSame, 3.01, 0.1);

    writeWav(outputPath("l00_ex6_crossfade_linear.wav"), xfLinear, sr);
    writeWav(outputPath("l00_ex6_crossfade_equal_power.wav"), xfEqual, sr);
    return dawlab::test::summary("l00_ex6");
}
