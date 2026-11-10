#pragma once
// Chapter 19, Step 2: a transport on a tempo map.
//
// The engine's timeline stays in samples: audio clips, automation, recording and the delay compensation all count
// samples, and a tempo change must not move a recorded take. Notes are in ticks. Step 1's map joins the two. This
// step is the transport that plays both:
//
//   Segment   what a block covers, as chapter 14's transport says it - a range of timeline samples at an offset in
//             the block - and now also the TICKS it plays, [tickFrom, tickTo). The MIDI player (chapter 18, Step 2)
//             plays the notes of those ticks, and knows from `ticksJump` whether they follow on from the last
//             segment's or start somewhere else (a seek, a loop's wrap: end the notes, chase the controllers).
//
//   Ticks     Normally a segment's ticks are Step 1's [first(s0), first(s1)). Two places need more care:
//               - a loop is set in TICKS (bars, as the user sees it). Its samples come from the map now:
//                 [sampleOfTick(loopStart), sampleOfTick(loopEnd)). A segment that ends at the loop's end ends at
//                 tick loopEnd, and the one after the wrap starts at tick loopStart - so every pass plays exactly
//                 the ticks of the loop, whatever rounding to samples does at its edges.
//               - the map changes while it plays (the user moves a tempo point). The playhead keeps its MUSICAL
//                 place: its exact (fractional) tick under the old map is played on at that tick's place under the
//                 new map. The samples jump - for the audio it is a seek - but the ticks go on: the next segment
//                 starts at the tick where the last one ended, and `ticksJump` is false, so held notes keep sounding.
//             In a stretch that goes on, tickFrom is the last segment's tickTo: a tick is never played twice or lost.
//
//   Clicks    a metronome on the map: a click on every beat (960 ticks), the first beat of a bar (4 beats) accented,
//             at the sample of its tick - so it speeds up and slows down with the ramps.
//
// No JUCE, no threads. Header-only: the starter and the solution define the functions marked YOUR TURN.

#include <algorithm>
#include <cmath>
#include <climits>
#include <cstdint>
#include <vector>

namespace minidaw {

constexpr int kTicksPerQuarter = 960;

// ---- Step 1 (solved) -----------------------------------------------------------------------------------------
struct TempoPoint {
    std::int64_t tick = 0;
    double bpm = 120.0;
    bool rampToNext = false;
};

class TempoMap {
public:
    bool set(double sampleRate, std::vector<TempoPoint> points) {
        if (!(sampleRate > 0.0) || points.empty() || points.front().tick != 0)
            return false;
        for (size_t i = 0; i < points.size(); ++i)
            if (!(points[i].bpm >= 20.0 && points[i].bpm <= 999.0) || (i > 0 && points[i].tick <= points[i - 1].tick))
                return false;
        sampleRate_ = sampleRate;
        points_ = std::move(points);
        starts_.assign(points_.size(), 0.0);
        for (size_t i = 1; i < points_.size(); ++i)
            starts_[i] = starts_[i - 1] + secondsInto(i - 1, static_cast<double>(points_[i].tick - points_[i - 1].tick));
        return true;
    }
    const std::vector<TempoPoint>& points() const noexcept { return points_; }
    double sampleRate() const noexcept { return sampleRate_; }

    double secondsAt(double tick) const noexcept {
        if (tick < 0.0)
            return tick * kSecondsPerTick / points_[0].bpm;
        const size_t i = segmentOfTick(tick);
        return starts_[i] + secondsInto(i, tick - static_cast<double>(points_[i].tick));
    }
    double tickAt(double seconds) const noexcept {
        if (seconds < 0.0)
            return seconds * points_[0].bpm / kSecondsPerTick;
        const auto after = std::upper_bound(starts_.begin(), starts_.end(), seconds);
        const size_t i = after == starts_.begin() ? 0 : static_cast<size_t>(after - starts_.begin()) - 1;
        const double s = seconds - starts_[i];
        const double k = slope(i);
        const double into = k == 0.0 ? s * points_[i].bpm / kSecondsPerTick : points_[i].bpm * std::expm1(k * s / kSecondsPerTick) / k;
        return static_cast<double>(points_[i].tick) + into;
    }
    std::int64_t sampleOfTick(std::int64_t tick) const noexcept { return std::llround(secondsAt(static_cast<double>(tick)) * sampleRate_); }
    std::int64_t firstTickAtOrAfter(std::int64_t sample) const noexcept {
        std::int64_t t = static_cast<std::int64_t>(std::ceil(tickAt(static_cast<double>(sample) / sampleRate_)));
        while (sampleOfTick(t) < sample)
            ++t;
        while (sampleOfTick(t - 1) >= sample)
            --t;
        return t;
    }

private:
    static constexpr double kSecondsPerTick = 60.0 / kTicksPerQuarter; // at 1 BPM
    size_t segmentOfTick(double tick) const noexcept {
        size_t lo = 0, hi = points_.size();
        while (hi - lo > 1) {
            const size_t mid = (lo + hi) / 2;
            (static_cast<double>(points_[mid].tick) <= tick ? lo : hi) = mid;
        }
        return lo;
    }
    double slope(size_t i) const noexcept {
        if (!points_[i].rampToNext || i + 1 >= points_.size())
            return 0.0;
        return (points_[i + 1].bpm - points_[i].bpm) / static_cast<double>(points_[i + 1].tick - points_[i].tick);
    }
    double secondsInto(size_t i, double ticks) const noexcept {
        const double k = slope(i);
        return k == 0.0 ? ticks * kSecondsPerTick / points_[i].bpm : kSecondsPerTick * std::log1p(k * ticks / points_[i].bpm) / k;
    }

