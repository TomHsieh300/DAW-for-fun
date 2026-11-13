#pragma once
// Transport: play / stop / position / loop / tempo, and the musical time grid (Step 2).
//
// The transport is the engine's clock. The audio thread reads and advances it once per block;
// the UI thread may change tempo, request a seek, toggle looping... at any time. Every UI
// write goes into an atomic, or - the tempo map and the loop range, values of several
// fields - into a triple buffer (see "Threads" below). A seek is "pending" until the audio
// thread picks it up at the start of its next block, so the audio thread never sees the
// position jump in the middle of a block.
//
// Time units
//   The timeline is measured in samples (int64_t: at 48 kHz an int32 would overflow after
//   12.4 hours, an int64 after 6 million years). Musical time is derived from it through the
//   TEMPO MAP (chapter 19): points of tempo jumps and ramps, in ticks (960 to a beat), turned into
//   seconds and samples (TempoMap.h). setTempo(bpm) is a map of one point.
//
//   "Beat" means the note value named by the time-signature denominator, and the tempo is in
//   those beats per minute (that is what every DAW displays).
//
// advance() and timeline segments
//   For a block of N samples, advance() reports which timeline ranges the block covers.
//   Normally that is one segment. When the loop end falls inside the block, the block is
//   split:
//
//        block (N samples)   |-----------------|
//        timeline            ..... loopEnd | loopStart .....
//        segments            [ seg 0       ][ seg 1          ]
//
//   Each segment says "samples [blockOffset, blockOffset + numSamples) of this block are the
//   timeline positions [timelineStart, timelineStart + numSamples)". Tracks use that to read
//   their clips sample-accurately. A very short loop (shorter than block / kMaxSegments) needs
//   more segments than fit in the array; advance() then covers only part of the block and
//   reports how much via `samplesCovered`, and the caller (Engine) calls it again for the
//   rest. So any loop length >= 1 sample works.
//
// Ticks (chapter 19, Step 2)
//   Every segment also says which ticks it plays, [tickFrom, tickTo), and whether they follow on from the
//   last segment's (ticksJump false) or start somewhere else (play, seek, a loop's wrap). In a stretch that
//   goes on, tickFrom is the last segment's tickTo: no tick is played twice or lost.
//   - A loop set in ticks (setLoopRangeTicks, setLoopRangeBars) follows the tempo, and its edges are
//     given in ticks; a loop set in samples (setLoopRange) stays where it is, as before.
//   - A new map (setTempo, setTempoMap) reaches the audio thread at the start of a block. The playhead
//     keeps its musical place - its exact tick under the old map, at that tick's sample under the new one -
//     and the ticks go on without a jump, so held notes keep sounding.
//
// Threads and the map
//   setTempo() / setTempoMap() and the conversions are for ONE writer, the UI thread: it keeps its own copy of
//   the map (getTempoMap()). The audio thread gets it through a TRIPLE BUFFER (TripleBuffer.h): three maps, each
//   owned by one side at a time. The UI writes its back buffer and swaps it with the middle one (marked new); the
//   audio thread, at the start of a block, swaps its front buffer with the middle one when that is new. One atomic
//   exchange each, no copy on the audio thread, no allocation, and never a map half written.
//   The loop range (start, end, and whether they are ticks) travels the same way, in a triple buffer of its own:
//   read field by field, a block could take ticks for samples, or the start of one range and the end of another,
//   and wrap where no loop ends. Its setters are for the UI thread too; on / off stays an atomic for any thread.
//   The map and the loop are two values: a block may still see a new map with the loop set just before it.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>

#include "TempoMap.h"
#include "TripleBuffer.h"

namespace dawlab::engine {

struct TimeSignature {
    int numerator = 4;   // beats per bar
    int denominator = 4; // note value of one beat (4 = quarter note, 8 = eighth note)
};

// Musical position, 1-based like every DAW display: "bar 3, beat 2, tick 480".
struct BarsBeats {
    int bar = 1;
    int beat = 1;
    int tick = 0; // 0 .. Transport::kTicksPerBeat - 1
};

struct TimelineSegment {
    int64_t timelineStart = 0; // timeline position of the first sample of this segment
    int blockOffset = 0;       // where the segment starts inside the audio block
    int numSamples = 0;
    int64_t tickFrom = 0;      // the ticks it plays: [tickFrom, tickTo) (chapter 19)
    int64_t tickTo = 0;
    bool ticksJump = false;    // they do not follow on from the last segment's
};

// The loop's edges and their unit: one value, handed to the audio thread whole.
struct LoopRange {
    int64_t start = 0;
    int64_t end = 0;
    bool inTicks = false; // ticks (setLoopRangeTicks, setLoopRangeBars) or samples (setLoopRange)
};

class Transport {
public:
    static constexpr int kTicksPerBeat = TempoMap::kTicksPerBeat; // PPQ resolution used by BarsBeats
    static constexpr int kMaxSegmentsPerBlock = 8; // per advance() call; see the header comment
    static constexpr double kMinTempo = TempoMap::kMinBpm;
    static constexpr double kMaxTempo = TempoMap::kMaxBpm;

