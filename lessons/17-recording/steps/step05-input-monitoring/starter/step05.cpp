// Step 5 (starter): input monitoring.
//
// Build and run:  l17_step05_starter   (prints [FAIL] until all four TODOs are done)
// Reference:      ../solution/step05.cpp, docs/step-05-input-monitoring.md ("Your turn")
//
// MonitorPath, MonitorMode, the classes and everything of Steps 1 to 4 (solved) are in ../minidaw.h.

#include "../minidaw.h"

namespace minidaw {

std::int64_t monitorLatency(const Latencies& latencies, MonitorPath path) noexcept {
    // TODO 1: input + output. Through the inserts, the mix's compensation on top.
    (void)latencies;
    (void)path;
    return -1;
}

bool wantsInput(MonitorMode mode, bool playing, std::int64_t position, const TimeWindow& window) noexcept {
    // TODO 2: Off: no. Always: yes. Auto: yes when stopped, and when position is inside the window.
    (void)mode;
    (void)playing;
    (void)position;
    (void)window;
    return false;
}

void FeedbackGuard::listen(float peak) noexcept {
    // TODO 3: Keep the highest peak of this window in windowPeak_, and count the frames in inWindow_.
    //         Only when the window is full (windowFrames_):
    //           rising_ + 1 if the window's peak is >= kLoud and >= kRise * lastPeak_; otherwise 0.
    //           pinned_ + 1 if it is >= kPinned; otherwise 0.
    //           tripped_ if rising_ reached kRisingWindows, or pinned_ reached kPinnedWindows.
    //           lastPeak_ is this window's peak. Start a new window.
    (void)peak;
}

void Monitor::run(const float* const* input, int numInputChannels, float* const* output, int numOutputChannels, int offset,
                  int count, std::int64_t position, bool playing, MonitorMode mode, float level,
                  const RecordRange& range) noexcept {
    // TODO 4: visibleWindow(range, position). For each frame i in [offset, offset + count), which is at
    //         position + (i - offset):
    //           The target is `level` if wantsInput() and the guard has not tripped; otherwise 0.
    //           Move gain_ towards it by at most rampStep_ (std::clamp).
    //           If gain_ is 0: next frame. What is not heard cannot come back.
    //           The highest absolute sample of all input channels goes to guard_.listen().
    //           Add gain_ * input to each output channel. If there are fewer input channels than
    //           output channels, the last input channel goes to the rest. Any input channel can be
    //           nullptr; there may be no input channels at all.
    (void)input;
    (void)numInputChannels;
    (void)output;
    (void)numOutputChannels;
    (void)offset;
    (void)count;
    (void)position;
    (void)playing;
    (void)mode;
    (void)level;
    (void)range;
}

void Monitor::process(const float* const* input, int numInputChannels, float* const* output, int numOutputChannels,
                      const TimelineSegment* segments, int numSegments, int n) noexcept {
    if (resetRequested_.exchange(false, std::memory_order_relaxed))
        guard_.reset();
    const MonitorMode mode = mode_.load(std::memory_order_relaxed);
    const float level = level_.load(std::memory_order_relaxed);
    RecordRange range;
    range.loopStart = loopStart_.load(std::memory_order_relaxed);
    range.loopEnd = loopEnd_.load(std::memory_order_relaxed);
    range.punchIn = punchIn_.load(std::memory_order_relaxed);
    range.punchOut = punchOut_.load(std::memory_order_relaxed);

    if (numSegments == 0)
        run(input, numInputChannels, output, numOutputChannels, 0, n, 0, false, mode, level, range);
    for (int s = 0; s < numSegments; ++s)
        run(input, numInputChannels, output, numOutputChannels, segments[s].blockOffset, segments[s].numSamples,
            segments[s].timelineStart, true, mode, level, range);

    feedback_.store(guard_.tripped(), std::memory_order_relaxed);
}

} // namespace minidaw

#include "../check.h"
