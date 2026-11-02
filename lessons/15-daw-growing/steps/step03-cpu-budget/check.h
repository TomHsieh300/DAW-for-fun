#pragma once
// check.h - the self-check of Step 3 (a CPU budget). Defines main(); included at the END of
// starter/step03.cpp and solution/step03.cpp, so both run exactly the same checks.
//
// Tests only what Step 3 adds. CpuMeter and CpuMonitor take the time as an argument, so almost
// every check feeds them made-up times and is exact on every machine. Only the last two parts use
// the real clock: the Engine measuring itself, and a UI thread polling while an audio thread runs
// (for ThreadSanitizer: scripts/build.sh --sanitize thread).

#include <dawlab/Common.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

namespace step03_check {

using namespace minidaw;

constexpr double kRate = 48000.0;

inline const char* nameOf(CpuState s) {
    switch (s) {
        case CpuState::Ok: return "Ok";
        case CpuState::High: return "High";
        case CpuState::Overload: return "Overload";
    }
    return "?";
}

// Feeds `seconds` worth of blocks of `blockSize` samples, each at `load` (time spent = load x budget).
inline void feed(CpuMeter& meter, int blockSize, double load, double seconds) {
    const double budget = blockSize / kRate;
    const int blocks = static_cast<int>(std::lround(seconds / budget));
    for (int i = 0; i < blocks; ++i)
        meter.addBlock(blockSize, load * budget);
}

inline void budgetAndOverruns() {
    CpuMeter meter;
    meter.prepare(kRate);
    meter.addBlock(480, 0.005); // budget 480 / 48000 = 10 ms, spent 5 ms: load 0.5
    const std::uint32_t afterHalf = meter.overruns();
    meter.addBlock(480, 0.012); // 12 ms of a 10 ms budget: an overrun
    meter.addBlock(480, 0.0099); // just inside the budget: in time
    std::printf("  10 ms budget: blocks taking 5, 12 and 9.9 ms -> %u overrun(s) (expected 1), %llu blocks\n",
                meter.overruns(), static_cast<unsigned long long>(meter.blocks()));
    DAWLAB_CHECK(afterHalf == 0 && meter.overruns() == 1);
    DAWLAB_CHECK(meter.blocks() == 3);
    DAWLAB_CHECK(meter.load() > 0.0f && meter.load() < 1.0f);

    // No budget, no measurement: an empty block, and a meter that was never prepared.
    meter.addBlock(0, 1.0);
    CpuMeter unprepared;
    unprepared.addBlock(480, 1.0);
    DAWLAB_CHECK(meter.blocks() == 3 && meter.overruns() == 1);
    DAWLAB_CHECK(unprepared.blocks() == 0 && unprepared.overruns() == 0 && unprepared.load() == 0.0f);
}

// The smoothed load must take the same TIME to settle at every block size.
inline void smoothingInSeconds() {
    const double target = 0.8 * (1.0 - std::exp(-1.0)); // 63 % of the way to 0.8 after one time constant
    double ours[2] = {}, ch14[2] = {};
    const int sizes[2] = {64, 1024};
    for (int k = 0; k < 2; ++k) {
        CpuMeter meter;
        meter.prepare(kRate);
        feed(meter, sizes[k], 0.8, CpuMeter::kTimeConstantSeconds);
        ours[k] = meter.load();
        // chapter 14: old + 0.1 (load - old) per callback, whatever the callback's length
        const int blocks = static_cast<int>(std::lround(CpuMeter::kTimeConstantSeconds * kRate / sizes[k]));
        ch14[k] = 0.8 * (1.0 - std::pow(0.9, blocks));
    }
    std::printf("  0.3 s at 80 %% load, then read the meter:  64-sample blocks %.3f, 1024-sample blocks %.3f (expected %.3f both)\n"
                "  (chapter 14's 10 %% per callback would show %.3f and %.3f)\n",
                ours[0], ours[1], target, ch14[0], ch14[1]);
    DAWLAB_CHECK_NEAR(ours[0], target, 0.01);
    DAWLAB_CHECK_NEAR(ours[1], target, 0.01);
}

inline void spikes() {
    CpuMeter meter;
    meter.prepare(kRate);
    CpuMonitor monitor(meter);
    feed(meter, 256, 0.4, 2.0); // a steady 40 %
    const CpuStatus calm = monitor.poll(10.0);
    meter.addBlock(256, 1.3 * 256 / kRate); // ONE block at 130 %: a dropout
    const CpuStatus spike = monitor.poll(10.1);
    const CpuStatus held = monitor.poll(11.0);
    feed(meter, 256, 0.4, 1.0); // settled again
    const CpuStatus later = monitor.poll(12.5);
    std::printf("  steady 40 %%, then one block at 130 %%: load %.3f, %u new overrun -> %s; 0.9 s later %s;\n"
                "  2.4 s later %s (expected below 0.5, 1, Overload, Overload, Ok - the average hides the spike)\n",
                spike.load, spike.newOverruns, nameOf(spike.state), nameOf(held.state), nameOf(later.state));
    DAWLAB_CHECK(calm.state == CpuState::Ok && calm.newOverruns == 0);
    DAWLAB_CHECK(spike.load < 0.5f && spike.newOverruns == 1 && spike.state == CpuState::Overload);
    DAWLAB_CHECK(held.newOverruns == 0 && held.state == CpuState::Overload);
    DAWLAB_CHECK(later.newOverruns == 0 && later.state == CpuState::Ok);
    DAWLAB_CHECK_NEAR(later.load, 0.4, 0.01);
}

inline void hysteresis() {
    CpuMeter meter;
    meter.prepare(kRate);
    CpuMonitor monitor(meter);
    const double loads[5] = {0.7, 0.8, 0.7, 0.6, 0.7};
    CpuState states[5] = {};
    for (int i = 0; i < 5; ++i) {
        feed(meter, 512, loads[i], 3.0); // 10 time constants: settled
        states[i] = monitor.poll(3.0 * (i + 1)).state;
    }
    std::printf("  load 70, 80, 70, 60, 70 %%: %s, %s, %s, %s, %s (expected Ok, High, High, Ok, Ok)\n",
                nameOf(states[0]), nameOf(states[1]), nameOf(states[2]), nameOf(states[3]), nameOf(states[4]));
    DAWLAB_CHECK(states[0] == CpuState::Ok && states[1] == CpuState::High && states[2] == CpuState::High);
    DAWLAB_CHECK(states[3] == CpuState::Ok && states[4] == CpuState::Ok);
}

inline void lateMonitor() {
    CpuMeter meter;
    meter.prepare(kRate);
    feed(meter, 256, 1.5, 0.016); // 3 overruns while nobody was watching
    feed(meter, 256, 0.1, 3.0);
    CpuMonitor monitor(meter);
    const CpuStatus first = monitor.poll(100.0);
    std::printf("  3 overruns before the monitor existed: first poll %u new, %s (expected 0, Ok)\n", first.newOverruns,
                nameOf(first.state));
    DAWLAB_CHECK(meter.overruns() == 3);
    DAWLAB_CHECK(first.newOverruns == 0 && first.state == CpuState::Ok);
}

// What limits a session is what its tracks cost, not how many there are.
inline void costNotCount() {
    const double budget = 64 / kRate; // 1.33 ms at 64 samples
    CpuMeter manyCheap, fewHeavy;
    manyCheap.prepare(kRate);
    fewHeavy.prepare(kRate);
    const int blocks = static_cast<int>(3 * kRate / 64); // three seconds: 10 time constants, settled
    for (int i = 0; i < blocks; ++i) {
        manyCheap.addBlock(64, 16 * 2.0e-6);  // 16 tracks with a gain: 2 us each
        fewHeavy.addBlock(64, 4 * 400.0e-6);  // 4 tracks with a convolution reverb: 400 us each
    }
    std::printf("  64-sample blocks (%.2f ms): 16 cheap tracks -> %.1f %%, %u overruns; 4 heavy tracks -> %.0f %%, %u overruns\n"
                "  (expected 2.4 %%, 0; 120 %%, %d)\n",
                budget * 1000.0, 100.0 * manyCheap.load(), manyCheap.overruns(), 100.0 * fewHeavy.load(),
                fewHeavy.overruns(), blocks);
    DAWLAB_CHECK_NEAR(manyCheap.load(), 0.024, 0.001);
    DAWLAB_CHECK(manyCheap.overruns() == 0);
    DAWLAB_CHECK_NEAR(fewHeavy.load(), 1.2, 0.01);
    DAWLAB_CHECK(fewHeavy.overruns() == static_cast<std::uint32_t>(blocks));
}

// The Engine measures itself with the real clock. The numbers depend on the machine; only the
// block count is exact.
inline void engineMeasuresItself() {
    Engine engine;
    engine.prepare(kRate);
    TrackManager ui(engine);
    ui.addTrack("Drums", 0.25f);
    ui.addTrack("Bass", 0.25f);
    std::vector<float> block(kMaxBlockSize);
    for (int i = 0; i < 200; ++i)
        engine.process(block.data(), kMaxBlockSize);
    const float load = engine.meter().load();
    std::printf("  the engine, 200 blocks of %d samples on this machine: %llu blocks measured, load %.4f %%\n", kMaxBlockSize,
                static_cast<unsigned long long>(engine.meter().blocks()), 100.0 * load);
    DAWLAB_CHECK(engine.meter().blocks() == 200);
    DAWLAB_CHECK(std::isfinite(load) && load >= 0.0f);
}

// The UI thread polls while the audio thread measures: they share only the meter's atomics.
inline void twoThreads() {
    Engine engine;
    engine.prepare(kRate);
    TrackManager ui(engine);
    CpuMonitor monitor(engine.meter());
    constexpr int kBlocks = 5000;
    std::atomic<bool> done{false};
    std::thread audio([&] {
        std::vector<float> block(kMaxBlockSize);
        for (int i = 0; i < kBlocks; ++i) {
            engine.process(block.data(), kMaxBlockSize);
            if (i % 8 == 0)
                std::this_thread::yield();
        }
        done.store(true, std::memory_order_release);
    });
    int polls = 0;
    std::uint32_t overruns = 0;
    const auto start = std::chrono::steady_clock::now();
    while (!done.load(std::memory_order_acquire)) {
        const std::chrono::duration<double> now = std::chrono::steady_clock::now() - start;
        const CpuStatus s = monitor.poll(now.count());
        overruns += s.newOverruns;
        if (polls % 16 == 0)
            ui.addTrack("T", 0.01f); // structure changes keep going too
        ++polls;
        std::this_thread::yield();
    }
    audio.join();
    overruns += monitor.poll(1.0e6).newOverruns;
    std::printf("  2 threads: %d blocks measured while the UI polled %d times; overruns seen by the UI %u, counted %u\n",
                static_cast<int>(engine.meter().blocks()), polls, overruns, engine.meter().overruns());
    DAWLAB_CHECK(engine.meter().blocks() == static_cast<std::uint64_t>(kBlocks));
    DAWLAB_CHECK(polls > 0);
    DAWLAB_CHECK(overruns == engine.meter().overruns()); // the UI saw every overrun exactly once
}

inline int run() {
    std::printf("Step 3: a CPU budget (build-along)\n");
    budgetAndOverruns();
    smoothingInSeconds();
    spikes();
    hysteresis();
    lateMonitor();
    costNotCount();
    engineMeasuresItself();
    twoThreads();
    return dawlab::test::summary("l15_step03");
}

} // namespace step03_check

int main() { return step03_check::run(); }
