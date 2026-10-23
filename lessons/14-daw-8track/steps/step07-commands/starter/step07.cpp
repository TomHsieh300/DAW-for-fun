// Step 7 (starter): the SPSC ring buffer and Engine::applyCommands().
//
// Build and run:  l14_step07_starter   (prints [FAIL] until the three TODOs are done)
// Reference:      ../solution/step07.cpp, docs/step-07-commands.md ("Your turn")
//
// Command, Garbage, Engine::addInsert / removeInsert / setTrackClips / collectGarbage and
// discard() are in ../minidaw.h.

#include "../minidaw.h"

namespace minidaw {

template <typename T>
bool SpscQueue<T>::push(const T& item) noexcept {
    // TODO 1: tail = tail_ (relaxed: only the producer writes it); next = increment(tail);
    //         full if next == head_ (load with memory_order_acquire) -> return false.
    //         Write slots_[tail] = item FIRST, then tail_.store(next, memory_order_release).
    (void)item;
    return false;
}

template <typename T>
bool SpscQueue<T>::pop(T& out) noexcept {
    // TODO 2: head = head_ (relaxed); empty if head == tail_ (acquire) -> return false.
    //         out = slots_[head]; then head_.store(increment(head), memory_order_release).
    (void)out;
    return false;
}

void Engine::applyCommands() noexcept {
    // TODO 3: while (commands_.pop(c)): strip = mixer_.getStrip(c.channel); then by c.type
    //   AddInsert    rejected = strip ? strip->getInserts().insertAt(c.slot, c.effect) : c.effect;
    //                if (rejected) discard(Garbage::of(rejected));
    //   RemoveInsert if strip and removeAt(c.slot) returned an effect: discard(Garbage::of(it));
    //   ReplaceClips if c.channel is a track and strip exists:
    //                old = static_cast<Track*>(strip)->exchangeClips(c.clips), else old = c.clips;
    //                if (old) discard(Garbage::of(const_cast<std::vector<Clip>*>(old)));
    //   and count every command: commandsApplied_.fetch_add(1).
}

} // namespace minidaw

#include "../check.h"
