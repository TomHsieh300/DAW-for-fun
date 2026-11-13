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
//
// Chapter 15 (Step 6): tracks come and go. The engine has a pool of kMaxTracks slots (Mixer.h).
//   addTrack(id)      UI thread: picks a free slot in the UI's own slot table (slotOwner_),
//                     resets that slot's values (atomics) and sends AddTrack
//   removeTrack(id)   UI thread: sends RemoveTrack; the audio thread hands the track's inserts
//                     and clips back as garbage and clears its DSP state
//   trackRef(id)      the ChannelRef (slot + id) for every other call that names a track
// Two tables, one writer each: slotOwner_ belongs to the UI thread, the mixer's trackInSlot_
// to the audio thread. The UI never reads the audio thread's table; the audio thread checks
// every track command against its own (a stale command changes nothing). Slots are reused at
// once: the command queue keeps its order, so RemoveTrack always arrives before the AddTrack
// that reuses the slot. And processBlock() times itself (getCpuMeter(), chapter 15 Step 3).

#include <dawlab/AudioBuffer.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <type_traits>
#include <memory>
#include <utility>
#include <vector>

#include "Clip.h"
#include "Commands.h"
#include "Constants.h"
#include "CpuMeter.h"
#include "InsertEffect.h"
#include "Mixer.h"
#include "SpscQueue.h"
#include "Transport.h"

namespace dawlab::engine {

class Engine {
public:
    // The garbage queue holds at least the garbage of one command (kMaxGarbagePerCommand).
    explicit Engine(std::size_t queueCapacity = kCommandQueueCapacity)
        : commands_(queueCapacity),
          garbage_(std::max(queueCapacity, static_cast<std::size_t>(kMaxGarbagePerCommand))),
          capture_(kCaptureQueueCapacity) {
        mixer_.setCaptureQueue(&capture_);
    }

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
        cpuMeter_.prepare(sampleRate);
        if (isPrepared())
            applyCommands(); // safe: no audio thread is running during prepare()
        auto reprepare = [this](ChannelStrip& s) {
            for (int i = 0; i < s.getInserts().getNumInserts(); ++i)
                if (InsertEffect* fx = s.getInserts().getInsert(i)) {
                    fx->prepare(sampleRate_, maxBlockSize_, kNumChannels);
                    fx->publishLatency();
                    fx->prepareLanes(sampleRate_);
                }
        };
        for (int slot = 0; slot < kMaxTracks; ++slot) // free slots have no inserts
            reprepare(mixer_.getTrack(slot));
        for (int b = 0; b < kNumBuses; ++b)
            reprepare(mixer_.getBus(b));
        reprepare(mixer_.getMaster());
        mixer_.updateLatencies();
        prepared_.store(true, std::memory_order_release);
    }

