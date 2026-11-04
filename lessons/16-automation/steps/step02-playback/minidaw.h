#pragma once
// minidaw.h - Step 2 snapshot of chapter 16: the curve on the audio thread.
//
// Step 1 stored a curve. Now the audio thread follows it while it plays, one value per SAMPLE:
//
//   UI thread                                       audio thread
//   copy the curve, change the copy   --swap-->     AutomationLane::render(): one value per sample
//   delete the old curve             <--garbage--   hands the old curve back, frees nothing
//
//   - The curve the audio thread reads is IMMUTABLE. An edit makes a new curve and swaps the
//     pointer, exactly like a track's clip list (chapter 14). This snapshot has no threads and no
//     queues yet: the check plays both roles, and Step 5 connects it to the engine's commands.
//   - A block is not one piece of timeline. The transport describes it as SEGMENTS: a loop that
//     wraps inside the block makes two, each with its own timeline start and block offset.
//   - A CurveCursor remembers the segment of the curve where the last block ended, so a block that
//     simply continues needs no search. It is only a HINT: locate() checks it against the position
//     and against the curve in hand, so a seek, a loop or a swapped curve cannot make it wrong.
//   - A seek, a loop wrap, a swapped curve or a Hold step make the value jump, and a gain that jumps
//     clicks. So the lane limits how far the value may move per sample. A curve slower than the
//     limit comes through EXACTLY; a jump becomes a ramp of at most `declickSamples` samples.
//   - No curve: the lane follows the knob instead, with the same limit.
//
// YOUR TURN (Step 2): CurveCursor::locate(), AutomationCurve::render() with a cursor,
// AutomationLane::render() and AutomationLane::limitSlew() in starter/step02.cpp.

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

namespace minidaw {

// ---- Step 1 (solved) ----------------------------------------------------------------------------

enum class Shape {
    Linear, // a straight line to the next point
    Hold,   // the value stays until the next point
};

struct AutomationPoint {
    std::int64_t position = 0; // timeline sample
    float value = 0.0f;        // normalized, 0 .. 1
    Shape shape = Shape::Linear; // how the curve goes on to the next point
};

class AutomationCurve;

// Where the last block ended in a curve. Belongs to whoever plays the curve (one per lane).
struct CurveCursor {
    int segment = -2;       // a hint, see locate(). -1 means "before the first point"; -2 "no hint yet"
    long long searches = 0; // how many times locate() had to search (for the check)

    // YOUR TURN (starter/step02.cpp): the index of the last point at or before `position`, or -1,
    // like curve.segmentAt(position). Uses `segment` if it is right for this position in THIS
    // curve; otherwise searches (and counts it) and remembers the answer.
    int locate(const AutomationCurve& curve, std::int64_t position) noexcept;
};

class AutomationCurve {
public:
    explicit AutomationCurve(float defaultValue = 0.0f) : default_(clamp01(defaultValue)) {}

    int add(AutomationPoint point) {
        point.value = clamp01(point.value);
        const auto at = std::upper_bound(points_.begin(), points_.end(), point.position,
                                         [](std::int64_t p, const AutomationPoint& a) { return p < a.position; });
        const auto inserted = points_.insert(at, point);
        return static_cast<int>(inserted - points_.begin());
    }
    bool remove(int index) {
        if (index < 0 || index >= size())
            return false;
        points_.erase(points_.begin() + index);
        return true;
    }
    float valueAt(std::int64_t position) const noexcept {
        if (points_.empty())
            return default_;
        const int i = segmentAt(position);
        if (i < 0)
            return points_.front().value;
        if (i == size() - 1)
            return points_.back().value;
        return between(i, position);
    }

    // ---- YOUR TURN (starter/step02.cpp) --------------------------------------------------------
    // Step 1's render(), with the cursor in place of its one search: out[i] = valueAt(start + i).
    // Afterwards the cursor holds the segment of the last sample written.
    void render(std::int64_t start, float* out, int n, CurveCursor& cursor) const noexcept;

    // ---- provided ---------------------------------------------------------------------------
    void render(std::int64_t start, float* out, int n) const noexcept {
        CurveCursor once;
        render(start, out, n, once);
    }
    const std::vector<AutomationPoint>& points() const noexcept { return points_; }
    int size() const noexcept { return static_cast<int>(points_.size()); }
    float defaultValue() const noexcept { return default_; }

