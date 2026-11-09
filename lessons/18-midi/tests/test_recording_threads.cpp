// test_recording_threads: the audio thread with an input never allocates, and nothing races (chapter 17,
// Step 6). Build it with -fsanitize=thread as well.
//
// Three threads, as in the app: the audio thread calls processBlock() with random block sizes, the disk
// thread pumps the session, and the UI arms tracks, changes the monitors, starts and stops, seeks, loops,
// and removes and adds tracks. Every frame of the input carries a counter, so each take can say for itself
// that it is one unbroken stream.

#include "TestHelpers.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <new>
#include <random>
#include <string>
#include <thread>
#include <vector>

// ---- allocation counting (see test_realtime_safety.cpp) -------------------------------------
namespace {
thread_local bool tCounting = false;
std::atomic<long> gAllocs{0};

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
        gAllocs.fetch_add(1, std::memory_order_relaxed);
    std::free(p);
}
} // namespace

void* operator new(std::size_t n) { return countedAlloc(n); }
void* operator new[](std::size_t n) { return countedAlloc(n); }
void operator delete(void* p) noexcept { countedFree(p); }
void operator delete[](void* p) noexcept { countedFree(p); }
void operator delete(void* p, std::size_t) noexcept { countedFree(p); }
void operator delete[](void* p, std::size_t) noexcept { countedFree(p); }

using namespace dawlab;
using namespace dawlab::engine;
using namespace l15test;
namespace fs = std::filesystem;

namespace {

constexpr int kMaxBlock = 2048;
constexpr std::int64_t kCycle = 1 << 23;

float valueOf(std::int64_t frame) { return static_cast<float>(frame % kCycle + 1); }

} // namespace

