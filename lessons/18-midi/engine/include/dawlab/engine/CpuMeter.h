#pragma once
// CpuMeter and CpuMonitor: the CPU budget of chapter 15, Step 3, as part of the engine.
//
// A block of n samples at sample rate fs must be ready within n / fs seconds - its budget.
// load = time spent / budget: 1.0 is the whole budget; above 1.0 the device runs dry (an overrun).
//
//   CpuMeter    audio thread: Engine::processBlock() measures itself and calls addBlock(); the
//               load is smoothed in SECONDS (the same at every block size) and every overrun is
//               counted in a counter that only counts up. The audio thread is its only writer.
//   CpuMonitor  UI thread: turns the meter into Ok / High / Overload, with hysteresis and a
//               hold; it remembers the count it saw instead of resetting the meter.
//
// Both take the time as an argument (the clock is read only in Engine::processBlock()), so
// tests can feed them made-up times.

#include <atomic>
#include <cmath>
#include <cstdint>

namespace dawlab::engine {

class CpuMeter {
public:
    static constexpr double kTimeConstantSeconds = 0.3;

    // Before the audio thread starts.
    void prepare(double sampleRate) noexcept { sampleRate_ = sampleRate; }

    // Audio thread, once per block: numSamples were produced in secondsSpent.
    void addBlock(int numSamples, double secondsSpent) noexcept {
        if (sampleRate_ <= 0.0 || numSamples <= 0)
            return; // no budget, nothing to measure against
        const double budget = numSamples / sampleRate_;
        const double load = secondsSpent / budget;
        // The only writer: a plain load + store is enough.
        blocks_.store(blocks_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
        if (load > 1.0)
            overruns_.store(overruns_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
        const double a = std::exp(-budget / kTimeConstantSeconds);
        smoothed_ = load + a * (smoothed_ - load);
        load_.store(static_cast<float>(smoothed_), std::memory_order_relaxed);
    }

    // ---- any thread ------------------------------------------------------------------------
    float load() const noexcept { return load_.load(std::memory_order_relaxed); } // smoothed; 1.0 = full
    std::uint32_t overruns() const noexcept { return overruns_.load(std::memory_order_relaxed); }
    std::uint64_t blocks() const noexcept { return blocks_.load(std::memory_order_relaxed); }

private:
    double sampleRate_ = 0.0; // set in prepare(), before the audio thread runs
    double smoothed_ = 0.0;   // audio thread only
    std::atomic<float> load_{0.0f};
    std::atomic<std::uint32_t> overruns_{0};
    std::atomic<std::uint64_t> blocks_{0};
};

enum class CpuState { Ok, High, Overload };

struct CpuStatus {
    CpuState state = CpuState::Ok;
    float load = 0.0f;
    std::uint32_t newOverruns = 0; // since the previous poll
};

class CpuMonitor {
public:
    static constexpr float kHighOn = 0.75f;
    static constexpr float kHighOff = 0.65f;
    static constexpr double kOverloadHoldSeconds = 2.0;

    explicit CpuMonitor(const CpuMeter& meter) noexcept : meter_(meter), seenOverruns_(meter.overruns()) {}

    // UI thread, e.g. ten times a second; nowSeconds from any clock that only counts up.
    CpuStatus poll(double nowSeconds) noexcept {
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

private:
    const CpuMeter& meter_;
    std::uint32_t seenOverruns_;
    double lastOverrunAt_ = -1.0e30;
    bool high_ = false;
};

} // namespace dawlab::engine
