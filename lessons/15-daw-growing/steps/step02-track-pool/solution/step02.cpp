// Step 2 (solution): a pool of tracks.
//
// Build and run:  l15_step02_solution   (ctest, label "solution")
// Your version:   starter/step02.cpp -> l15_step02_starter

#include "../minidaw.h"

#include <memory>
#include <utility>

namespace minidaw {

void Engine::applyCommand(const Command& c) noexcept {
    Slot* slot = inRange(c.slot) ? &slots_[static_cast<size_t>(c.slot)] : nullptr;
    // A command acts only on the slot that still holds its track (the slot is a hint, the id is the truth).
    const bool holdsTrack = slot != nullptr && c.id.isValid() && slot->id == c.id;
    switch (c.type) {
        case CommandType::AddTrack:
            if (slot != nullptr && !slot->id.isValid() && c.id.isValid())
                *slot = Slot{c.id, c.source, c.gain};
            else
                returnAsGarbage(c.source); // never overwrite a live track, never leak the source
            break;
        case CommandType::RemoveTrack:
            if (holdsTrack) {
                returnAsGarbage(slot->source);
                *slot = Slot{};
            }
            break;
        case CommandType::SetGain:
            if (holdsTrack)
                slot->gain = c.gain;
            break;
        case CommandType::None:
            break;
    }
}

TrackId TrackManager::addTrack(std::string name, float level) {
    collectGarbage();
    const int slot = freeSlot();
    if (slot < 0)
        return TrackId{}; // the pool is full: nothing made, nothing changed
    auto source = std::make_unique<ToneSource>(level);
    const TrackId id = list_.add(std::move(name));
    if (!engine_.send(Command{CommandType::AddTrack, id, slot, source.get(), 1.0f})) {
        list_.remove(id); // the queue is full: undo, the unique_ptr deletes the source
        return TrackId{};
    }
    source.release(); // the engine owns it now; it comes back as garbage
    slotOwner_[static_cast<size_t>(slot)] = id;
    return id;
}

bool TrackManager::removeTrack(TrackId id) {
    collectGarbage();
    const int slot = slotOf(id);
    if (slot < 0)
        return false;
    if (!engine_.send(Command{CommandType::RemoveTrack, id, slot, nullptr, 1.0f}))
        return false;
    list_.remove(id);
    slotOwner_[static_cast<size_t>(slot)] = TrackId{}; // reusable at once: the queue keeps the order
    return true;
}

} // namespace minidaw

#include "../check.h"
