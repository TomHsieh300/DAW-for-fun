// test_track_format: mono and stereo tracks (Steps 4 and 5).
//
//   Mono    (L+R)/2 -> inserts on one channel -> copy to both sides -> pan law (centre -3 dB)
//   Stereo  inserts on both channels -> balance (centre unity, image kept)

#include "TestHelpers.h"

using namespace l14test;

namespace {

constexpr int kLen = 4800; // 100 ms

// A stereo clip with a different constant on each side, so L and R can be told apart.
std::shared_ptr<const AudioBuffer> makeStereoConstant(float left, float right, int numSamples) {
    auto b = std::make_shared<AudioBuffer>(2, numSamples);
    for (int i = 0; i < numSamples; ++i) {
        b->setSample(0, i, left);
        b->setSample(1, i, right);
    }
    return b;
}

float lastSample(Engine& e, int ch) {
    AudioBuffer out = render(e, kLen);
    return out.getSample(ch, kLen - 1);
}

void setupOneTrack(Engine& e, std::shared_ptr<const AudioBuffer> clip) {
    e.prepare(kSampleRate, kBlockSize);
    e.setTrackClips(0, {Clip::fromBuffer(std::move(clip), 0, "clip")});
}

void testFormatSelectsPanMode() {
    Track t;
    DAWLAB_CHECK(t.getFormat() == TrackFormat::Mono); // default, as before: pan law
    DAWLAB_CHECK(t.getPanMode() == PanMode::PanLaw);
    t.setFormat(TrackFormat::Stereo);
    DAWLAB_CHECK(t.getPanMode() == PanMode::Balance);
    DAWLAB_CHECK(t.getNumProcessingChannels() == 2);
    t.setFormat(TrackFormat::Mono);
    DAWLAB_CHECK(t.getPanMode() == PanMode::PanLaw);
    DAWLAB_CHECK(t.getNumProcessingChannels() == 1);
    DAWLAB_CHECK(std::string(trackFormatName(TrackFormat::Stereo)) == "Stereo");
}

void testMonoTrack() {
    Engine e;
    setupOneTrack(e, makeStereoConstant(0.6f, 0.2f, kLen));
    Track& t = e.getMixer().getTrack(0);
    t.setFormat(TrackFormat::Mono);

    // folded to (0.6 + 0.2) / 2 = 0.4, then centred with constant power: 0.4 * 0.7071 per side
    DAWLAB_CHECK_NEAR(lastSample(e, 0), 0.4 * 0.70711, 1e-4);
    DAWLAB_CHECK_NEAR(lastSample(e, 1), 0.4 * 0.70711, 1e-4);

    // hard right: all of the mono source on the right, nothing on the left
    t.setPan(1.0f);
    DAWLAB_CHECK_NEAR(lastSample(e, 0), 0.0, 1e-5);
    DAWLAB_CHECK_NEAR(lastSample(e, 1), 0.4, 1e-4);
}

void testStereoTrack() {
    Engine e;
    setupOneTrack(e, makeStereoConstant(0.6f, 0.2f, kLen));
    Track& t = e.getMixer().getTrack(0);
    t.setFormat(TrackFormat::Stereo);

    // balance at the centre: unity, and the image is kept (L != R)
    DAWLAB_CHECK_NEAR(lastSample(e, 0), 0.6, 1e-4);
    DAWLAB_CHECK_NEAR(lastSample(e, 1), 0.2, 1e-4);

    // balance right by half: left side x0.5, right side untouched
    t.setPan(0.5f);
    DAWLAB_CHECK_NEAR(lastSample(e, 0), 0.3, 1e-4);
    DAWLAB_CHECK_NEAR(lastSample(e, 1), 0.2, 1e-4);
}

// The same mono file (L = R) on a mono and on a stereo track differs by the pan law: 3 dB.
void testMonoFileOnBothFormats() {
    Engine e;
    setupOneTrack(e, makeConstant(0.5f, kLen));
    Track& t = e.getMixer().getTrack(0);
    t.setFormat(TrackFormat::Mono);
    const float mono = lastSample(e, 0);
    t.setFormat(TrackFormat::Stereo);
    const float stereo = lastSample(e, 0);
    std::printf("mono file at the centre: mono track %.4f, stereo track %.4f -> %.2f dB\n", mono, stereo,
                gainToDb(stereo / mono));
    DAWLAB_CHECK_NEAR(gainToDb(stereo / mono), 3.0103, 1e-3);
}

// Every built-in effect must run on a one-channel block (a mono track's inserts) without
// touching a second channel, produce finite output and report the same latency.
void testEveryEffectOnOneChannel() {
    AudioBuffer noise = makeWhiteNoise(kSampleRate, 0.25, 0.3f, 1);
    int ok = 0;
    for (int t = 0; t < kNumUserEffectTypes; ++t) {
        const auto type = static_cast<EffectType>(t);
        auto fx = createEffect(type);
        DAWLAB_CHECK(fx != nullptr);
        if (fx == nullptr)
            continue;
        fx->prepare(kSampleRate, kBlockSize, kNumChannels);
        const int latency = fx->getLatencySamples();
        AudioBuffer buf = noise;
        bool finite = true;
        for (int start = 0; start + kBlockSize <= buf.getNumSamples(); start += kBlockSize) {
            AudioBlock block = AudioBlock(buf).getSubBlock(start, kBlockSize);
            fx->process(block);
        }
        finite = !hasNanOrInf(buf);
        DAWLAB_CHECK(finite);
        DAWLAB_CHECK(fx->getLatencySamples() == latency);
        if (finite && fx->getLatencySamples() == latency)
            ++ok;
        else
            std::printf("  %s failed on one channel\n", effectTypeName(type));
    }
    std::printf("%d of %d effects run on a one-channel block\n", ok, kNumUserEffectTypes);
}

// A stereo-widening insert (chorus with full spread) on a mono track: its output is folded to
// the mono path, so at the centre both sides are identical. On a stereo track it is wide.
void testWideningInsert() {
    auto runWith = [](TrackFormat format) {
        Engine e;
        e.prepare(kSampleRate, kBlockSize);
        auto clip = std::make_shared<AudioBuffer>(makeSine(440.0, kSampleRate, 0.5, 0.5f, 2));
        e.setTrackClips(0, {Clip::fromBuffer(clip, 0, "sine")});
        e.getMixer().getTrack(0).setFormat(format);
        auto fx = createEffect(EffectType::Chorus);
        fx->setParameter(fx->findParameter("Stereo Spread"), 1.0f);
        e.addInsert(ChannelRef::track(0), 0, std::move(fx));
        e.applyPendingCommandsOffline();
        AudioBuffer out = render(e, 12000);
        float d = 0.0f;
        for (int i = 0; i < out.getNumSamples(); ++i)
            d = std::max(d, std::abs(out.getSample(0, i) - out.getSample(1, i)));
        return d;
    };
    const float monoDiff = runWith(TrackFormat::Mono);
    const float stereoDiff = runWith(TrackFormat::Stereo);
    std::printf("chorus (spread 1): max |L - R| mono track %.6f, stereo track %.4f\n", monoDiff, stereoDiff);
    DAWLAB_CHECK(monoDiff < 1e-6f);
    DAWLAB_CHECK(stereoDiff > 0.01f);
}

} // namespace

int main() {
    testFormatSelectsPanMode();
    testMonoTrack();
    testStereoTrack();
    testMonoFileOnBothFormats();
    testEveryEffectOnOneChannel();
    testWideningInsert();
    return dawlab::test::summary("l14_test_track_format");
}
