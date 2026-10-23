// Exercise 5 starter: stereo -> mono downmix and mono -> stereo upmix (mind the gain).
//
// Complete the two functions marked // TODO so that every self-check passes.
// Run: bin/Debug/l00_ex5_starter.exe (it prints FAIL until you finish; that is expected)

#include <dawlab/Common.h>

#include <cmath>
#include <cstdio>

namespace {

// Mixes a multichannel buffer down to mono: mono[i] = channelGain * Σ_ch in[ch][i]
//
// Choosing channelGain (stereo as the example):
//   0.5   (-6 dB): level unchanged when left and right are identical, -3 dB when they are
//                  uncorrelated. Never clips.
//   0.707 (-3 dB): level unchanged when left and right are uncorrelated, +3 dB when they are
//                  identical, and it may clip.
dawlab::AudioBuffer downmixToMono(const dawlab::AudioBuffer& in, float channelGain) {
    dawlab::AudioBuffer mono(1, in.getNumSamples());
    // TODO: for every channel and every sample: mono[0][i] += channelGain * in[ch][i]
    (void)channelGain;
    return mono;
}

// Copies the mono signal into numChannels channels.
dawlab::AudioBuffer upmixToStereo(const dawlab::AudioBuffer& mono, int numChannels = 2) {
    dawlab::AudioBuffer out(numChannels, mono.getNumSamples());
    // TODO: copy channel 0 of mono into every channel of out (hint: AudioBuffer::copyFrom)
    return out;
}

float maxAbsDifference(const dawlab::AudioBuffer& a, const dawlab::AudioBuffer& b) {
    float d = 0.0f;
    for (int ch = 0; ch < std::min(a.getNumChannels(), b.getNumChannels()); ++ch)
        for (int i = 0; i < std::min(a.getNumSamples(), b.getNumSamples()); ++i)
            d = std::max(d, std::abs(a.getSample(ch, i) - b.getSample(ch, i)));
    return d;
}

} // namespace

int main() {
    using namespace dawlab;
    constexpr double sr = 48000.0;
    const float minus6 = 0.5f;
    const float minus3 = static_cast<float>(1.0 / std::sqrt(2.0));

    // 1. Left and right identical (sine): gain 0.5 -> mono is as loud as the original; gain 0.707 -> +3.01 dB
    AudioBuffer sine = makeSine(440.0, sr, 1.0, 0.5f, 2);
    AudioBuffer monoA = downmixToMono(sine, minus6);
    AudioBuffer monoB = downmixToMono(sine, minus3);
    const float sameA = gainToDb(rms(monoA, 0)) - gainToDb(rms(sine, 0));
    const float sameB = gainToDb(rms(monoB, 0)) - gainToDb(rms(sine, 0));
    std::printf("Identical L/R:     gain 0.5 -> %+.2f dB, gain 0.707 -> %+.2f dB\n", sameA, sameB);
    DAWLAB_CHECK_NEAR(sameA, 0.0, 0.01);
    DAWLAB_CHECK_NEAR(sameB, 3.01, 0.02);
    DAWLAB_CHECK_NEAR(maxAbsDifference(monoA, sine), 0.0, 1e-6);

    // 2. Left and right uncorrelated (white noise): gain 0.5 -> -3.01 dB; gain 0.707 -> 0 dB
    AudioBuffer noise = makeWhiteNoise(sr, 2.0, 0.25f, 2);
    AudioBuffer noiseA = downmixToMono(noise, minus6);
    AudioBuffer noiseB = downmixToMono(noise, minus3);
    const float uncA = gainToDb(rms(noiseA, 0)) - gainToDb(rms(noise, 0));
    const float uncB = gainToDb(rms(noiseB, 0)) - gainToDb(rms(noise, 0));
    std::printf("Uncorrelated L/R:  gain 0.5 -> %+.2f dB, gain 0.707 -> %+.2f dB\n", uncA, uncB);
    DAWLAB_CHECK_NEAR(uncA, -3.01, 0.1);
    DAWLAB_CHECK_NEAR(uncB, 0.0, 0.1);

    // 3. Left and right in anti-phase (L = -R): the signal vanishes completely after downmixing
    AudioBuffer antiPhase = makeSine(440.0, sr, 1.0, 0.5f, 2);
    AudioBlock(antiPhase).getChannelSubset(1, 1).multiplyBy(-1.0f);
    DAWLAB_CHECK(peakAbs(downmixToMono(antiPhase, minus6)) == 0.0f);

    // 4. gain 0.5 never clips
    AudioBuffer fullScale = makeSine(440.0, sr, 0.1, 1.0f, 2);
    DAWLAB_CHECK(peakAbs(downmixToMono(fullScale, minus6)) <= 1.0f);
    DAWLAB_CHECK(peakAbs(downmixToMono(fullScale, minus3)) > 1.0f);

    // 5. upmix: both channels equal the mono signal; stereo -> mono -> stereo is lossless for material whose left and right are identical
    AudioBuffer pluck = makePluckSequence(sr, 2.0, 0.4f, 2);
    AudioBuffer pluckMono = downmixToMono(pluck, minus6);
    AudioBuffer pluckStereo = upmixToStereo(pluckMono);
    DAWLAB_CHECK(pluckStereo.getNumChannels() == 2);
    DAWLAB_CHECK(pluckStereo.getNumSamples() == pluck.getNumSamples());
    DAWLAB_CHECK_NEAR(maxAbsDifference(pluckStereo, pluck), 0.0, 1e-6);
    float lrDiff = 0.0f;
    for (int i = 0; i < pluckStereo.getNumSamples(); ++i)
        lrDiff = std::max(lrDiff, std::abs(pluckStereo.getSample(0, i) - pluckStereo.getSample(1, i)));
    DAWLAB_CHECK(lrDiff == 0.0f);

    writeWav(outputPath("l00_ex5_mono.wav"), pluckMono, sr);
    writeWav(outputPath("l00_ex5_stereo_again.wav"), pluckStereo, sr);
    return dawlab::test::summary("l00_ex5");
}
