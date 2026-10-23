// Step 8 (solution): the compensating delay line and the PDC computation.
//
// Build and run:  l14_step08_solution   (ctest, label "solution")
// Your version:   starter/step08.cpp -> l14_step08_starter

#include "../minidaw.h"

namespace minidaw {

void LatencyCompensator::process(AudioBlock block) noexcept {
    if (ring_.empty())
        return;
    const int n = block.getNumSamples();
    const int nch = std::min(numChannels_, block.getNumChannels());
    int w = writeIndex_;
    for (int ch = 0; ch < nch; ++ch) {
        float* ring = ring_.data() + static_cast<size_t>(ch) * static_cast<size_t>(capacity_);
        float* x = block.getChannelPointer(ch);
        w = writeIndex_; // every channel starts at the same position
        for (int i = 0; i < n; ++i) {
            int r = w - delay_;
            if (r < 0)
                r += capacity_;
            ring[w] = x[i];
            x[i] = ring[r]; // delay 0: r == w, the sample just written
            if (++w == capacity_)
                w = 0;
        }
    }
    writeIndex_ = w;
}

void Mixer::updateLatencies() noexcept {
    for (auto& t : tracks_)
        t.getInserts().applyPendingParameters();
    for (auto& b : buses_)
        b.getInserts().applyPendingParameters();
    master_.getInserts().applyPendingParameters();

    const bool enabled = pdcEnabled_.load(std::memory_order_relaxed);
    int maxTrack = 0;
    for (const auto& t : tracks_)
        maxTrack = std::max(maxTrack, t.getInsertLatencySamples());
    int maxBus = 0;
    for (const auto& b : buses_)
        maxBus = std::max(maxBus, b.getInsertLatencySamples());
    const int limit = getMaxPdcSamples();
    const int targetTrack = std::min(maxTrack, limit);
    const int targetBus = std::min(maxBus, limit);
    pdcOverflow_.store(enabled && (maxTrack > targetTrack || maxBus > targetBus), std::memory_order_relaxed);
    for (auto& t : tracks_) // a path slower than the target gets 0 (clamped) and arrives late
        t.getCompensator().setDelaySamples(enabled ? targetTrack - t.getInsertLatencySamples() : 0);
    for (auto& b : buses_)
        b.getCompensator().setDelaySamples(enabled ? targetBus - b.getInsertLatencySamples() : 0);
    trackSumCompensator_.setDelaySamples(enabled ? targetBus : 0);
    totalLatency_.store(enabled ? targetTrack + targetBus + master_.getInsertLatencySamples() : 0,
                        std::memory_order_relaxed);
}

} // namespace minidaw

#include "../check.h"