    // Non-audio thread, before processing starts.
    void prepare(double sampleRate) noexcept { sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0; }
    double getSampleRate() const noexcept { return sampleRate_; }

    // ---- tempo & meter (UI thread: one writer) ----------------------------------------------
    // A map of one point.
    void setTempo(double bpm) noexcept { setTempoMap(TempoMap::constant(bpm)); }
    // The first point's tempo: THE tempo of a song with one.
    double getTempo() const noexcept { return maps_->ui.point(0).bpm; }
    void setTempoMap(const TempoMap& map) noexcept {
        maps_->ui = map;
        maps_->audio.write(map);
    }
    const TempoMap& getTempoMap() const noexcept { return maps_->ui; }

    void setTimeSignature(int numerator, int denominator) noexcept {
        numerator_.store(std::clamp(numerator, 1, 32), std::memory_order_relaxed);
        denominator_.store(std::clamp(denominator, 1, 32), std::memory_order_relaxed);
    }
    TimeSignature getTimeSignature() const noexcept {
        return {numerator_.load(std::memory_order_relaxed), denominator_.load(std::memory_order_relaxed)};
    }

    // ---- play state (any thread) ----------------------------------------------------------
    void play() noexcept { playing_.store(true, std::memory_order_relaxed); }
    void stop() noexcept { playing_.store(false, std::memory_order_relaxed); }
    void setPlaying(bool shouldPlay) noexcept { playing_.store(shouldPlay, std::memory_order_relaxed); }
    bool isPlaying() const noexcept { return playing_.load(std::memory_order_relaxed); }

    // Seek (any thread). Applied by the audio thread at the start of its next block.
    void setPosition(int64_t samples) noexcept {
        pendingSeek_.store(std::max<int64_t>(0, samples), std::memory_order_release);
    }
    void setPositionSeconds(double seconds) noexcept { setPosition(fromSeconds(seconds)); }
    void setPositionBarsBeats(const BarsBeats& p) noexcept { setPosition(barsBeatsToSamples(p)); }

    // Current position. If a seek is pending, that is what a UI should display.
    int64_t getPosition() const noexcept {
        const int64_t pending = pendingSeek_.load(std::memory_order_acquire);
        return pending >= 0 ? pending : position_.load(std::memory_order_relaxed);
    }
    double getPositionSeconds() const noexcept { return toSeconds(getPosition()); }
    BarsBeats getPositionBarsBeats() const noexcept { return samplesToBarsBeats(getPosition()); }

    // ---- loop (the range: UI thread, one writer; on / off: any thread) ------------------------
    // The loop is active when enabled AND end > start. If the playhead is already past the
    // loop end when looping is switched on, it keeps playing linearly (like most DAWs).
    // The range is one value (see "Threads and the map"): the UI keeps its own copy, which the
    // getters read, and the audio thread takes it whole at the start of a block.
    // In samples: it stays where it is when the tempo changes.
    void setLoopRange(int64_t startSample, int64_t endSample) noexcept {
        setLoop({std::max<int64_t>(0, startSample), std::max<int64_t>(0, endSample), false});
    }
    // In ticks (chapter 19): it follows the tempo, and its edges are exact.
    void setLoopRangeTicks(int64_t startTick, int64_t endTick) noexcept {
        setLoop({std::max<int64_t>(0, startTick), std::max<int64_t>(0, endTick), true});
    }
    void setLoopRangeBars(int firstBar, int numBars) noexcept {
        const int64_t ticksPerBar = static_cast<int64_t>(getTimeSignature().numerator) * kTicksPerBeat;
        setLoopRangeTicks((firstBar - 1) * ticksPerBar, (firstBar - 1 + numBars) * ticksPerBar);
    }
    void setLoopEnabled(bool enabled) noexcept { loopEnabled_.store(enabled, std::memory_order_relaxed); }
    bool isLoopEnabled() const noexcept { return loopEnabled_.load(std::memory_order_relaxed); }
    bool isLoopInTicks() const noexcept { return loopUi_.inTicks; }
    // As it was set: ticks when isLoopInTicks(), samples otherwise (a project file keeps it that way).
    int64_t getLoopStartValue() const noexcept { return loopUi_.start; }
    int64_t getLoopEndValue() const noexcept { return loopUi_.end; }
    // In samples, whichever way it was set (through the UI's own map).
    int64_t getLoopStart() const noexcept { return loopSample(loopUi_.start, maps_->ui, loopUi_.inTicks); }
    int64_t getLoopEnd() const noexcept { return loopSample(loopUi_.end, maps_->ui, loopUi_.inTicks); }

