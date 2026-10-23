#pragma once
// check.h - the self-check of Step 7 (command queue + garbage queue). Defines main(); included
// at the END of starter/step07.cpp and solution/step07.cpp.
//
// Tests only what Step 7 adds: the SPSC ring buffer (alone and across two threads), commands
// applied at the next block, ownership travelling to the audio thread and back as garbage,
// full queues, and an engine edited from a second thread while it plays.

#include <dawlab/Common.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <random>
#include <thread>
#include <vector>

namespace step07_check {

using namespace dawlab;
using namespace minidaw;

constexpr double kSampleRate = 48000.0;
constexpr float kCentre = 0.70710678f;

// A Gain effect that counts its living instances: proves "no leak, no double free".
inline std::atomic<int> gAlive{0};
class CountedGain final : public EffectBase {
public:
    explicit CountedGain(float db = 0.0f) : EffectBase(kInfos, 1) {
        ++gAlive;
        setParameter(0, db);
    }
    ~CountedGain() override { --gAlive; }
    const char* getName() const noexcept override { return "CountedGain"; }
    void prepare(double, int, int) override { finishPrepare(); }
    void reset() noexcept override {}

protected:
    void applyParameter(int, float v) noexcept override { gain_ = dbToGain(v); }
    void processDsp(AudioBlock block) noexcept override { block.multiplyBy(gain_); }

private:
    static constexpr ParameterInfo kInfos[1] = {{"Gain", "dB", -60.0f, 24.0f, 0.0f, ParameterKind::Float, false}};
    float gain_ = 1.0f;
};

inline Clip dcClip(float value, int n = 96000) {
    auto b = std::make_shared<AudioBuffer>(1, n);
    for (int i = 0; i < n; ++i)
        b->setSample(0, i, value);
    return Clip::fromBuffer(b, 0);
}

inline bool testQueueAlone() {
    SpscQueue<int> q(4);
    bool ok = q.capacity() == 4;
    for (int i = 0; i < 4; ++i)
        ok = q.push(i) && ok;
    const bool fifthRefused = !q.push(99);
    int v = -1, order = 0;
    bool fifo = true;
    while (q.pop(v))
        fifo = fifo && (v == order++);
    bool wrap = true; // 1000 times around the ring
    for (int i = 0; i < 1000; ++i) {
        int out = -1;
        wrap = wrap && q.push(i) && q.push(i + 1) && q.pop(out) && out == i && q.pop(out) && out == i + 1;
    }
    std::printf("  SpscQueue<int>(4): 4 accepted %s, 5th refused %s, FIFO %s, 1000 wrap-arounds %s\n", ok ? "yes" : "NO",
                fifthRefused ? "yes" : "NO", (fifo && order == 4) ? "yes" : "NO", wrap ? "yes" : "NO");
    DAWLAB_CHECK(ok);
    DAWLAB_CHECK(fifthRefused);
    DAWLAB_CHECK(fifo && order == 4);
    DAWLAB_CHECK(wrap && q.size() == 0);
    return ok && fifthRefused && fifo && order == 4 && wrap;
}

inline void testQueueTwoThreads() {
    constexpr int kItems = 200000;
    SpscQueue<int> q(256);
    std::thread producer([&] {
        for (int i = 0; i < kItems; ++i)
            while (!q.push(i))
                std::this_thread::yield();
    });
    int expected = 0, outOfOrder = 0;
    while (expected < kItems) {
        int v = 0;
        if (q.pop(v)) {
            if (v != expected)
                ++outOfOrder;
            ++expected;
        } else {
            std::this_thread::yield();
        }
    }
    producer.join();
    std::printf("  two threads, %d items through a 256-slot queue: %d out of order (expected 0)\n", kItems, outOfOrder);
    DAWLAB_CHECK(outOfOrder == 0);
}

inline int run() {
    std::printf("Step 7: command queue and garbage queue (build-along)\n");
    if (testQueueAlone())
        testQueueTwoThreads();
    else
        std::printf("  (two-thread test skipped until push() / pop() work)\n");

    AudioBuffer out(kNumChannels, 512);
    // ---- commands are applied at the start of the next block -------------------------------------
    {
        Engine engine;
        engine.prepare(kSampleRate, 512);
        engine.setTrackClips(0, {dcClip(0.1f)});
        InsertEffect* x2 = engine.addInsert(ChannelRef::track(0), -1, std::make_unique<CountedGain>(6.0206f));
        InsertEffect* x3 = engine.addInsert(ChannelRef::track(0), -1, std::make_unique<CountedGain>(9.5424f));
        DAWLAB_CHECK(x2 != nullptr && x3 != nullptr);
        DAWLAB_CHECK(engine.getMixer().getTrack(0).getInserts().getNumInserts() == 0); // not yet
        engine.getTransport().play();
        engine.processBlock(out.toBlock());
        std::printf("  two inserts (x2, x3) queued on DC 0.1: %d inserts after one block, output %.4f (expected 2, 0.4243)\n",
                    engine.getMixer().getTrack(0).getInserts().getNumInserts(), out.getSample(0, 100));
        DAWLAB_CHECK(engine.getMixer().getTrack(0).getInserts().getNumInserts() == 2);
        DAWLAB_CHECK_NEAR(out.getSample(0, 100), 0.1 * 6.0 * kCentre, 1e-4);
        DAWLAB_CHECK(engine.getCommandsApplied() == 3);

        // remove: the effect stays alive until the UI thread collects the garbage
        const int aliveBefore = gAlive.load();
        engine.removeInsert(ChannelRef::track(0), 0);
        engine.processBlock(out.toBlock());
        DAWLAB_CHECK(engine.getMixer().getTrack(0).getInserts().getNumInserts() == 1);
        DAWLAB_CHECK(gAlive.load() == aliveBefore); // handed back, not freed on the audio thread
        const int freed = engine.collectGarbage();
        std::printf("  removed insert: alive until collectGarbage() frees %d object (expected 1)\n", freed);
        DAWLAB_CHECK(freed == 1 && gAlive.load() == aliveBefore - 1);
        DAWLAB_CHECK_NEAR(out.getSample(0, 100), 0.1 * 3.0 * kCentre, 1e-4);

        // a replaced clip list comes back as garbage
        engine.setTrackClips(0, {dcClip(0.2f)});
        engine.processBlock(out.toBlock());
        DAWLAB_CHECK(engine.collectGarbage() == 1);
        DAWLAB_CHECK_NEAR(out.getSample(0, 100), 0.2 * 3.0 * kCentre, 1e-4);

        // the 9th insert of a full chain is sent straight back
        for (int i = 0; i < 9; ++i) // one more than kMaxInserts
            engine.addInsert(ChannelRef::master(), -1, std::make_unique<CountedGain>());
        engine.processBlock(out.toBlock());
        DAWLAB_CHECK(engine.getMixer().getMaster().getInserts().getNumInserts() == 8);
        DAWLAB_CHECK(engine.collectGarbage() == 1);
        DAWLAB_CHECK(engine.addInsert(ChannelRef::bus(7), -1, std::make_unique<CountedGain>()) == nullptr); // no such strip
    }
    std::printf("  living effects after the engine is gone: %d (expected 0)\n", gAlive.load());
    DAWLAB_CHECK(gAlive.load() == 0);

    // ---- a full command queue drops and deletes at once; pending payloads die with the engine -----
    {
        Engine small(4);
        small.prepare(kSampleRate, 512);
        int accepted = 0;
        for (int i = 0; i < 10; ++i)
            accepted += small.addInsert(ChannelRef::track(1), -1, std::make_unique<CountedGain>()) != nullptr ? 1 : 0;
        std::printf("  queue of 4: %d accepted, %llu dropped, %d alive (expected 4, 6, 4)\n", accepted,
                    static_cast<unsigned long long>(small.getCommandsDropped()), gAlive.load());
        DAWLAB_CHECK(accepted == 4 && small.getCommandsDropped() == 6 && gAlive.load() == 4);
    } // destroyed with 4 commands pending
    DAWLAB_CHECK(gAlive.load() == 0);

    // ---- edited from a UI thread while an audio thread plays ------------------------------------------
    {
        Engine engine;
        engine.prepare(kSampleRate, 256);
        engine.getTransport().play();
        std::atomic<bool> stop{false};
        std::atomic<int> blocks{0};
        std::thread audio([&] {
            AudioBuffer buf(kNumChannels, 256);
            while (!stop.load()) {
                engine.processBlock(buf.toBlock());
                ++blocks;
            }
        });
        std::mt19937 rng(3);
        int pushed = 0;
        for (int i = 0; i < 600; ++i) {
            const int track = static_cast<int>(rng() % kNumTracks);
            switch (rng() % 3) {
            case 0: pushed += engine.setTrackClips(track, {dcClip(0.01f * static_cast<float>(i % 7), 2000)}) ? 1 : 0; break;
            case 1: pushed += engine.addInsert(ChannelRef::track(track), -1, std::make_unique<CountedGain>()) ? 1 : 0; break;
            default: pushed += engine.removeInsert(ChannelRef::track(track), 0) ? 1 : 0; break;
            }
            engine.collectGarbage();
            if (i % 50 == 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        while (engine.getCommandsApplied() < static_cast<uint64_t>(pushed) && blocks.load() < 1000000)
            std::this_thread::yield();
        stop.store(true);
        audio.join();
        engine.collectGarbage();
        std::printf("  UI thread vs audio thread: %d commands pushed, %llu applied, %llu dropped, %llu leaked\n", pushed,
                    static_cast<unsigned long long>(engine.getCommandsApplied()),
                    static_cast<unsigned long long>(engine.getCommandsDropped()),
                    static_cast<unsigned long long>(engine.getGarbageLeaked()));
        DAWLAB_CHECK(pushed == 600 && engine.getCommandsApplied() == 600);
        DAWLAB_CHECK(engine.getGarbageLeaked() == 0);
    }
    DAWLAB_CHECK(gAlive.load() == 0);

    return dawlab::test::summary("l14_step07");
}

} // namespace step07_check

int main() { return step07_check::run(); }