int main() {
    std::printf("the engine with an input, on three threads\n");
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path folder = fs::path("dawlab_output") / ("l17_test_threads_" + std::to_string(now));
    fs::create_directories(folder);

    Engine e;
    e.prepare(kSampleRate, kMaxBlock);
    e.addInsert(ChannelRef::master(), -1, std::make_unique<PureDelayEffect>(64));
    constexpr int kTracks = 4;
    std::vector<TrackId> ids;
    for (int t = 0; t < kTracks; ++t) {
        ids.push_back(TrackId{static_cast<std::uint32_t>(t + 1)});
        e.addTrack(ids.back(), t % 2 == 0 ? TrackFormat::Mono : TrackFormat::Stereo);
        e.setTrackClips(ids.back(), {Clip::fromBuffer(makeConstant(0.1f, 48000), 0)});
    }
    e.applyPendingCommandsOffline();
    e.getTransport().play();

    std::atomic<bool> quit{false};
    std::atomic<std::int64_t> device{0}, allowed{0};

    std::thread audio([&] {
        std::mt19937 random(3u);
        std::vector<float> a(kMaxBlock * 2), b(kMaxBlock * 2), left(kMaxBlock * 2), right(kMaxBlock * 2);
        const float* inputs[] = {a.data(), b.data()};
        float* outputs[] = {left.data(), right.data()};
        std::int64_t frame = 0;
        while (!quit.load(std::memory_order_relaxed)) {
            if (frame >= allowed.load(std::memory_order_relaxed)) {
                std::this_thread::yield();
                continue;
            }
            const int n = std::uniform_int_distribution<int>(1, kMaxBlock + 500)(random);
            for (int i = 0; i < n; ++i) {
                a[static_cast<std::size_t>(i)] = valueOf(frame + i);
                b[static_cast<std::size_t>(i)] = -valueOf(frame + i);
            }
            tCounting = true;
            e.processBlock(AudioBlock(outputs, 2, n), InputBlock{inputs, 2});
            tCounting = false;
            frame += n;
            device.store(frame, std::memory_order_relaxed);
        }
    });

    std::mt19937 random(9u);
    const auto between = [&random](int a, int z) { return std::uniform_int_distribution<int>(a, z)(random); };
    const auto begin = std::chrono::steady_clock::now();
    const auto running = [&] { return std::chrono::steady_clock::now() - begin < std::chrono::seconds(40); };
    const auto keepGoing = [&](std::int64_t ahead) {
        allowed.store(device.load(std::memory_order_relaxed) + ahead, std::memory_order_relaxed);
        std::this_thread::yield();
    };

    int recordings = 0, takes = 0, slotChanges = 0, livePeeks = 0;
    std::uint64_t frames = 0, wrong = 0;
    std::uint32_t nextId = 100;
    for (int round = 0; round < 60 && running(); ++round) {
        for (TrackId id : ids) {
            Track* t = e.getTrack(id);
            if (t == nullptr)
                continue;
            t->setArmed(between(0, 3) != 0);
            t->setInputChannel(between(0, 1));
            t->setMonitorPath(between(0, 1) == 0 ? MonitorPath::Direct : MonitorPath::ThroughInserts);
            t->getMonitor().setMode(static_cast<MonitorMode>(between(0, 2)));
        }
        RecordRange range;
        if (round % 3 == 1) {
            range.loopStart = between(0, 20000);
            range.loopEnd = range.loopStart + between(100, 20000);
            e.getTransport().setLoopRange(range.loopStart, range.loopEnd);
            e.getTransport().setLoopEnabled(true);
        } else {
            e.getTransport().setLoopEnabled(false);
        }
        e.getTransport().setPosition(between(0, 40000));
        keepGoing(4096);

        RecordingSession session;
        const int started = session.begin(e, folder, "take" + std::to_string(round), Latencies{10, 20, 0, 0}, range);
        if (started == 0) {
            e.stopRecording();
            while (!e.isRecordingOver() && running())
                keepGoing(4096);
            continue;
        }
        std::atomic<bool> diskDone{false};
        std::thread disk([&] {
            while (!session.pump()) {
                std::this_thread::yield();
                if (quit.load(std::memory_order_relaxed))
                    break;
            }
            diskDone.store(true, std::memory_order_relaxed);
        });

        const std::int64_t until = device.load(std::memory_order_relaxed) + between(2000, 30000);
        int change = 0;
        while (device.load(std::memory_order_relaxed) < until && running()) {
            for (TrackId id : ids) {
                LiveTake live;
                if (session.getLive(id, live)) {
                    wrong += live.trail->size() > 0 ? 0 : 1;
                    wrong += live.trail->at(live.trail->size() - 1) >= 0.0f ? 0 : 1;
                    livePeeks += live.stampedPosition >= 0 ? 1 : 0;
                }
                if (Track* t = e.getTrack(id)) {
                    t->getMonitor().setLevel(static_cast<float>(change % 4) * 0.25f);
                    t->getMonitor().resetFeedback();
                    (void)t->getMonitor().feedbackDetected();
                }
            }
            e.collectGarbage();
            ++change;
            keepGoing(4096);
        }
        // A track may leave in the middle of a recording, and another one take its slot.
        if (round % 5 == 2) {
            const auto which = static_cast<std::size_t>(between(0, kTracks - 1));
            if (e.removeTrack(ids[which])) {
                ids[which] = TrackId{nextId++};
                e.addTrack(ids[which], TrackFormat::Mono);
                ++slotChanges;
            }
        }
        // A seek or a stop of the transport ends it just as well.
        if (round % 4 == 3)
            e.getTransport().setPosition(between(100000, 200000));
        e.stopRecording();
        while (!diskDone.load(std::memory_order_relaxed)) {
            keepGoing(4096);
            if (!running()) {
                e.stopRecording();
                allowed.store(std::int64_t{1} << 62, std::memory_order_relaxed);
            }
        }
        disk.join();

        ++recordings;
        for (const auto& result : session.results()) {
            if (!result.take.ok)
                continue;
            ++takes;
            WavData wav;
            if (!readWav(result.take.path.string(), wav) || wav.buffer.getNumSamples() != static_cast<int>(result.take.numFrames)) {
                ++wrong;
                continue;
            }
            // One unbroken stream on channel 0: each frame is the one after the last, unless the ring
            // dropped it. The input channel can be 0 or 1, and channel 1 is negative.
            const int n = wav.buffer.getNumSamples();
            const int firstReal = n > 0 ? 0 : -1;
            if (firstReal >= 0) {
                const float base = std::abs(wav.buffer.getSample(0, 0));
                for (int i = 1; i < n; ++i) {
                    const float got = std::abs(wav.buffer.getSample(0, i));
                    const float want = base + static_cast<float>(i);
                    wrong += got == want || (result.droppedFrames > 0 && got == 0.0f) ? 0 : 1;
                }
            }
            frames += static_cast<std::uint64_t>(n);
            std::error_code ec;
            fs::remove(result.take.path, ec);
        }
    }
    quit.store(true, std::memory_order_relaxed);
    audio.join();
    e.collectGarbage();

    const long allocs = gAllocs.load();
    std::printf("  %d recordings, %d takes, %llu frames, %d tracks replaced: %llu wrong (expected 0)\n", recordings, takes,
                static_cast<unsigned long long>(frames), slotChanges, static_cast<unsigned long long>(wrong));
    std::printf("  allocations on the audio thread: %ld (expected 0)\n", allocs);
    DAWLAB_CHECK(recordings > 0);
    DAWLAB_CHECK(takes > 0);
    DAWLAB_CHECK(wrong == 0);
    DAWLAB_CHECK(allocs == 0);
    DAWLAB_CHECK(livePeeks > 0);
    std::error_code ec;
    fs::remove_all(folder, ec);
    return dawlab::test::summary("l17_test_recording_threads");
}
