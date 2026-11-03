// test_commands: SpscQueue, the command queue and the garbage (recycling) queue (Step 7).

#include "TestHelpers.h"

#include <atomic>
#include <cstring>
#include <thread>

using namespace l14test;

namespace {

// An effect that counts how many instances are alive, to prove nothing leaks or is freed twice.
std::atomic<int> gAlive{0};

class CountingEffect final : public EffectBase {
public:
    enum Param { Gain, kCount };
    explicit CountingEffect(float gain = 1.0f) : EffectBase(kInfos, kCount) {
        ++gAlive;
        setParameter(Gain, gain);
    }
    ~CountingEffect() override { --gAlive; }
    const char* getName() const noexcept override { return "Counting"; }
    void prepare(double sr, int, int) override { finishPrepare(sr); }
    void reset() noexcept override {}

protected:
    void applyParameter(int index, float v) noexcept override {
        if (index == Gain)
            gain_ = v;
    }
    void processDsp(AudioBlock block) noexcept override { block.multiplyBy(gain_); }

private:
    static constexpr ParameterInfo kInfos[kCount] = {
        {"Gain", "x", 0.0f, 4.0f, 1.0f, ParameterKind::Float, false, nullptr, 0},
    };
    float gain_ = 1.0f;
};

void testSpscBasics() {
    SpscQueue<int> q(4);
    DAWLAB_CHECK(q.capacity() == 4);
    DAWLAB_CHECK(q.empty());
    for (int i = 0; i < 4; ++i)
        DAWLAB_CHECK(q.push(i));
    DAWLAB_CHECK(!q.push(99)); // full
    DAWLAB_CHECK(q.size() == 4);
    int v = -1;
    for (int i = 0; i < 4; ++i) {
        DAWLAB_CHECK(q.pop(v));
        DAWLAB_CHECK(v == i); // FIFO
    }
    DAWLAB_CHECK(!q.pop(v));
    // wrap-around many times
    for (int i = 0; i < 1000; ++i) {
        DAWLAB_CHECK(q.push(i));
        DAWLAB_CHECK(q.pop(v) && v == i);
    }
}

void testSpscThreads() {
    // One producer thread, one consumer thread, 2 million items: every item arrives, in order.
    SpscQueue<uint32_t> q(256);
    constexpr uint32_t kItems = 2000000;
    std::thread producer([&] {
        for (uint32_t i = 0; i < kItems;)
            if (q.push(i))
                ++i;
    });
    uint32_t expected = 0;
    bool inOrder = true;
    while (expected < kItems) {
        uint32_t v = 0;
        if (q.pop(v)) {
            inOrder = inOrder && (v == expected);
            ++expected;
        }
    }
    producer.join();
    DAWLAB_CHECK(inOrder);
    DAWLAB_CHECK(q.empty());
}

ChannelRef t0() { return ChannelRef::track(0); }

void testInsertCommands() {
    {
        Engine e;
        e.prepare(kSampleRate, kBlockSize);
        e.setTrackClips(0, {Clip::fromBuffer(makeConstant(0.1f, 48000), 0)});
        InsertEffect* a = e.addInsert(t0(), -1, std::make_unique<CountingEffect>(2.0f));
        InsertEffect* b = e.addInsert(t0(), -1, std::make_unique<CountingEffect>(3.0f));
        DAWLAB_CHECK(a != nullptr && b != nullptr);
        DAWLAB_CHECK(gAlive == 2);
        DAWLAB_CHECK(e.getNumInserts(t0()) == 0); // queued, not applied yet
        DAWLAB_CHECK(e.getPendingCommands() == 3);

        e.getTransport().play();
        AudioBuffer block(2, 256);
        e.processBlock(block); // the audio thread applies the commands at the block start
        DAWLAB_CHECK(e.getNumInserts(t0()) == 2);
        DAWLAB_CHECK(e.getInsert(t0(), 0) == a && e.getInsert(t0(), 1) == b);
        DAWLAB_CHECK_NEAR(block.getSample(0, 100), 0.1 * 6.0 * 0.70711, 1e-4);

        // parameters go through atomics, no command needed
        a->setParameter(CountingEffect::Gain, 1.0f);
        e.processBlock(block);
        DAWLAB_CHECK_NEAR(block.getSample(0, 100), 0.1 * 3.0 * 0.70711, 1e-4);

        // move b to the front, then remove slot 1 (= a)
        DAWLAB_CHECK(e.moveInsert(t0(), 1, 0));
        DAWLAB_CHECK(e.removeInsert(t0(), 1));
        e.processBlock(block);
        DAWLAB_CHECK(e.getNumInserts(t0()) == 1);
        DAWLAB_CHECK(e.getInsert(t0(), 0) == b);
        DAWLAB_CHECK(gAlive == 2);              // a is in the garbage queue, not freed yet
        DAWLAB_CHECK(e.getPendingGarbage() == 1);
        DAWLAB_CHECK(e.collectGarbage() == 1);  // the UI thread frees it
        DAWLAB_CHECK(gAlive == 1);

        // replace: new effect in, old one to the garbage
        InsertEffect* c = e.replaceInsert(t0(), 0, std::make_unique<CountingEffect>(0.5f));
        e.processBlock(block);
        DAWLAB_CHECK(e.getInsert(t0(), 0) == c);
        DAWLAB_CHECK_NEAR(block.getSample(0, 100), 0.1 * 0.5 * 0.70711, 1e-4);
        DAWLAB_CHECK(e.collectGarbage() == 1);
        DAWLAB_CHECK(gAlive == 1);

        // bypass is an atomic flag
        c->setBypassed(true);
        e.processBlock(block);
        DAWLAB_CHECK_NEAR(block.getSample(0, 100), 0.1 * 0.70711, 1e-4);

        // the chain holds at most kMaxInserts: the extra one comes back as garbage
        for (int i = 0; i < kMaxInserts; ++i)
            e.addInsert(t0(), -1, std::make_unique<CountingEffect>());
        e.processBlock(block);
        DAWLAB_CHECK(e.getNumInserts(t0()) == kMaxInserts);
        DAWLAB_CHECK(e.collectGarbage() == 1);
        DAWLAB_CHECK(gAlive == kMaxInserts);

        // clip list replacement: the old list becomes garbage
        e.setTrackClips(0, {Clip::fromBuffer(makeConstant(0.2f, 1000), 0)});
        e.processBlock(block);
        DAWLAB_CHECK(e.getMixer().getTrack(0).getNumClips() == 1);
        DAWLAB_CHECK(e.collectGarbage() == 1);
        DAWLAB_CHECK(e.getGarbageLeaked() == 0);
        DAWLAB_CHECK(e.getCommandsDropped() == 0);
    }
    DAWLAB_CHECK(gAlive == 0); // the engine's destructor freed the chain
}

void testQueueFull() {
    {
        Engine e(4); // a tiny queue
        e.prepare(kSampleRate, kBlockSize);
        int accepted = 0;
        for (int i = 0; i < 10; ++i)
            if (e.addInsert(t0(), -1, std::make_unique<CountingEffect>()) != nullptr)
                ++accepted;
        DAWLAB_CHECK(accepted == 4);
        DAWLAB_CHECK(e.getCommandsDropped() == 6);
        DAWLAB_CHECK(gAlive == 4); // the 6 dropped effects were deleted right away
        AudioBuffer block(2, 64);
        e.processBlock(block);
        DAWLAB_CHECK(e.getNumInserts(t0()) == 4);
    }
    DAWLAB_CHECK(gAlive == 0);
    {
        Engine e(8);
        e.prepare(kSampleRate, kBlockSize);
        e.addInsert(t0(), -1, std::make_unique<CountingEffect>());
        // destroyed with a command still queued: the payload is freed, no leak
    }
    DAWLAB_CHECK(gAlive == 0);
}

void testInvalidTargets() {
    Engine e;
    e.prepare(kSampleRate, kBlockSize);
    DAWLAB_CHECK(e.addInsert(ChannelRef::track(99), -1, std::make_unique<CountingEffect>()) == nullptr);
    DAWLAB_CHECK(!e.setTrackClips(-1, {}));
    DAWLAB_CHECK(e.removeInsert(ChannelRef::bus(0), 5)); // queued, harmless no-op
    AudioBuffer block(2, 64);
    e.processBlock(block);
    DAWLAB_CHECK(e.collectGarbage() == 0);
    DAWLAB_CHECK(gAlive == 0);
    // bus and master accept inserts too
    DAWLAB_CHECK(e.addInsert(ChannelRef::bus(1), -1, std::make_unique<CountingEffect>()) != nullptr);
    DAWLAB_CHECK(e.addInsert(ChannelRef::master(), -1, std::make_unique<CountingEffect>()) != nullptr);
    e.processBlock(block);
    DAWLAB_CHECK(e.getNumInserts(ChannelRef::bus(1)) == 1);
    DAWLAB_CHECK(e.getNumInserts(ChannelRef::master()) == 1);
}

} // namespace

int main() {
    testSpscBasics();
    testSpscThreads();
    testInsertCommands();
    testQueueFull();
    testInvalidTargets();
    DAWLAB_CHECK(gAlive == 0);
    return dawlab::test::summary("l14_test_commands");
}
