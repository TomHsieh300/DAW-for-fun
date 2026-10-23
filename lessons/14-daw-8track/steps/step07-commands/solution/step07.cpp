// Step 7 (solution): the SPSC ring buffer and Engine::applyCommands().
//
// Build and run:  l14_step07_solution   (ctest, label "solution")
// Your version:   starter/step07.cpp -> l14_step07_starter

#include "../minidaw.h"

namespace minidaw {

template <typename T>
bool SpscQueue<T>::push(const T& item) noexcept {
    const std::size_t tail = tail_.load(std::memory_order_relaxed); // only we write tail_
    const std::size_t next = increment(tail);
    if (next == head_.load(std::memory_order_acquire))
        return false; // full
    slots_[tail] = item;                          // 1. write the slot
    tail_.store(next, std::memory_order_release); // 2. publish it
    return true;
}

template <typename T>
bool SpscQueue<T>::pop(T& out) noexcept {
    const std::size_t head = head_.load(std::memory_order_relaxed); // only we write head_
    if (head == tail_.load(std::memory_order_acquire))              // 3. sees the publication...
        return false;                                               // empty
    out = slots_[head];                                             // 4. ...so the slot is complete
    head_.store(increment(head), std::memory_order_release);        // 5. hand the slot back
    return true;
}

void Engine::applyCommands() noexcept {
    Command c;
    while (commands_.pop(c)) {
        ChannelStrip* strip = mixer_.getStrip(c.channel);
        switch (c.type) {
        case CommandType::AddInsert: {
            InsertEffect* rejected = strip ? strip->getInserts().insertAt(c.slot, c.effect) : c.effect;
            if (rejected)
                discard(Garbage::of(rejected)); // chain full or no such strip
            break;
        }
        case CommandType::RemoveInsert:
            if (strip)
                if (InsertEffect* removed = strip->getInserts().removeAt(c.slot))
                    discard(Garbage::of(removed));
            break;
        case CommandType::ReplaceClips: {
            const std::vector<Clip>* old = c.clips; // invalid target: the new list goes straight back
            if (c.channel.kind == ChannelKind::Track && strip != nullptr)
                old = static_cast<Track*>(strip)->exchangeClips(c.clips);
            if (old)
                discard(Garbage::of(const_cast<std::vector<Clip>*>(old)));
            break;
        }
        case CommandType::None: break;
        }
        commandsApplied_.fetch_add(1, std::memory_order_relaxed);
    }
}

} // namespace minidaw

#include "../check.h"