    // ---- tracks (UI thread = the single producer) ------------------------------------------
    // Gives track `id` a free slot, resets the slot's values (fader 0 dB, centre, no mute, solo
    // or sends) and the format, and queues AddTrack. Returns the track's reference, or one with
    // index -1 if the id is invalid or already here, the pool is full or the queue is full -
    // then nothing changed. The slot's values are written now; if the slot's previous track is
    // still playing its last block (its RemoveTrack is queued), that block may already hear
    // them - it is being deleted anyway.
    ChannelRef addTrack(TrackId id, TrackFormat format = TrackFormat::Mono, std::string name = {}) {
        const ChannelRef none = ChannelRef::track(-1, TrackId{});
        if (!id.isValid() || slotOf(id) >= 0)
            return none;
        const auto freeSlot = std::find_if(slotOwner_.begin(), slotOwner_.end(), [](TrackId t) { return !t.isValid(); });
        if (freeSlot == slotOwner_.end())
            return none;
        const int slot = static_cast<int>(freeSlot - slotOwner_.begin());
        Track& t = mixer_.getTrack(slot);
        t.setName(std::move(name));
        t.setGainDb(0.0f);
        t.setPan(0.0f);
        t.setMute(false);
        t.setSolo(false);
        for (int b = 0; b < kNumBuses; ++b) {
            t.setSendLevelDb(b, kMinusInfinityDb);
            t.setSendPreFader(b, false);
        }
        t.setFormat(format);
        t.resetLaneModes();
        const ChannelRef ref = ChannelRef::track(slot, id);
        if (!pushCommand(Command::addTrack(ref)))
            return none;
        *freeSlot = id;
        uiChainLengths_[static_cast<size_t>(slot)] = 0; // the queued RemoveTrack takes the old chain away
        return ref;
    }
    // Queues RemoveTrack; the slot is free for the next addTrack() at once. False if the id is
    // not here or the queue is full (then nothing changed). Removing twice is a harmless false.
    bool removeTrack(TrackId id) {
        const int slot = slotOf(id);
        if (slot < 0 || !pushCommand(Command::removeTrack(ChannelRef::track(slot, id))))
            return false;
        slotOwner_[static_cast<size_t>(slot)] = TrackId{};
        return true;
    }
    // The slot the UI gave this track, or -1.
    int slotOf(TrackId id) const noexcept {
        if (!id.isValid())
            return -1;
        for (int s = 0; s < kMaxTracks; ++s)
            if (slotOwner_[static_cast<size_t>(s)] == id)
                return s;
        return -1;
    }
    // The reference every other call needs; index -1 if the id is not here.
    ChannelRef trackRef(TrackId id) const noexcept {
        const int slot = slotOf(id);
        return slot >= 0 ? ChannelRef::track(slot, id) : ChannelRef::track(-1, TrackId{});
    }
    // The track's strip, for its values (fader, pan, mute, solo, sends, format, name); nullptr
    // if the id is not here.
    Track* getTrack(TrackId id) noexcept {
        const int slot = slotOf(id);
        return slot >= 0 ? &mixer_.getTrack(slot) : nullptr;
    }
    // UI thread: the strip behind any reference (a track, a bus, the master), for its values;
    // nullptr for a track this engine does not have (any more). It answers by the UI's own slot
    // table, so a track is here from addTrack() on and gone from removeTrack() on. The UI must
    // never use Mixer::getStrip(): that one reads the audio thread's table.
    ChannelStrip* getStrip(ChannelRef ref) noexcept { return const_cast<ChannelStrip*>(uiStrip(ref)); }
    const ChannelStrip* getStrip(ChannelRef ref) const noexcept { return uiStrip(ref); }
    int getNumTracks() const noexcept {
        return static_cast<int>(std::count_if(slotOwner_.begin(), slotOwner_.end(), [](TrackId t) { return t.isValid(); }));
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
    // removed AND collectGarbage() has run; nullptr on failure - also when the chain will be
    // full once the commands queued so far are applied (the effect is deleted right away; the
    // audio thread would have to refuse it, and the observer would dangle).
    InsertEffect* addInsert(ChannelRef channel, int slot, std::unique_ptr<InsertEffect> effect) {
        int* length = uiChainLength(channel);
        if (!effect || !isPrepared() || length == nullptr || *length >= kMaxInserts)
            return nullptr;
        effect->prepare(sampleRate_, maxBlockSize_, kNumChannels);
        effect->publishLatency(); // the UI may ask for it before the audio thread runs
        identify(*effect);
        InsertEffect* observer = effect.get();
        if (!pushCommand(Command::addInsert(channel, slot, effect.release())))
            return nullptr;
        ++*length;
        return observer;
    }

    // Swaps the effect in `slot` for a new one in one block boundary (no gap, no double
    // processing). Use it to change "prepare-time" settings such as a convolution IR. nullptr if
    // the chain will not have that slot once the commands queued so far are applied.
    InsertEffect* replaceInsert(ChannelRef channel, int slot, std::unique_ptr<InsertEffect> effect) {
        const int* length = uiChainLength(channel);
        if (!effect || !isPrepared() || length == nullptr || slot < 0 || slot >= *length)
            return nullptr;
        effect->prepare(sampleRate_, maxBlockSize_, kNumChannels);
        effect->publishLatency(); // the UI may ask for it before the audio thread runs
        identify(*effect);
        InsertEffect* observer = effect.get();
        return pushCommand(Command::replaceInsert(channel, slot, effect.release())) ? observer : nullptr;
    }

    // False if the chain will not have that slot once the commands queued so far are applied.
    bool removeInsert(ChannelRef channel, int slot) {
        int* length = uiChainLength(channel);
        if (length == nullptr || slot < 0 || slot >= *length || !pushCommand(Command::removeInsert(channel, slot)))
            return false;
        --*length;
        return true;
    }
    bool moveInsert(ChannelRef channel, int fromSlot, int toSlot) {
        return pushCommand(Command::moveInsert(channel, fromSlot, toSlot));
    }

    // Replaces the clip list of a track. The list is copied into a new vector here (UI
    // thread); the audio thread swaps one pointer.
    bool setTrackClips(TrackId track, std::vector<Clip> clips) {
        const ChannelRef ref = trackRef(track);
        if (ref.index < 0)
            return false;
        auto* list = new std::vector<Clip>(std::move(clips));
        return pushCommand(Command::replaceClips(ref, list));
    }

    // ---- automation (UI thread = the single producer; chapter 16, Step 5) ----------------------
    // The lane of a parameter, for its mode, its knob, touch() and release(); nullptr if its strip or
    // its effect is not here. The pointer stays valid as long as an insert pointer does (below). An
    // effect added a moment ago is here once the audio thread has taken it.
    AutomationLane* getLane(const LaneTarget& target) noexcept {
        ChannelStrip* strip = getStrip(target.channel);
        if (strip == nullptr)
            return nullptr;
        if (!target.effect.isValid())
            return strip->getOwnLane(target.parameter);
        InsertEffect* effect = strip->getInserts().findById(target.effect);
        return effect != nullptr ? effect->getLane(target.parameter) : nullptr;
    }
    // Publishes a copy of `curve`; the audio thread swaps one pointer at the next block. The UI keeps
    // its own copy to edit: a curve in the engine never changes. False if the strip is not here or the
    // queue is full. An effect's target is checked on the audio thread, in queue order, so this may
    // directly follow addInsert(). If that effect is gone by then, the curve comes back as garbage.
    bool setLaneCurve(const LaneTarget& target, AutomationCurve curve) {
        if (getStrip(target.channel) == nullptr)
            return false;
        syncKnob(target);
        return pushCommand(Command::replaceCurve(target.channel, target.effect, target.parameter, new AutomationCurve(std::move(curve))));
    }
    bool clearLaneCurve(const LaneTarget& target) {
        return getStrip(target.channel) != nullptr &&
               pushCommand(Command::replaceCurve(target.channel, target.effect, target.parameter, nullptr));
    }
    // What the hand did: AutomationRecorder::collect() takes it from here, on the UI thread.
    CaptureQueue& getCaptureQueue() noexcept { return capture_; }
    uint64_t getLanesNotFound() const noexcept { return lanesNotFound_.load(std::memory_order_relaxed); }

    // ---- observing the structure (any thread) ----------------------------------------------
    // The effect currently in `slot` (nullptr if empty). The pointer stays valid until the
    // effect is removed and the calling (UI) thread runs collectGarbage(); so only the thread
    // that collects garbage should hold on to it.
    InsertEffect* getInsert(ChannelRef channel, int slot) const noexcept {
        const ChannelStrip* s = uiStrip(channel);
        return s ? s->getInserts().getInsert(slot) : nullptr;
    }
    int getNumInserts(ChannelRef channel) const noexcept {
        const ChannelStrip* s = uiStrip(channel);
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
    // The time processBlock() takes against the block's budget (chapter 15, Step 3). Its load()
    // and overruns() may be read by any thread; build a CpuMonitor on the UI thread from it.
    const CpuMeter& getCpuMeter() const noexcept { return cpuMeter_; }
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
        const auto start = std::chrono::steady_clock::now();
        const int total = out.getNumSamples();
        int done = 0;
        while (done < total) {
            const int len = std::min(maxBlockSize_, total - done);
            processChunk(out.getSubBlock(done, len));
            done += len;
        }
        const std::chrono::duration<double> spent = std::chrono::steady_clock::now() - start;
        cpuMeter_.addBlock(total, spent.count());
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
            mixer_.process(out.getSubBlock(done, covered), segments, numSegments, transport_.getTempo(), transport_.getPosition());
            done += covered;
        }
        blocksProcessed_.fetch_add(1, std::memory_order_relaxed);
    }

