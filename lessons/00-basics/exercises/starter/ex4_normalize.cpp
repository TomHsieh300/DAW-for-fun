// Exercise 4 starter: normalize to a given peak and count clipped samples.
//
// Complete the two functions marked // TODO so that every self-check passes.
// Run: bin/Debug/l00_ex4_starter.exe (it prints FAIL until you finish; that is expected)
// Hints: dawlab::peakAbs, dawlab::dbToGain, dawlab::gainToDb, AudioBuffer::applyGain.

#include <dawlab/Common.h>

#include <cmath>
#include <cstdio>

namespace {

// Counts the samples with |x| >= threshold (summed over all channels).
int countClippedSamples(const dawlab::AudioBuffer& buffer, float threshold = 1.0f) {
    // TODO: walk over every channel and every sample and count those with std::abs(x) >= threshold
    (void)buffer;
    (void)threshold;
    return -1;
}

// Scales the whole buffer so its peak lands on targetPeakDb (every channel is multiplied by the same gain).
// Returns the gain applied (in dB). A completely silent buffer is left untouched and 0 is returned.
float normalizeToPeakDb(dawlab::AudioBuffer& buffer, float targetPeakDb) {
    // TODO:
    //   1. Find peak = the largest |sample| (hint: dawlab::peakAbs(buffer))
    //   2. If peak <= 0, return 0 right away (you cannot divide by 0)
    //   3. gain = dbToGain(targetPeakDb) / peak, applied to the whole buffer
    //   4. Return gainToDb(gain)
    (void)buffer;
    (void)targetPeakDb;
    return 0.0f;
}

} // namespace

int main() {
    using namespace dawlab;
    constexpr double sr = 48000.0;

    // 1. Normalize a sine of amplitude 0.25 (-12.04 dBFS) to -1 dBFS
    AudioBuffer sine = makeSine(440.0, sr, 1.0, 0.25f);
    const float applied = normalizeToPeakDb(sine, -1.0f);
    std::printf("Sine 0.25 -> -1 dBFS: applied %+.2f dB, resulting peak = %.4f (%.2f dBFS)\n", applied, peakAbs(sine),
                gainToDb(peakAbs(sine)));
    DAWLAB_CHECK_NEAR(peakAbs(sine), dbToGain(-1.0f), 1e-5);
    DAWLAB_CHECK_NEAR(applied, -1.0 - gainToDb(0.25f), 0.01); // -1 - (-12.04) = +11.04 dB
    DAWLAB_CHECK(countClippedSamples(sine) == 0);

    // 2. The two channels have different levels: normalization goes by the "loudest channel", so the left/right ratio is unchanged
    AudioBuffer stereo = makeSine(440.0, sr, 1.0, 0.5f);
    AudioBlock(stereo).getChannelSubset(1, 1).multiplyBy(0.5f); // right channel -6 dB
    const float ratioBefore = peakAbs(stereo.getReadPointer(1), stereo.getNumSamples()) /
                              peakAbs(stereo.getReadPointer(0), stereo.getNumSamples());
    normalizeToPeakDb(stereo, 0.0f);
    const float ratioAfter = peakAbs(stereo.getReadPointer(1), stereo.getNumSamples()) /
                             peakAbs(stereo.getReadPointer(0), stereo.getNumSamples());
    DAWLAB_CHECK_NEAR(peakAbs(stereo), 1.0, 1e-5);
    DAWLAB_CHECK_NEAR(ratioAfter, ratioBefore, 1e-5);

    // 3. Silent buffer: must not produce NaN
    AudioBuffer silence(2, 1000);
    const float silentGain = normalizeToPeakDb(silence, -1.0f);
    DAWLAB_CHECK(silentGain == 0.0f);
    DAWLAB_CHECK(!hasNanOrInf(silence));
    DAWLAB_CHECK(peakAbs(silence) == 0.0f);

    // 4. Clipping count: for a 1 kHz sine of amplitude 1.5, 26 of the 48 samples in each period have |x| >= 1
    AudioBuffer loud = makeSine(1000.0, sr, 1.0, 1.5f, 1);
    const int clipped = countClippedSamples(loud);
    std::printf("Sine of amplitude 1.5: %d / %d samples exceed ±1.0\n", clipped, loud.getNumSamples());
    DAWLAB_CHECK(clipped == 26000);
    normalizeToPeakDb(loud, -0.1f);
    DAWLAB_CHECK(countClippedSamples(loud) == 0);
    DAWLAB_CHECK_NEAR(peakAbs(loud), dbToGain(-0.1f), 1e-5);

    // 5. Real material: normalize the drum loop to -1 dBFS and write it out
    AudioBuffer drums = makeDrumLoop(sr);
    normalizeToPeakDb(drums, -1.0f);
    DAWLAB_CHECK_NEAR(gainToDb(peakAbs(drums)), -1.0, 0.01);
    writeWav(outputPath("l00_ex4_normalized.wav"), drums, sr);

    return dawlab::test::summary("l00_ex4");
}
