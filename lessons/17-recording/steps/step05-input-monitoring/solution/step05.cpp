// Step 5 (solution): input monitoring.
//
// Build and run:  l17_step05_solution   (ctest, label "solution")
// Your version:   starter/step05.cpp -> l17_step05_starter

#include "../minidaw.h"

namespace minidaw {

std::int64_t monitorLatency(const Latencies& latencies, MonitorPath path) noexcept {
    const std::int64_t device = static_cast<std::int64_t>(latencies.input) + latencies.output;
    return path == MonitorPath::ThroughInserts ? device + latencies.mix : device;
}

bool wantsInput(MonitorMode mode, bool playing, std::int64_t position, const TimeWindow& window) noexcept {
    switch (mode) {
    case MonitorMode::Off:
        return false;
    case MonitorMode::Always:
        return true;
    case MonitorMode::Auto:
        return !playing || (position >= window.from && position < window.to);
    }
    return false;
}

void FeedbackGuard::listen(float peak) noexcept {
    windowPeak_ = std::max(windowPeak_, peak);
    if (++inWindow_ < windowFrames_)
        return;

    rising_ = windowPeak_ >= kLoud && windowPeak_ >= kRise * lastPeak_ ? rising_ + 1 : 0;
    pinned_ = windowPeak_ >= kPinned ? pinned_ + 1 : 0;
    if (rising_ >= kRisingWindows || pinned_ >= kPinnedWindows)
        tripped_ = true;
    lastPeak_ = windowPeak_;
    windowPeak_ = 0.0f;
    inWindow_ = 0;
}

void Monitor::run(const float* const* input, int numInputChannels, float* const* output, int numOutputChannels, int offset,
                  int count, std::int64_t position, bool playing, MonitorMode mode, float level,
                  const RecordRange& range) noexcept {
    const TimeWindow window = visibleWindow(range, position);
    for (int i = offset; i < offset + count; ++i) {
        const bool on = wantsInput(mode, playing, position + (i - offset), window) && !guard_.tripped();
        const float target = on ? level : 0.0f;
        gain_ += std::clamp(target - gain_, -rampStep_, rampStep_);
        if (gain_ <= 0.0f)
            continue; // not heard, so it cannot come back: nothing to listen for

        float peak = 0.0f;
        for (int ch = 0; ch < numInputChannels; ++ch)
            if (input[ch] != nullptr)
                peak = std::max(peak, std::abs(input[ch][i]));
        guard_.listen(peak);

        // A mono input goes to every output channel.
        for (int ch = 0; ch < numOutputChannels && numInputChannels > 0; ++ch) {
            const float* source = input[std::min(ch, numInputChannels - 1)];
            if (source != nullptr)
                output[ch][i] += gain_ * source[i];
        }
    }
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
