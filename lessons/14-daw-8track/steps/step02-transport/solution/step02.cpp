// Step 2 (solution): the musical grid and Transport::advance().
//
// Build and run:  l14_step02_solution   (ctest, label "solution")
// Your version:   starter/step02.cpp -> l14_step02_starter

#include "../minidaw.h"

namespace minidaw {

int64_t Transport::barsBeatsToSamples(const BarsBeats& p) const noexcept {
    const int beatsPerBar = getTimeSignature().numerator;
    const double beats = static_cast<double>(p.bar - 1) * beatsPerBar + static_cast<double>(p.beat - 1) +
                         static_cast<double>(p.tick) / kTicksPerBeat;
    return static_cast<int64_t>(std::llround(beats * getSamplesPerBeat())); // round once, at the end
}

BarsBeats Transport::samplesToBarsBeats(int64_t samples) const noexcept {
    const int beatsPerBar = getTimeSignature().numerator;
    const double totalBeats = static_cast<double>(std::max<int64_t>(0, samples)) / getSamplesPerBeat() + 1e-9;
    const auto wholeBeats = static_cast<int64_t>(std::floor(totalBeats));
    BarsBeats p;
    p.bar = static_cast<int>(wholeBeats / beatsPerBar) + 1;
    p.beat = static_cast<int>(wholeBeats % beatsPerBar) + 1;
    p.tick = std::min(kTicksPerBeat - 1,
                      static_cast<int>(std::floor((totalBeats - static_cast<double>(wholeBeats)) * kTicksPerBeat)));
    return p;
}

int Transport::advance(int numSamples, TimelineSegment* segments, int& samplesCovered) noexcept {
    const int64_t seek = pendingSeek_.exchange(-1, std::memory_order_acq_rel); // seeks only at block starts
    if (seek >= 0)
        position_.store(seek, std::memory_order_relaxed);
    samplesCovered = std::max(0, numSamples);
    if (!isPlaying() || numSamples <= 0)
        return 0;

    int64_t pos = position_.load(std::memory_order_relaxed);
    const int64_t loopStart = getLoopStart();
    const int64_t loopEnd = getLoopEnd();
    const bool loopActive = isLoopEnabled() && loopEnd > loopStart;

    int count = 0;
    int offset = 0;
    while (offset < numSamples && count < kMaxSegmentsPerBlock) {
        int64_t n = numSamples - offset;
        if (loopActive && pos < loopEnd)
            n = std::min<int64_t>(n, loopEnd - pos); // stop exactly at the loop end
        segments[count++] = {pos, offset, static_cast<int>(n)};
        pos += n;
        offset += static_cast<int>(n);
        if (loopActive && pos == loopEnd)
            pos = loopStart; // the next segment (or block) continues at the loop start
    }
    samplesCovered = offset;
    position_.store(pos, std::memory_order_relaxed);
    return count;
}

} // namespace minidaw

#include "../check.h"
