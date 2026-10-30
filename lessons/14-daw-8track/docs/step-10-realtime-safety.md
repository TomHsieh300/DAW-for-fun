# Step 10 - A real-time safety audit

**English** | [繁體中文](step-10-realtime-safety.zh-TW.md)

[Overview](../README.md) | Previous: [Step 9](step-09-offline-render.md) | Next: [Step 11 - Audio device](step-11-audio-device.md) (stage 2: the app) | Back to the [roadmap](../README.md#4-the-twenty-one-steps)

## Goal

"The audio thread never allocates, never locks and works with any block size" is easy to claim and
easy to break with one innocent `push_back`. Turn the claims into automated tests that fail the
build when somebody breaks them.

## Design

### What real-time means here

The audio device asks for $`N`$ samples every $`N / f_s`$ seconds (512 at 48 kHz: every 10.67 ms). If
`processBlock()` is late *once*, the listener hears a click or a gap. Average speed is irrelevant;
the **worst case** counts. Operations with unbounded or unpredictable duration are therefore banned
on the audio thread:

<div align="center">

| banned | why | what the engine does instead |
|---|---|---|
| `new` / `delete` / `malloc` (also hidden in `std::vector::push_back`, `std::string`, `std::function`, `make_shared`) | the allocator may take a lock or ask the OS for pages | allocate in `prepare()`; ownership arrives in commands |
| mutexes, condition variables, `join` | priority inversion | atomics, wait-free `SpscQueue` |
| file / console I/O (`printf`, logging) | system calls, locks | statistics in atomics, printed by another thread |
| exceptions | allocation + unwinding cost | everything is `noexcept` |
| waiting for another thread | unbounded | the audio thread only ever *polls* queues |

</div>

### Audit 1 - count allocations

C++ lets a program replace the global `operator new` / `operator delete`. The test does that (all
variants: array, nothrow, sized, aligned) and counts calls while a `thread_local` flag is set:

```cpp
thread_local bool tCounting = false;
void* operator new(std::size_t n) { if (tCounting) ++gAllocs; return std::malloc(n ? n : 1); ... }
void operator delete(void* p) noexcept { if (p && tCounting) ++gFrees; std::free(p); }
...
{
    CountScope scope;          // tCounting = true for this thread only
    engine.processBlock(block);
}                              // tCounting = false
```

`thread_local` matters: the UI thread allocates freely (new effects, clip lists) at the same time,
and those allocations must not be counted. Any allocation or free inside `processBlock()` - by the
engine or by any chapter-01..12 DSP class - shows up as a non-zero count.

### Audit 2 - every effect on its own

The same counter wraps 1 s of `process()` calls (with a parameter change every block) for each of
the 20 effect types (the 19 adapters plus the `PureDelayEffect` test instrument of Step 8), so a
failure names the culprit.

### Audit 3 - the whole engine under fire

An 8-track project (two inserts per track cycling through all effect types, sends, a reverb and a
delay bus, convolution + limiter on the master, a loop) runs on a `std::thread` with random block
sizes 1..2048 for 12 s of audio. Meanwhile the main thread, as fast as it can (with a 0.1 ms
pause), does random UI work: faders, pans, mutes, solos, send levels and pre/post, effect
parameters through observer pointers, bypass, add / remove / move / replace inserts (any effect
type), new clip lists, seeks, loop ranges, tempo, play/stop, master fader, `collectGarbage()`. The
audio thread counts allocations and checks each block for NaN / inf.

A good extra exercise is to build this test with a thread sanitizer (Clang/GCC:
`-fsanitize=thread`; not available in MSVC), which reports data races - exactly the bugs the
atomics and queues are supposed to prevent. (The chapter's tests were run with MSVC only.)

### Audit 4 - block-size invariance

`test_block_invariance` renders a project (compressor, EQ, gate, delay bus, limiter, PDC, fades,
sends, a clip deliberately starting at sample 777) with block size 512, 64 and two random sequences
of sizes 1..1500, and requires the maximum difference to be below $`10^{-5}`$.

### The simulated device

`l14_realtime_sim` is the friendly version of audit 3 for humans: the audio thread is paced like a
real device (`sleep_until` the time the previous block "has been played"), the UI does one random
action and then sleeps 5 ms (the OS timer granularity stretches that: about 380 actions in 6 s, i.e.
60 per second, on Windows), and the program reports the CPU load per block relative to its deadline.

## The code

<div align="center">

| File | What |
|---|---|
| `tests/test_realtime_safety.cpp` | operator new/delete replacement, audits 2 and 3 |
| `tests/test_block_invariance.cpp` | audit 4 |
| `examples/realtime_sim.cpp` | the paced simulation with load statistics |

</div>

## Your turn

`steps/step10-realtime-safety/minidaw.h` is your complete Step 9 engine plus three audit tools:
`audit::counters()`, `audit::countingOnThisThread()` (a `thread_local` flag) and
`audit::CountScope`. There is no new engine code - the engine is what you audit.

<div align="center">

| | |
|---|---|
| You write | the counting in the replaced global `operator new` / `operator delete` (only when `countingOnThisThread()` is true), and `audit::renderWithBlockSizes()` (play from 0 with block sizes cycling through a list) |
| File | `steps/step10-realtime-safety/starter/step10.cpp` (the replacements already allocate with `malloc` / `free`; they just do not count yet) |
| Run | `l14_step10_starter`; it prints `[FAIL] l14_step10 (7 of 11 checks failed)` until you are done |
| Solution | `steps/step10-realtime-safety/solution/step10.cpp` (ctest `l14_step10_solution`) |

</div>

The check runs the three audits on your mini engine (8 tracks with fades, Gain / Compressor /
PureDelay inserts on tracks, buses and master, sends, PDC, a loop). Expected output (verified,
Debug build):

```
Step 10: real-time safety audit (build-along)
  counter: new/delete + new[]/delete[] inside a CountScope -> 2 allocations, 2 frees (expected 2, 2)
  another thread allocating 1000 times meanwhile: 0 counted (expected 0)
  audio thread: 3043228 samples, random blocks 1..2048; UI thread: 400 actions, 165 commands applied
  allocations + frees inside processBlock(): 0 + 0 (expected 0), blocks with NaN/inf: 0
  max |difference| 512 vs 64: 0, 512 vs random 1..1500: 0 (expected 0), peak 0.463
[PASS] l14_step10 (11 checks passed)
```

The sample and command counts of the third line vary from run to run (thread timing); the zeros
must not. The first two lines matter as much as the zeros: an audit that counts nothing because the
counter is broken would "pass" - so the check first proves that the counter sees this thread's
allocations and ignores another thread's.

## Verify (reference route)

```powershell
.\scripts\build.ps1 -Only 14-daw-8track       # runs all tests
cd build; .\bin\Debug\l14_test_realtime_safety.exe; .\bin\Debug\l14_realtime_sim.exe
```

Verified output on the author's machine (Debug build):

```
  audio: 577111 samples in 1398 chunks, max block time 53.38 ms
  ui: 603 operations, 191 commands applied, 0 dropped, 0 garbage leaked
  allocations + frees on the audio thread: 0, blocks with NaN/inf: 0
[PASS] l14_test_realtime_safety (25 checks passed)

  max |difference| 512 vs 64: 0, 512 vs random: 0, 512 vs random(2): 0
[PASS] l14_test_block_invariance (5 checks passed)
```

(The sample count is a little above 12 s because the last random block overshoots. The "max block
time" is for a 2048-sample block in a Debug build; it is not a real-time measurement. A Release
build renders 12 s so fast that the UI thread would hardly get a turn, so the audio thread keeps
going until the UI thread has made at least 200 calls: in Release the sample count is larger.)

```
Steps 7 + 10: simulated audio device, random block sizes 32..1024
  audio      : 6.00 s in 537 callbacks (537 engine chunks)
  CPU load   : average 49.4 %, worst block 633.3 % of its deadline (a 24-sample block), 22 deadline misses
  UI         : 381 actions, 135 commands applied, 0 dropped
  garbage    : 44 objects freed on the UI thread, 0 leaked
  NaN blocks : 0
```

In a Debug build the average load is about 50 % and some blocks miss their deadline - the
simulation stays correct, but you would hear dropouts. Debug code (unoptimized, with checked STL
iterators) is several times slower than optimized code, so build Release
(`.\scripts\build.ps1 -Only 14-daw-8track -Config Release`) to see the real headroom. Measured on
the author's machine (Release: three runs):

<div align="center">

| build | average load | worst block | deadline misses | `l14_render_song` (19 s of audio) |
|---|---|---|---|---|
| Debug | 49.4 % | 633 % (24 samples) | 22 | 13.14 s (1.4x real time) |
| Release | 10.0 - 10.1 % | 69 - 109 % (24 samples) | 0 - 1 | 1.38 s (13.8x real time) |

</div>

Notice *which* block is the worst: the last one, cut to 24 samples so that the run ends at exactly
6 s. Its deadline is only 0.5 ms, and much of the engine's work per block does not shrink with the
block (11 strips with meters and smoothers, PDC bookkeeping, every insert's per-block setup).
Small blocks are expensive relative to their duration - which is why low-latency settings (32 or 64
samples) need far more CPU headroom than 512, and why the average load says little about dropouts.
A rare miss in Release also comes from scheduling: this "audio thread" is an ordinary
`std::thread`, while real drivers call back on a high-priority thread. The counts of actions and
commands vary from run to run (thread timing); the zeros (dropped, leaked, NaN) must not.

The difference between the two block-size renders is exactly 0: every smoother advances per
sample, clips and PDC are integer arithmetic, and the DSP classes keep their state across blocks.

## What you learned

- Real-time means bounded worst-case time; the banned list follows from that.
- Replace `operator new` in a test to *prove* the absence of allocations - per thread.
- Fuzz the API from a second thread while the audio thread runs, and check the output for NaN.
- Render with different block sizes and compare: the cheapest test that finds the most bugs in
  audio code.
