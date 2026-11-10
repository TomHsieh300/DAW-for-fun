// Chapter 19, Step 2 (solution): a transport on a tempo map.
//
// Build and run:  l19_step02_solution   (ctest, label "solution")
// Your version:   starter/step02.cpp -> l19_step02_starter

#include "../minidaw.h"

namespace minidaw {

void Transport::setMap(TempoMap next) noexcept {
    const double tick = map_.tickAt(static_cast<double>(position_) / map_.sampleRate()); // the musical place, exactly
    map_ = std::move(next);
    position_ = std::llround(map_.secondsAt(tick) * map_.sampleRate());
    // The ticks go on from nextTick_ - or a pending jump stays pending.
}

int Transport::advance(int numSamples, Segment* out, int& covered) noexcept {
    covered = std::max(numSamples, 0);
    if (!playing_ || numSamples <= 0)
        return 0;
    const std::int64_t loopStartSample = map_.sampleOfTick(loopStart_), loopEndSample = map_.sampleOfTick(loopEnd_);
    const bool looping = loopEnabled_ && loopEndSample > loopStartSample && position_ < loopEndSample;

    int count = 0, offset = 0;
    while (offset < numSamples && count < kMaxSegments) {
        std::int64_t n = numSamples - offset;
        const bool toLoopEnd = looping && position_ + n >= loopEndSample;
        if (toLoopEnd)
            n = loopEndSample - position_;
        Segment& s = out[count++];
        s.timelineStart = position_;
        s.blockOffset = offset;
        s.numSamples = static_cast<int>(n);
        s.ticksJump = jump_;
        if (!jump_)
            s.tickFrom = nextTick_;
        else
            s.tickFrom = jumpTick_ != kFindTick ? jumpTick_ : map_.firstTickAtOrAfter(position_);
        // A segment that ends at the loop's end ends at its tick: rounding to samples must not move it.
        s.tickTo = toLoopEnd ? loopEnd_ : map_.firstTickAtOrAfter(position_ + n);
        offset += static_cast<int>(n);
        position_ += n;
        nextTick_ = s.tickTo;
        jump_ = false;
        if (toLoopEnd) { // the wrap: the samples to the loop's first, the ticks to the loop's first tick
            position_ = loopStartSample;
            jumpTo(loopStart_);
        }
    }
    covered = offset;
    return count;
}

int clicksIn(const TempoMap& map, const Segment* segments, int numSegments, Click* out, int maxClicks) noexcept {
    int n = 0;
    for (int k = 0; k < numSegments; ++k) {
        const Segment& s = segments[k];
        // The first beat at or after tickFrom (beats are multiples of 960, also below 0).
        std::int64_t beat = s.tickFrom >= 0 ? (s.tickFrom + kTicksPerQuarter - 1) / kTicksPerQuarter * kTicksPerQuarter
                                            : -((-s.tickFrom) / kTicksPerQuarter) * kTicksPerQuarter;
        for (; beat < s.tickTo && n < maxClicks; beat += kTicksPerQuarter) {
            const std::int64_t at = std::clamp<std::int64_t>(map.sampleOfTick(beat) - s.timelineStart, 0, s.numSamples - 1);
            out[n++] = {s.blockOffset + static_cast<int>(at), (beat / kTicksPerQuarter) % 4 == 0};
        }
    }
    return n;
}

} // namespace minidaw

#include "../check.h"