    // ---- conversions (UI thread: the map it set) ----------------------------------------------
    // The first tempo's: what a song of one tempo has.
    double getSamplesPerBeat() const noexcept { return sampleRate_ * 60.0 / getTempo(); }
    double getSamplesPerBar() const noexcept { return getSamplesPerBeat() * getTimeSignature().numerator; }

    // Through the map.
    double beatsToSamples(double beats) const noexcept { return maps_->ui.secondsAt(beats * kTicksPerBeat) * sampleRate_; }
    double samplesToBeats(double samples) const noexcept { return maps_->ui.tickAtSample(samples, sampleRate_) / kTicksPerBeat; }
    int64_t tickToSample(int64_t tick) const noexcept { return maps_->ui.sampleOfTick(tick, sampleRate_); }
    int64_t sampleToFirstTick(int64_t sample) const noexcept { return maps_->ui.firstTickAtOrAfter(sample, sampleRate_); }

    double toSeconds(int64_t samples) const noexcept { return static_cast<double>(samples) / sampleRate_; }
    int64_t fromSeconds(double seconds) const noexcept {
        return static_cast<int64_t>(std::llround(seconds * sampleRate_));
    }

    // bar / beat are 1-based: bar 1 beat 1 tick 0 is sample 0.
    //   beats   = (bar - 1) * beatsPerBar + (beat - 1) + tick / kTicksPerBeat
    //   samples = the map's sample of those beats
    int64_t barsBeatsToSamples(const BarsBeats& p) const noexcept {
        const int beatsPerBar = getTimeSignature().numerator;
        const double beats = static_cast<double>(p.bar - 1) * beatsPerBar + static_cast<double>(p.beat - 1) +
                             static_cast<double>(p.tick) / kTicksPerBeat;
        return static_cast<int64_t>(std::llround(beatsToSamples(beats)));
    }

    // The tick that plays at `samples`: the last one whose sample is not after it - so a grid position lands on
    // its tick exactly, whatever rounding to samples did (a beat at 24000 samples is beat 2, not beat 1 tick 959).
    BarsBeats samplesToBarsBeats(int64_t samples) const noexcept {
        const int beatsPerBar = getTimeSignature().numerator;
        const int64_t s = std::max<int64_t>(0, samples);
        int64_t tick = maps_->ui.firstTickAtOrAfter(s, sampleRate_);
        if (maps_->ui.sampleOfTick(tick, sampleRate_) > s)
            --tick;
        tick = std::max<int64_t>(tick, 0);
        const int64_t wholeBeats = tick / kTicksPerBeat;
        BarsBeats p;
        p.bar = static_cast<int>(wholeBeats / beatsPerBar) + 1;
        p.beat = static_cast<int>(wholeBeats % beatsPerBar) + 1;
        p.tick = static_cast<int>(tick % kTicksPerBeat);
        return p;
    }

    // ---- audio thread -----------------------------------------------------------------------
    // The map the audio thread plays (taken at the start of its last advance()).
    const TempoMap& getAudioTempoMap() const noexcept { return maps_->audio.front(); }

