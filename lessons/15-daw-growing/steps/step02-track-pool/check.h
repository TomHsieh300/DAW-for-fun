#pragma once
// check.h - the self-check of Step 2 (a pool of tracks). Defines main(); included at the END of
// starter/step02.cpp and solution/step02.cpp, so both run exactly the same checks.
//
// Tests only what Step 2 adds: adding and removing tracks through the command queue, slots that
// are reused, commands that arrive too late, a full pool and a full queue, no leaked sources.
// Ends with a real audio thread processing blocks while the UI thread adds, removes and turns
// tracks up and down at random. Build it with -fsanitize=thread (scripts/build.sh --sanitize
// thread) and ThreadSanitizer checks that the two threads share nothing without synchronisation.

#include <dawlab/Common.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace step02_check {

using namespace minidaw;

// Runs one block on the calling thread (the "audio thread" of the single-threaded checks) and
// returns the mix level.
inline float mixOf(Engine& engine) {
    std::vector<float> block(64, -1.0f);
    engine.process(block.data(), static_cast<int>(block.size()));
    return block[0];
}

inline int liveSources() { return ToneSource::live.load(); }

struct Lcg {
    std::uint32_t state = 12345u;
    std::uint32_t next() noexcept {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    }
    int below(int n) noexcept { return n <= 0 ? 0 : static_cast<int>(next() % static_cast<std::uint32_t>(n)); }
};

inline void workedExample() {
    Engine engine;
    TrackManager ui(engine);

    const TrackId drums = ui.addTrack("Drums", 0.125f);
    const TrackId bass = ui.addTrack("Bass", 0.25f);
    const TrackId keys = ui.addTrack("Keys", 0.5f);
    const int bassSlot = ui.slotOf(bass);
    const float mix1 = mixOf(engine);
    std::printf("  add Drums 0.125, Bass 0.25, Keys 0.5: slots %d %d %d, mix %.3f (expected 0 1 2, 0.875)\n",
                ui.slotOf(drums), bassSlot, ui.slotOf(keys), mix1);
    DAWLAB_CHECK(drums.isValid() && bass.isValid() && keys.isValid());
    DAWLAB_CHECK(ui.slotOf(drums) == 0 && bassSlot == 1 && ui.slotOf(keys) == 2);
    DAWLAB_CHECK_NEAR(mix1, 0.875, 1e-6);
    DAWLAB_CHECK(engine.activeTracks() == 3 && engine.idInSlot(1) == bass);

    // A fader move for Bass, recorded while Bass was still there (say, an automation write or an
    // undo entry), arrives only after Bass is gone and Vox has taken its slot.
    const Command lateBassFader{CommandType::SetGain, bass, bassSlot, nullptr, 0.0f};
    DAWLAB_CHECK(ui.removeTrack(bass));
    const TrackId vox = ui.addTrack("Vox", 1.0f);
    DAWLAB_CHECK(engine.send(lateBassFader));
    const float mix2 = mixOf(engine);
    std::printf("  remove Bass, add Vox 1.0: Vox in slot %d (expected 1, Bass's old slot); a late Bass fader at\n"
                "  -inf dB for slot 1 arrives: mix %.3f (expected 1.625 - the late command must not mute Vox)\n",
                ui.slotOf(vox), mix2);
    DAWLAB_CHECK(ui.slotOf(vox) == bassSlot && engine.idInSlot(bassSlot) == vox);
    DAWLAB_CHECK_NEAR(mix2, 1.625, 1e-6);
    DAWLAB_CHECK(ui.collectGarbage() == 1); // Bass's source came back
    DAWLAB_CHECK(liveSources() == 3);

    DAWLAB_CHECK(ui.setGainDb(vox, -6.0f));
    const float mix3 = mixOf(engine);
    std::printf("  Vox to -6 dB: mix %.3f (expected 1.126)\n", mix3);
    DAWLAB_CHECK_NEAR(mix3, 0.625 + 1.0 * dawlab::dbToGain(-6.0), 1e-5);

    // Removing is idempotent: twice is the same as once.
    DAWLAB_CHECK(ui.removeTrack(keys));
    const bool again = ui.removeTrack(keys);
    const bool invalid = ui.removeTrack(TrackId{});
    const float mix4 = mixOf(engine);
    std::printf("  remove Keys, then again, then an invalid id: %s, %s; mix %.3f (expected false, false, 0.626)\n",
                again ? "true" : "false", invalid ? "true" : "false", mix4);
    DAWLAB_CHECK(!again && !invalid);
    DAWLAB_CHECK_NEAR(mix4, 0.125 + dawlab::dbToGain(-6.0), 1e-5);
    DAWLAB_CHECK(ui.list().size() == 2 && engine.activeTracks() == 2);
    DAWLAB_CHECK(ui.collectGarbage() == 1 && liveSources() == 2);
}

