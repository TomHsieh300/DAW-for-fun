#pragma once
// Exercise 3 - Write only what a take can use.
//
// Step 4 writes one unbroken stream from the moment the transport plays. With a punch-in at bar 41
// and the transport started at bar 1, the first 80 seconds go to disk and are never part of a take:
// 15 MB a minute of mono for nothing (Step 4's docs leave this open). This exercise drops them on the
// audio thread, before the ring - but keeps a PREROLL of frames before every take, so that the user
// can still move a clip's left edge a little.
//
// Dropping the first `skip` frames is the same as a stream that began `skip` frames later. So the
// recording keeps Step 4's takesOf() unchanged, with a stamped position moved by `skip`:
//
//   takesOf(stamped, L, range, n)  ==  takesOf(stamped + skip, L, range, n - skip)
//                                      with every take's skipFrames smaller by `skip`
//
// as long as the moved position still means the same thing - in a loop, it must stay before the
// loop's end, or the transport would no longer count as looping (RecordRange::loopsFrom()).
//
// Two pieces, both for the audio thread (no allocation, no waiting):
//
//   prerollFor(stamped, latencies, range, preroll)  how many frames to drop, when the recording begins
//   StreamGate                                       drops them, block by block

#include "model.h"

namespace minidaw {

struct Preroll {
    std::int64_t skip = 0;            // the stream's first frames that no take needs
    std::int64_t stampedPosition = 0; // the stamped position of the first frame that is kept
};

// ---- YOUR TURN (starter/ex3_preroll.cpp) -------------------------------------------------------
// The first take begins at frame  first = visibleWindow(range, stamped).from - (stamped - L)  of the
// stream (L = latencies.total()). skip = first - preroll, but not below 0, and - if the transport loops
// from `stamped` - small enough that stamped + skip stays before loopEnd. An empty window (a punch
// outside the loop): nothing to gain, skip = 0.
Preroll prerollFor(std::int64_t stampedPosition, const Latencies& latencies, const RecordRange& range, std::int64_t preroll) noexcept;

// Drops the first `skip` frames of a stream, whatever the block sizes are.
class StreamGate {
public:
    // Before the first block of a recording.
    void arm(std::int64_t skip) noexcept;
    // For a block of n frames: how many of its FIRST frames to drop (the rest are pushed into the ring).
    int drop(int n) noexcept;
    // How many frames were dropped so far.
    std::int64_t dropped() const noexcept { return dropped_; }

private:
    std::int64_t remaining_ = 0;
    std::int64_t dropped_ = 0;
};

} // namespace minidaw
