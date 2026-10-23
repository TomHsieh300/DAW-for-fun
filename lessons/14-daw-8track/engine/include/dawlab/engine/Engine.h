#pragma once
// Engine: Transport + Mixer + the two lock-free queues between the UI and the audio thread
// (Steps 1, 2 and 7). This is the object the JUCE app (stage 2) talks to.
//
//   UI / message thread                               audio thread
//   -------------------                               ------------
//   getTransport().play(), setTempo() ---atomics--->  transport.advance()
//   getMixer().getTrack(0).setGainDb() --atomics--->  strip.beginBlock()
//   getInsert(ch, slot)->setParameter() -atomics--->  effect.applyPendingParameters()
//   addInsert / removeInsert / replaceInsert /
//   moveInsert / setTrackClips    ---Command queue--> applyCommands()
//   collectGarbage()  (timer, ~30 Hz) <--Garbage queue--  old effects / old clip lists
//
// processBlock(out) is real-time safe: no allocation, no locks, no I/O, no exceptions.
// Everything it needs was allocated in prepare() or handed over ready-made in a Command.
// tests/test_realtime_safety.cpp replaces global operator new / delete and checks that
// processBlock() never calls them, with random block sizes and a second thread hammering the
// API.
//
// Offline use (examples, tests, bounce): the same object works on one thread. Configure it,
// call applyPendingCommandsOffline() (or just processBlock()), render, collectGarbage().
// See Renderer.h.

#include <dawlab/AudioBuffer.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "Clip.h"
#include "Commands.h"
#include "Constants.h"
#include "InsertEffect.h"
#include "Mixer.h"
#include "SpscQueue.h"
#include "Transport.h"

namespace dawlab::engine {

class Engine {
public:
    explicit Engine(std::size_t queueCapacity = kCommandQueueCapacity)
        : commands_(queueCapacity), garbage_(queueCapacity) {}