    // Takes a new map (keeping the playhead's musical place), a new loop range and a pending seek. If playing,
    // fills `segments` (capacity kMaxSegmentsPerBlock) for up to numSamples samples, advances the position and
    // returns the number of segments. `samplesCovered` receives how many of the numSamples were described
    // (== numSamples unless the loop is so short that the segment array filled up). When stopped it returns
    // 0 and sets samplesCovered = numSamples.
    int advance(int numSamples, TimelineSegment* segments, int& samplesCovered) noexcept {
        takeNewMap();
        loop_.take(); // a range the UI set since the last block, whole (also while stopped)
        const int64_t seek = pendingSeek_.exchange(-1, std::memory_order_acq_rel);
        if (seek >= 0) {
            position_.store(seek, std::memory_order_relaxed);
            jumpTo(kFindTick);
        }
        samplesCovered = std::max(0, numSamples);
        if (!isPlaying() || numSamples <= 0)
            return 0;

        int64_t pos = position_.load(std::memory_order_relaxed);
        const LoopRange& loop = loop_.front();
        const bool inTicks = loop.inTicks;
        const int64_t loopStartRaw = loop.start;
        const int64_t loopEndRaw = loop.end;
        const int64_t loopStart = loopSample(loopStartRaw, getAudioTempoMap(), inTicks);
        const int64_t loopEnd = loopSample(loopEndRaw, getAudioTempoMap(), inTicks);
        const bool loopActive = isLoopEnabled() && loopEnd > loopStart;

        int count = 0;
        int offset = 0;
        while (offset < numSamples && count < kMaxSegmentsPerBlock) {
            int64_t n = numSamples - offset;
            const bool toLoopEnd = loopActive && pos < loopEnd && pos + n >= loopEnd;
            if (toLoopEnd)
                n = loopEnd - pos; // stop exactly at the loop end
            TimelineSegment& s = segments[count++];
            s.timelineStart = pos;
            s.blockOffset = offset;
            s.numSamples = static_cast<int>(n);
            s.ticksJump = jump_;
            if (!jump_)
                s.tickFrom = nextTick_;
            else
                s.tickFrom = jumpTick_ != kFindTick ? jumpTick_ : getAudioTempoMap().firstTickAtOrAfter(pos, sampleRate_);
            // A segment that ends at a loop in ticks ends at its tick: rounding to samples must not move it.
            s.tickTo = toLoopEnd && inTicks ? loopEndRaw : getAudioTempoMap().firstTickAtOrAfter(pos + n, sampleRate_);
            pos += n;
            offset += static_cast<int>(n);
            nextTick_ = s.tickTo;
            jump_ = false;
            if (toLoopEnd) { // wrap: the next segment continues at the loop start, its ticks at the loop's first
                pos = loopStart;
                jumpTo(inTicks ? loopStartRaw : kFindTick);
            }
        }
        samplesCovered = offset;
        position_.store(pos, std::memory_order_relaxed);
        return count;
    }

private:
    static constexpr int64_t kFindTick = INT64_MIN; // a jump to wherever the playhead's sample is

    void jumpTo(int64_t tick) noexcept {
        jump_ = true;
        jumpTick_ = tick;
    }
    int64_t loopSample(int64_t value, const TempoMap& map, bool inTicks) const noexcept {
        return inTicks ? map.sampleOfTick(value, sampleRate_) : value;
    }
    // UI thread: the one writer of the loop range.
    void setLoop(const LoopRange& loop) noexcept {
        loopUi_ = loop;
        loop_.write(loop);
    }

    // Audio thread: a map the UI published since the last block. The playhead keeps its musical place; the ticks
    // go on (or a pending jump stays pending).
    void takeNewMap() noexcept {
        if (!maps_->audio.hasNew())
            return;
        const double position = static_cast<double>(position_.load(std::memory_order_relaxed));
        const double tick = getAudioTempoMap().tickAtSample(position, sampleRate_);
        maps_->audio.take();
        position_.store(std::llround(getAudioTempoMap().secondsAt(tick) * sampleRate_), std::memory_order_relaxed);
    }

    double sampleRate_ = 48000.0;
    std::atomic<int> numerator_{4};
    std::atomic<int> denominator_{4};
    std::atomic<bool> playing_{false};
    std::atomic<int64_t> position_{0};
    std::atomic<int64_t> pendingSeek_{-1};
    std::atomic<bool> loopEnabled_{false};
    LoopRange loopUi_;              // UI thread: the range as it set it last
    TripleBuffer<LoopRange> loop_;  // UI thread writes, audio thread takes

    // The map: the UI's own copy, and the triple buffer that hands it to the audio thread (the UI writes, the
    // audio thread takes). On the heap: 4 maps of 8 KB would make an Engine too large for a test's stack.
    struct Maps {
        TempoMap ui;
        TripleBuffer<TempoMap> audio;
    };
    std::unique_ptr<Maps> maps_ = std::make_unique<Maps>();

    // Audio thread: the ticks.
    bool jump_ = true;
    int64_t jumpTick_ = kFindTick;
    int64_t nextTick_ = 0;
};

} // namespace dawlab::engine