    // Takes a command only when the garbage queue has room for all it may hand back (removing
    // a track returns up to kMaxGarbagePerCommand objects). Otherwise the rest waits for the
    // next block: nothing is ever leaked, and the audio thread never blocks.
    void applyCommands() noexcept {
        Command c;
        while (garbage_.capacity() - garbage_.size() >= static_cast<std::size_t>(kMaxGarbagePerCommand) &&
               commands_.pop(c)) {
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
        case CommandType::AddTrack:
            mixer_.activateTrack(c.channel.index, c.channel.trackId); // a wrong slot or id: nothing happens
            break;
        case CommandType::RemoveTrack:
            mixer_.deactivateTrack(c.channel, [this](auto* object) {
                using T = std::remove_const_t<std::remove_pointer_t<decltype(object)>>;
                discard(Garbage::of(const_cast<T*>(object)));
            });
            break;
        case CommandType::ReplaceCurve: {
            const AutomationCurve* old = c.curve; // no such lane: hand the new curve straight back
            if (AutomationLane* lane = audioLane(strip, c.effectId, c.slot))
                old = lane->exchangeCurve(c.curve);
            else
                lanesNotFound_.fetch_add(1, std::memory_order_relaxed);
            if (old)
                discard(Garbage::of(const_cast<AutomationCurve*>(old)));
            break;
        }
        case CommandType::None: break;
        }
    }

