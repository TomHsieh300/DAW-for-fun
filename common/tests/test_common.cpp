// Self-test of the shared utilities: WAV round trip, Goertzel measurement accuracy, dB conversion, smoothToward.
#include <dawlab/Common.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
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

// Sign changes in [from, to) of channel 0
static int zeroCrossings(const dawlab::AudioBuffer& b, int from, int to) {
    int count = 0;
    for (int i = from + 1; i < to; ++i)
        count += (b.getSample(0, i - 1) < 0.0f) != (b.getSample(0, i) < 0.0f);
    return count;
}

static double rmsOf(const dawlab::AudioBuffer& b, int from, int to) {
    return dawlab::rms(b.getReadPointer(0) + from, to - from);
}

// The drum loop at its defaults (48 kHz): 2 bars of 4 beats at 120 bpm, one step an eighth note (0.25 s = 12000
// samples). Each window below holds one sound alone.
static void checkDrumLoop(const dawlab::AudioBuffer& drums) {
    DAWLAB_CHECK(drums.getNumSamples() == 192000);
    // A hit lasts exactly its length: the kick at 1.0 s (step 4) lasts 0.45 s, and nothing sounds after it until the
    // hi-hat of the next eighth at 1.5 s
    DAWLAB_CHECK(drums.getSample(0, 69599) != 0.0f && dawlab::peakAbs(drums.getReadPointer(0) + 69600, 2400) == 0.0f);
    // The kick's pitch drops to 45 Hz: from 0.31 s (the hi-hat of step 1 is over) to 0.45 s it is a 45 Hz tone,
    // about 12.6 sign changes in those 0.14 s
    const int crossings = zeroCrossings(drums, 14880, 21600);
    std::printf("kick from 0.31 to 0.45 s: %d sign changes\n", crossings);
    DAWLAB_CHECK(crossings >= 11 && crossings <= 14);
    // The snare (0.5 s, step 2) decays: from 0.56 s, when its hi-hat is over, to its end at 0.75 s
    DAWLAB_CHECK(rmsOf(drums, 33600, 36000) < 0.25 * rmsOf(drums, 26880, 29280));
    // The hi-hat of step 7 (1.75 s, 0.06 s long, alone) decays, and it is a first difference of noise: next samples
    // are anti-correlated (-0.5 for white noise)
    const int hat = 84000;
    DAWLAB_CHECK(rmsOf(drums, hat + 2400, hat + 2880) < 0.2 * rmsOf(drums, hat, hat + 480));
    double lag1 = 0.0, energy = 0.0;
    for (int i = hat + 1; i < hat + 960; ++i) {
        lag1 += double(drums.getSample(0, i)) * drums.getSample(0, i - 1);
        energy += double(drums.getSample(0, i)) * drums.getSample(0, i);
    }
    DAWLAB_CHECK(energy > 0.0 && lag1 / energy < -0.3);
    // At 480 bpm a bar is 0.5 s and the kicks and snare outlast it: they are cut at the end of the loop
    const dawlab::AudioBuffer fast = dawlab::makeDrumLoop(48000.0, 1, 480.0);
    DAWLAB_CHECK(fast.getNumSamples() == 24000 && fast.getSample(0, 23999) != 0.0f && !dawlab::hasNanOrInf(fast));
}