    double sampleRate_ = 48000.0;
    std::vector<TempoPoint> points_{TempoPoint{}};
    std::vector<double> starts_{0.0};
};

// ---- Step 2 -----------------------------------------------------------------------------------------------------
struct Segment {
    std::int64_t timelineStart = 0; // the timeline sample of the segment's first sample
    int blockOffset = 0;            // where it starts in the block
    int numSamples = 0;
    std::int64_t tickFrom = 0;      // the ticks it plays: [tickFrom, tickTo)
    std::int64_t tickTo = 0;
    bool ticksJump = false;         // its ticks do not follow on from the last segment's
};

class Transport {
public:
    static constexpr int kMaxSegments = 8;

    // ---- provided -----------------------------------------------------------------------------------------
    explicit Transport(TempoMap map) : map_(std::move(map)) {}
    const TempoMap& map() const noexcept { return map_; }
    void play() noexcept {
        if (!playing_) {
            playing_ = true;
            jumpTo(kFindTick); // the first segment's ticks start where the playhead is
        }
    }
    void stop() noexcept { playing_ = false; }
    bool isPlaying() const noexcept { return playing_; }
    std::int64_t position() const noexcept { return position_; }
    // A seek: the samples and the ticks start again at `sample`.
    void seek(std::int64_t sample) noexcept {
        position_ = sample;
        jumpTo(kFindTick);
    }
    void seekTick(std::int64_t tick) noexcept { seek(map_.sampleOfTick(tick)); }
    // The loop, in ticks; active when enabled and end > start.
    void setLoop(std::int64_t startTick, std::int64_t endTick, bool enabled) noexcept {
        loopStart_ = startTick;
        loopEnd_ = endTick;
        loopEnabled_ = enabled;
    }

    // ---- YOUR TURN (starter/step02.cpp) ---------------------------------------------------------------------
    // A new map while it plays or not: the playhead keeps its musical place (its exact tick under the old map, at
    // that tick's sample under the new one, rounded), and the ticks go on without a jump.
    void setMap(TempoMap next) noexcept;
    // Up to numSamples of the block as segments (at most kMaxSegments): split at the loop's end, which sends the
    // playhead to the loop's start. `covered` gets how many samples the segments describe. Stopped: no segments,
    // covered = numSamples. A playhead at or past the loop's end when the block starts plays on without looping.
    int advance(int numSamples, Segment* out, int& covered) noexcept;

private:
    // ---- provided -----------------------------------------------------------------------------------------
    static constexpr std::int64_t kFindTick = INT64_MIN; // a jump to wherever the playhead's sample is
    // The next segment's ticks start again: at `tick`, or (kFindTick) at the first tick of its first sample.
    void jumpTo(std::int64_t tick) noexcept {
        jump_ = true;
        jumpTick_ = tick;
    }

    TempoMap map_;
    bool playing_ = false;
    std::int64_t position_ = 0;
    bool jump_ = true;                  // the next segment's ticks do not follow on (play, seek, a loop's wrap)
    std::int64_t jumpTick_ = kFindTick; // and where they start then
    std::int64_t nextTick_ = 0;         // where they start when they follow on
    std::int64_t loopStart_ = 0, loopEnd_ = 0;
    bool loopEnabled_ = false;
};

struct Click {
    int offset = 0;      // in the block
    bool accent = false; // the first beat of a bar
};

// ---- YOUR TURN ------------------------------------------------------------------------------------------------
// The metronome's clicks in the block's segments: one for every beat tick (a multiple of 960) in each segment's
// [tickFrom, tickTo), at blockOffset + the tick's sample - timelineStart, clamped into the segment (rounding may put
// a tick at the loop's edge one sample outside it). Bars of 4 beats. Returns how many, at most maxClicks.
int clicksIn(const TempoMap& map, const Segment* segments, int numSegments, Click* out, int maxClicks) noexcept;

} // namespace minidaw
