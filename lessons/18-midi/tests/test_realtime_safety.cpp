// test_realtime_safety: the audio thread never allocates or frees, survives random block sizes
// and a second thread hammering the whole API (Step 10).
//
// How we catch allocations: this file replaces the global operator new / delete (all the
// variants). The replacements count calls while a thread_local flag is set, and the "audio
// thread" sets the flag only around processBlock(). Any std::vector growth, std::string,
// std::function, make_shared, ... inside processBlock() would show up in the counters.
//
// Part 1 checks every effect adapter on its own (so a failure names the culprit).
// Part 2 runs a full 8-track project on an audio thread with random block sizes (1..2048)
// while the main thread plays "UI": faders, pans, mutes, solos, sends, effect parameters,
// bypass, add / remove / move / replace inserts, new clip lists, seeks, loops, tempo,
// play / stop and garbage collection.
// Part 3 (chapter 15) does the same while tracks come and go: random add / remove track, clips
// and inserts on the tracks that exist, and late commands for tracks that were removed. The
// audio thread must still never allocate, and afterwards the UI's slot table and the audio
// thread's must agree.

#include "TestHelpers.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <new>
#include <random>
#include <thread>

#include "TestTracks.h"

// ---- allocation counting ------------------------------------------------------------------
namespace {
thread_local bool tCounting = false;
std::atomic<long> gAllocs{0};
std::atomic<long> gFrees{0};

void* countedAlloc(std::size_t n) {
    if (tCounting)
        gAllocs.fetch_add(1, std::memory_order_relaxed);
    void* p = std::malloc(n == 0 ? 1 : n);
    if (p == nullptr)
        throw std::bad_alloc();
    return p;
}
void countedFree(void* p) noexcept {
    if (p != nullptr && tCounting)
        gFrees.fetch_add(1, std::memory_order_relaxed);
    std::free(p);
}
void* countedAlignedAlloc(std::size_t n, std::size_t align) {
    if (tCounting)
        gAllocs.fetch_add(1, std::memory_order_relaxed);
#if defined(_MSC_VER)
    void* p = _aligned_malloc(n == 0 ? 1 : n, align);
#else
    void* p = std::aligned_alloc(align, ((n == 0 ? 1 : n) + align - 1) / align * align);
#endif
    if (p == nullptr)
        throw std::bad_alloc();
    return p;
}
void countedAlignedFree(void* p) noexcept {
    if (p != nullptr && tCounting)
        gFrees.fetch_add(1, std::memory_order_relaxed);
#if defined(_MSC_VER)
    _aligned_free(p);
#else
    std::free(p);
#endif
}

struct CountScope { // RAII: count allocations of this thread while alive
    CountScope() noexcept { tCounting = true; }
    ~CountScope() { tCounting = false; }
    CountScope(const CountScope&) = delete;
    CountScope& operator=(const CountScope&) = delete;
};
} // namespace