inline void fullPool() {
    Engine engine;
    TrackManager ui(engine);
    int added = 0;
    for (int i = 0; i < kMaxTracks; ++i)
        added += ui.addTrack("T" + std::to_string(i), 0.0625f).isValid() ? 1 : 0;
    const TrackId extra = ui.addTrack("one too many", 1.0f);
    const float mix = mixOf(engine);
    std::printf("  fill the pool (%d slots), then one more: %d added, extra valid: %s, %d in the list, mix %.3f\n"
                "  (expected %d, no, %d, %.3f)\n",
                kMaxTracks, added, extra.isValid() ? "yes" : "no", ui.list().size(), mix, kMaxTracks, kMaxTracks,
                kMaxTracks * 0.0625);
    DAWLAB_CHECK(added == kMaxTracks && !extra.isValid());
    DAWLAB_CHECK(ui.list().size() == kMaxTracks && ui.freeSlot() == -1);
    DAWLAB_CHECK(engine.activeTracks() == kMaxTracks && liveSources() == kMaxTracks); // nothing left over
    DAWLAB_CHECK_NEAR(mix, kMaxTracks * 0.0625, 1e-6);

    // After one track is gone there is room again, in exactly that slot.
    const TrackId fifth = ui.list().size() > 4 ? ui.list().tracks()[4].id : TrackId{}; // (the starter's list is empty)
    DAWLAB_CHECK(ui.removeTrack(fifth));
    const TrackId again = ui.addTrack("again", 0.0625f);
    DAWLAB_CHECK(again.isValid() && ui.slotOf(again) == 4);
    mixOf(engine);
    DAWLAB_CHECK(engine.idInSlot(4) == again && ui.collectGarbage() == 1);
}

inline void fullQueue() {
    Engine engine; // its process() is not called until the end: nothing leaves the queue
    TrackManager ui(engine);
    const TrackId keep = ui.addTrack("Keep", 0.5f);
    int sent = 1;
    while (ui.setGainDb(keep, -1.0f * static_cast<float>(sent)))
        ++sent;
    const int before = liveSources();
    const TrackId lost = ui.addTrack("Lost", 1.0f);
    const bool removed = ui.removeTrack(keep);
    std::printf("  a full queue (%d commands): add -> valid %s, remove -> %s, list size %d, sources made %d\n"
                "  (expected %zu, no, false, 1, 0)\n",
                sent, lost.isValid() ? "yes" : "no", removed ? "true" : "false", ui.list().size(),
                liveSources() - before, kCommandCapacity);
    DAWLAB_CHECK(sent == static_cast<int>(kCommandCapacity));
    DAWLAB_CHECK(!lost.isValid() && !removed);
    DAWLAB_CHECK(ui.list().size() == 1 && ui.slotOf(keep) == 0 && ui.freeSlot() == 1);
    DAWLAB_CHECK(liveSources() == before); // the failed add deleted the source it had made

    mixOf(engine); // the queue drains
    DAWLAB_CHECK(engine.activeTracks() == 1 && engine.idInSlot(0) == keep);
    DAWLAB_CHECK(ui.removeTrack(keep)); // and works again
    mixOf(engine);
    DAWLAB_CHECK(engine.activeTracks() == 0 && ui.collectGarbage() == 1);
}

// Commands that a correct TrackManager never sends; the engine must survive them without leaks.
inline void rogueCommands() {
    Engine engine;
    TrackManager ui(engine);
    const TrackId a = ui.addTrack("A", 0.5f);
    mixOf(engine);
    const int before = liveSources();
    engine.applyCommand(Command{CommandType::AddTrack, TrackId{900}, 0, new ToneSource(1.0f), 1.0f}); // slot taken
    engine.applyCommand(Command{CommandType::AddTrack, TrackId{901}, kMaxTracks, new ToneSource(1.0f), 1.0f}); // no such slot
    engine.applyCommand(Command{CommandType::RemoveTrack, TrackId{902}, 0, nullptr, 1.0f}); // wrong id
    engine.applyCommand(Command{CommandType::SetGain, a, -1, nullptr, 0.0f});               // no such slot
    const int returned = ui.collectGarbage();
    std::printf("  rogue commands (add to a taken slot, add out of range, remove the wrong id, gain out of range):\n"
                "  slot 0 still holds A: %s, sources returned %d, leaked %d (expected yes, 2, 0)\n",
                engine.idInSlot(0) == a ? "yes" : "no", returned, liveSources() - before);
    DAWLAB_CHECK(engine.idInSlot(0) == a && engine.activeTracks() == 1);
    DAWLAB_CHECK(returned == 2 && liveSources() == before);
    DAWLAB_CHECK_NEAR(mixOf(engine), 0.5, 1e-6);
}