    // Audio thread: the lane a command means, by the audio thread's own view of the chain.
    static AutomationLane* audioLane(ChannelStrip* strip, EffectId effect, int parameter) noexcept {
        if (strip == nullptr)
            return nullptr;
        if (!effect.isValid())
            return strip->getOwnLane(parameter);
        InsertEffect* fx = strip->getInserts().findById(effect);
        return fx != nullptr ? fx->getLane(parameter) : nullptr;
    }

    // UI thread: gives a new effect its id (or keeps the one undo brought back) and its lanes.
    void identify(InsertEffect& effect) {
        if (effect.getEffectId().isValid())
            nextEffectId_ = std::max(nextEffectId_, effect.getEffectId().value + 1);
        else
            effect.setEffectId(EffectId{nextEffectId_++});
        effect.prepareLanes(sampleRate_);
    }

    // UI thread: the first curve of an effect's parameter starts from the parameter's value now, which
    // is where it returns when the curve goes away. (A strip's knob is always current: setGainDb().)
    void syncKnob(const LaneTarget& target) noexcept {
        if (!target.effect.isValid())
            return;
        const ChannelStrip* strip = uiStrip(target.channel);
        InsertEffect* effect = strip != nullptr ? strip->getInserts().findById(target.effect) : nullptr;
        AutomationLane* lane = effect != nullptr ? effect->getLane(target.parameter) : nullptr;
        if (lane != nullptr && !lane->isActive())
            lane->setKnobValue(effect->getParameterNormalized(target.parameter));
    }

    // UI thread: the strip behind a reference, by the UI's OWN slot table (never by the audio
    // thread's). nullptr if the UI did not give that slot to that id.
    const ChannelStrip* uiStrip(ChannelRef ref) const noexcept {
        if (ref.kind == ChannelKind::Track)
            return (ref.index >= 0 && ref.index < kMaxTracks && ref.trackId.isValid() &&
                    slotOwner_[static_cast<size_t>(ref.index)] == ref.trackId)
                       ? &mixer_.getTrack(ref.index)
                       : nullptr;
        return mixer_.getStrip(ref); // buses and master never change
    }

    // UI thread: the length `ref`'s insert chain WILL have once every command queued so far is
    // applied - the audio thread applies them in order, so the UI can tell now whether an insert
    // command can work. nullptr if the UI does not own that channel.
    int* uiChainLength(ChannelRef ref) noexcept {
        if (uiStrip(ref) == nullptr)
            return nullptr;
        const int index = ref.kind == ChannelKind::Track ? ref.index
                          : ref.kind == ChannelKind::Bus ? kMaxTracks + ref.index
                                                         : kMaxTracks + kNumBuses;
        return &uiChainLengths_[static_cast<size_t>(index)];
    }

    // Audio thread: hand an object to the UI thread for deletion. applyCommands() makes sure
    // there is room; should the queue still be full, the object leaks and is counted (we may not
    // delete it here: that would free memory on the audio thread). The tests expect 0.
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
        else if (c.type == CommandType::ReplaceCurve)
            delete c.curve;
        c.effect = nullptr;
        c.clips = nullptr;
        c.curve = nullptr;
    }

    double sampleRate_ = 0.0;
    int maxBlockSize_ = 0;
    std::atomic<bool> prepared_{false};
    std::atomic<bool> resetRequested_{false};

    Transport transport_;
    Mixer mixer_;
    CpuMeter cpuMeter_;
    std::array<TrackId, kMaxTracks> slotOwner_{}; // UI thread only: which id the UI gave each slot
    std::array<int, kMaxTracks + kNumBuses + 1> uiChainLengths_{}; // UI thread only: see uiChainLength()
    SpscQueue<Command> commands_;
    SpscQueue<Garbage> garbage_;
    CaptureQueue capture_;
    uint32_t nextEffectId_ = 1; // UI thread only

    std::atomic<uint64_t> blocksProcessed_{0};
    std::atomic<uint64_t> commandsApplied_{0};
    std::atomic<uint64_t> commandsDropped_{0};
    std::atomic<uint64_t> garbageLeaked_{0};
    std::atomic<uint64_t> lanesNotFound_{0};
};

} // namespace dawlab::engine
