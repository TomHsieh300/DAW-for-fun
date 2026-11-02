// Step 3 (solution): a CPU budget.
//
// Build and run:  l15_step03_solution   (ctest, label "solution")
// Your version:   starter/step03.cpp -> l15_step03_starter

#include "../minidaw.h"

#include <cmath>

namespace minidaw {

void CpuMeter::addBlock(int numSamples, double secondsSpent) noexcept {
    if (sampleRate_ <= 0.0 || numSamples <= 0)
        return; // no budget, nothing to measure against
    const double budget = numSamples / sampleRate_;
    const double load = secondsSpent / budget;
    // The only writer: a plain load + store is enough.
    blocks_.store(blocks_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    if (load > 1.0)
        overruns_.store(overruns_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    // One-pole smoothing whose coefficient follows the block's length, so the time constant is
    // in seconds whatever the block size.
    const double a = std::exp(-budget / kTimeConstantSeconds);
    smoothed_ = load + a * (smoothed_ - load);
    load_.store(static_cast<float>(smoothed_), std::memory_order_relaxed);
}

CpuStatus CpuMonitor::poll(double nowSeconds) noexcept {
    CpuStatus status;
    status.load = meter_.load();
    const std::uint32_t count = meter_.overruns();
    status.newOverruns = count - seenOverruns_; // unsigned: correct across the wrap-around too
    seenOverruns_ = count;
    if (status.newOverruns > 0)
        lastOverrunAt_ = nowSeconds;
    high_ = high_ ? status.load >= kHighOff : status.load >= kHighOn;
    if (nowSeconds - lastOverrunAt_ < kOverloadHoldSeconds)
        status.state = CpuState::Overload;
    else if (high_)
        status.state = CpuState::High;
    return status;
}

} // namespace minidaw

#include "../check.h"