// The generators' recipes, so that every run (and every chapter's printed numbers) gets the same signals
static void checkGeneratorRecipes() {
    using namespace dawlab;
    constexpr double sr = 48000.0;
    // White noise: channel ch is the xorshift stream of seed + 7919 * ch, times the amplitude
    const AudioBuffer noise = makeWhiteNoise(sr, 0.001, 0.5f, 2, 5);
    NoiseGenerator left(5), right(5 + 7919);
    bool same = noise.getNumSamples() == 48;
    for (int i = 0; i < noise.getNumSamples(); ++i)
        same = same && noise.getSample(0, i) == 0.5f * left.next() && noise.getSample(1, i) == 0.5f * right.next();
    DAWLAB_CHECK(same);

    // A Karplus-Strong pluck (480 Hz: a period of 100 samples): its first period is the noise burst itself, and every
    // later sample is 0.996 x the average of the two samples one period back
    const AudioBuffer pluck = makePluck(480.0, sr, 0.01, 1.0f, 1, 7);
    NoiseGenerator burst(7);
    bool ks = pluck.getNumSamples() == 480;
    for (int i = 0; i < 100; ++i)
        ks = ks && pluck.getSample(0, i) == burst.next();
    for (int i = 100; i < pluck.getNumSamples(); ++i) {
        const float twoBack = pluck.getSample(0, i - 100) + pluck.getSample(0, i - 99);
        ks = ks && std::abs(pluck.getSample(0, i) - 0.996f * 0.5f * twoBack) < 1e-6f;
    }
    DAWLAB_CHECK(ks);

    // The pluck sequence: note n starts at n * 0.25 s; it is A2 (110 Hz) then E3 (164.81 Hz), each a pluck with its
    // own seed 99 + n, cut at the end of the sequence
    const AudioBuffer seq = makePluckSequence(sr, 0.5, 0.4f, 1);
    const AudioBuffer a2 = makePluck(110.0, sr, 0.5, 0.4f, 1, 99);
    const AudioBuffer e3 = makePluck(164.81, sr, 0.25, 0.4f, 1, 100);
    bool sum = seq.getNumSamples() == 24000;
    for (int i = 0; i < seq.getNumSamples(); ++i) {
        const float both = a2.getSample(0, i) + (i >= 12000 ? e3.getSample(0, i - 12000) : 0.0f);
        sum = sum && std::abs(seq.getSample(0, i) - both) < 1e-6f;
    }
    DAWLAB_CHECK(sum);
    // At 22050 Hz a quarter second is 5512.5 samples: the second note starts at 5513 but is 5513 long, and it is
    // cut to the 5512 samples left
    const AudioBuffer at22k = makePluckSequence(22050.0, 0.5, 0.4f, 1);
    DAWLAB_CHECK(at22k.getNumSamples() == 11025 && !hasNanOrInf(at22k) && at22k.getSample(0, 11024) != 0.0f);
}

// A WAV file made by hand, for readWav's formats and refusals
struct HandWav {
    uint16_t format = 1, channels = 1;
    uint32_t rate = 48000;
    uint16_t blockAlign = 2, bits = 16;
    uint32_t fmtSize = 16;  // 26 or 40 for WAVE_FORMAT_EXTENSIBLE, whose SubFormat starts at byte 24 of the fmt body
    uint16_t subFormat = 1; // the first two bytes of that SubFormat GUID
    std::vector<uint8_t> samples;
    int64_t declared = -1;     // the data chunk's size field; -1: samples.size()
    std::vector<uint8_t> head; // chunks before the fmt chunk
    std::vector<uint8_t> tail; // bytes after the data chunk
};

static std::string writeHandWav(const std::string& name, const HandWav& w) {
    std::vector<uint8_t> b;
    const auto u16 = [&b](uint32_t v) { b.push_back(uint8_t(v & 0xff)), b.push_back(uint8_t((v >> 8) & 0xff)); };
    const auto u32 = [&u16](uint32_t v) { u16(v & 0xffff), u16(v >> 16); };
    const auto tag = [&b](const char* t) { b.insert(b.end(), t, t + 4); };
    tag("RIFF"), u32(uint32_t(4 + w.head.size() + 8 + w.fmtSize + 8 + w.samples.size() + w.tail.size())), tag("WAVE");
    b.insert(b.end(), w.head.begin(), w.head.end());
    tag("fmt "), u32(w.fmtSize), u16(w.format), u16(w.channels), u32(w.rate), u32(w.rate * w.blockAlign);
    u16(w.blockAlign), u16(w.bits);
    for (uint32_t i = 16; i < w.fmtSize; ++i) // cbSize, valid bits and channel mask are left 0
        b.push_back(uint8_t(i == 24 ? w.subFormat & 0xff : i == 25 ? w.subFormat >> 8 : 0));
    tag("data"), u32(uint32_t(w.declared < 0 ? int64_t(w.samples.size()) : w.declared));
    b.insert(b.end(), w.samples.begin(), w.samples.end());
    b.insert(b.end(), w.tail.begin(), w.tail.end());
    const std::string path = dawlab::outputPath("common_" + name + ".wav");
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
    return path;
}

