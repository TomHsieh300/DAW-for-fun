#pragma once
// minidaw.h - Step 2 snapshot of chapter 15: a pool of tracks.
//
// Step 1 gave every track a stable id, on the UI thread. This step adds the audio side and lets
// the user add and delete tracks WHILE the audio thread runs - still without allocating, freeing
// or locking on the audio thread. The idea: allocate the room for kMaxTracks tracks once, up
// front, and let adding a track mean "start using a free slot".
//
//   UI thread                                         audio thread
//   TrackManager                                      Engine
//     TrackList (Step 1)     --- Command queue --->     slots_[kMaxTracks]   (written ONLY here)
//     slotOwner_ (id per slot)                          process(): commands, then render
//                            <--- Garbage queue ---     (sources of removed tracks)
//
// Two rules make this safe:
//   - One writer per piece of state. The audio thread is the only one that writes the slots; the
//     UI thread is the only one that writes the TrackList and its own copy of who owns which slot.
//     They talk only through the two SPSC queues (chapter 14, Step 7).
//   - The slot is a hint, the id is the truth. Every command names its track by id AND slot. The
//     audio thread acts only if that slot still holds that id, so a command for a deleted track
//     can never touch the track that took its slot over.
//
// YOUR TURN (Step 2): write Engine::applyCommand() (audio thread), TrackManager::addTrack() and
// TrackManager::removeTrack() (UI thread) in starter/step02.cpp. They are only declared here.
// TrackManager::setGainDb() is provided and shows the pattern.

#include <dawlab/DspMath.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

namespace minidaw {

// ================================================================================================
// Step 1 (solved): stable track ids
// ================================================================================================

struct TrackId {
    std::uint32_t value = 0;

    constexpr bool isValid() const noexcept { return value != 0; }
    friend constexpr bool operator==(TrackId a, TrackId b) noexcept { return a.value == b.value; }
    friend constexpr bool operator!=(TrackId a, TrackId b) noexcept { return a.value != b.value; }
};

struct TrackInfo {
    TrackId id;
    std::string name;
    float gainDb = 0.0f;
};

class TrackList {
public:
    TrackId add(std::string name) {
        const TrackId id{nextId_++};
        tracks_.push_back(TrackInfo{id, std::move(name), 0.0f});
        return id;
    }
    bool remove(TrackId id) {
        const int i = indexOf(id);
        if (i < 0)
            return false;
        tracks_.erase(tracks_.begin() + i);
        return true;
    }
    int indexOf(TrackId id) const noexcept {
        if (!id.isValid())
            return -1;
        for (size_t i = 0; i < tracks_.size(); ++i)
            if (tracks_[i].id == id)
                return static_cast<int>(i);
        return -1;
    }
    bool move(TrackId id, int newIndex) {
        const int from = indexOf(id);
        if (from < 0)
            return false;
        const int to = std::clamp(newIndex, 0, size() - 1);
        TrackInfo track = std::move(tracks_[static_cast<size_t>(from)]);
        tracks_.erase(tracks_.begin() + from);
        tracks_.insert(tracks_.begin() + to, std::move(track));
        return true;
    }

    int size() const noexcept { return static_cast<int>(tracks_.size()); }
    const std::vector<TrackInfo>& tracks() const noexcept { return tracks_; }
    TrackInfo* find(TrackId id) noexcept {
        const int i = indexOf(id);
        return i >= 0 ? &tracks_[static_cast<size_t>(i)] : nullptr;
    }

private:
    std::vector<TrackInfo> tracks_;
    std::uint32_t nextId_ = 1;
};

// ================================================================================================
// Step 2: the pool
// ================================================================================================

// Small here so that the check can fill the pool; the finished app uses 128 (see the step
// document: the real limit is CPU time, and Step 3 measures it).
constexpr int kMaxTracks = 16;
constexpr int kMaxBlockSize = 256;
constexpr std::size_t kCommandCapacity = 64;
// Every command returns at most one piece of garbage, and the audio thread takes a command only
// when the garbage queue has room for it (Engine::process), so this never overflows.
constexpr std::size_t kGarbageCapacity = kCommandCapacity + kMaxTracks;

// A track's sound, kept trivial so the mix is easy to check: a constant level (DC). It stands in
// for everything a real track owns on the heap (clip list, insert chain...). Made and deleted on
// the UI thread; owned by the engine from AddTrack until it comes back as garbage.
struct ToneSource {
    explicit ToneSource(float lvl) noexcept : level(lvl) { live.fetch_add(1, std::memory_order_relaxed); }
    ~ToneSource() { live.fetch_sub(1, std::memory_order_relaxed); }
    ToneSource(const ToneSource&) = delete;
    ToneSource& operator=(const ToneSource&) = delete;

