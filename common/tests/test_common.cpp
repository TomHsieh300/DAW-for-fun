// Self-test of the shared utilities: WAV round trip, Goertzel measurement accuracy, dB conversion.
#include <dawlab/Common.h>

#include <cstdio>

int main() {
    using namespace dawlab;
    constexpr double sr = 48000.0;

    // dB <-> gain
    DAWLAB_CHECK_NEAR(dbToGain(-6.0206f), 0.5, 1e-4);
    DAWLAB_CHECK_NEAR(gainToDb(2.0f), 6.0206, 1e-3);
    DAWLAB_CHECK_NEAR(dbToGain(-6), 0.50119, 1e-4);
    DAWLAB_CHECK(dbToGain(-200.0f) == 0.0f);

    // Goertzel: a 1 kHz sine wave with amplitude 0.3
    AudioBuffer sine = makeSine(1000.0, sr, 0.5, 0.3f, 1);
    DAWLAB_CHECK_NEAR(goertzelAmplitude(sine.getReadPointer(0), sine.getNumSamples(), 1000.0, sr), 0.3, 1e-3);
    DAWLAB_CHECK(goertzelAmplitude(sine.getReadPointer(0), sine.getNumSamples(), 3000.0, sr) < 1e-4);
    DAWLAB_CHECK_NEAR(rms(sine, 0), 0.3 / std::sqrt(2.0), 1e-3);

    // measureGainDb: identity system = 0 dB, multiply by 0.5 = -6.02 dB
    DAWLAB_CHECK_NEAR(measureGainDb([](float x) { return x; }, 440.0, sr), 0.0, 1e-3);
    DAWLAB_CHECK_NEAR(measureGainDb([](float x) { return 0.5f * x; }, 440.0, sr), -6.0206, 1e-3);

    // THD of a pure sine wave should be close to 0
    DAWLAB_CHECK(thdPercent(sine.getReadPointer(0), sine.getNumSamples(), 1000.0, sr) < 0.01);

    // WAV round trip: 32-bit float must match exactly, 24-bit error < 1 LSB
    AudioBuffer noise = makeWhiteNoise(sr, 0.1, 0.5f, 2);
    for (int bits : {16, 24, 32}) {
        const std::string path = outputPath("common_roundtrip_" + std::to_string(bits) + ".wav");
        DAWLAB_CHECK(writeWav(path, noise, sr, bits));
        WavData back;
        std::string err;
        DAWLAB_CHECK(readWav(path, back, &err));
        DAWLAB_CHECK(back.buffer.getNumChannels() == 2);
        DAWLAB_CHECK(back.buffer.getNumSamples() == noise.getNumSamples());
        DAWLAB_CHECK_NEAR(back.sampleRate, sr, 0.0);
        float maxErr = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < noise.getNumSamples(); ++i)
                maxErr = std::max(maxErr, std::abs(back.buffer.getSample(ch, i) - noise.getSample(ch, i)));
        const double tol = bits == 32 ? 0.0 : (bits == 24 ? 1.0 / 8388607.0 : 1.0 / 32767.0);
        DAWLAB_CHECK(maxErr <= tol);
    }

    // AudioBlock sub-blocks and mixing
    AudioBuffer a(2, 8), b(2, 8);
    for (int i = 0; i < 8; ++i) {
        a.setSample(0, i, 1.0f);
        b.setSample(1, i, 2.0f);
    }
    AudioBlock(a).getSubBlock(4, 4).addFrom(AudioBlock(b), 0.5f);
    DAWLAB_CHECK_NEAR(a.getSample(1, 3), 0.0, 0.0);
    DAWLAB_CHECK_NEAR(a.getSample(1, 4), 1.0, 0.0);
    DAWLAB_CHECK_NEAR(a.getSample(0, 7), 1.0, 0.0);

    // Test signals must contain no NaN and must not clip
    AudioBuffer drums = makeDrumLoop(sr);
    AudioBuffer plucks = makePluckSequence(sr);
    DAWLAB_CHECK(!hasNanOrInf(drums) && !hasNanOrInf(plucks));
    DAWLAB_CHECK(peakAbs(drums) < 1.0f && peakAbs(drums) > 0.3f);
    DAWLAB_CHECK(peakAbs(plucks) < 1.0f && peakAbs(plucks) > 0.1f);
    std::printf("drum loop peak = %.2f dBFS, pluck sequence peak = %.2f dBFS\n", gainToDb(peakAbs(drums)),
                gainToDb(peakAbs(plucks)));

    return dawlab::test::summary("common");
}
