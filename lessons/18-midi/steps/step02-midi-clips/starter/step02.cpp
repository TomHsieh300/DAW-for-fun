// Step 2 (starter): playing a MIDI clip.
//
// Build and run:  l18_step02_starter   (prints [FAIL] until all four TODOs are done)
// Reference:      ../solution/step02.cpp, docs/step-02-midi-clips.md ("Your turn")
//
// MidiClip, MiniTransport, Block and ClipPlayer are in ../minidaw.h, with Step 1 solved.

#include "../minidaw.h"

namespace minidaw {

bool MidiClip::place(const Note& n, std::int64_t& on, std::int64_t& off) const noexcept {
    // TODO 1: Not in the window [offset, offset + length): false. Otherwise on = timelineTick(n.start),
    //         off = the timeline tick of its end, but not after end().
    (void)n;
    (void)on;
    (void)off;
    return false;
}

void ClipPlayer::allNotesOff(int offset, std::vector<BlockEvent>& out) noexcept {
    // TODO 2: A note off for each of the numSounding_ entries of sounding_, then an empty table.
    (void)offset;
    (void)out;
}

void ClipPlayer::chase(const std::vector<MidiClip>& clips, std::int64_t tick, int offset, std::vector<BlockEvent>& out) const noexcept {
    // TODO 3: For each unmuted clip with start <= tick < end(): the material tick is tick - start + offset.
    //         Keep the last value of each (channel, number) among the points before it - a fixed array
    //         of 16 x 128 values, -1 for none, no allocation - and send them in that order.
    (void)clips;
    (void)tick;
    (void)offset;
    (void)out;
}

void ClipPlayer::process(const std::vector<MidiClip>& clips, const Block& block, const Tempo& tempo, std::vector<BlockEvent>& out) noexcept {
    // TODO 4: The steps in the header, segment by segment. Collect a segment's events in scratch_ as
    //         Pending{tick, rank, seq, message} (never more than its capacity, and always with room for a note
    //         off of every sounding note), std::sort them by tick, rank, seq, and write them to `out`.
    (void)clips;
    (void)block;
    (void)tempo;
    out.clear();
}

} // namespace minidaw

#include "../check.h"