// readWav reads the file, and its first channel is `expected`
static bool readsAs(const HandWav& w, const std::string& name, const std::vector<float>& expected) {
    dawlab::WavData wav;
    if (!dawlab::readWav(writeHandWav(name, w), wav))
        return false;
    const float* x = wav.buffer.getReadPointer(0);
    return std::vector<float>(x, x + wav.buffer.getNumSamples()) == expected;
}

static bool refused(const HandWav& w, const std::string& name) {
    dawlab::WavData wav;
    return !dawlab::readWav(writeHandWav(name, w), wav);
}

// The formats readWav reads (each sample decoded to its exact value) and the ones it refuses
static void checkWavFormats() {
    HandWav u8;
    u8.bits = 8, u8.blockAlign = 1, u8.samples = {0, 128, 255}; // 8-bit is unsigned, 128 = 0
    DAWLAB_CHECK(readsAs(u8, "pcm8", {-1.0f, 0.0f, 127.0f / 128.0f}));
    HandWav s32;
    s32.bits = 32, s32.blockAlign = 4, s32.samples = {0, 0, 0, 0x40, 0, 0, 0, 0x80};
    DAWLAB_CHECK(readsAs(s32, "pcm32", {0.5f, -1.0f}));
    HandWav f64;
    f64.format = 3, f64.bits = 64, f64.blockAlign = 8, f64.samples = {0, 0, 0, 0, 0, 0, 0xD8, 0x3F}; // 0.375
    DAWLAB_CHECK(readsAs(f64, "float64", {0.375f}));
    // WAVE_FORMAT_EXTENSIBLE: the format is the SubFormat's, in a fmt chunk of 40 bytes or just long enough (26)
    HandWav ext;
    ext.format = 0xFFFE, ext.fmtSize = 40, ext.samples = {0, 0x40}; // 16-bit PCM 0.5
    DAWLAB_CHECK(readsAs(ext, "extensible40", {0.5f}));
    ext.fmtSize = 26;
    DAWLAB_CHECK(readsAs(ext, "extensible26", {0.5f}));
    HandWav pcm12;
    pcm12.bits = 12, pcm12.samples = {0, 0};
    HandWav float16;
    float16.format = 3, float16.samples = {0, 0};
    HandWav narrow; // stereo 16-bit needs 4 bytes a frame
    narrow.channels = 2, narrow.samples = {0, 0, 0, 0};
    DAWLAB_CHECK(refused(pcm12, "pcm12") && refused(float16, "float16") && refused(narrow, "narrow_frame"));

    // The chunk walk: a chunk of an odd size is followed by its pad byte; a data chunk 2 bytes short of what it says
    // is refused; an empty data chunk that ends the file is read (0 samples); stray bytes after the last chunk, fewer
    // than a chunk header, are ignored
    HandWav afterOdd;
    afterOdd.head = {'L', 'I', 'S', 'T', 3, 0, 0, 0, 'a', 'b', 'c', 0}, afterOdd.samples = {0, 0x40};
    DAWLAB_CHECK(readsAs(afterOdd, "after_odd_chunk", {0.5f}));
    HandWav shortBy2;
    shortBy2.samples = {0, 0, 0, 0}, shortBy2.declared = 6;
    HandWav empty;
    HandWav stray;
    stray.samples = {0, 0x40}, stray.tail = {'f', 'm', 't', ' ', 16, 0, 0};
    DAWLAB_CHECK(refused(shortBy2, "short_by_2"));
    DAWLAB_CHECK(readsAs(empty, "empty_data", {}) && readsAs(stray, "stray_tail", {0.5f}));
    // A RIFF/WAVE header of 12 bytes with no chunk: a WAVE without its fmt and data, not "not a WAVE"
    const std::string bare = dawlab::outputPath("common_bare.wav");
    std::ofstream(bare, std::ios::binary).write("RIFF\x04\0\0\0WAVE", 12);
    dawlab::WavData wav;
    std::string why;
    DAWLAB_CHECK(!dawlab::readWav(bare, wav, &why) && why.find("not found") != std::string::npos);
}