    const float level;
    static inline std::atomic<int> live{0}; // how many exist right now (the check looks for leaks)
};

// The SPSC queue of chapter 14, Step 7, unchanged.
template <typename T>
class SpscQueue {
    static_assert(std::is_trivially_copyable_v<T>, "SpscQueue<T> requires a trivially copyable T");

public:
    explicit SpscQueue(std::size_t capacity) : slots_(capacity + 1) {}
    SpscQueue(const SpscQueue&) = delete;
    SpscQueue& operator=(const SpscQueue&) = delete;

    bool push(const T& item) noexcept {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t next = increment(tail);
        if (next == head_.load(std::memory_order_acquire))
            return false;
        slots_[tail] = item;
        tail_.store(next, std::memory_order_release);
        return true;
    }
    bool pop(T& out) noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire))
            return false;
        out = slots_[head];
        head_.store(increment(head), std::memory_order_release);
        return true;
    }
    // Exact for the thread that owns one end, an upper bound for the producer (the consumer may
    // have popped since), a lower bound for the consumer.
    std::size_t size() const noexcept {
        const std::size_t head = head_.load(std::memory_order_acquire);
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        return tail >= head ? tail - head : slots_.size() - head + tail;
    }
    std::size_t capacity() const noexcept { return slots_.size() - 1; }

private:
    std::size_t increment(std::size_t i) const noexcept { return (i + 1 == slots_.size()) ? 0 : i + 1; }

    std::vector<T> slots_;
    std::atomic<std::size_t> head_{0};
    std::atomic<std::size_t> tail_{0};
};

enum class CommandType : std::uint8_t {
    None,
    AddTrack,    // id, slot, source        (the source's ownership -> engine)
    RemoveTrack, // id, slot                (the source -> garbage)
    SetGain,     // id, slot, gain (linear)
};

struct Command {
    CommandType type = CommandType::None;
    TrackId id;
    int slot = -1;
    ToneSource* source = nullptr;
    float gain = 1.0f;
};

// A ToneSource the audio thread is done with; the UI thread deletes it.
struct Garbage {
    ToneSource* source = nullptr;
};

// One slot of the pool. Written and read by the audio thread only, so nothing here is atomic.
struct Slot {
    TrackId id; // invalid = the slot is free
    ToneSource* source = nullptr;
    float gain = 1.0f;
};

class Engine {
public:
    Engine() : commands_(kCommandCapacity), garbage_(kGarbageCapacity) {}
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    // Runs after the audio thread has stopped: deletes what is still in the slots, in the
    // command queue and in the garbage queue.
    ~Engine() {
        for (Slot& s : slots_)
            delete s.source;
        Command c;
        while (commands_.pop(c))
            if (c.type == CommandType::AddTrack)
                delete c.source;
        Garbage g;
        while (garbage_.pop(g))
            delete g.source;
    }

    // ---- UI thread --------------------------------------------------------------------------------
    // False when the queue is full: the command was NOT sent.
    bool send(const Command& c) noexcept { return commands_.push(c); }
    bool takeGarbage(Garbage& g) noexcept { return garbage_.pop(g); }

    // ---- audio thread -----------------------------------------------------------------------------
    // Applies the pending commands, then writes numSamples (<= kMaxBlockSize) samples of the mix.
    void process(float* out, int numSamples) noexcept {
        Command c;
        // Take a command only when its garbage is sure to fit: never block, never drop garbage.
        while (garbage_.size() < garbage_.capacity() && commands_.pop(c))
            applyCommand(c);
        render(out, numSamples);
    }