    int segmentAt(std::int64_t position) const noexcept {
        const auto after = std::upper_bound(points_.begin(), points_.end(), position,
                                            [](std::int64_t p, const AutomationPoint& a) { return p < a.position; });
        return static_cast<int>(after - points_.begin()) - 1;
    }
    float between(int i, std::int64_t position) const noexcept {
        const AutomationPoint& a = points_[static_cast<size_t>(i)];
        const AutomationPoint& b = points_[static_cast<size_t>(i) + 1];
        if (a.shape == Shape::Hold || b.position == a.position)
            return a.value;
        const double t = static_cast<double>(position - a.position) / static_cast<double>(b.position - a.position);
        return static_cast<float>(a.value + (static_cast<double>(b.value) - a.value) * t);
    }
    static float clamp01(float v) noexcept { return v >= 0.0f ? (v <= 1.0f ? v : 1.0f) : 0.0f; }

private:
    std::vector<AutomationPoint> points_; // sorted by position; equal positions in the order added
    float default_;
};

// ---- the transport, as far as this step needs it (chapter 14; provided) -------------------------

struct TimelineSegment {
    std::int64_t timelineStart = 0; // timeline position of the segment's first sample
    int blockOffset = 0;            // where the segment starts inside the block
    int numSamples = 0;
};

class MiniTransport {
public:
    // Enough for the checks: blocks of up to 512 samples, loops of at least 64.
    static constexpr int kMaxSegments = 32;

    void play() noexcept { playing_ = true; }
    void stop() noexcept { playing_ = false; }
    bool isPlaying() const noexcept { return playing_; }
    void seek(std::int64_t position) noexcept { position_ = position; }
    std::int64_t position() const noexcept { return position_; }
    void setLoop(std::int64_t start, std::int64_t end) noexcept {
        loopStart_ = start;
        loopEnd_ = end;
    }
    void clearLoop() noexcept { loopEnd_ = loopStart_ = 0; }

    // Describes the next n samples as segments and moves on. Stopped: no segments.
    int advance(int n, TimelineSegment* segments) noexcept {
        if (!playing_)
            return 0;
        const bool looping = loopEnd_ > loopStart_;
        int count = 0, offset = 0;
        while (offset < n && count < kMaxSegments) {
            std::int64_t len = n - offset;
            if (looping && position_ < loopEnd_)
                len = std::min<std::int64_t>(len, loopEnd_ - position_);
            segments[count++] = {position_, offset, static_cast<int>(len)};
            position_ += len;
            offset += static_cast<int>(len);
            if (looping && position_ == loopEnd_)
                position_ = loopStart_;
        }
        return count;
    }

private:
    bool playing_ = false;
    std::int64_t position_ = 0, loopStart_ = 0, loopEnd_ = 0;
};

// ---- this step ----------------------------------------------------------------------------------

class AutomationLane {
public:
    // The biggest jump (from 0 to 1) becomes a ramp of declickSamples samples. (Step 5 computes the
    // number from the sample rate. 64 samples are 1.3 ms at 48 kHz.)
    explicit AutomationLane(int declickSamples = 64)
        : maxStep_(1.0f / static_cast<float>(std::max(1, declickSamples))) {}
    ~AutomationLane() { delete curve_.load(std::memory_order_relaxed); }
    AutomationLane(const AutomationLane&) = delete;
    AutomationLane& operator=(const AutomationLane&) = delete;

    // Audio thread (via a command in Step 5). The lane owns `next` (may be nullptr: no automation).
    // Returns the previous curve, which the CALLER deletes on another thread, like exchangeClips().
    const AutomationCurve* exchangeCurve(const AutomationCurve* next) noexcept {
        return curve_.exchange(next, std::memory_order_acq_rel);
    }
    const AutomationCurve* getCurve() const noexcept { return curve_.load(std::memory_order_acquire); }

    // Any thread: the parameter's value when no curve is playing.
    void setKnobValue(float value) noexcept { knob_.store(AutomationCurve::clamp01(value), std::memory_order_relaxed); }
    float getKnobValue() const noexcept { return knob_.load(std::memory_order_relaxed); }

    long long getSearches() const noexcept { return cursor_.searches; }

    // ---- YOUR TURN (starter/step02.cpp) --------------------------------------------------------
    // Audio thread: one value per sample of the block. segments describe the block (from
    // MiniTransport::advance); no segments means stopped, and the curve's value at `playhead` fills
    // the block. No curve: the knob's value fills it. Then limitSlew(). No allocation, no search
    // while the block simply continues.
    void render(const TimelineSegment* segments, int numSegments, std::int64_t playhead, float* out, int n) noexcept;

private:
    // YOUR TURN: limits |out[i] - out[i-1]| to maxStep_, also across blocks (the very first sample
    // the lane ever renders is taken as it is). A value within the limit stays EXACTLY what it was.
    void limitSlew(float* out, int n) noexcept;

    std::atomic<const AutomationCurve*> curve_{nullptr};
    std::atomic<float> knob_{0.0f};
    CurveCursor cursor_;
    float maxStep_;
    float last_ = 0.0f;
    bool hasLast_ = false;
};

// One gain per sample (provided): what a fader does with the lane's values.
inline void applyGains(float* samples, const float* gains, int n) noexcept {
    for (int i = 0; i < n; ++i)
        samples[i] *= gains[i];
}

} // namespace minidaw
