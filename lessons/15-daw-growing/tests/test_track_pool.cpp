// test_track_pool: the pool of track slots that replaced chapter 14's 8 fixed tracks (chapter 15,
// Step 6). Built with a small pool (DAWLAB_ENGINE_MAX_TRACKS=16, see CMakeLists.txt) so that the
// test can fill it.
//
// Every track plays a constant ("DC") clip, so the output shows exactly which tracks are heard:
// out = (sum of the DC values of the audible tracks) x unit, where `unit` is what one track at
// 1.0 gives (fader 0 dB, centre).

#include "TestHelpers.h"
#include "TestTracks.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>

using namespace l15test;
using namespace dawlab::engine::test;

namespace {

static_assert(kMaxTracks == 16, "this test expects the small pool of CMakeLists.txt");

constexpr int kLen = 4096;

std::atomic<int> gAlive{0};

// An effect that counts its living instances (as in test_commands), to see what comes back as
// garbage when a track is removed.
class CountingEffect final : public EffectBase {
public:
    enum Param { Gain, kCount };
    CountingEffect() : EffectBase(kInfos, kCount) { ++gAlive; }
    ~CountingEffect() override { --gAlive; }
    const char* getName() const noexcept override { return "Counting"; }
    void prepare(double sr, int, int) override { finishPrepare(sr); }
    void reset() noexcept override {}

protected:
    void applyParameter(int, float) noexcept override {}
    void processDsp(AudioBlock) noexcept override {}

private:
    static constexpr ParameterInfo kInfos[kCount] = {
        {"Gain", "x", 0.0f, 4.0f, 1.0f, ParameterKind::Float, false, nullptr, 0},
    };
};

// The largest |sample| in a buffer, both channels.
float peak(const AudioBuffer& b) {
    float p = 0.0f;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            p = std::max(p, std::abs(b.getSample(ch, i)));
    return p;
}

// The output level (left channel, last sample) of a short render.
float level(Engine& e) {
    AudioBuffer out = render(e, kLen);
    return out.getSample(0, kLen - 1);
}

ChannelRef addDcTrack(Engine& e, std::uint32_t id, float dc) {
    const ChannelRef ref = e.addTrack(TrackId{id}, TrackFormat::Mono, "T" + std::to_string(id));
    e.setTrackClips(TrackId{id}, {Clip::fromBuffer(makeConstant(dc, kLen), 0, "dc")});
    return ref;
}

void testAddRemoveReuse() {
    Engine e;
    e.prepare(kSampleRate, kBlockSize);
    const ChannelRef a = addDcTrack(e, 1, 1.0f);
    const float unit = level(e);
    const ChannelRef b = addDcTrack(e, 2, 0.5f);
    const ChannelRef c = addDcTrack(e, 3, 0.25f);
    const float abc = level(e);
    std::printf("  add 3 tracks (1.0, 0.5, 0.25): slots %d %d %d, level %.4f = %.3f units (expected 0 1 2, 1.75)\n", a.index,
                b.index, c.index, abc, abc / unit);
    DAWLAB_CHECK(a.index == 0 && b.index == 1 && c.index == 2);
    DAWLAB_CHECK(unit > 0.1f);
    DAWLAB_CHECK_NEAR(abc / unit, 1.75, 1e-5);
    DAWLAB_CHECK(e.getNumTracks() == 3 && e.getMixer().getNumActiveTracks() == 3);

    // Remove the middle track and add a new one: it takes slot 1 at once (no handshake), and
    // plays only its own clip.
    DAWLAB_CHECK(e.removeTrack(TrackId{2}));
    DAWLAB_CHECK(!e.removeTrack(TrackId{2})); // twice = once
    const ChannelRef d = addDcTrack(e, 4, 0.125f);
    const float acd = level(e);
    std::printf("  remove track 2, add track 4 (0.125): slot %d, level %.3f units (expected 1, 1.375)\n", d.index, acd / unit);
    DAWLAB_CHECK(d.index == 1 && e.getMixer().trackInSlot(1) == TrackId{4});
    DAWLAB_CHECK_NEAR(acd / unit, 1.375, 1e-5);
    DAWLAB_CHECK(e.slotOf(TrackId{2}) == -1 && e.getTrack(TrackId{2}) == nullptr);
    e.collectGarbage();
}

void testStaleCommands() {
    Engine e;
    e.prepare(kSampleRate, kBlockSize);
    addDcTrack(e, 1, 1.0f);
    const ChannelRef bass = addDcTrack(e, 2, 0.5f); // slot 1
    level(e);
    // Bass is deleted and Vox takes its slot...
    e.removeTrack(TrackId{2});
    const ChannelRef vox = addDcTrack(e, 3, 0.25f);
    const int aliveBefore = gAlive.load();
    // ...then commands recorded for Bass arrive (an undo entry, a plug-in window still open).
    const bool uiRefusesInsert = e.addInsert(bass, -1, std::make_unique<CountingEffect>()) == nullptr;
    const bool uiRefusesClips = !e.setTrackClips(TrackId{2}, {Clip::fromBuffer(makeConstant(9.0f, kLen), 0, "x")});
    // Even past the UI's checks (a raw command), the audio thread refuses: wrong id for slot 1.
    e.pushCommand(Command::addInsert(bass, -1, new CountingEffect()));
    e.pushCommand(Command::replaceClips(bass, new std::vector<Clip>{Clip::fromBuffer(makeConstant(9.0f, kLen), 0, "x")}));
    e.pushCommand(Command::removeTrack(bass));
    const float lvl = level(e);
    e.collectGarbage();
    std::printf("  late commands for deleted Bass (slot %d, now Vox): UI refuses %s; audio thread: Vox inserts %d,\n"
                "  Vox still playing %s, the rejected effect freed %s (expected yes, 0, yes, yes)\n",
                bass.index, (uiRefusesInsert && uiRefusesClips) ? "yes" : "no", e.getNumInserts(vox),
                e.getMixer().trackInSlot(vox.index) == TrackId{3} ? "yes" : "no", gAlive.load() == aliveBefore ? "yes" : "no");
    DAWLAB_CHECK(vox.index == bass.index);
    DAWLAB_CHECK(uiRefusesInsert && uiRefusesClips);
    DAWLAB_CHECK(e.getNumInserts(vox) == 0 && e.getMixer().trackInSlot(vox.index) == TrackId{3});
    DAWLAB_CHECK(gAlive.load() == aliveBefore);
    DAWLAB_CHECK(lvl > 0.0f && lvl < 2.0f); // no 9.0 clip anywhere
}

void testRemoveReturnsEverything() {
    Engine e;
    e.prepare(kSampleRate, kBlockSize);
    const ChannelRef t = addDcTrack(e, 1, 1.0f);
    for (int i = 0; i < kMaxInserts; ++i)
        e.addInsert(t, -1, std::make_unique<CountingEffect>());
    auto buffer = makeConstant(1.0f, kLen);
    e.setTrackClips(TrackId{1}, {Clip::fromBuffer(buffer, 0, "dc")});
    level(e);
    e.collectGarbage();
    const int alive = gAlive.load();
    const long uses = buffer.use_count();
    e.removeTrack(TrackId{1});
    AudioBuffer block(2, 64);
    e.processBlock(block); // (not level(): renderOffline() collects the garbage itself)
    const int freed = e.collectGarbage();
    std::printf("  remove a track with %d inserts and a clip: %d objects back as garbage, effects alive %d -> %d,\n"
                "  clip buffer users %ld -> %ld (expected %d, %d -> 0, 2 -> 1)\n",
                kMaxInserts, freed, alive, gAlive.load(), uses, buffer.use_count(), kMaxInserts + 1, kMaxInserts);
    DAWLAB_CHECK(freed == kMaxInserts + 1 && alive == kMaxInserts && gAlive.load() == 0);
    DAWLAB_CHECK(uses == 2 && buffer.use_count() == 1);
    DAWLAB_CHECK(e.getGarbageLeaked() == 0);
}

// A reused slot starts silent. The old track's inserts leave with it, but the strip itself holds
// audio too: its latency-compensation delay line. Here track 2 has 2000 samples of insert
// latency, so track 1's compensator delays it by 2000 samples - and is full of track 1's DC when
// track 1 is removed. Without the reset in deactivateTrack(), the next track in slot 0 would
// play those 2000 samples of the old track first.
void testCleanSlot() {
    Engine e;
    e.prepare(kSampleRate, kBlockSize);
    e.addTrack(TrackId{1}, TrackFormat::Stereo, "old");
    e.setTrackClips(TrackId{1}, {Clip::fromBuffer(makeConstant(1.0f, 48000), 0, "dc")});
    const ChannelRef slow = e.addTrack(TrackId{2}, TrackFormat::Stereo, "slow"); // no clips: silent
    e.addInsert(slow, -1, std::make_unique<PureDelayEffect>(2000));
    e.applyPendingCommandsOffline();
    e.getTransport().setPosition(0);
    e.getTransport().play();
    AudioBuffer block(2, kBlockSize);
    float before = 0.0f;
    for (int i = 0; i < 20; ++i) {
        e.processBlock(block);
        before = std::max(before, peak(block));
    }
    const int compensation = e.getMixer().getTrack(0).getCompensator().getDelaySamples();
    e.removeTrack(TrackId{1});
    e.addTrack(TrackId{3}, TrackFormat::Stereo, "new"); // slot 0 again, no clips
    float after = 0.0f;
    for (int i = 0; i < 20; ++i) {
        e.processBlock(block);
        after = std::max(after, peak(block));
    }
    e.collectGarbage();
    std::printf("  slot 0 delayed by %d samples of PDC, full of the old track's audio; reused by an empty track:\n"
                "  before %.3f, after %.6f (expected 2000, > 0, 0)\n",
                compensation, before, after);
    DAWLAB_CHECK(compensation == 2000);
    DAWLAB_CHECK(before > 0.5f);
    DAWLAB_CHECK(after == 0.0f);
}

void testGarbageBackpressure() {
    // A queue of 16: one removal returns up to 9 objects, so only one fits until the UI collects.
    Engine e(16);
    e.prepare(kSampleRate, kBlockSize);
    for (std::uint32_t id = 1; id <= 4; ++id) {
        const ChannelRef t = e.addTrack(TrackId{id});
        e.applyPendingCommandsOffline();
        for (int i = 0; i < kMaxInserts; ++i) {
            e.addInsert(t, -1, std::make_unique<CountingEffect>());
            e.applyPendingCommandsOffline();
        }
    }
    const int alive = gAlive.load();
    for (std::uint32_t id = 1; id <= 4; ++id)
        e.removeTrack(TrackId{id});
    AudioBuffer block(2, 64);
    for (int i = 0; i < 10; ++i)
        e.processBlock(block); // nobody collects garbage meanwhile
    const std::size_t waiting = e.getPendingCommands();
    int rounds = 0;
    while (e.getMixer().getNumActiveTracks() > 0 && rounds < 10) {
        e.collectGarbage();
        e.processBlock(block);
        ++rounds;
    }
    e.collectGarbage();
    std::printf("  remove 4 tracks with %d inserts each, garbage queue 16, no collecting: %zu removals wait, %llu leaked;\n"
                "  after %d collect rounds: %d tracks left, effects alive %d -> %d (expected 3, 0; 0, %d -> 0)\n",
                kMaxInserts, waiting, static_cast<unsigned long long>(e.getGarbageLeaked()), rounds,
                e.getMixer().getNumActiveTracks(), alive, gAlive.load(), 4 * kMaxInserts);
    DAWLAB_CHECK(waiting == 3);
    DAWLAB_CHECK(e.getGarbageLeaked() == 0);
    DAWLAB_CHECK(e.getMixer().getNumActiveTracks() == 0 && gAlive.load() == 0);
}

void testFullPoolAndIds() {
    Engine e;
    e.prepare(kSampleRate, kBlockSize);
    int added = 0;
    for (std::uint32_t id = 1; id <= kMaxTracks; ++id)
        added += e.addTrack(TrackId{id}).index >= 0 ? 1 : 0;
    const bool extra = e.addTrack(TrackId{100}).index >= 0;
    const bool duplicate = e.addTrack(TrackId{3}).index >= 0; // id 3 is already here
    e.removeTrack(TrackId{5});
    const bool invalid = e.addTrack(TrackId{}).index >= 0;
    const ChannelRef again = e.addTrack(TrackId{101});
    e.applyPendingCommandsOffline();
    std::printf("  a pool of %d: %d added, one more %s, id 3 again %s, id 0 %s; after removing id 5: slot %d\n"
                "  (expected %d, refused, refused, refused, 4)\n",
                kMaxTracks, added, extra ? "added?!" : "refused", duplicate ? "added?!" : "refused",
                invalid ? "added?!" : "refused", again.index, kMaxTracks);
    DAWLAB_CHECK(added == kMaxTracks && !extra && !duplicate && !invalid);
    DAWLAB_CHECK(again.index == 4 && e.getMixer().trackInSlot(4) == TrackId{101});
    DAWLAB_CHECK(e.getMixer().getNumActiveTracks() == kMaxTracks);
}

// A soloed track that is deleted must not keep everything else silent.
void testSoloOfRemovedTrack() {
    Engine e;
    e.prepare(kSampleRate, kBlockSize);
    addDcTrack(e, 1, 1.0f);
    addDcTrack(e, 2, 0.5f);
    const float unit = level(e) / 1.5f;
    e.getTrack(TrackId{2})->setSolo(true);
    const float soloed = level(e);
    e.removeTrack(TrackId{2}); // its slot keeps the old solo flag until it is reused
    const float after = level(e);
    std::printf("  solo track 2, then delete it: %.2f units, then %.2f units (expected 0.50, 1.00)\n", soloed / unit,
                after / unit);
    DAWLAB_CHECK_NEAR(soloed / unit, 0.5, 1e-5);
    DAWLAB_CHECK_NEAR(after / unit, 1.0, 1e-5);
    e.collectGarbage();
}

// Engine::getStrip() answers by the UI's table: a track is there from addTrack() on and gone from
// removeTrack() on, whatever the audio thread has applied so far (Step 7's app relies on it).
void testUiStripLookup() {
    Engine e;
    e.prepare(kSampleRate, kBlockSize);
    const ChannelRef a = e.addTrack(TrackId{1}, TrackFormat::Mono, "A");
    const bool seenBeforeApplied = e.getStrip(a) != nullptr && e.getMixer().trackInSlot(a.index) != TrackId{1};
    e.applyPendingCommandsOffline();
    e.removeTrack(TrackId{1});
    const bool goneBeforeApplied = e.getStrip(a) == nullptr && e.getMixer().trackInSlot(a.index) == TrackId{1};
    e.applyPendingCommandsOffline();
    const ChannelRef b = e.addTrack(TrackId{2}); // takes slot 0 over
    const bool oldRefRefused = e.getStrip(a) == nullptr && e.getStrip(b) != nullptr && a.index == b.index;
    const bool busAndMaster = e.getStrip(ChannelRef::bus(0)) != nullptr && e.getStrip(ChannelRef::master()) != nullptr;
    std::printf("  UI lookup: new track seen before the audio thread applied it: %s, removed track gone at once: %s,\n"
                "  old reference to a reused slot refused: %s, bus and master: %s\n",
                seenBeforeApplied ? "yes" : "no", goneBeforeApplied ? "yes" : "no", oldRefRefused ? "yes" : "no",
                busAndMaster ? "yes" : "no");
    DAWLAB_CHECK(seenBeforeApplied && goneBeforeApplied && oldRefRefused && busAndMaster);
    e.applyPendingCommandsOffline();
    e.collectGarbage();
}

void testCpuMeter() {
    Engine e;
    e.prepare(kSampleRate, kBlockSize);
    addDcTrack(e, 1, 0.5f);
    AudioBuffer block(2, 1000); // longer than kBlockSize: processed in 2 chunks, measured once
    for (int i = 0; i < 50; ++i)
        e.processBlock(block);
    const CpuMeter& m = e.getCpuMeter();
    std::printf("  CPU meter after 50 processBlock() calls: %llu blocks measured, load %.4f %%\n",
                static_cast<unsigned long long>(m.blocks()), 100.0 * m.load());
    DAWLAB_CHECK(m.blocks() == 50);
    DAWLAB_CHECK(m.load() >= 0.0f && m.load() < 100.0f);
}

} // namespace

int main() {
    std::printf("l15_test_track_pool: a pool of %d track slots\n", kMaxTracks);
    testAddRemoveReuse();
    testStaleCommands();
    testRemoveReturnsEverything();
    testCleanSlot();
    testGarbageBackpressure();
    testFullPoolAndIds();
    testSoloOfRemovedTrack();
    testUiStripLookup();
    testCpuMeter();
    DAWLAB_CHECK(gAlive.load() == 0);
    return dawlab::test::summary("l15_test_track_pool");
}