// The UI thread (this one) adds, removes and changes gains at random while a second thread plays
// the audio thread. Afterwards both sides must agree, and the mix must be what the UI expects.
inline void twoThreads() {
    Engine engine;
    TrackManager ui(engine);
    std::atomic<bool> stop{false};
    std::atomic<long> blocks{0};
    std::thread audio([&] {
        std::vector<float> block(kMaxBlockSize);
        while (!stop.load(std::memory_order_acquire)) {
            engine.process(block.data(), kMaxBlockSize);
            blocks.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::yield();
        }
    });

    std::map<std::uint32_t, float> level; // the UI's record of each track's source level
    Lcg rng;
    constexpr int kOps = 20000;
    int adds = 0, removes = 0, gains = 0, refused = 0;
    for (int op = 0; op < kOps; ++op) {
        const int kind = rng.below(10);
        const int n = ui.list().size();
        if (kind < 4 || n == 0) {
            const float lvl = static_cast<float>(1 + rng.below(8)) / 64.0f;
            const TrackId id = ui.addTrack("T" + std::to_string(op), lvl);
            if (id.isValid()) {
                level[id.value] = lvl;
                ++adds;
            } else {
                ++refused; // pool or queue full: fine, nothing changed
            }
        } else if (kind < 7) {
            const TrackId id = ui.list().tracks()[static_cast<size_t>(rng.below(n))].id;
            if (ui.removeTrack(id)) {
                level.erase(id.value);
                ++removes;
            } else {
                ++refused;
            }
        } else {
            const TrackId id = ui.list().tracks()[static_cast<size_t>(rng.below(n))].id;
            if (ui.setGainDb(id, -static_cast<float>(rng.below(4)) * 6.0f))
                ++gains;
            else
                ++refused;
        }
        // Let the audio thread keep up (a real UI is far slower than this loop): every 16
        // operations, wait until it has processed another block.
        if (op % 16 == 15) {
            const long seen = blocks.load(std::memory_order_relaxed);
            while (blocks.load(std::memory_order_relaxed) == seen)
                std::this_thread::yield();
        }
    }
    stop.store(true, std::memory_order_release);
    audio.join();

    // The audio thread has stopped: drain what is left on this thread, then compare.
    for (int i = 0; i < 4; ++i) {
        ui.collectGarbage();
        mixOf(engine);
    }
    ui.collectGarbage();
    int mismatches = 0;
    double expected = 0.0;
    for (const TrackInfo& t : ui.list().tracks()) {
        if (engine.idInSlot(ui.slotOf(t.id)) != t.id)
            ++mismatches;
        expected += level[t.id.value] * dawlab::dbToGain(static_cast<double>(t.gainDb));
    }
    const float mix = mixOf(engine);
    std::printf("  2 threads, %d random operations (%d adds, %d removes, %d gain changes, %d refused) over %ld blocks:\n"
                "  %d slot mismatches, %d tracks in the engine for %d in the list, %d sources alive, mix %.4f\n"
                "  (expected 0 mismatches, equal counts, sources = tracks, mix %.4f)\n",
                kOps, adds, removes, gains, refused, blocks.load(), mismatches, engine.activeTracks(),
                ui.list().size(), liveSources(), mix, expected);
    DAWLAB_CHECK(adds > kOps / 10 && removes > kOps / 10 && gains > kOps / 10); // the threads really interleaved
    DAWLAB_CHECK(blocks.load() >= kOps / 16);
    DAWLAB_CHECK(mismatches == 0);
    DAWLAB_CHECK(engine.activeTracks() == ui.list().size());
    DAWLAB_CHECK(liveSources() == ui.list().size());
    DAWLAB_CHECK_NEAR(mix, expected, 1e-4);
}

inline int run() {
    std::printf("Step 2: a pool of tracks (build-along)\n");
    workedExample();
    fullPool();
    fullQueue();
    rogueCommands();
    twoThreads();
    std::printf("  sources alive after every engine is gone: %d (expected 0)\n", liveSources());
    DAWLAB_CHECK(liveSources() == 0);
    return dawlab::test::summary("l15_step02");
}

} // namespace step02_check

int main() { return step02_check::run(); }