void* operator new(std::size_t n) { return countedAlloc(n); }
void* operator new[](std::size_t n) { return countedAlloc(n); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
    try {
        return countedAlloc(n);
    } catch (...) {
        return nullptr;
    }
}
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
    try {
        return countedAlloc(n);
    } catch (...) {
        return nullptr;
    }
}
void operator delete(void* p) noexcept { countedFree(p); }
void operator delete[](void* p) noexcept { countedFree(p); }
void operator delete(void* p, std::size_t) noexcept { countedFree(p); }
void operator delete[](void* p, std::size_t) noexcept { countedFree(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { countedFree(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { countedFree(p); }
void* operator new(std::size_t n, std::align_val_t a) { return countedAlignedAlloc(n, static_cast<std::size_t>(a)); }
void* operator new[](std::size_t n, std::align_val_t a) { return countedAlignedAlloc(n, static_cast<std::size_t>(a)); }
void operator delete(void* p, std::align_val_t) noexcept { countedAlignedFree(p); }
void operator delete[](void* p, std::align_val_t) noexcept { countedAlignedFree(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { countedAlignedFree(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { countedAlignedFree(p); }

using namespace l15test;
using namespace dawlab::engine::test;

namespace {

bool blockHasNan(const AudioBlock& b) noexcept {
    for (int ch = 0; ch < b.getNumChannels(); ++ch) {
        const float* x = b.getChannelPointer(ch);
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (!std::isfinite(x[i]))
                return true;
    }
    return false;
}

// ---- part 1: every adapter on its own -------------------------------------------------------
void testEachEffect() {
    AudioBuffer input = makePluckSequence(kSampleRate, 1.0, 0.5f);
    for (int t = 0; t < kNumEffectTypes; ++t) {
        auto fx = createEffect(static_cast<EffectType>(t));
        fx->prepare(kSampleRate, kBlockSize, 2);
        AudioBuffer x = input; // copy outside the counting scope
        const long before = gAllocs.load() + gFrees.load();
        {
            CountScope scope;
            for (int pos = 0; pos < x.getNumSamples(); pos += kBlockSize) {
                // parameter changes are part of the audio-thread work, so count them too
                fx->setParameterNormalized(0, (pos / kBlockSize) % 2 ? 0.3f : 0.7f);
                fx->process(x.toBlock().getSubBlock(pos, std::min(kBlockSize, x.getNumSamples() - pos)));
            }
        }
        const long allocs = gAllocs.load() + gFrees.load() - before;
        if (allocs != 0)
            std::printf("  [FAIL] %s allocated/freed %ld times in process()\n", fx->getName(), allocs);
        DAWLAB_CHECK(allocs == 0);
    }
}

// ---- part 2: the whole engine under fire ----------------------------------------------------
void buildProject(Engine& e) {
    auto drums = std::make_shared<const AudioBuffer>(makeDrumLoop(kSampleRate, 2, 120.0, 0.7f));
    auto pluck = std::make_shared<const AudioBuffer>(makePluckSequence(kSampleRate, 4.0, 0.4f));
    for (int t = 0; t < kTestTracks; ++t) {
        std::vector<Clip> clips;
        for (int k = 0; k < 4; ++k)
            clips.push_back(Clip::fromBuffer(t % 2 ? pluck : drums, k * 192000 + t * 1111));
        e.setTrackClips(testId(t), std::move(clips));
        // two inserts per track, cycling through all effect types
        e.addInsert(testTrack(t), -1, createEffect(static_cast<EffectType>((2 * t) % kNumUserEffectTypes)));
        e.addInsert(testTrack(t), -1,
                    createEffect(static_cast<EffectType>((2 * t + 1) % kNumUserEffectTypes)));
        e.getMixer().getTrack(t).setSendLevelDb(t % kNumBuses, -12.0f);
    }
    e.addInsert(ChannelRef::bus(0), -1, createEffect(EffectType::Reverb));
    e.addInsert(ChannelRef::bus(1), -1, createEffect(EffectType::Delay));
    e.addInsert(ChannelRef::master(), -1, createEffect(EffectType::ConvolutionReverb));
    e.addInsert(ChannelRef::master(), -1, createEffect(EffectType::Limiter));
}

void testEngineUnderFire() {
    Engine e;
    addTestTracks(e);
    e.prepare(kSampleRate, 512);
    buildProject(e);
    e.getTransport().setLoopRange(0, 4 * 96000);
    e.getTransport().setLoopEnabled(true);
    e.getTransport().play();

    constexpr int64_t kTotalSamples = 12 * 48000; // 12 s of audio
    // A Release build renders 12 s in a fraction of a second, before the UI thread has done much.
    // So the audio thread keeps going until the UI thread has made at least kMinUiOps calls
    // (with a cap, in case the UI thread is stalled).
    constexpr long kMinUiOps = 200;
    constexpr int64_t kMaxSamples = 240 * 48000;
    std::atomic<long> uiOpsSeen{0};
    std::atomic<bool> done{false};
    std::atomic<int> nanBlocks{0};
    std::atomic<long> audioAllocs{0};
    std::atomic<int64_t> rendered{0};
    double maxBlockMs = 0.0;

    std::thread audio([&] {
        AudioBuffer buffer(2, 2048); // allocated before counting starts
        std::mt19937 rng(99);
        std::uniform_int_distribution<int> sizeDist(1, 2048);
        int64_t total = 0;
        while (total < kTotalSamples || (uiOpsSeen.load() < kMinUiOps && total < kMaxSamples)) {
            const int n = sizeDist(rng);
            AudioBlock block = buffer.toBlock().getSubBlock(0, n);
            const long before = gAllocs.load() + gFrees.load();
            const auto t0 = std::chrono::steady_clock::now();
            {
                CountScope scope;
                e.processBlock(block);
            }
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            maxBlockMs = std::max(maxBlockMs, ms);
            audioAllocs.fetch_add(gAllocs.load() + gFrees.load() - before);
            if (blockHasNan(block))
                nanBlocks.fetch_add(1);
            total += n;
            rendered.store(total);
        }
        done.store(true);
    });

    // "UI thread": random API calls until the audio thread is done.
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    auto pick = [&](int n) { return static_cast<int>(u(rng) * static_cast<float>(n)) % n; };
    auto randomChannel = [&]() {
        const int k = pick(kTestTracks + kNumBuses + 1);
        if (k < kTestTracks)
            return testTrack(k);
        if (k < kTestTracks + kNumBuses)
            return ChannelRef::bus(k - kTestTracks);
        return ChannelRef::master();
    };
    auto pluck = std::make_shared<const AudioBuffer>(makePluckSequence(kSampleRate, 1.0, 0.3f));
    long uiOps = 0;
    while (!done.load()) {
        Mixer& m = e.getMixer();
        switch (pick(16)) {
        case 0: m.getTrack(pick(kTestTracks)).setGainDb(-40.0f + 52.0f * u(rng)); break;
        case 1: m.getTrack(pick(kTestTracks)).setPan(2.0f * u(rng) - 1.0f); break;
        case 2: m.getTrack(pick(kTestTracks)).setMute(u(rng) < 0.2f); break;
        case 3: m.getTrack(pick(kTestTracks)).setSolo(u(rng) < 0.1f); break;
        case 4: m.getTrack(pick(kTestTracks)).setSendLevelDb(pick(kNumBuses), -60.0f + 60.0f * u(rng)); break;
        case 5: m.getTrack(pick(kTestTracks)).setSendPreFader(pick(kNumBuses), u(rng) < 0.5f); break;
        case 6: { // effect parameter through the observer pointer
            const ChannelRef ch = randomChannel();
            if (InsertEffect* fx = e.getInsert(ch, pick(kMaxInserts)))
                fx->setParameterNormalized(pick(std::max(1, fx->getNumParameters())), u(rng));
            break;
        }
        case 7:
            if (InsertEffect* fx = e.getInsert(randomChannel(), pick(kMaxInserts)))
                fx->setBypassed(u(rng) < 0.3f);
            break;
        case 8: { // keep chains at <= 4 inserts so the Debug build stays reasonably fast
            const ChannelRef ch = randomChannel();
            if (e.getNumInserts(ch) < 4)
                e.addInsert(ch, -1, createEffect(static_cast<EffectType>(pick(kNumUserEffectTypes))));
            break;
        }
        case 9: {
            const ChannelRef ch = randomChannel();
            e.removeInsert(ch, pick(std::max(1, e.getNumInserts(ch))));
            break;
        }
        case 10: e.moveInsert(randomChannel(), pick(kMaxInserts), pick(kMaxInserts)); break;
        case 11:
            e.replaceInsert(randomChannel(), pick(kMaxInserts),
                            createEffect(static_cast<EffectType>(pick(kNumUserEffectTypes))));
            break;
        case 12: {
            std::vector<Clip> clips;
            for (int k = 0; k < 1 + pick(6); ++k)
                clips.push_back(Clip::fromBuffer(pluck, pick(400000)));
            e.setTrackClips(testId(pick(kTestTracks)), std::move(clips));
            break;
        }
        case 13: e.getTransport().setPosition(pick(400000)); break;
        case 14:
            e.getTransport().setLoopRange(pick(200000), 200000 + pick(200000));
            e.getTransport().setLoopEnabled(u(rng) < 0.7f);
            e.getTransport().setTempo(60.0 + 120.0 * u(rng));
            e.getTransport().setPlaying(u(rng) < 0.9f);
            break;
        case 15: m.getMaster().setGainDb(-12.0f + 12.0f * u(rng)); break;
        default: break;
        }
        e.collectGarbage();
        ++uiOps;
        uiOpsSeen.store(uiOps);
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    audio.join();
    e.collectGarbage();

    std::printf("  audio: %lld samples in %llu chunks, max block time %.2f ms\n",
                static_cast<long long>(rendered.load()), static_cast<unsigned long long>(e.getBlocksProcessed()),
                maxBlockMs);
    std::printf("  ui: %ld operations, %llu commands applied, %llu dropped, %llu garbage leaked\n", uiOps,
                static_cast<unsigned long long>(e.getCommandsApplied()),
                static_cast<unsigned long long>(e.getCommandsDropped()),
                static_cast<unsigned long long>(e.getGarbageLeaked()));
    std::printf("  allocations + frees on the audio thread: %ld, blocks with NaN/inf: %d\n", audioAllocs.load(),
                nanBlocks.load());
    DAWLAB_CHECK(audioAllocs.load() == 0);
    DAWLAB_CHECK(nanBlocks.load() == 0);
    DAWLAB_CHECK(e.getGarbageLeaked() == 0);
    DAWLAB_CHECK(e.getCommandsApplied() > 0);
    DAWLAB_CHECK(uiOps > 100);
}

// ---- part 3: tracks that come and go ----------------------------------------------------------
void testTracksComeAndGo() {
    Engine e;
    e.prepare(kSampleRate, 512);
    e.addInsert(ChannelRef::bus(0), -1, createEffect(EffectType::Reverb));
    e.getTransport().setLoopRange(0, 2 * 96000);
    e.getTransport().setLoopEnabled(true);
    e.getTransport().play();

    constexpr long kUiOps = 3000;
    std::atomic<bool> done{false};
    std::atomic<long> audioAllocs{0};
    std::atomic<int> nanBlocks{0};
    std::thread audio([&] {
        AudioBuffer buffer(2, 1024);
        std::mt19937 rng(5);
        std::uniform_int_distribution<int> sizeDist(16, 1024);
        while (!done.load()) {
            AudioBlock block = buffer.toBlock().getSubBlock(0, sizeDist(rng));
            const long before = gAllocs.load() + gFrees.load();
            {
                CountScope scope;
                e.processBlock(block);
            }
            audioAllocs.fetch_add(gAllocs.load() + gFrees.load() - before);
            if (blockHasNan(block))
                nanBlocks.fetch_add(1);
        }
    });

    std::mt19937 rng(11);
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    auto pick = [&](int n) { return static_cast<int>(u(rng) * static_cast<float>(n)) % n; };
    auto pluck = std::make_shared<const AudioBuffer>(makePluckSequence(kSampleRate, 1.0, 0.3f));
    std::vector<TrackId> live;
    std::vector<ChannelRef> removed; // references of deleted tracks, for late commands
    std::uint32_t nextId = 1;
    int adds = 0, removes = 0, late = 0;
    for (long op = 0; op < kUiOps; ++op) {
        const int kind = pick(10);
        if (kind < 3 && live.size() < 24) { // a Debug build stays reasonably fast with <= 24 tracks
            const TrackId id{nextId++};
            if (e.addTrack(id, u(rng) < 0.5f ? TrackFormat::Mono : TrackFormat::Stereo).index >= 0) {
                live.push_back(id);
                ++adds;
            }
        } else if (kind < 5 && !live.empty()) {
            const size_t i = static_cast<size_t>(pick(static_cast<int>(live.size())));
            const ChannelRef ref = e.trackRef(live[i]);
            if (e.removeTrack(live[i])) {
                removed.push_back(ref);
                live.erase(live.begin() + static_cast<std::ptrdiff_t>(i));
                ++removes;
            }
        } else if (kind < 6 && !live.empty()) {
            std::vector<Clip> clips;
            for (int k = 0; k < 1 + pick(3); ++k)
                clips.push_back(Clip::fromBuffer(pluck, pick(150000)));
            e.setTrackClips(live[static_cast<size_t>(pick(static_cast<int>(live.size())))], std::move(clips));
        } else if (kind < 7 && !live.empty()) {
            const ChannelRef ref = e.trackRef(live[static_cast<size_t>(pick(static_cast<int>(live.size())))]);
            if (e.getNumInserts(ref) < 3)
                e.addInsert(ref, -1, createEffect(static_cast<EffectType>(pick(kNumUserEffectTypes))));
        } else if (kind < 8 && !removed.empty()) { // late commands for a deleted track
            const ChannelRef stale = removed[static_cast<size_t>(pick(static_cast<int>(removed.size())))];
            e.pushCommand(Command::addInsert(stale, -1, createEffect(EffectType::Gain).release()));
            e.pushCommand(Command::replaceClips(stale, new std::vector<Clip>{Clip::fromBuffer(pluck, 0)}));
            e.pushCommand(Command::removeTrack(stale));
            ++late;
        } else if (!live.empty()) {
            Track* t = e.getTrack(live[static_cast<size_t>(pick(static_cast<int>(live.size())))]);
            t->setGainDb(-30.0f + 30.0f * u(rng));
            t->setPan(2.0f * u(rng) - 1.0f);
            t->setSolo(u(rng) < 0.05f);
            t->setSendLevelDb(0, -20.0f);
        }
        e.collectGarbage();
        if (op % 8 == 0)
            std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    done.store(true);
    audio.join();
    // Drain on this thread: apply what is still queued, free what came back.
    AudioBuffer block(2, 64);
    for (int i = 0; i < 50 && (e.getPendingCommands() > 0 || e.getPendingGarbage() > 0); ++i) {
        e.collectGarbage();
        e.processBlock(block);
    }
    e.collectGarbage();

    int mismatches = 0;
    for (TrackId id : live) {
        const int slot = e.slotOf(id);
        if (slot < 0 || e.getMixer().trackInSlot(slot) != id)
            ++mismatches;
    }
    std::printf("  tracks come and go: %d adds, %d removes, %d rounds of late commands; %zu tracks left, the audio thread\n"
                "  has %d, %d slot mismatches; allocations + frees on the audio thread: %ld, NaN blocks %d, garbage leaked %llu\n",
                adds, removes, late, live.size(), e.getMixer().getNumActiveTracks(), mismatches, audioAllocs.load(),
                nanBlocks.load(), static_cast<unsigned long long>(e.getGarbageLeaked()));
    DAWLAB_CHECK(adds > 300 && removes > 200 && late > 100);
    DAWLAB_CHECK(e.getMixer().getNumActiveTracks() == static_cast<int>(live.size()));
    DAWLAB_CHECK(mismatches == 0);
    DAWLAB_CHECK(audioAllocs.load() == 0);
    DAWLAB_CHECK(nanBlocks.load() == 0);
    DAWLAB_CHECK(e.getGarbageLeaked() == 0);
    DAWLAB_CHECK(e.getPendingCommands() == 0);
}

} // namespace

int main() {
    testEachEffect();
    testEngineUnderFire();
    testTracksComeAndGo();
    return dawlab::test::summary("l15_test_realtime_safety");
}
