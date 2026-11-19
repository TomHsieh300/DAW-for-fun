// Self-test of the shared utilities: WAV round trip, Goertzel measurement accuracy, dB conversion, smoothToward.
#include <dawlab/Common.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <limits>
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

// Every check is counted, and every failed one, by DAWLAB_CHECK and by DAWLAB_CHECK_NEAR. The probes run on the global
// Counters (the ones the macros use) and put them back before checking what they saw.
static void checkCounting() {
    using dawlab::test::Counters;
    using dawlab::test::counters;
    const Counters before = counters();
    std::printf("(the two [FAIL] lines below are probes: a failed check must be counted)\n");
    dawlab::test::report(true, "probe: a check that holds", __FILE__, __LINE__);
    const Counters afterPass = counters();
    dawlab::test::report(false, "probe: a check that fails", __FILE__, __LINE__);
    const Counters afterFail = counters();
    dawlab::test::reportNear(1.0, 1.05, 0.1, "probe: within the tolerance", __FILE__, __LINE__);
    const Counters afterNearPass = counters();
    dawlab::test::reportNear(1.0, 1.5, 0.1, "probe: outside the tolerance", __FILE__, __LINE__);
    const Counters afterNearFail = counters();
    counters() = before;
    DAWLAB_CHECK(afterPass.checks == before.checks + 1 && afterPass.failures == before.failures);
    DAWLAB_CHECK(afterFail.checks == before.checks + 2 && afterFail.failures == before.failures + 1);
    DAWLAB_CHECK(afterNearPass.checks == before.checks + 3 && afterNearPass.failures == before.failures + 1);
    DAWLAB_CHECK(afterNearFail.checks == before.checks + 4 && afterNearFail.failures == before.failures + 2);
}

// The analysis tools read exactly the n samples they are given: a loud sample just past the end changes nothing.
// Too few samples to measure give 0, not NaN.
static void checkAnalysisReadsNSamples() {
    using namespace dawlab;
    constexpr double sr = 48000.0;
    constexpr int n = 4800;
    std::vector<float> x(n + 1);
    for (int i = 0; i < n; ++i)
        x[size_t(i)] = static_cast<float>(0.3 * std::sin(kTwoPi * 1000.0 * i / sr));
    x[n] = 100.0f;
    DAWLAB_CHECK_NEAR(peakAbs(x.data(), n), 0.3, 1e-4);
    DAWLAB_CHECK_NEAR(rms(x.data(), n), 0.3 / std::sqrt(2.0), 1e-4);
    DAWLAB_CHECK_NEAR(goertzelAmplitude(x.data(), n, 1000.0, sr), 0.3, 1e-3);
    DAWLAB_CHECK(rms(x.data(), 0) == 0.0f);
    DAWLAB_CHECK(goertzelAmplitude(x.data(), 1, 1000.0, sr) == 0.0);
    // A Hann window of 3 samples is {0, 1, 0}: only the middle sample counts, and a sine's peak there is 2 x it
    const float three[] = {0.9f, 0.25f, -0.7f};
    DAWLAB_CHECK_NEAR(goertzelAmplitude(three, 3, 1000.0, sr), 0.5, 1e-9);

    // hasNanOrInf looks at every sample of every channel, up to the last one
    AudioBuffer b(2, 4);
    DAWLAB_CHECK(!hasNanOrInf(b));
    b.setSample(1, 3, std::numeric_limits<float>::infinity());
    DAWLAB_CHECK(hasNanOrInf(b));
    b.setSample(1, 3, std::nanf(""));
    DAWLAB_CHECK(hasNanOrInf(b));
}

// measureGainDb feeds amplitude * sin(2 pi f i / fs) for skip + n samples and measures only what comes after the
// skip, so a system that is silent for its first 0.1 s still measures 0 dB.
static void checkMeasureGainDb() {
    using namespace dawlab;
    constexpr double sr = 48000.0;
    std::vector<float> fed;
    const double db = measureGainDb(
        [&fed](float x) {
            fed.push_back(x);
            return fed.size() > 4800 ? x : 0.0f;
        },
        1000.0, sr);
    DAWLAB_CHECK_NEAR(db, 0.0, 1e-3);
    DAWLAB_CHECK(fed.size() == 33600); // (0.2 + 0.5) s
    // 1 kHz at 48 kHz: a quarter period is 12 samples, so sample 12 is the peak and sample 24 a zero crossing
    DAWLAB_CHECK(fed.size() > 24 && std::abs(fed[12] - 0.25f) < 1e-6f && std::abs(fed[24]) < 1e-6f);
}

