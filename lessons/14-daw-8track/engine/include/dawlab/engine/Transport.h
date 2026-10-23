#pragma once
// Transport: play / stop / position / loop / tempo, and the musical time grid (Step 2).
//
// The transport is the engine's clock. The audio thread reads and advances it once per block;
// the UI thread may change tempo, request a seek, toggle looping... at any time. Every UI
// write goes into an atomic. A seek is "pending" until the audio thread picks it up at the
// start of its next block, so the audio thread never sees the position jump in the middle of
// a block.
//
// Time units
//   The timeline is measured in samples (int64_t: at 48 kHz an int32 would overflow after
//   12.4 hours, an int64 after 6 million years). Musical time is derived from it:
//
//     samplesPerBeat = sampleRate * 60 / bpm          (120 BPM @ 48 kHz -> 24000)
//     samplesPerBar  = samplesPerBeat * beatsPerBar   (4/4             -> 96000)
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

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

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
};

class Transport {
public:
    static constexpr int kTicksPerBeat = 960;      // PPQ resolution used by BarsBeats
    static constexpr int kMaxSegmentsPerBlock = 8; // per advance() call; see the header comment
    static constexpr double kMinTempo = 20.0;
    static constexpr double kMaxTempo = 999.0;

    // Non-audio thread, before processing starts.
    void prepare(double sampleRate) noexcept { sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0; }
    double getSampleRate() const noexcept { return sampleRate_; }

    // ---- tempo & meter (any thread) -------------------------------------------------------
    void setTempo(double bpm) noexcept {
        tempo_.store(std::clamp(bpm, kMinTempo, kMaxTempo), std::memory_order_relaxed);
    }
    double getTempo() const noexcept { return tempo_.load(std::memory_order_relaxed); }

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

    // ---- loop (any thread) ----------------------------------------------------------------
    // The loop is active when enabled AND end > start. If the playhead is already past the
    // loop end when looping is switched on, it keeps playing linearly (like most DAWs).
    void setLoopRange(int64_t startSample, int64_t endSample) noexcept {
        loopStart_.store(std::max<int64_t>(0, startSample), std::memory_order_relaxed);
        loopEnd_.store(std::max<int64_t>(0, endSample), std::memory_order_relaxed);
    }
    void setLoopRangeBars(int firstBar, int numBars) noexcept {
        const int64_t s = barsBeatsToSamples({firstBar, 1, 0});
        const int64_t e = barsBeatsToSamples({firstBar + numBars, 1, 0});
        setLoopRange(s, e);
    }
    void setLoopEnabled(bool enabled) noexcept { loopEnabled_.store(enabled, std::memory_order_relaxed); }
    bool isLoopEnabled() const noexcept { return loopEnabled_.load(std::memory_order_relaxed); }
    int64_t getLoopStart() const noexcept { return loopStart_.load(std::memory_order_relaxed); }
    int64_t getLoopEnd() const noexcept { return loopEnd_.load(std::memory_order_relaxed); }

    // ---- conversions (pure functions of tempo / meter / sample rate) ----------------------
    double getSamplesPerBeat() const noexcept { return sampleRate_ * 60.0 / getTempo(); }
    double getSamplesPerBar() const noexcept { return getSamplesPerBeat() * getTimeSignature().numerator; }

    double beatsToSamples(double beats) const noexcept { return beats * getSamplesPerBeat(); }
    double samplesToBeats(double samples) const noexcept { return samples / getSamplesPerBeat(); }

    double toSeconds(int64_t samples) const noexcept { return static_cast<double>(samples) / sampleRate_; }
    int64_t fromSeconds(double seconds) const noexcept {
        return static_cast<int64_t>(std::llround(seconds * sampleRate_));
    }

    // bar / beat are 1-based: bar 1 beat 1 tick 0 is sample 0.
    //   beats   = (bar - 1) * beatsPerBar + (beat - 1) + tick / kTicksPerBeat
    //   samples = round(beats * samplesPerBeat)
    int64_t barsBeatsToSamples(const BarsBeats& p) const noexcept {
        const int beatsPerBar = getTimeSignature().numerator;
        const double beats = static_cast<double>(p.bar - 1) * beatsPerBar + static_cast<double>(p.beat - 1) +
                             static_cast<double>(p.tick) / kTicksPerBeat;
        return static_cast<int64_t>(std::llround(beatsToSamples(beats)));
    }

    BarsBeats samplesToBarsBeats(int64_t samples) const noexcept {
        const int beatsPerBar = getTimeSignature().numerator;
        // The tiny epsilon makes exact grid positions (e.g. 24000 samples = 1.0 beat) land on
        // the beat instead of tick 959 because of floating-point rounding.
        const double totalBeats = samplesToBeats(static_cast<double>(std::max<int64_t>(0, samples))) + 1e-9;
        const auto wholeBeats = static_cast<int64_t>(std::floor(totalBeats));
        BarsBeats p;
        p.bar = static_cast<int>(wholeBeats / beatsPerBar) + 1;
        p.beat = static_cast<int>(wholeBeats % beatsPerBar) + 1;
        p.tick = std::min(kTicksPerBeat - 1,
                          static_cast<int>(std::floor((totalBeats - static_cast<double>(wholeBeats)) * kTicksPerBeat)));
        return p;
    }

    // ---- audio thread -----------------------------------------------------------------------
    // Applies a pending seek. If playing, fills `segments` (capacity kMaxSegmentsPerBlock) for
    // up to numSamples samples, advances the position and returns the number of segments.
    // `samplesCovered` receives how many of the numSamples were described (== numSamples
    // unless the loop is so short that the segment array filled up). When stopped it returns
    // 0 and sets samplesCovered = numSamples.
    int advance(int numSamples, TimelineSegment* segments, int& samplesCovered) noexcept {
        const int64_t seek = pendingSeek_.exchange(-1, std::memory_order_acq_rel);
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
                pos = loopStart; // wrap: the next segment continues at the loop start
        }
        samplesCovered = offset;
        position_.store(pos, std::memory_order_relaxed);
        return count;
    }

private:
    double sampleRate_ = 48000.0;
    std::atomic<double> tempo_{120.0};
    std::atomic<int> numerator_{4};
    std::atomic<int> denominator_{4};
    std::atomic<bool> playing_{false};
    std::atomic<int64_t> position_{0};
    std::atomic<int64_t> pendingSeek_{-1};
    std::atomic<bool> loopEnabled_{false};
    std::atomic<int64_t> loopStart_{0};
    std::atomic<int64_t> loopEnd_{0};
};

} // namespace dawlab::engine