static std::vector<uint8_t> fileBytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

// writeWav's header: the RIFF size is the file size less 8 (with the pad byte of an odd data chunk), the byte rate
// is rate x channels x bytes per sample, and it must fit in 32 bits
static void checkWavHeader() {
    using dawlab::writeWav;
    const std::string path = dawlab::outputPath("common_header.wav");
    DAWLAB_CHECK(writeWav(path, dawlab::AudioBuffer(1, 1), 44100.0, 24)); // 3 bytes of data and a pad byte
    std::vector<uint8_t> b = fileBytes(path);
    DAWLAB_CHECK(b.size() == 48 && dawlab::detail::readU32(&b[4]) == 40 && dawlab::detail::readU32(&b[28]) == 132300);
    DAWLAB_CHECK(writeWav(path, dawlab::AudioBuffer(2, 2), 8000.0, 16));
    b = fileBytes(path);
    DAWLAB_CHECK(b.size() == 52 && dawlab::detail::readU32(&b[4]) == 44 && dawlab::detail::readU32(&b[28]) == 32000);
    // 0xFFFFFFFF = 1431655765 x 3: a mono 24-bit file at that rate is the largest byte rate there is
    DAWLAB_CHECK(writeWav(path, dawlab::AudioBuffer(1, 1), 1431655765.0, 24) &&
                 !writeWav(path, dawlab::AudioBuffer(1, 1), 1431655766.0, 24));
    // The sample rates a WAV can say: 1 Hz up to 0xFFFFFFFF Hz
    using dawlab::detail::isWavSampleRate;
    DAWLAB_CHECK(isWavSampleRate(1.0) && !isWavSampleRate(0.999));
    DAWLAB_CHECK(isWavSampleRate(4294967295.0) && !isWavSampleRate(4294967296.0));
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

    checkWavFormats();
    checkWavHeader();

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
    // addFrom adds as many samples as the shorter block has, and not one more: b's 8 samples onto a's samples 2..5
    // leave sample 6 alone
    AudioBlock(a).getSubBlock(2, 4).addFrom(AudioBlock(b));
    DAWLAB_CHECK(a.getSample(1, 5) == 3.0f && a.getSample(1, 6) == 1.0f);

    // Test signals must contain no NaN and must not clip
    AudioBuffer drums = makeDrumLoop(sr);
    AudioBuffer plucks = makePluckSequence(sr);
    DAWLAB_CHECK(!hasNanOrInf(drums) && !hasNanOrInf(plucks));
    DAWLAB_CHECK(peakAbs(drums) < 1.0f && peakAbs(drums) > 0.3f);
    DAWLAB_CHECK(peakAbs(plucks) < 1.0f && peakAbs(plucks) > 0.1f);
    std::printf("drum loop peak = %.2f dBFS, pluck sequence peak = %.2f dBFS\n", gainToDb(peakAbs(drums)),
                gainToDb(peakAbs(plucks)));
    checkDrumLoop(drums);
    checkGeneratorRecipes();

    checkSummaryRule();
    checkCounting();

    // The verdict rests on the counters, not on summaryOf alone: summaryOf is under test above, and a summaryOf that
    // got its rule backwards would pass a run whose probes of it failed.
    const int verdict = dawlab::test::summary("common");
    const dawlab::test::Counters& counted = dawlab::test::counters();
    return verdict == 0 && counted.checks > 0 && counted.failures == 0 ? 0 : 1;
}