// thdPercent: harmonics 2..numHarmonics, up to but not including Nyquist, relative to the fundamental
static void checkThd() {
    using namespace dawlab;
    constexpr double sr = 48000.0;
    // A fundamental of 0.5 with a 2nd harmonic of 0.05 and a 3rd of 0.025: THD = 100 * sqrt(0.05^2 + 0.025^2) / 0.5
    std::vector<float> x(24000);
    for (size_t i = 0; i < x.size(); ++i) {
        const double w = kTwoPi * 1000.0 * double(i) / sr;
        x[i] = static_cast<float>(0.5 * std::sin(w) + 0.05 * std::sin(2.0 * w) + 0.025 * std::sin(3.0 * w));
    }
    const int n = int(x.size());
    const double expected = 100.0 * std::sqrt(0.05 * 0.05 + 0.025 * 0.025) / 0.5;
    DAWLAB_CHECK_NEAR(thdPercent(x.data(), n, 1000.0, sr, 3), expected, 0.01);
    DAWLAB_CHECK_NEAR(thdPercent(x.data(), n, 1000.0, sr, 2), 10.0, 0.01);
    // A fundamental of 12 kHz at 48 kHz: its 2nd harmonic is Nyquist, which is not counted (a tone there is ignored)
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = static_cast<float>(std::sin(kTwoPi * 0.25 * double(i)) + ((i & 1) ? -0.1 : 0.1));
    DAWLAB_CHECK(thdPercent(x.data(), n, 12000.0, sr) == 0.0);
    // Silence has no fundamental: 0 %, not NaN
    const std::vector<float> silence(480, 0.0f);
    DAWLAB_CHECK(thdPercent(silence.data(), 480, 1000.0, sr) == 0.0);
}

int main() {
    using namespace dawlab;
    constexpr double sr = 48000.0;

    // dB <-> gain
    DAWLAB_CHECK_NEAR(dbToGain(-6.0206f), 0.5, 1e-4);
    DAWLAB_CHECK_NEAR(gainToDb(2.0f), 6.0206, 1e-3);
    DAWLAB_CHECK_NEAR(dbToGain(-6), 0.50119, 1e-4);
    DAWLAB_CHECK(dbToGain(-200.0f) == 0.0f);
    // kMinusInfinityDb itself is silence, and anything above it is not
    DAWLAB_CHECK(dbToGain(kMinusInfinityDb) == 0.0f && dbToGain(-99.9f) > 0.0f);
    DAWLAB_CHECK(dbToGain(double(kMinusInfinityDb)) == 0.0 && dbToGain(-99.9) > 0.0);

    // smoothToward: a one-pole smoother arrives. Each row glides from `start` toward `target` with the step k of a
    // smoother in dsp/; it must give exactly the target within `maxSteps` steps and never a subnormal on the way.
    // Without the rule a glide toward 0 parks at a subnormal number and a glide toward anything else parks a hair
    // short of it (a step below half an ulp changes nothing).
    {
        const auto k = [sr](double tauMs) { return static_cast<float>(1.0 - std::exp(-1.0 / (tauMs * 0.001 * sr))); };
        struct Glide {
            const char* what;
            float start, target, k;
            long maxSteps;
        };
        const Glide glides[] = {
            {"Reverb mix 0.3 -> 0 (30 ms)", 0.3f, 0.0f, k(30.0), 48000},
            {"Limiter release 0.5 -> 1 (100 ms)", 0.5f, 1.0f, k(100.0), 96000},
            {"Phaser centre 800 -> 1200 Hz (20 ms)", 800.0f, 1200.0f, k(20.0), 48000},
            {"Reverb delay 4096.3 -> 4000 samples (30 ms)", 4096.3f, 4000.0f, k(30.0), 48000},
            {"makeup gain 1 -> 0 (10 ms)", 1.0f, 0.0f, k(10.0), 48000},
            {"k = 1: there in one step", 0.25f, -0.75f, 1.0f, 1},
        };
        for (const Glide& g : glides) {
            float y = g.start;
            long steps = 0, subnormal = 0;
            while (y != g.target && steps < g.maxSteps) {
                y = smoothToward(y, g.target, g.k);
                ++steps;
                if (std::fpclassify(y) == FP_SUBNORMAL)
                    ++subnormal;
            }
            std::printf("smoothToward %-44s %s after %ld steps (subnormal values on the way: %ld)\n", g.what,
                        y == g.target ? "arrived" : "PARKED SHORT", steps, subnormal);
            DAWLAB_CHECK(y == g.target);
            DAWLAB_CHECK(subnormal == 0);
        }
        // The arrival is a snap only at the end: one step from far away is the plain one-pole step
        DAWLAB_CHECK(smoothToward(0.0f, 1.0f, 0.25f) == 0.25f);
        DAWLAB_CHECK(smoothToward(1.0f, 1.0f, 0.25f) == 1.0f);
        // Within epsilon of the target it takes the target; the epsilon can be given (Reverb's modulation uses 1e-4)
        DAWLAB_CHECK(smoothToward(5.0e-7f, 0.0f, 0.001f) == 0.0f);
        DAWLAB_CHECK(smoothToward(5.0e-5f, 0.0f, 0.001f) != 0.0f && smoothToward(5.0e-5f, 0.0f, 0.001f, 1.0e-4f) == 0.0f);
        // Exactly epsilon away is not there yet, and a step that lands on the target's mirror image (-target) is a
        // plain step too
        DAWLAB_CHECK(smoothToward(0.0f, 1.0f, 0.5f, 0.5f) == 0.5f);
        DAWLAB_CHECK(smoothToward(-3.0f, 1.0f, 0.5f) == -1.0f);
    }

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

    checkAnalysisReadsNSamples();
    checkMeasureGainDb();
    checkThd();

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
    checkCounting();

    // The verdict rests on the counters, not on summaryOf alone: summaryOf is under test above, and a summaryOf that
    // got its rule backwards would pass a run whose probes of it failed.
    const int verdict = dawlab::test::summary("common");
    const dawlab::test::Counters& counted = dawlab::test::counters();
    return verdict == 0 && counted.checks > 0 && counted.failures == 0 ? 0 : 1;
}
