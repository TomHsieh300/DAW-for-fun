# Step 3 - A CPU budget

**English** | [繁體中文](step-03-cpu-budget.zh-TW.md)

[Overview](../README.md) | Previous: [Step 2 - A pool of tracks](step-02-track-pool.md) | Next: [Step 4 - Saving and opening projects](step-04-project-files.md)

## Goal

Measure how much of its time budget the audio thread uses, count the blocks that did not make
it, and turn both into a warning the user can act on. Along the way, see why the number of tracks
a machine can play is a question of CPU time, not of a track count.

## Design

### The budget of a block

The audio device asks for a block of $`N`$ samples and needs it before it has played the previous
one. At sample rate $`f_s`$ that leaves

```math
T_{\text{budget}} = \frac{N}{f_s}
```

seconds - 1.33 ms for 64 samples at 48 kHz, 21.3 ms for 1024. The load of one block is the time
the callback actually spent, divided by that budget:

```math
\text{load} = \frac{t_{\text{spent}}}{T_{\text{budget}}}
```

A load of 1.0 uses the whole budget. Above 1.0 the block is late: the device has nothing to play,
and the listener hears a click or a gap. Such a block is an **overrun**.

### What limits the number of tracks

Step 2's `kMaxTracks` is a memory limit: room for 128 tracks, allocated up front. Whether those
tracks can be *played* depends on what they do. The self-check feeds the meter two made-up
sessions at 64-sample blocks:

<div align="center">

| Session | Time per block | Load | Overruns in 3 s |
|---|---|---|---|
| 16 tracks with a gain (2 µs each) | 32 µs | 2.4 % | 0 |
| 4 tracks with a convolution reverb (400 µs each) | 1.6 ms | 120 % | every block |

</div>

Four tracks overload the machine that plays sixteen with ease. That is why a DAW does not refuse
the 17th track; it shows a CPU meter and lets the user decide - freeze a track, raise the buffer
size, use a lighter plug-in. This step builds that meter.

### What chapter 14's meter gets wrong

DawLab 8 already shows "CPU 12 %" (`AudioHost::getCpuLoad()`). It measures the right thing but
has two weaknesses:

1. **The smoothing counts callbacks, not seconds.** It moves 10 % towards each callback's load.
   With 64-sample blocks that settles in about 13 ms - the display jitters; with 1024-sample blocks
   it takes about 200 ms. The same mix looks different at another buffer size.
2. **An average hides the blocks that matter.** A steady 40 % with a single block at 130 % still
   reads 41.5 %. Nothing looks wrong, but the user heard a click.

### Smoothing in seconds

A one-pole filter whose coefficient follows the block's own length has a time constant in
seconds, whatever the block size:

```math
a = e^{-T_{\text{budget}} / \tau}, \qquad \bar{\ell} \leftarrow \ell + a \, (\bar{\ell} - \ell)
```

with $`\ell`$ the block's load, $`\bar{\ell}`$ the smoothed load and $`\tau = 0.3`$ s. After one
time constant the display has moved 63 % of the way to a new steady load, at 64 samples
and at 1024 alike. The check shows exactly that - 0.506 and 0.504 after 0.3 s at 80 % - where
chapter 14's formula gives 0.800 and 0.617.

### Count the overruns, separately

The smoothed load is for the eye. What the ear hears is overruns, so the meter counts every block
whose load is above 1.0, in a counter that only counts up. The audio thread is its only writer.
The UI never resets it; the monitor remembers the count it saw last and reports the difference.
That keeps one writer per value (Step 2's rule), and it has a second benefit: a monitor that
starts late - a window opened after a few overruns - does not report old news.

An overrun is a block that *our* processing made late. The audio device can also run dry for
reasons outside the callback (another process, a driver, power management); those are the
device's xruns, and JUCE reports them through `AudioIODevice::getXRunCount()` (or -1 where the
driver cannot tell). The app of Step 7 shows the first; adding the device's count is a small
extension.

### From numbers to a warning

The transport bar needs one of three states, and the user should not have to interpret numbers:

<div align="center">

| State | When | What it tells the user |
|---|---|---|
| **Overload** | an overrun less than 2 s ago | you heard (or will hear) a dropout |
| **High** | the smoothed load is high | close to the limit: one more heavy plug-in may be too much |
| **Ok** | otherwise | |

</div>

"High" uses **hysteresis**: it switches on at 75 % and off only below 65 %. A load that hovers
around 75 % would otherwise make the warning flicker. "Overload" is held for 2 s, because a single
overrun lasts a few milliseconds - far too short to see.

### Keep the logic pure

The clock is read in exactly one place, `Engine::process()`. `CpuMeter::addBlock()` takes the
time spent as an argument, and `CpuMonitor::poll()` takes the current time as an argument. So
nearly every check feeds them made-up times and is exact on every machine, in Debug and in
Release - instead of a test that passes or fails depending on how busy the computer is.
`std::chrono::steady_clock` is the right clock for the measurement: it never jumps (unlike the
wall clock) and reading it does not lock or allocate.

