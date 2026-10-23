// test_block_invariance: the mix must not depend on the audio callback's block size (Step 10).
//
// A host may call us with 64, 512, 441 or a different size every time. If the result changed
// with the block size, a bounce would not sound like playback and automation would jitter.
// We render the same project with block 512, block 64 and random block sizes 1..1500 and
// require the renders to match.

#include "TestHelpers.h"

#include <random>

using namespace l14test;

namespace {

constexpr int kSeconds = 3;

void buildProject(Engine& e) {
    e.prepare(kSampleRate, 512);
    auto drums = std::make_shared<const AudioBuffer>(makeDrumLoop(kSampleRate, 1, 120.0, 0.7f));
    auto pluck = std::make_shared<const AudioBuffer>(makePluckSequence(kSampleRate, 3.0, 0.4f));
    auto noise = std::make_shared<const AudioBuffer>(makeWhiteNoise(kSampleRate, 3.0, 0.1f));

    e.setTrackClips(0, {Clip::fromBuffer(drums, 0), Clip::fromBuffer(drums, 96000)});
    Clip p = Clip::fromBuffer(pluck, 777); // not on a block boundary on purpose
    p.fadeInSamples = 300;
    p.fadeOutSamples = 4800;
    p.fadeShape = FadeShape::EqualPower;
    e.setTrackClips(1, {p});
    e.setTrackClips(2, {Clip::fromBuffer(noise, 12345)});

    Mixer& m = e.getMixer();
    m.getTrack(0).setGainDb(-3.0f);
    m.getTrack(1).setPan(-0.4f);
    m.getTrack(2).setPan(0.6f);
    m.getTrack(2).setGainDb(-12.0f);
    m.getTrack(1).setSendLevelDb(0, -6.0f);
    m.getTrack(0).setSendLevelDb(1, -10.0f);
    m.getTrack(0).setSendPreFader(1, true);

    // inserts whose output is a deterministic per-sample function of the input
    e.addInsert(ChannelRef::track(0), -1, std::make_unique<CompressorEffect>());
    e.addInsert(ChannelRef::track(1), -1, std::make_unique<PureDelayEffect>(333));
    auto eq = std::make_unique<ParametricEqEffect>();
    eq->setBand(2, true, dsp::EqBandType::Peak, 1500.0f, 1.0f, 6.0f);
    e.addInsert(ChannelRef::track(1), -1, std::move(eq));
    e.addInsert(ChannelRef::track(2), -1, std::make_unique<GateEffect>());
    auto delay = std::make_unique<DelayEffect>();
    delay->setParameter(DelayEffect::Mix, 1.0f);
    e.addInsert(ChannelRef::bus(0), -1, std::move(delay));
    auto lim = std::make_unique<LimiterEffect>();
    lim->setParameter(LimiterEffect::InputGain, 6.0f);
    e.addInsert(ChannelRef::master(), -1, std::move(lim));
}

AudioBuffer renderWithBlocks(int fixedBlock, uint32_t seed) {
    Engine e;
    buildProject(e);
    e.applyPendingCommandsOffline();
    e.getTransport().play();
    const int n = kSeconds * static_cast<int>(kSampleRate);
    AudioBuffer out(2, n);
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> dist(1, 1500);
    int pos = 0;
    while (pos < n) {
        const int len = std::min(fixedBlock > 0 ? fixedBlock : dist(rng), n - pos);
        e.processBlock(out.toBlock().getSubBlock(pos, len));
        pos += len;
    }
    return out;
}

} // namespace

int main() {
    const AudioBuffer a = renderWithBlocks(512, 0);
    const AudioBuffer b = renderWithBlocks(64, 0);
    const AudioBuffer c = renderWithBlocks(0, 42);
    const AudioBuffer d = renderWithBlocks(0, 4242);

    DAWLAB_CHECK(!hasNanOrInf(a));
    DAWLAB_CHECK(peakAbs(a) > 0.05f); // the project actually makes sound
    const float d64 = maxAbsDiff(a, b), dRandom = maxAbsDiff(a, c), dRandom2 = maxAbsDiff(a, d);
    std::printf("  max |difference| 512 vs 64: %.3g, 512 vs random: %.3g, 512 vs random(2): %.3g\n", d64,
                dRandom, dRandom2);
    DAWLAB_CHECK(d64 < 1e-5f);
    DAWLAB_CHECK(dRandom < 1e-5f);
    DAWLAB_CHECK(dRandom2 < 1e-5f);
    return dawlab::test::summary("l14_test_block_invariance");
}
