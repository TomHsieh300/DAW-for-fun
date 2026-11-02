// Step 3 (starter): a CPU budget.
//
// Build and run:  l15_step03_starter   (prints [FAIL] until both TODOs are done)
// Reference:      ../solution/step03.cpp, docs/step-03-cpu-budget.md ("Your turn")
//
// CpuMeter, CpuMonitor and their data members are in ../minidaw.h; Engine::process() already
// reads the clock and calls addBlock().

#include "../minidaw.h"

namespace minidaw {

void CpuMeter::addBlock(int numSamples, double secondsSpent) noexcept {
    // TODO 1 (audio thread: no allocation, no locks; you are the only writer of every member).
    //   - no budget (sampleRate_ <= 0 or numSamples <= 0): return without counting anything;
    //   - budget = numSamples / sampleRate_ seconds; this block's load = secondsSpent / budget;
    //   - count the block (blocks_), and an overrun (overruns_) if the load is above 1.0. You are
    //     the only writer, so load + store is enough - no fetch_add needed;
    //   - smoothing in SECONDS, not in blocks: a = exp(-budget / kTimeConstantSeconds), then
    //     smoothed_ = load + a * (smoothed_ - load); publish it with load_.store().
    (void)numSamples;
    (void)secondsSpent;
}

CpuStatus CpuMonitor::poll(double nowSeconds) noexcept {
    // TODO 2 (UI thread: only READ the meter; it has one writer, the audio thread).
    //   - newOverruns = meter_.overruns() - seenOverruns_ (unsigned arithmetic survives the
    //     wrap-around); remember the new count; if there were new ones, lastOverrunAt_ = nowSeconds;
    //   - high_ with hysteresis: when not high, high from load >= kHighOn; when high, stays high
    //     while load >= kHighOff;
    //   - state: Overload if nowSeconds - lastOverrunAt_ < kOverloadHoldSeconds, else High if
    //     high_, else Ok.
    (void)nowSeconds;
    (void)meter_;
    return CpuStatus{};
}

} // namespace minidaw

#include "../check.h"