    ~Engine() {
        // Nothing may be processing any more: free what the audio thread handed back, and
        // the payload of commands that never reached it.
        collectGarbage();
        Command c;
        while (commands_.pop(c))
            disposeNow(c);
    }

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Allocates everything. Non-audio thread, while no audio callback is running (first
    // start, or after the audio device stopped because the sample rate or buffer size
    // changed). Pending commands are applied first, then every insert already in the mixer is
    // prepared again for the new settings, so a running project survives a device change.
    void prepare(double sampleRate, int maxBlockSize, int maxPdcSamples = kDefaultMaxPdcSamples) {
        sampleRate_ = sampleRate;
        maxBlockSize_ = std::max(1, maxBlockSize);
        transport_.prepare(sampleRate);
        mixer_.prepare(sampleRate, maxBlockSize_, maxPdcSamples);
        if (isPrepared())
            applyCommands(); // safe: no audio thread is running during prepare()
        for (ChannelRef ref : allChannels())
            if (ChannelStrip* s = mixer_.getStrip(ref))
                for (int i = 0; i < s->getInserts().getNumInserts(); ++i)
                    if (InsertEffect* fx = s->getInserts().getInsert(i)) {
                        fx->prepare(sampleRate_, maxBlockSize_, kNumChannels);
                        fx->publishLatency();
                    }
        mixer_.updateLatencies();
        prepared_.store(true, std::memory_order_release);
    }

    // The 11 strips in mixer order: tracks 1-8, bus A, bus B, master.
    static std::array<ChannelRef, kNumTracks + kNumBuses + 1> allChannels() noexcept {
        std::array<ChannelRef, kNumTracks + kNumBuses + 1> refs{};
        for (int i = 0; i < kNumTracks; ++i)
            refs[static_cast<size_t>(i)] = ChannelRef::track(i);
        for (int i = 0; i < kNumBuses; ++i)
            refs[static_cast<size_t>(kNumTracks + i)] = ChannelRef::bus(i);
        refs[static_cast<size_t>(kNumTracks + kNumBuses)] = ChannelRef::master();
        return refs;
    }

    bool isPrepared() const noexcept { return prepared_.load(std::memory_order_acquire); }
    double getSampleRate() const noexcept { return sampleRate_; }
    int getMaxBlockSize() const noexcept { return maxBlockSize_; }

    Transport& getTransport() noexcept { return transport_; }
    const Transport& getTransport() const noexcept { return transport_; }
    Mixer& getMixer() noexcept { return mixer_; }
    const Mixer& getMixer() const noexcept { return mixer_; }

    // Total output latency of the mix (PDC result), for the host / the render.
    int getLatencySamples() const noexcept { return mixer_.getTotalLatencySamples(); }
    // True when an insert latency exceeds the PDC limit (prepare's maxPdcSamples), so some path
    // could not be aligned (Mixer.h, "The PDC limit"). Any thread; the app shows a warning.
    bool hasPdcOverflow() const noexcept { return mixer_.hasPdcOverflow(); }

    // Asks the audio thread to clear all DSP state (tails, meters, smoothers) at the start of
    // its next block. Any thread.
    void requestReset() noexcept { resetRequested_.store(true, std::memory_order_relaxed); }

    // ---- structural changes (UI thread = the single producer) ---------------------------------
    // Raw command access. Returns false (and counts a drop) when the queue is full; a command
    // that carries ownership (effect / clips) is disposed of for you in that case.
    bool pushCommand(const Command& c) {
        if (commands_.push(c))
            return true;
        commandsDropped_.fetch_add(1, std::memory_order_relaxed);
        Command copy = c;
        disposeNow(copy);
        return false;
    }

    // Prepares `effect` with the engine's sample rate / block size (allocation happens here,
    // on the calling thread) and queues it for insertion at `slot` (-1 = append). Ownership
    // passes to the engine. Returns an observer pointer that stays valid until the effect is
    // removed AND collectGarbage() has run; nullptr on failure.
    InsertEffect* addInsert(ChannelRef channel, int slot, std::unique_ptr<InsertEffect> effect) {
        if (!effect || !isPrepared() || mixer_.getStrip(channel) == nullptr)
            return nullptr;
        effect->prepare(sampleRate_, maxBlockSize_, kNumChannels);
        effect->publishLatency(); // the UI may ask for it before the audio thread runs
        InsertEffect* observer = effect.get();
        return pushCommand(Command::addInsert(channel, slot, effect.release())) ? observer : nullptr;
    }

    // Swaps the effect in `slot` for a new one in one block boundary (no gap, no double
    // processing). Use it to change "prepare-time" settings such as a convolution IR.
    InsertEffect* replaceInsert(ChannelRef channel, int slot, std::unique_ptr<InsertEffect> effect) {
        if (!effect || !isPrepared() || mixer_.getStrip(channel) == nullptr)
            return nullptr;
        effect->prepare(sampleRate_, maxBlockSize_, kNumChannels);
        effect->publishLatency(); // the UI may ask for it before the audio thread runs
        InsertEffect* observer = effect.get();
        return pushCommand(Command::replaceInsert(channel, slot, effect.release())) ? observer : nullptr;
    }

    bool removeInsert(ChannelRef channel, int slot) { return pushCommand(Command::removeInsert(channel, slot)); }
    bool moveInsert(ChannelRef channel, int fromSlot, int toSlot) {
        return pushCommand(Command::moveInsert(channel, fromSlot, toSlot));
    }

    // Replaces the clip list of a track. The list is copied into a new vector here (UI
    // thread); the audio thread swaps one pointer.
    bool setTrackClips(int track, std::vector<Clip> clips) {
        if (track < 0 || track >= kNumTracks)
            return false;
        auto* list = new std::vector<Clip>(std::move(clips));
        return pushCommand(Command::replaceClips(track, list));
    }

    // ---- observing the structure (any thread) ----------------------------------------------
    // The effect currently in `slot` (nullptr if empty). The pointer stays valid until the
    // effect is removed and the calling (UI) thread runs collectGarbage(); so only the thread
    // that collects garbage should hold on to it.
    InsertEffect* getInsert(ChannelRef channel, int slot) const noexcept {
        const ChannelStrip* s = mixer_.getStrip(channel);
        return s ? s->getInserts().getInsert(slot) : nullptr;
    }
    int getNumInserts(ChannelRef channel) const noexcept {
        const ChannelStrip* s = mixer_.getStrip(channel);
        return s ? s->getInserts().getNumInserts() : 0;
    }

    // Deletes everything the audio thread has finished with. Call it regularly from the UI
    // thread (a ~30 Hz timer) and after every offline render. Returns the number freed.
    int collectGarbage() {
        int freed = 0;
        Garbage g;
        while (garbage_.pop(g)) {
            g.destroy();
            ++freed;
        }
        return freed;
    }

    // OFFLINE ONLY (no audio thread running): applies queued commands and recomputes PDC on
    // the calling thread, so that e.g. getLatencySamples() and the song length are known
    // before rendering. Never call this while an audio device drives processBlock().
    void applyPendingCommandsOffline() noexcept {
        applyCommands();
        mixer_.updateLatencies();
    }

    // ---- statistics (any thread) ---------------------------------------------------------------
    uint64_t getBlocksProcessed() const noexcept { return blocksProcessed_.load(std::memory_order_relaxed); }
    uint64_t getCommandsApplied() const noexcept { return commandsApplied_.load(std::memory_order_relaxed); }
    uint64_t getCommandsDropped() const noexcept { return commandsDropped_.load(std::memory_order_relaxed); }
    uint64_t getGarbageLeaked() const noexcept { return garbageLeaked_.load(std::memory_order_relaxed); }
    std::size_t getPendingCommands() const noexcept { return commands_.size(); }
    std::size_t getPendingGarbage() const noexcept { return garbage_.size(); }

    // ---- audio thread ----------------------------------------------------------------------------
    // Fills `out` (stereo; a mono `out` gets the left channel). Any block size is accepted:
    // blocks longer than maxBlockSize are processed in chunks.
    void processBlock(AudioBlock out) noexcept {
        if (!isPrepared()) {
            out.clear();
            return;
        }
        const int total = out.getNumSamples();
        int done = 0;
        while (done < total) {
            const int len = std::min(maxBlockSize_, total - done);
            processChunk(out.getSubBlock(done, len));
            done += len;
        }
    }

private:
    void processChunk(AudioBlock out) noexcept {
        applyCommands();
        if (resetRequested_.exchange(false, std::memory_order_relaxed))
            mixer_.reset();
        mixer_.updateLatencies();

        // Usually one pass. Only a loop shorter than chunk / kMaxSegmentsPerBlock needs more.
        const int n = out.getNumSamples();
        int done = 0;
        while (done < n) {
            TimelineSegment segments[Transport::kMaxSegmentsPerBlock];
            int covered = 0;
            const int numSegments = transport_.advance(n - done, segments, covered);
            if (covered <= 0)
                covered = n - done;
            mixer_.process(out.getSubBlock(done, covered), segments, numSegments, transport_.getTempo());
            done += covered;
        }
        blocksProcessed_.fetch_add(1, std::memory_order_relaxed);
    }

    void applyCommands() noexcept {
        Command c;
        while (commands_.pop(c)) {
            apply(c);
            commandsApplied_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void apply(Command& c) noexcept {
        ChannelStrip* strip = mixer_.getStrip(c.channel);
        switch (c.type) {
        case CommandType::AddInsert: {
            InsertEffect* rejected = strip ? strip->getInserts().insertAt(c.slot, c.effect) : c.effect;
            if (rejected)
                discard(Garbage::of(rejected)); // chain full or bad channel
            break;
        }
        case CommandType::RemoveInsert:
            if (strip)
                if (InsertEffect* removed = strip->getInserts().removeAt(c.slot))
                    discard(Garbage::of(removed));
            break;
        case CommandType::ReplaceInsert: {
            InsertEffect* old = strip ? strip->getInserts().replaceAt(c.slot, c.effect) : c.effect;
            if (old)
                discard(Garbage::of(old));
            break;
        }
        case CommandType::MoveInsert:
            if (strip)
                strip->getInserts().moveTo(c.slot, c.slot2);
            break;
        case CommandType::ReplaceClips: {
            const std::vector<Clip>* old = c.clips; // invalid target: hand the new list straight back
            if (c.channel.kind == ChannelKind::Track && strip != nullptr)
                old = static_cast<Track*>(strip)->exchangeClips(c.clips);
            if (old)
                discard(Garbage::of(const_cast<std::vector<Clip>*>(old)));
            break;
        }
        case CommandType::None: break;
        }
    }

    // Audio thread: hand an object to the UI thread for deletion. If the garbage queue is full
    // we may not delete here (that would free memory on the audio thread), so the object leaks
    // and we count it. With kCommandQueueCapacity slots that does not happen as long as
    // collectGarbage() runs regularly: every command produces at most one garbage ticket.
    void discard(Garbage g) noexcept {
        if (!garbage_.push(g))
            garbageLeaked_.fetch_add(1, std::memory_order_relaxed);
    }

    // UI thread: a command that never reached the audio thread still owns its payload.
    static void disposeNow(Command& c) noexcept {
        if (c.type == CommandType::AddInsert || c.type == CommandType::ReplaceInsert)
            delete c.effect;
        else if (c.type == CommandType::ReplaceClips)
            delete c.clips;
        c.effect = nullptr;
        c.clips = nullptr;
    }

    double sampleRate_ = 0.0;
    int maxBlockSize_ = 0;
    std::atomic<bool> prepared_{false};
    std::atomic<bool> resetRequested_{false};

    Transport transport_;
    Mixer mixer_;
    SpscQueue<Command> commands_;
    SpscQueue<Garbage> garbage_;

    std::atomic<uint64_t> blocksProcessed_{0};
    std::atomic<uint64_t> commandsApplied_{0};
    std::atomic<uint64_t> commandsDropped_{0};
    std::atomic<uint64_t> garbageLeaked_{0};
};

} // namespace dawlab::engine
