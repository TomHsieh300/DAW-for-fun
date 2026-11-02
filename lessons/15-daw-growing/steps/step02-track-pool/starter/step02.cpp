// Step 2 (starter): a pool of tracks.
//
// Build and run:  l15_step02_starter   (prints [FAIL] until all three TODOs are done)
// Reference:      ../solution/step02.cpp, docs/step-02-track-pool.md ("Your turn")
//
// Engine, TrackManager (with the provided setGainDb() as the pattern), Command and Slot are in
// ../minidaw.h.

#include "../minidaw.h"

namespace minidaw {

void Engine::applyCommand(const Command& c) noexcept {
    // TODO 1 (audio thread: never allocate, never free, never block).
    //   AddTrack:    if c.slot is in range (inRange) and slots_[c.slot] is free, put
    //                Slot{c.id, c.source, c.gain} there; otherwise returnAsGarbage(c.source) -
    //                the source must not leak, and the track already in the slot must stay.
    //   RemoveTrack: only if the slot is in range and holds c.id: returnAsGarbage(its source) and
    //                make the slot free (Slot{}). Any other id: a stale or repeated command, ignore.
    //   SetGain:     only if the slot is in range and holds c.id: set its gain to c.gain.
    (void)c;
}

TrackId TrackManager::addTrack(std::string name, float level) {
    // TODO 2 (UI thread). All or nothing - follow setGainDb():
    //   - collectGarbage() first (it keeps the garbage queue short);
    //   - take freeSlot(); if it is -1 the pool is full: return TrackId{} BEFORE making anything;
    //   - make the source (new ToneSource(level)) and the id (list_.add(name));
    //   - send Command{CommandType::AddTrack, id, slot, source, 1.0f};
    //   - if the send fails, undo both (delete the source, list_.remove(id)) and return TrackId{};
    //   - otherwise the engine owns the source now: record slotOwner_[slot] = id, return id.
    (void)name;
    (void)level;
    (void)engine_;
    return TrackId{};
}

bool TrackManager::removeTrack(TrackId id) {
    // TODO 3 (UI thread). collectGarbage(); find the slot (slotOf); no slot -> false. Send
    // Command{CommandType::RemoveTrack, id, slot}; if the send fails -> false and change nothing.
    // Otherwise remove the track from list_ and free slotOwner_[slot] (TrackId{}) - right away:
    // the queue keeps its order, so an AddTrack for this slot always reaches the audio thread
    // after this RemoveTrack. Do NOT delete the source here; it comes back as garbage.
    (void)id;
    return false;
}

} // namespace minidaw

#include "../check.h"
