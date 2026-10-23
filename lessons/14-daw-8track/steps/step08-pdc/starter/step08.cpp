// Step 8 (starter): the compensating delay line and the PDC computation.
//
// Build and run:  l14_step08_starter   (prints [FAIL] until both TODOs are done)
// Reference:      ../solution/step08.cpp, docs/step-08-pdc.md ("Your turn")
//
// getLatencySamples(), PureDelayEffect, the compensators in every strip and the call order in
// Engine::processChunk() are in ../minidaw.h.

#include "../minidaw.h"

namespace minidaw {

void LatencyCompensator::process(AudioBlock block) noexcept {
    // TODO 1: for every channel ch < min(numChannels_, block channels), ring = ring_.data() +
    //         ch * capacity_, starting at w = writeIndex_: r = w - delay_ (+ capacity_ if < 0);
    //         ring[w] = x[i]; x[i] = ring[r]; advance w (wrap at capacity_).
    //         Finally store the advanced w in writeIndex_.
    (void)block;
}

void Mixer::updateLatencies() noexcept {
    // TODO 2a: applyPendingParameters() on the insert chains of all tracks, buses and the master.
    // TODO 2b: maxTrack / maxBus = the largest getInsertLatencySamples() of tracks / buses;
    //          targetTrack = min(maxTrack, getMaxPdcSamples()), targetBus likewise.
    // TODO 2c: if pdcEnabled_: track compensators = targetTrack - own latency, bus compensators =
    //          targetBus - own latency, trackSumCompensator_ = targetBus,
    //          pdcOverflow_ = maxTrack > targetTrack || maxBus > targetBus,
    //          totalLatency_ = targetTrack + targetBus + master_.getInsertLatencySamples();
    //          otherwise everything 0 / false.
}

} // namespace minidaw

#include "../check.h"
