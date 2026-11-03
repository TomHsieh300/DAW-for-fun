#pragma once
// Commands: the messages the UI thread sends to the audio thread for *structural* changes,
// and the "garbage" tickets the audio thread sends back (Step 7).
//
// Two kinds of change, two mechanisms:
//   - Values (fader, pan, mute, send level, effect parameters, tempo...) are single numbers.
//     They live in atomics; the UI writes, the audio thread reads. No queue needed.
//   - Structure (insert an effect, remove one, swap the clip list of a track) needs memory
//     to be allocated or freed, and several pointers to change together. That goes through a
//     command queue, and the audio thread applies it between two blocks.
//
// The rule that makes it real-time safe: the audio thread never allocates and never frees.
//   - Anything that needs memory (a new effect, a new clip list) is allocated and prepared by
//     the UI thread and travels to the audio thread as a raw pointer inside a Command.
//   - Anything the audio thread stops using (the removed effect, the old clip list) is put
//     into the garbage queue and deleted by the UI thread in Engine::collectGarbage().
//
// Both queues are SpscQueue<T>, so Command and Garbage must be trivially copyable: plain
// fields and raw pointers only.

#include <cstdint>
#include <vector>

namespace dawlab::engine {

class InsertEffect;
struct Clip;

enum class ChannelKind : uint8_t { Track, Bus, Master };

struct ChannelRef {
    ChannelKind kind = ChannelKind::Track;
    int index = 0;

    static ChannelRef track(int i) noexcept { return {ChannelKind::Track, i}; }
    static ChannelRef bus(int i) noexcept { return {ChannelKind::Bus, i}; }
    static ChannelRef master() noexcept { return {ChannelKind::Master, 0}; }

    bool operator==(const ChannelRef& o) const noexcept { return kind == o.kind && index == o.index; }
};

enum class CommandType : uint8_t {
    None,
    AddInsert,     // channel, slot (-1 = append), effect   (effect ownership -> engine)
    RemoveInsert,  // channel, slot                         (removed effect -> garbage)
    ReplaceInsert, // channel, slot, effect                 (old effect -> garbage)
    MoveInsert,    // channel, slot (from), slot2 (to)
    ReplaceClips,  // channel (a track), clips              (old list -> garbage)
};

struct Command {
    CommandType type = CommandType::None;
    ChannelRef channel;
    int slot = -1;
    int slot2 = -1;
    InsertEffect* effect = nullptr;
    std::vector<Clip>* clips = nullptr;

    static Command addInsert(ChannelRef ch, int slot, InsertEffect* effect) noexcept {
        Command c;
        c.type = CommandType::AddInsert;
        c.channel = ch;
        c.slot = slot;
        c.effect = effect;
        return c;
    }
    static Command removeInsert(ChannelRef ch, int slot) noexcept {
        Command c;
        c.type = CommandType::RemoveInsert;
        c.channel = ch;
        c.slot = slot;
        return c;
    }
    static Command replaceInsert(ChannelRef ch, int slot, InsertEffect* effect) noexcept {
        Command c;
        c.type = CommandType::ReplaceInsert;
        c.channel = ch;
        c.slot = slot;
        c.effect = effect;
        return c;
    }
    static Command moveInsert(ChannelRef ch, int from, int to) noexcept {
        Command c;
        c.type = CommandType::MoveInsert;
        c.channel = ch;
        c.slot = from;
        c.slot2 = to;
        return c;
    }
    static Command replaceClips(int trackIndex, std::vector<Clip>* clips) noexcept {
        Command c;
        c.type = CommandType::ReplaceClips;
        c.channel = ChannelRef::track(trackIndex);
        c.clips = clips;
        return c;
    }
};

// A type-erased "please delete this on a non-audio thread" ticket. The deleter is a plain
// function pointer generated per type, so the ticket stays trivially copyable.
struct Garbage {
    void* ptr = nullptr;
    void (*deleter)(void*) = nullptr;

    template <typename T>
    static Garbage of(T* object) noexcept {
        Garbage g;
        g.ptr = object;
        g.deleter = [](void* p) { delete static_cast<T*>(p); };
        return g;
    }

    // Non-audio thread.
    void destroy() noexcept {
        if (ptr != nullptr && deleter != nullptr)
            deleter(ptr);
        ptr = nullptr;
        deleter = nullptr;
    }
};

} // namespace dawlab::engine