    // YOUR TURN (starter/step02.cpp). Applies one command to slots_; never allocates, never frees.
    //   AddTrack     the slot must be in range and free; otherwise the source goes back as garbage
    //   RemoveTrack  only if the slot holds this id: the source goes back as garbage, the slot is free
    //   SetGain      only if the slot holds this id
    // Anything else (a stale or repeated command) changes nothing.
    void applyCommand(const Command& c) noexcept;

    // ---- inspection: only while the audio thread is NOT running ---------------------------------
    TrackId idInSlot(int slot) const noexcept { return inRange(slot) ? slots_[static_cast<size_t>(slot)].id : TrackId{}; }
    int activeTracks() const noexcept {
        return static_cast<int>(std::count_if(slots_.begin(), slots_.end(), [](const Slot& s) { return s.id.isValid(); }));
    }

private:
    static bool inRange(int slot) noexcept { return slot >= 0 && slot < kMaxTracks; }
    void returnAsGarbage(ToneSource* source) noexcept {
        if (source != nullptr)
            garbage_.push(Garbage{source}); // cannot fail: process() checked for room
    }
    void render(float* out, int numSamples) const noexcept {
        float sum = 0.0f;
        for (const Slot& s : slots_)
            if (s.id.isValid())
                sum += s.source->level * s.gain;
        std::fill(out, out + std::clamp(numSamples, 0, kMaxBlockSize), sum);
    }

    std::array<Slot, kMaxTracks> slots_{};
    SpscQueue<Command> commands_;
    SpscQueue<Garbage> garbage_;
};

// The UI thread's side: the track list, and which slot each track lives in.
class TrackManager {
public:
    explicit TrackManager(Engine& engine) noexcept : engine_(engine) {}
    TrackManager(const TrackManager&) = delete;
    TrackManager& operator=(const TrackManager&) = delete;

    // ---- YOUR TURN (starter/step02.cpp) ---------------------------------------------------------
    // Adds a track playing `level`. All or nothing: returns an invalid id and changes nothing (no
    // track in the list, no slot taken, no source left over) when the pool or the queue is full.
    TrackId addTrack(std::string name, float level);
    // Removes the track. False and no change if there is no such track or the queue is full.
    // A second remove of the same id is a harmless false.
    bool removeTrack(TrackId id);

    // ---- provided -----------------------------------------------------------------------------
    // The pattern for both functions above: find the slot, send, and only when the send worked,
    // change the UI side.
    bool setGainDb(TrackId id, float gainDb) {
        collectGarbage();
        TrackInfo* track = list_.find(id);
        const int slot = slotOf(id);
        if (track == nullptr || slot < 0)
            return false;
        if (!engine_.send(Command{CommandType::SetGain, id, slot, nullptr, dawlab::dbToGain(gainDb)}))
            return false;
        track->gainDb = gainDb;
        return true;
    }

    // Deletes the sources the audio thread has handed back; returns how many.
    int collectGarbage() noexcept {
        int n = 0;
        Garbage g;
        while (engine_.takeGarbage(g)) {
            delete g.source;
            ++n;
        }
        return n;
    }

    const TrackList& list() const noexcept { return list_; }
    // The slot the UI has assigned to this track, or -1.
    int slotOf(TrackId id) const noexcept {
        if (!id.isValid())
            return -1;
        for (int s = 0; s < kMaxTracks; ++s)
            if (slotOwner_[static_cast<size_t>(s)] == id)
                return s;
        return -1;
    }
    // The lowest slot the UI has not assigned, or -1 when the pool is full.
    int freeSlot() const noexcept {
        for (int s = 0; s < kMaxTracks; ++s)
            if (!slotOwner_[static_cast<size_t>(s)].isValid())
                return s;
        return -1;
    }

private:
    Engine& engine_;
    TrackList list_;
    std::array<TrackId, kMaxTracks> slotOwner_{}; // the UI's copy of who lives in which slot
};

} // namespace minidaw
