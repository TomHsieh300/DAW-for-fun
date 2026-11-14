// Self-test of the shared utilities: WAV round trip, Goertzel measurement accuracy, dB conversion.
#include <dawlab/Common.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

// The rule of Check.h's summary(): a run passes only if at least one check ran and none failed. The probes use their own
// Counters, so they do not touch the global one that main() reports.
static void checkSummaryRule() {
    using dawlab::test::Counters;
    using dawlab::test::summaryOf;
    DAWLAB_CHECK(summaryOf(Counters{0, 0}, "summary_probe_no_check") == 1);
    DAWLAB_CHECK(summaryOf(Counters{3, 0}, "summary_probe_all_pass") == 0);
    DAWLAB_CHECK(summaryOf(Counters{3, 1}, "summary_probe_one_fails") == 1);
}

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

    // A WAV from outside: readWav refuses a sample rate of 0 and a data chunk that says more than the file holds.
    // A 16-bit mono file of 4 frames, made by hand: `rate` in its fmt chunk, `declared` as its data chunk's size.
    const auto handMade = [](const std::string& name, uint32_t rate, uint32_t declared) {
        std::vector<char> b;
        const auto u32 = [&b](uint32_t v) {
            for (int i = 0; i < 4; ++i)
                b.push_back(char((v >> (8 * i)) & 0xff));
        };
        const auto u16 = [&b](uint32_t v) { b.push_back(char(v & 0xff)), b.push_back(char((v >> 8) & 0xff)); };
        const auto tag = [&b](const char* t) { b.insert(b.end(), t, t + 4); };
        tag("RIFF"), u32(36 + 8), tag("WAVE");
        tag("fmt "), u32(16), u16(1), u16(1), u32(rate), u32(rate * 2), u16(2), u16(16);
        tag("data"), u32(declared);
        b.insert(b.end(), 8, char(0));
        const std::string path = outputPath("common_" + name + ".wav");
        std::ofstream(path, std::ios::binary).write(b.data(), std::streamsize(b.size()));
        return path;
    };
    WavData wav;
    std::string why;
    DAWLAB_CHECK(readWav(handMade("hand_ok", 48000, 8), wav) && wav.buffer.getNumSamples() == 4 && wav.sampleRate == 48000.0);
    DAWLAB_CHECK(!readWav(handMade("hand_rate0", 0, 8), wav, &why));
    std::printf("a WAV with a sample rate of 0: %s\n", why.c_str());
    DAWLAB_CHECK(!readWav(handMade("hand_short", 48000, 1000000), wav, &why));
    std::printf("a data chunk of 1000000 bytes with 8 there: %s\n", why.c_str());
    DAWLAB_CHECK(!readWav(handMade("hand_huge", 48000, 0xFFFFFFFFu), wav));

    // writeWav refuses a sample rate that a WAV cannot hold, before it writes anything.
    AudioBuffer one(1, 1);
    const std::string rated = outputPath("common_rate.wav");
    DAWLAB_CHECK(!writeWav(rated, one, 0.0, 24, &why) && !writeWav(rated, one, -48000.0) && !writeWav(rated, one, std::nan("")));
    std::printf("writeWav at 0 Hz: %s\n", why.c_str());
    DAWLAB_CHECK(writeWav(rated, one, 48000.0) && readWav(rated, wav) && wav.buffer.getNumSamples() == 1);
    // And more than 4 GiB of samples, which the RIFF header's 32-bit size cannot say: 1.5e9 stereo 24-bit frames
    // are 9e9 bytes, which wrapped to 410065408 in 32 bits. The size is counted in 64 bits and checked first.
    DAWLAB_CHECK(detail::wavDataBytes(1500000000, 2, 3) == 9000000000ull && !detail::fitsWav(9000000000ull));
    DAWLAB_CHECK(detail::fitsWav(0xFFFFFFFFull - 37) && !detail::fitsWav(0xFFFFFFFFull - 36)); // 36 header bytes, a pad byte

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

    checkSummaryRule();

    return dawlab::test::summary("common");
}
