// Chapter 19, Step 2 (starter): a transport on a tempo map.
//
// Fill in the TODOs. Until then the self-check prints FAIL (expected). The solution is
// solution/step02.cpp. What the functions must do is in ../minidaw.h.

#include "../minidaw.h"

namespace minidaw {

void Transport::setMap(TempoMap next) noexcept {
    // TODO: the playhead's exact tick under the old map (tickAt), then that tick's sample under the new one.
    map_ = std::move(next);
}

int Transport::advance(int numSamples, Segment* out, int& covered) noexcept {
    // TODO: chapter 14's advance() - segments up to the loop's end, the wrap to its start - with the loop's samples
    //       from the map, and every segment's ticks: on from nextTick_, or after a jump from jumpTick_ (or the first
    //       tick of its first sample); a segment that ends at the loop's end ends at its tick. jumpTo() at a wrap.
    covered = std::max(numSamples, 0);
    if (!playing_ || numSamples <= 0)
        return 0;
    out[0] = {position_, 0, numSamples, 0, 0, false};
    position_ += numSamples;
    return 1;
}

int clicksIn(const TempoMap& map, const Segment* segments, int numSegments, Click* out, int maxClicks) noexcept {
    // TODO: every multiple of 960 in each segment's [tickFrom, tickTo), at its sample's offset in the block.
    (void)map;
    (void)segments;
    (void)numSegments;
    (void)out;
    (void)maxClicks;
    return 0;
}

} // namespace minidaw

#include "../check.h"
