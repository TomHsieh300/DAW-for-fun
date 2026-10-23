// l14_realtime_sim -- Steps 7 and 10: the engine driven like a real audio device.
//
// What it shows
//   - An "audio thread" calls Engine::processBlock() with a random block size (32..1024) each
//     time, the way some drivers and plugin hosts do, and paces itself like a sound card:
//     it waits until the previous block "has been played" before rendering the next one.
//   - At the same time the "UI thread" (main) does what a user does: moves faders and pans,
//     mutes and solos, turns effect knobs, bypasses, adds / removes / reorders / replaces
//     inserts, edits the clip list, seeks, loops, changes the tempo, starts and stops, and
//     collects garbage on a ~30 Hz timer.
//   - Values travel through atomics, structural changes through the lock-free command queue,
//     dead objects come back through the garbage queue. No mutex anywhere.
//   - The audio thread measures how long each block takes compared with its deadline (the
//     block's duration) and checks every block for NaN / inf.
//
// Run
//   build/bin/Debug/l14_realtime_sim.exe [seconds]      (default 6 s)
//
// Output
//   dawlab_output/l14_realtime_sim.wav   what the "sound card" played (busy, but no clicks
//                                        other than the ones the random edits ask for)
//
// What to look at
//   The printed statistics: commands applied, garbage collected, 0 leaked, 0 NaN blocks,
//   and the CPU load = render time / audio time. Build Release to see the real headroom:
//   a Debug build of all these effects may well be slower than real time; the simulation then
//   reports "deadline misses" (what you would hear as dropouts) but stays correct.

#include <dawlab/Common.h>
#include <dawlab/engine/All.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <random>
#include <thread>
#include <vector>

using namespace dawlab;
using namespace dawlab::engine;
using Clock = std::chrono::steady_clock;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kMaxBlock = 1024;

struct AudioStats {
    std::atomic<int64_t> samples{0};
    std::atomic<int> blocks{0};
    std::atomic<int> nanBlocks{0};
    std::atomic<int> deadlineMisses{0};
    std::atomic<double> busySeconds{0.0};
    std::atomic<double> worstLoad{0.0};
    std::atomic<int> worstBlockSize{0}; // size of the block with the worst load
};

bool isFinite(const AudioBlock& b) {
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (!std::isfinite(b.getChannelPointer(ch)[i]))
                return false;
    return true;
}

void buildProject(Engine& e) {
    auto drums = std::make_shared<const AudioBuffer>(makeDrumLoop(kSampleRate, 2, 120.0, 0.7f));
    auto arp = std::make_shared<const AudioBuffer>(makePluckSequence(kSampleRate, 4.0, 0.35f));
    for (int t = 0; t < kNumTracks; ++t) {
        std::vector<Clip> clips;
        for (int k = 0; k < 2; ++k)
            clips.push_back(Clip::fromBuffer(t % 2 ? arp : drums, k * 192000 + t * 6000));
        e.setTrackClips(t, std::move(clips));
        e.getMixer().getTrack(t).setGainDb(-9.0f);
        e.getMixer().getTrack(t).setPan(-0.8f + 0.2f * static_cast<float>(t));
        e.getMixer().getTrack(t).setSendLevelDb(t % 2, -12.0f);
    }
    e.addInsert(ChannelRef::track(0), -1, createEffect(EffectType::Compressor));
    e.addInsert(ChannelRef::track(1), -1, createEffect(EffectType::Chorus));
    e.addInsert(ChannelRef::bus(0), -1, createEffect(EffectType::Reverb));
    auto delay = createEffect(EffectType::Delay);
    delay->setParameterByName("Mix", 1.0f);
    e.addInsert(ChannelRef::bus(1), -1, std::move(delay));
    e.addInsert(ChannelRef::master(), -1, createEffect(EffectType::Limiter));
    e.getTransport().setLoopRange(0, 384000); // 4 bars
    e.getTransport().setLoopEnabled(true);
}

} // namespace

