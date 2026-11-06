#pragma once
// check.h - the self-check of Step 1 (from the audio thread to the disk thread). Defines main();
// included at the END of starter/step01.cpp and solution/step01.cpp, so both run the same checks.
//
// Hand-made cases on one thread first (wrapping, a full ring, gaps, channels), then random blocks
// against a plain reference, then a real audio thread next to a real disk thread. Build it with
// -fsanitize=thread (scripts/build.sh --sanitize thread) and ThreadSanitizer checks that the two
// threads share nothing without synchronisation.
//
// Every test signal numbers its frames: frame i of the device has the value i + 1 on channel 0 and
// -(i + 1) on channel 1. So a frame that comes out in the wrong place is seen at once, and a dropped
// frame comes out as 0.

#include <dawlab/Common.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <thread>
#include <vector>

// Every allocation in this program is counted: push() and pop() must not make any.
namespace step01_check {
inline std::atomic<long long> allocations{0};
inline void* countedAlloc(std::size_t size) {
    allocations.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(size > 0 ? size : 1))
        return p;
    throw std::bad_alloc();
}
} // namespace step01_check
void* operator new(std::size_t size) { return step01_check::countedAlloc(size); }
void* operator new[](std::size_t size) { return step01_check::countedAlloc(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace step01_check {

using namespace minidaw;

using ull = unsigned long long;

constexpr std::uint64_t kNumberRange = 8000000; // every number up to here is exact in a float

inline float numbered(std::uint64_t frame) noexcept {
    return static_cast<float>(frame % kNumberRange + 1);
}

// The device: numbers its frames and goes on counting, whether a block fit or not. A real device
// does not wait either.
class FakeDevice {
public:
    explicit FakeDevice(int maxBlock)
        : left_(static_cast<std::size_t>(maxBlock)), right_(static_cast<std::size_t>(maxBlock)) {}

    bool deliver(InputRing& ring, int n) noexcept {
        for (int i = 0; i < n; ++i) {
            const float v = numbered(next_ + static_cast<std::uint64_t>(i));
            left_[static_cast<std::size_t>(i)] = v;
            right_[static_cast<std::size_t>(i)] = -v;
        }
        next_ += static_cast<std::uint64_t>(n);
        const float* in[2] = {left_.data(), right_.data()};
        return ring.push(in, 2, n);
    }
    std::uint64_t framesDelivered() const noexcept { return next_; }

private:
    std::vector<float> left_, right_;
    std::uint64_t next_ = 0;
};

// What came out, compared with what the device sent.
struct StreamReport {
    std::uint64_t frames = 0;
    std::uint64_t silent = 0;
    std::uint64_t wrong = 0; // neither its own number nor silence, or the channels disagree
};

inline StreamReport compare(const std::vector<float>& left, const std::vector<float>& right) {
    StreamReport report;
    report.frames = left.size();
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (left[i] == 0.0f && right[i] == 0.0f)
            ++report.silent;
        else if (left[i] != numbered(i) || right[i] != -numbered(i))
            ++report.wrong;
    }
    return report;
}
inline StreamReport compare(const Take& take) {
    return compare(take.channels()[0], take.channels()[1]);
}

// ---- hand-made cases ----------------------------------------------------------------------------

inline void checkEmpty() {
    InputRing ring;
    ring.prepare(2, 1000);
    DAWLAB_CHECK(ring.capacity() == 1024); // rounded up to a power of two
    DAWLAB_CHECK(ring.numChannels() == 2);
    DAWLAB_CHECK(InputRing::framesForStall(48000.0, 0.5) >= 24000);

    float a[4] = {}, b[4] = {};
    float* out[2] = {a, b};
    DAWLAB_CHECK(ring.pop(out, 4) == 0); // nothing in it
    const float* in[2] = {a, b};
    DAWLAB_CHECK(ring.push(in, 2, 0)); // an empty block always fits
    DAWLAB_CHECK(ring.fillLevel() == 0 && ring.droppedBlocks() == 0);
}

// Blocks that cross the end of the storage, one after the other. The rest of the checks need this to
// work: main() does not start them otherwise, because some of them wait for the ring to empty.
inline bool checkWrapping() {
    InputRing ring;
    ring.prepare(2, 16);
    FakeDevice device(16);
    Take take(2);
    bool allFit = true;
    for (int n : {10, 10, 10, 13, 1, 16, 7, 16, 16}) {
        allFit = device.deliver(ring, n) && allFit;
        take.collect(ring);
    }
    DAWLAB_CHECK(allFit);
    const StreamReport r = compare(take);
    DAWLAB_CHECK(r.frames == device.framesDelivered());
    DAWLAB_CHECK(r.wrong == 0 && r.silent == 0);
    return allFit && r.frames == device.framesDelivered() && r.wrong == 0 && r.silent == 0;
}

inline void checkFullRing() {
    InputRing ring;
    ring.prepare(2, 16);
    FakeDevice device(32);

    DAWLAB_CHECK(device.deliver(ring, 16)); // exactly the capacity fits
    DAWLAB_CHECK(ring.fillLevel() == 16);
    DAWLAB_CHECK(!device.deliver(ring, 1)); // one more does not, and push() returns instead of waiting
    DAWLAB_CHECK(ring.droppedBlocks() == 1 && ring.droppedFrames() == 1);

    Take take(2);
    take.collect(ring);
    DAWLAB_CHECK(!device.deliver(ring, 32)); // bigger than the whole ring: it never fits
    DAWLAB_CHECK(ring.droppedBlocks() == 2 && ring.droppedFrames() == 33);
    take.collect(ring);
    DAWLAB_CHECK(take.numFrames() == 16); // a gap at the very end moves nothing, so it is not written
}

// The point of the gaps: what comes after a dropped block stays at its place.
inline void checkGapKeepsTheTime() {
    InputRing ring;
    ring.prepare(2, 16);
    FakeDevice device(16);
    Take take(2);

    DAWLAB_CHECK(device.deliver(ring, 12));
    DAWLAB_CHECK(!device.deliver(ring, 8));
    DAWLAB_CHECK(!device.deliver(ring, 8)); // two dropped blocks in a row are one gap of 16
    take.collect(ring);
    DAWLAB_CHECK(device.deliver(ring, 8));
    take.collect(ring);

    const StreamReport r = compare(take);
    DAWLAB_CHECK(r.frames == 36); // as long as what the device delivered
    DAWLAB_CHECK(r.silent == 16); // exactly the dropped frames
    DAWLAB_CHECK(r.wrong == 0);   // every frame after the gap is at its own place
    const auto& left = take.channels()[0];
    DAWLAB_CHECK(left.size() == 36 && left[11] == numbered(11) && left[12] == 0.0f && left[27] == 0.0f &&
                 left[28] == numbered(28));
}

// pop() may stop in the middle of the frames, and in the middle of a gap.
inline void checkSmallPops() {
    InputRing ring;
    ring.prepare(2, 16);
    FakeDevice device(16);
    DAWLAB_CHECK(device.deliver(ring, 16));
    DAWLAB_CHECK(!device.deliver(ring, 8));

    std::vector<float> got;
    float a[3], b[3];
    float* out[2] = {a, b};
    for (int i = 0; i < 2; ++i) {
        const int n = ring.pop(out, 3);
        got.insert(got.end(), a, a + n);
    }
    DAWLAB_CHECK(device.deliver(ring, 4));
    while (const int n = ring.pop(out, 3))
        got.insert(got.end(), a, a + n);
    DAWLAB_CHECK(got.size() == 28);

    bool right = true;
    for (std::size_t i = 0; i < got.size(); ++i)
        right = right && got[i] == (i >= 16 && i < 24 ? 0.0f : numbered(i));
    DAWLAB_CHECK(right);
}

// More gaps than the gap queue holds: a block that fits is dropped too until the queue has room, and
// the time stays right.
inline void checkGapQueueFull() {
    InputRing ring;
    ring.prepare(2, 512);
    FakeDevice device(8);
    std::vector<float> a(8), b(8), left, right;
    float* out[2] = {a.data(), b.data()};
    auto popSome = [&] {
        const int n = ring.pop(out, 8);
        left.insert(left.end(), a.begin(), a.begin() + n);
        right.insert(right.end(), b.begin(), b.begin() + n);
    };

    for (int i = 0; i < 1000 && device.deliver(ring, 8); ++i) {
    }
    int droppedAlthoughItFit = 0;
    for (int i = 0; i < 60; ++i) {
        popSome();
        if (!device.deliver(ring, 8))
            ++droppedAlthoughItFit; // 8 frames were popped just now, so there was room for it
        device.deliver(ring, 8);    // and this one is dropped, which makes the next gap
    }
    DAWLAB_CHECK(droppedAlthoughItFit > 0);

    // Empty the ring until a block fits again, so that no gap stays at the end.
    bool fit = false;
    for (int i = 0; i < 100000 && !fit; ++i) {
        popSome();
        fit = ring.fillLevel() == 0 && device.deliver(ring, 8);
    }
    for (int i = 0; i < 100000 && ring.fillLevel() > 0; ++i)
        popSome();

    const StreamReport r = compare(left, right);
    DAWLAB_CHECK(r.wrong == 0);
    DAWLAB_CHECK(r.frames == device.framesDelivered());
    DAWLAB_CHECK(r.silent == ring.droppedFrames());
}

// The ring is as big as the block, so every block lands on what the last one left there: a channel
// that records silence has to write the zeros.
inline void checkChannels() {
    InputRing ring;
    ring.prepare(2, 4);
    const float mono[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    float a[4], b[4];
    float* out[2] = {a, b};

    const float* twoInputs[2] = {mono, mono};
    DAWLAB_CHECK(ring.push(twoInputs, 2, 4));
    DAWLAB_CHECK(ring.pop(out, 4) == 4);
    DAWLAB_CHECK(a[3] == 4.0f && b[3] == 4.0f);

    // Fewer inputs than ring channels: the channel without an input records silence.
    const float* oneInput[1] = {mono};
    DAWLAB_CHECK(ring.push(oneInput, 1, 4));
    DAWLAB_CHECK(ring.pop(out, 4) == 4);
    DAWLAB_CHECK(a[3] == 4.0f && b[0] == 0.0f && b[3] == 0.0f);

    // A null pointer (a device may send one for an inactive channel) records silence too.
    const float* nullFirst[2] = {nullptr, mono};
    DAWLAB_CHECK(ring.push(nullFirst, 2, 4));
    DAWLAB_CHECK(ring.pop(out, 4) == 4);
    DAWLAB_CHECK(a[0] == 0.0f && a[3] == 0.0f && b[2] == 3.0f);

    // Inputs beyond the ring's channels are ignored.
    const float* threeInputs[3] = {mono, mono, mono};
    DAWLAB_CHECK(ring.push(threeInputs, 3, 4));
    DAWLAB_CHECK(ring.pop(out, 4) == 4);
    DAWLAB_CHECK(a[1] == 2.0f && b[1] == 2.0f);
}

// collect() empties the ring, however much more than one chunk is waiting.
inline void checkLongCollect() {
    InputRing ring;
    ring.prepare(2, 20000);
    FakeDevice device(20000);
    Take take(2);
    DAWLAB_CHECK(device.deliver(ring, 20000));
    DAWLAB_CHECK(take.collect(ring) == 20000);
    DAWLAB_CHECK(ring.fillLevel() == 0);
    DAWLAB_CHECK(compare(take).wrong == 0);
}

inline void checkNoAllocation() {
    InputRing ring;
    ring.prepare(2, 256);
    FakeDevice device(64);
    float a[64], b[64];
    float* out[2] = {a, b};

    const long long before = allocations.load();
    for (int i = 0; i < 2000; ++i) {
        device.deliver(ring, 1 + i % 64);
        if (i % 7 == 0)
            while (ring.pop(out, 64) > 0) {
            }
    }
    const long long made = allocations.load() - before;
    std::printf("  2000 blocks, %llu of them dropped: %lld allocations (expected 0)\n",
                static_cast<ull>(ring.droppedBlocks()), made);
    DAWLAB_CHECK(made == 0);
    DAWLAB_CHECK(ring.droppedBlocks() > 0); // the test really filled the ring
}

// ---- random blocks against a reference ----------------------------------------------------------

struct Lcg {
    std::uint32_t state = 217u;
    std::uint32_t next() noexcept {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    }
    int below(int n) noexcept { return n <= 0 ? 0 : static_cast<int>(next() % static_cast<std::uint32_t>(n)); }
};

// The reference is as plain as it gets: a vector with one value per frame the device delivered, zero
// where the block did not fit. Whatever follows the last block that fit never comes out.
inline void checkRandom() {
    Lcg random;
    std::uint64_t compared = 0, differ = 0, drops = 0;
    for (int session = 0; session < 200; ++session) {
        InputRing ring;
        const int capacity = 16 << random.below(6);
        ring.prepare(2, capacity);
        FakeDevice device(capacity * 2);
        Take take(2);
        std::vector<float> reference;
        std::size_t lastFit = 0;

        for (int step = 0; step < 400; ++step) {
            if (random.below(3) == 0) {
                take.collect(ring);
                continue;
            }
            const int n = 1 + random.below(capacity + capacity / 4);
            const std::uint64_t first = device.framesDelivered();
            const bool fits = device.deliver(ring, n);
            for (int i = 0; i < n; ++i)
                reference.push_back(fits ? numbered(first + static_cast<std::uint64_t>(i)) : 0.0f);
            if (fits)
                lastFit = reference.size();
        }
        take.collect(ring);
        drops += ring.droppedBlocks();

        differ += take.numFrames() == lastFit ? 0 : 1;
        const auto& left = take.channels()[0];
        const auto& right = take.channels()[1];
        const std::size_t n = std::min(left.size(), reference.size());
        for (std::size_t i = 0; i < n; ++i, ++compared)
            differ += left[i] == reference[i] && right[i] == -reference[i] ? 0 : 1;
    }
    std::printf("  200 random sessions, %llu frames, %llu dropped blocks: %llu differ from the reference (expected 0)\n",
                static_cast<ull>(compared), static_cast<ull>(drops), static_cast<ull>(differ));
    DAWLAB_CHECK(differ == 0);
    DAWLAB_CHECK(drops > 0);
}

// ---- two real threads ---------------------------------------------------------------------------

// The audio thread delivers `blocks` random blocks and waits until each one fits, which a real audio
// thread may never do; it shows the stream without any drop. With `drops`, the disk thread stalls now
// and then and one block in eight does not wait, so it is dropped if the ring is full at that moment.
// The last block always fits, so that no gap stays at the end. A ring that hangs is given up on.
inline void runThreads(InputRing& ring, FakeDevice& device, Take& take, int blocks, bool drops) {
    const auto giveUp = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    auto waitUntil = [&](auto condition) {
        while (!condition() && std::chrono::steady_clock::now() < giveUp)
            std::this_thread::yield();
    };
    // How often the two threads happen to meet at a full ring depends on the machine, so now and then
    // the disk stalls for certain: it waits until the audio thread has seen a block that does not
    // fit. Relaxed: the test's own flag must not synchronize anything for the code under test.
    std::atomic<bool> done{false}, stalled{false};
    std::thread audio([&] {
        Lcg random;
        for (int i = 0; i < blocks; ++i) {
            const int n = 1 + random.below(256);
            if (drops && i % 256 == 0) {
                stalled.store(true, std::memory_order_relaxed);
                while (device.deliver(ring, n) && std::chrono::steady_clock::now() < giveUp) {
                }
                stalled.store(false, std::memory_order_relaxed);
                continue;
            }
            if (!drops || i % 8 != 0)
                waitUntil([&] { return ring.capacity() - ring.fillLevel() >= static_cast<std::size_t>(n); });
            device.deliver(ring, n);
        }
        bool fit = false;
        while (!fit && std::chrono::steady_clock::now() < giveUp) {
            waitUntil([&] { return ring.fillLevel() == 0; });
            fit = device.deliver(ring, 64);
        }
        done.store(true, std::memory_order_release);
    });
    std::thread disk([&] {
        Lcg random;
        while (!done.load(std::memory_order_acquire)) {
            if (stalled.load(std::memory_order_relaxed)) {
                std::this_thread::yield();
                continue;
            }
            take.collect(ring);
            if (drops && random.below(8) == 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            else
                std::this_thread::yield();
        }
        take.collect(ring);
    });
    audio.join();
    disk.join();
}

inline void checkThreads() {
    {
        InputRing ring;
        ring.prepare(2, 4096);
        FakeDevice device(256);
        Take take(2);
        runThreads(ring, device, take, 30000, false);
        const StreamReport r = compare(take);
        std::printf("  two threads, nothing dropped: %llu frames, %llu wrong, %llu silent (expected 0, 0)\n",
                    static_cast<ull>(r.frames), static_cast<ull>(r.wrong), static_cast<ull>(r.silent));
        DAWLAB_CHECK(r.frames == device.framesDelivered());
        DAWLAB_CHECK(r.wrong == 0 && r.silent == 0);
        DAWLAB_CHECK(ring.droppedBlocks() == 0);
    }
    {
        InputRing ring;
        ring.prepare(2, 1024);
        FakeDevice device(256);
        Take take(2);
        runThreads(ring, device, take, 30000, true);
        const StreamReport r = compare(take);
        std::printf("  two threads, the disk stalls: %llu frames, %llu wrong, %llu silent\n", static_cast<ull>(r.frames),
                    static_cast<ull>(r.wrong), static_cast<ull>(r.silent));
        DAWLAB_CHECK(ring.droppedBlocks() > 0); // the stalls made the ring overflow
        DAWLAB_CHECK(r.wrong == 0);             // every frame is at its own place, whatever was dropped
        DAWLAB_CHECK(r.frames == device.framesDelivered());
        DAWLAB_CHECK(r.silent == ring.droppedFrames());
    }
}

} // namespace step01_check

int main() {
    using namespace step01_check;
    std::printf("Step 1: from the audio thread to the disk thread\n");
    checkEmpty();
    if (!checkWrapping())
        return dawlab::test::summary("l17_step01");
    checkFullRing();
    checkGapKeepsTheTime();
    checkSmallPops();
    checkGapQueueFull();
    checkChannels();
    checkLongCollect();
    checkNoAllocation();
    checkRandom();
    checkThreads();
    return dawlab::test::summary("l17_step01");
}
