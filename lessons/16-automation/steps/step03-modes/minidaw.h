#pragma once
// minidaw.h - Step 3 snapshot of chapter 16: automation and the user.
//
// A curve plays, and the user grabs the fader. Who wins? And when the user wins, the movement is
// recorded, so that it plays back next time. The MODE of the lane decides:
//
//             the curve plays...           the user wins...              recorded
//   Read      always                       never (while playing)         nothing
//   Touch     until touched, after release while touching                while touching
//   Latch     until the first touch        from the first touch to stop  from the first touch to stop
//   Write     never                        from play to stop             from play to stop
//
// Stopped, the hand is simply heard while it touches, in every mode; nothing is recorded.
//
// Recording crosses the threads the other way round:
//
//   audio thread                                            UI thread
//   AutomationLane::render(): where the block is on  --->   Recorder::collect(): groups the events
//   the timeline and what the user's value is               into passes, thins each pass and
//   (CaptureEvents in a fixed queue, no allocation)         punches it into its copy of the curve
//
//   - Only the audio thread knows which timeline position is being heard, so IT stamps the value.
//   - A PASS is one unbroken stretch of timeline. A loop wrap or a seek ends a pass, because the
//     next event does not continue where the last one ended. So does an end marker (numSamples 0)
//     when the user lets go or the transport stops. So does an event the full queue had to drop.
//   - THINNING: about 94 values a second (one per block) are far more than a fader move needs. Keep
//     the points without which the line would be off by more than a tolerance (Ramer-Douglas-Peucker).
//   - PUNCHING: the pass replaces the curve between its first and its last position, with a jump at
//     both ends if needed. The curve outside that range does not change.
//
// This snapshot has no threads yet: the check plays both roles; Step 5 connects them.
//
// YOUR TURN (Step 3): AutomationLane::userWins(), AutomationLane::render(), thin(), punch() and
// Recorder::collect() in starter/step03.cpp.

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace minidaw {

// ---- Steps 1 and 2 (solved) ---------------------------------------------------------------------

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

struct CurveCursor {
    int segment = -2;       // a hint, see locate(). -1 means "before the first point"; -2 "no hint yet"
    long long searches = 0;

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
    void render(std::int64_t start, float* out, int n, CurveCursor& cursor) const noexcept {
        if (n <= 0)
            return;
        if (points_.empty()) {
            std::fill_n(out, n, default_);
            return;
        }
        int i = cursor.locate(*this, start);
        const int last = size() - 1;
        std::int64_t position = start;
        for (int k = 0; k < n; ++k, ++position) {
            while (i < last && points_[static_cast<size_t>(i) + 1].position <= position)
                ++i;
            out[k] = i < 0 ? points_.front().value : (i == last ? points_.back().value : between(i, position));
        }
        cursor.segment = i;
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

inline int CurveCursor::locate(const AutomationCurve& curve, std::int64_t position) noexcept {
    const auto& points = curve.points();
    const int last = curve.size() - 1;
    const int i = segment;
    if (i >= -1 && i <= last && (i < 0 || points[static_cast<size_t>(i)].position <= position) &&
        (i == last || points[static_cast<size_t>(i) + 1].position > position))
        return i;
    ++searches;
    segment = curve.segmentAt(position);
    return segment;
}

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

enum class AutomationMode { Read, Touch, Latch, Write };

// "From `position`, for numSamples samples, the user's value was `value`." numSamples == 0 marks
// the end of the recording: the user let go, or the transport stopped.
struct CaptureEvent {
    std::int64_t position = 0;
    int numSamples = 0;
    float value = 0.0f;
};

// A fixed queue from the audio thread to the UI (provided). Pushing never allocates; when the queue
// is full the event is lost, which ends the pass. (Single-threaded here. Step 5 uses SpscQueue.)
class CaptureQueue {
public:
    static constexpr int kCapacity = 1024;

    bool push(const CaptureEvent& event) noexcept {
        if (size_ == kCapacity) {
            ++dropped_;
            return false;
        }
        events_[static_cast<size_t>((head_ + size_) % kCapacity)] = event;
        ++size_;
        return true;
    }
    bool pop(CaptureEvent& event) noexcept {
        if (size_ == 0)
            return false;
        event = events_[static_cast<size_t>(head_)];
        head_ = (head_ + 1) % kCapacity;
        --size_;
        return true;
    }
    int size() const noexcept { return size_; }
    long long getDropped() const noexcept { return dropped_; }

private:
    std::array<CaptureEvent, kCapacity> events_{};
    int head_ = 0, size_ = 0;
    long long dropped_ = 0;
};

class AutomationLane {
public:
    explicit AutomationLane(int declickSamples = 64)
        : maxStep_(1.0f / static_cast<float>(std::max(1, declickSamples))) {}
    ~AutomationLane() { delete curve_.load(std::memory_order_relaxed); }
    AutomationLane(const AutomationLane&) = delete;
    AutomationLane& operator=(const AutomationLane&) = delete;

    const AutomationCurve* exchangeCurve(const AutomationCurve* next) noexcept {
        return curve_.exchange(next, std::memory_order_acq_rel);
    }
    const AutomationCurve* getCurve() const noexcept { return curve_.load(std::memory_order_acquire); }

    // ---- the user (any thread) ------------------------------------------------------------------
    void setMode(AutomationMode mode) noexcept { mode_.store(mode, std::memory_order_relaxed); }
    AutomationMode getMode() const noexcept { return mode_.load(std::memory_order_relaxed); }
    // The knob's value: what the user's hand says, touching or not.
    void setKnobValue(float value) noexcept { knob_.store(AutomationCurve::clamp01(value), std::memory_order_relaxed); }
    float getKnobValue() const noexcept { return knob_.load(std::memory_order_relaxed); }
    // Mouse down / mouse up on the control. touchCount_ lets the audio thread see a touch that began
    // and ended between two of its blocks: with a large buffer a short click can do that.
    void touch() noexcept {
        touchCount_.fetch_add(1, std::memory_order_relaxed);
        touching_.store(true, std::memory_order_relaxed);
    }
    void release() noexcept { touching_.store(false, std::memory_order_relaxed); }

    CaptureQueue& getCaptureQueue() noexcept { return capture_; }

    // ---- YOUR TURN (starter/step03.cpp) --------------------------------------------------------
    // Audio thread, once per block: does the user's value win this block? See the table at the top
    // of this file. Updates the latch: set by any touch seen while playing in Latch mode, cleared by
    // a stop and by leaving Latch mode.
    bool userWins(bool playing) noexcept;

    // Audio thread: one value per sample of the block, as in Step 2, but the user's value when
    // userWins() (or when there is no curve). When the user wins while playing, one CaptureEvent per
    // segment. When that ends, one end marker (if the queue is full, tried again next block). Then
    // limitSlew(). No allocation.
    void render(const TimelineSegment* segments, int numSegments, std::int64_t playhead, float* out, int n) noexcept;

    // ---- provided ---------------------------------------------------------------------------
    long long getSearches() const noexcept { return cursor_.searches; }

private:
    void limitSlew(float* out, int n) noexcept {
        if (n <= 0)
            return;
        float value = hasLast_ ? last_ : out[0];
        for (int i = 0; i < n; ++i) {
            const float wanted = out[i];
            if (wanted - value > maxStep_)
                value += maxStep_;
            else if (value - wanted > maxStep_)
                value -= maxStep_;
            else
                value = wanted;
            out[i] = value;
        }
        last_ = value;
        hasLast_ = true;
    }

    std::atomic<const AutomationCurve*> curve_{nullptr};
    std::atomic<float> knob_{0.0f};
    std::atomic<AutomationMode> mode_{AutomationMode::Read};
    std::atomic<bool> touching_{false};
    std::atomic<unsigned> touchCount_{0};
    CurveCursor cursor_;
    CaptureQueue capture_;
    float maxStep_;
    float last_ = 0.0f;
    bool hasLast_ = false;
    // audio thread only
    unsigned seenTouches_ = 0; // touchCount_ at the last block
    bool latched_ = false;     // Latch: touched since the transport started
    bool capturing_ = false;   // the last block sent CaptureEvents
    bool endOwed_ = false;     // an end marker still has to go out
};

// YOUR TURN: the points to keep, so that no dropped point is further than `tolerance` (in value, at
// its position) from the line through the kept ones. The first and the last point always stay. The
// positions of `points` must be strictly increasing, as those of a pass are.
std::vector<AutomationPoint> thin(const std::vector<AutomationPoint>& points, float tolerance);

// YOUR TURN: `old` with `pass` (at least two points) in place of everything from the pass's first
// position to its last. Just before the first position and from the last position on, the result
// has the value `old` had there; see docs/step-03-modes.md.
AutomationCurve punch(const AutomationCurve& old, const std::vector<AutomationPoint>& pass);

// UI thread: turns the audio thread's events into edits of the UI's copy of the curve.
class Recorder {
public:
    explicit Recorder(float tolerance = 0.002f) : tolerance_(tolerance) {}

    // YOUR TURN: empties the queue. Each event that continues the pass adds a point (its position,
    // its value); an event that does not continue where the pass ended, and an end marker, finish
    // the pass: add a last point (where the pass ended, the last value), thin(), punch() into
    // `curve`. Returns true if `curve` changed (then the caller publishes a copy of it).
    bool collect(CaptureQueue& queue, AutomationCurve& curve);

    // ---- provided ---------------------------------------------------------------------------
    bool isRecording() const noexcept { return !pass_.empty(); }

private:
    float tolerance_;
    std::vector<AutomationPoint> pass_; // the pass so far
    std::int64_t passEnd_ = 0;          // where the last event ended
    float lastValue_ = 0.0f;
};

} // namespace minidaw