int main(int argc, char** argv) {
    const double seconds = argc > 1 ? std::max(1.0, std::atof(argv[1])) : 6.0;
    const int64_t totalSamples = static_cast<int64_t>(seconds * kSampleRate);

    Engine engine;
    engine.prepare(kSampleRate, kMaxBlock);
    buildProject(engine);
    engine.getTransport().play();

    AudioBuffer recording(kNumChannels, static_cast<int>(totalSamples)); // allocated up front
    AudioStats stats;
    std::atomic<bool> finished{false};

    // ---- the "sound card" ----
    std::thread audio([&] {
        std::mt19937 rng(2024);
        std::uniform_int_distribution<int> sizeDist(32, kMaxBlock);
        const auto start = Clock::now();
        int64_t pos = 0;
        while (pos < totalSamples) {
            const int n = static_cast<int>(std::min<int64_t>(sizeDist(rng), totalSamples - pos));
            AudioBlock block = recording.toBlock().getSubBlock(static_cast<int>(pos), n);
            const auto t0 = Clock::now();
            engine.processBlock(block); // <- the callback
            const double busy = std::chrono::duration<double>(Clock::now() - t0).count();
            const double deadline = n / kSampleRate;
            stats.busySeconds.store(stats.busySeconds.load() + busy);
            if (busy / deadline > stats.worstLoad.load()) {
                stats.worstLoad.store(busy / deadline);
                stats.worstBlockSize.store(n);
            }
            if (busy > deadline)
                stats.deadlineMisses.fetch_add(1);
            if (!isFinite(block))
                stats.nanBlocks.fetch_add(1);
            stats.blocks.fetch_add(1);
            pos += n;
            stats.samples.store(pos);
            // Pace like a device: the next callback comes when this block has been played.
            std::this_thread::sleep_until(start + std::chrono::duration<double>(pos / kSampleRate));
        }
        finished.store(true);
    });

    // ---- the "user" ----
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    auto pick = [&](int n) { return std::min(n - 1, static_cast<int>(u(rng) * static_cast<float>(n))); };
    auto pluck = std::make_shared<const AudioBuffer>(makePluck(220.0, kSampleRate, 1.0, 0.4f));
    int uiActions = 0, garbageFreed = 0;
    auto lastCollect = Clock::now();
    while (!finished.load()) {
        Mixer& mix = engine.getMixer();
        const ChannelRef someTrack = ChannelRef::track(pick(kNumTracks));
        switch (pick(12)) {
        case 0: mix.getTrack(someTrack.index).setGainDb(-24.0f + 18.0f * u(rng)); break;
        case 1: mix.getTrack(someTrack.index).setPan(2.0f * u(rng) - 1.0f); break;
        case 2: mix.getTrack(someTrack.index).setMute(u(rng) < 0.15f); break;
        case 3: mix.getTrack(someTrack.index).setSolo(u(rng) < 0.05f); break;
        case 4: mix.getTrack(someTrack.index).setSendLevelDb(pick(kNumBuses), -30.0f + 24.0f * u(rng)); break;
        case 5: // turn a knob on whatever is in slot 0
            if (InsertEffect* fx = engine.getInsert(someTrack, 0))
                fx->setParameterNormalized(pick(fx->getNumParameters()), u(rng));
            break;
        case 6:
            if (engine.getNumInserts(someTrack) < 3)
                engine.addInsert(someTrack, -1, createEffect(static_cast<EffectType>(pick(kNumUserEffectTypes))));
            break;
        case 7: engine.removeInsert(someTrack, pick(3)); break;
        case 8: engine.moveInsert(someTrack, 0, 1); break;
        case 9: {
            std::vector<Clip> clips;
            for (int k = 0; k < 8; ++k)
                clips.push_back(Clip::fromBuffer(pluck, pick(384000)));
            engine.setTrackClips(someTrack.index, std::move(clips));
            break;
        }
        case 10: engine.getTransport().setPosition(pick(384000)); break;
        case 11: engine.getTransport().setTempo(90.0 + 60.0 * u(rng)); break;
        default: break;
        }
        ++uiActions;
        if (Clock::now() - lastCollect > std::chrono::milliseconds(33)) { // the ~30 Hz UI timer
            garbageFreed += engine.collectGarbage();
            lastCollect = Clock::now();
        }
        // A busy user: one action every 5 ms at most. The OS timer granularity usually makes the
        // sleep longer (on Windows about 60 actions per second result).
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    audio.join();
    garbageFreed += engine.collectGarbage();

    writeWav(outputPath("l14_realtime_sim.wav"), recording, kSampleRate);

    const double audioSeconds = static_cast<double>(stats.samples.load()) / kSampleRate;
    std::printf("Steps 7 + 10: simulated audio device, random block sizes 32..%d\n", kMaxBlock);
    std::printf("  audio      : %.2f s in %d callbacks (%llu engine chunks)\n", audioSeconds, stats.blocks.load(),
                static_cast<unsigned long long>(engine.getBlocksProcessed()));
    std::printf("  CPU load   : average %.1f %%, worst block %.1f %% of its deadline (a %d-sample block), "
                "%d deadline misses\n",
                100.0 * stats.busySeconds.load() / audioSeconds, 100.0 * stats.worstLoad.load(),
                stats.worstBlockSize.load(), stats.deadlineMisses.load());
    std::printf("  UI         : %d actions, %llu commands applied, %llu dropped\n", uiActions,
                static_cast<unsigned long long>(engine.getCommandsApplied()),
                static_cast<unsigned long long>(engine.getCommandsDropped()));
    std::printf("  garbage    : %d objects freed on the UI thread, %llu leaked\n", garbageFreed,
                static_cast<unsigned long long>(engine.getGarbageLeaked()));
    std::printf("  NaN blocks : %d\n", stats.nanBlocks.load());
    std::printf("  wrote dawlab_output/l14_realtime_sim.wav\n");

    DAWLAB_CHECK(stats.nanBlocks.load() == 0);
    DAWLAB_CHECK(engine.getGarbageLeaked() == 0);
    DAWLAB_CHECK(engine.getCommandsApplied() > 0);
    DAWLAB_CHECK(stats.samples.load() == totalSamples);
    DAWLAB_CHECK(!hasNanOrInf(recording));
    return dawlab::test::summary("l14_realtime_sim");
}
