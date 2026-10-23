// Step 6 (starter): the send and the two send taps of a track.
//
// Build and run:  l14_step06_starter   (prints [FAIL] until both TODOs are done)
// Reference:      ../solution/step06.cpp, docs/step-06-sends-buses.md ("Your turn")
//
// Send levels, the bus strips and Mixer::process() are in ../minidaw.h.

#include "../minidaw.h"

namespace minidaw {

void ChannelStrip::addSend(int bus, AudioBlock dest, const AudioBlock& src) noexcept {
    // TODO 1: ignore invalid buses (validBus). For every sample i: g = sendGain_[bus].getNextValue()
    //         (once per sample, for all channels - also when g is 0, so the ramp keeps moving),
    //         dest[ch][i] += src[ch][i] * g.
    (void)bus;
    (void)dest;
    (void)src;
}

void Mixer::processTrack(int t, AudioBlock tb, AudioBlock sum, const std::array<AudioBlock, kNumBuses>& bus,
                         const TimelineSegment* segments, int numSegments) noexcept {
    // TODO 2: the Step 5 chain (renderClips, beginBlock(isTrackAudible(t)), processInserts,
    //         applyMuteSolo) - then for each bus with isSendPreFader(b): addSend(b, bus[b], tb);
    //         then applyFaderAndPan; then for each bus with !isSendPreFader(b): addSend(...);
    //         then meter(tb) and sum.addFrom(tb).
    (void)t;
    (void)tb;
    (void)sum;
    (void)bus;
    (void)segments;
    (void)numSegments;
}

} // namespace minidaw

#include "../check.h"
