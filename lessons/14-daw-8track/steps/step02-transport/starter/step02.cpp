// Step 2 (starter): the musical grid and Transport::advance().
//
// Build and run:  l14_step02_starter   (prints [FAIL] until the three TODOs are done)
// Reference:      ../solution/step02.cpp, docs/step-02-transport.md ("Your turn")
//
// Everything else - the atomics, the engine that plays the segments - is in ../minidaw.h.

#include "../minidaw.h"

namespace minidaw {

int64_t Transport::barsBeatsToSamples(const BarsBeats& p) const noexcept {
    // TODO 1: beats = (bar - 1) * numerator + (beat - 1) + tick / kTicksPerBeat;
    //         return round(beats * getSamplesPerBeat())   (std::llround: round only once)
    (void)p;
    return 0;
}

BarsBeats Transport::samplesToBarsBeats(int64_t samples) const noexcept {
    // TODO 2: totalBeats = samples / getSamplesPerBeat() + 1e-9; wholeBeats = floor(totalBeats);
    //         bar = wholeBeats / numerator + 1, beat = wholeBeats % numerator + 1,
    //         tick = floor(fraction * kTicksPerBeat), at most kTicksPerBeat - 1.
    (void)samples;
    return {};
}

int Transport::advance(int numSamples, TimelineSegment* segments, int& samplesCovered) noexcept {
    // TODO 3a: apply a pending seek: seek = pendingSeek_.exchange(-1); if (seek >= 0) store it
    //          in position_.
    // TODO 3b: samplesCovered = numSamples; if not playing, return 0 segments.
    // TODO 3c: while (offset < numSamples && count < kMaxSegmentsPerBlock): the next segment
    //          starts at `pos`; when the loop is active (enabled, end > start) and pos < loopEnd
    //          it may not cross loopEnd; when pos reaches loopEnd, continue at loopStart.
    // TODO 3d: samplesCovered = offset; store the new position; return the segment count.
    (void)numSamples;
    (void)segments;
    samplesCovered = 0;
    return 0;
}

} // namespace minidaw

#include "../check.h"