### Threads

<div align="center">

| Value | Written by | Read by |
|---|---|---|
| `smoothed_` | audio thread | audio thread |
| `load_`, `overruns_`, `blocks_` (atomics) | audio thread | UI thread |
| `seenOverruns_`, `lastOverrunAt_`, `high_` | UI thread | UI thread |
| `sampleRate_` | `prepare()`, before the audio thread starts | audio thread |

</div>

Relaxed atomics are enough here: each value is a single number for display, and nothing else is
published through it. The last part of the check runs a real audio thread while the UI thread
polls. Built with `--sanitize thread`, it passes; with `load_` changed to a plain `float`,
ThreadSanitizer reports a data race and the run exits with code 66.

## The code

`steps/step03-cpu-budget/minidaw.h` (abridged):

```cpp
class CpuMeter {                       // audio thread writes, UI thread reads
public:
    static constexpr double kTimeConstantSeconds = 0.3;
    void prepare(double sampleRate) noexcept;
    void addBlock(int numSamples, double secondsSpent) noexcept; // YOUR TURN
    float load() const noexcept;                 // smoothed; 1.0 = the whole budget
    std::uint32_t overruns() const noexcept;     // only counts up
private:
    double sampleRate_ = 0.0;
    double smoothed_ = 0.0;                      // audio thread only
    std::atomic<float> load_{0.0f};
    std::atomic<std::uint32_t> overruns_{0};
    std::atomic<std::uint64_t> blocks_{0};
};

class CpuMonitor {                     // UI thread
public:
    explicit CpuMonitor(const CpuMeter& meter) noexcept; // starts from the current count
    CpuStatus poll(double nowSeconds) noexcept;          // YOUR TURN
private:
    std::uint32_t seenOverruns_;
    double lastOverrunAt_ = -1.0e30;
    bool high_ = false;
};

void Engine::process(float* out, int numSamples) noexcept {
    const auto start = std::chrono::steady_clock::now();
    // ... commands and render, as in Step 2 ...
    const std::chrono::duration<double> spent = std::chrono::steady_clock::now() - start;
    meter_.addBlock(numSamples, spent.count());
}
```

## Your turn

`steps/step03-cpu-budget/minidaw.h` has Steps 1 and 2 solved, `CpuMeter` and `CpuMonitor` with
their data members, and `Engine::process()` already reading the clock.

<div align="center">

| | |
|---|---|
| You write | `CpuMeter::addBlock()` and `CpuMonitor::poll()` |
| File | `steps/step03-cpu-budget/starter/step03.cpp` |
| Run | `l15_step03_starter`; it prints `[FAIL] l15_step03 (16 of 25 checks failed)` until you are done |
| Solution | `steps/step03-cpu-budget/solution/step03.cpp` (ctest `l15_step03_solution`) |

</div>

In `addBlock()` you are the only writer of every member, so `overruns_.store(overruns_.load() + 1)`
is enough. In `poll()` compute the new overruns with unsigned subtraction: it stays correct when
the counter wraps around after $`2^{32}`$ overruns.

Expected output (verified; the last two lines depend on the machine):

```
Step 3: a CPU budget (build-along)
  10 ms budget: blocks taking 5, 12 and 9.9 ms -> 1 overrun(s) (expected 1), 3 blocks
  0.3 s at 80 % load, then read the meter:  64-sample blocks 0.506, 1024-sample blocks 0.504 (expected 0.506 both)
  (chapter 14's 10 % per callback would show 0.800 and 0.617)
  steady 40 %, then one block at 130 %: load 0.415, 1 new overrun -> Overload; 0.9 s later Overload;
  2.4 s later Ok (expected below 0.5, 1, Overload, Overload, Ok - the average hides the spike)
  load 70, 80, 70, 60, 70 %: Ok, High, High, Ok, Ok (expected Ok, High, High, Ok, Ok)
  3 overruns before the monitor existed: first poll 0 new, Ok (expected 0, Ok)
  64-sample blocks (1.33 ms): 16 cheap tracks -> 2.4 %, 0 overruns; 4 heavy tracks -> 120 %, 2250 overruns
  (expected 2.4 %, 0; 120 %, 2250)
  the engine, 200 blocks of 256 samples on this machine: 200 blocks measured, load 0.0006 %
  2 threads: 5000 blocks measured while the UI polled 31248 times; overruns seen by the UI 0, counted 0
[PASS] l15_step03 (25 checks passed)
```

## What you learned

- A block of $`N`$ samples has $`N / f_s`$ seconds; load is time spent over that budget, and a
  block above 1.0 is an overrun - a dropout.
- What limits a session is what its tracks cost, not how many there are; a DAW shows the cost and
  lets the user decide.
- Smooth in seconds, not in callbacks, or the meter behaves differently at every buffer size.
- An average hides spikes: count overruns separately, in a counter with one writer that only
  counts up.
- Keep the clock at the edge and the logic pure, and the tests are exact on every machine.
