# Step 8 - Plugin delay compensation (PDC)

**English** | [繁體中文](step-08-pdc.zh-TW.md)

[Overview](../README.md) | Previous: [Step 7](step-07-commands.md) | Next: [Step 9 - Offline render](step-09-offline-render.md)

## Goal

Some effects delay their signal: a lookahead limiter (chapter 05) has to see a peak before it
arrives, an oversampled saturator (chapter 06) has linear-phase filters, a partitioned convolution
(chapter 12) needs a full partition of input. Make every path through the mixer equally late, so
that tracks stay in time with each other no matter which effects they use.

## Design

### What goes wrong without PDC

Track 1 and track 2 play the same snare; track 1 has an insert with latency $`D`$. The master gets

$$
y[n] = x[n] + x[n - D]
\quad\Longrightarrow\quad
H(e^{j\omega}) = 1 + e^{-j\omega D},\qquad |H| = 2\left|\cos\frac{\omega D}{2}\right| = 2\left|\cos\frac{\pi f D}{f_s}\right|
$$

a comb filter with notches at $`f_k = \frac{(2k+1) f_s}{2D}`$. For a 5 ms lookahead ($`D = 240`$ at
48 kHz) the notches are at 100, 300, 500, ... Hz: the kick and snare lose their body and every
hit is doubled ("flam"). Even when the two tracks play *different* parts, rhythm feels sloppy -
5 ms is audible for tight percussion.

### The rule

Every insert reports its latency $`L`$; a chain reports the sum of its non-bypassed inserts
(effects in series add their delays). Then

$$
c_t = \max_u L_u - L_t \quad(\text{track } t), \qquad
c_b = \max_v L_v - L_b \quad(\text{bus } b), \qquad
c_{\text{sum}} = \max_v L_v
$$

$$
L_{\text{total}} = \max_u L_u + \max_v L_v + L_{\text{master}}
$$

```
track t:  clips -> inserts (L_t) -> delay c_t -> mute/solo -> sends --------+
                                                   |                        |
                                                   +-> fader/pan -> track sum -> delay c_sum --+
bus b:   sends -> inserts (L_b) -> delay c_b -> fader --------------------------------------+--> master (L_m)
```

- Each track's compensator sits *after its inserts and before its sends*, so all tracks are
  aligned (at $`\max L_u`$) when they reach both the dry sum and the buses.
- A bus adds its own $`L_b`$; padding each bus to $`\max L_v`$ aligns the returns with each other.
- The dry sum must wait for the returns: it is delayed by $`\max L_v`$.
- The master's latency cannot be compensated inside the mix (everything passes through it); it is
  part of the reported total, which the host (or the bounce, Step 9) compensates at the output.

Note that a bus with latency delays the whole mix even if nothing is sent to it - the engine cannot
know that it is silent. An unused latent effect is a waste.

### The delay line

`LatencyCompensator` is an integer delay: a ring buffer of `maxDelay + 1` samples per channel,
allocated in `prepare()`; `setDelaySamples()` only moves the read offset.

$$
y[n] = x[n - c], \qquad 0 \le c \le 16\,384
$$

Changing $`c`$ makes the output jump (the read position skips or repeats samples) - a click, but it
only happens when an insert is added, removed, bypassed or changes its latency. The ring is written
on every block, also while $`c = 0`$; otherwise growing $`c`$ from 0 to $`D`$ would replay $`D`$ samples of
stale audio from whenever the delay was last in use, instead of just repeating the most recent $`D`$
samples. Professional DAWs crossfade here; that is a good extension exercise.

### The limit, and what happens beyond it

$`C = 16\,384`$ samples (`kDefaultMaxPdcSamples`, about 341 ms at 48 kHz, 171 ms at 96 kHz) is the
default capacity of every compensator; `Engine::prepare(sampleRate, blockSize, maxPdcSamples)`
takes another value. Built-in effects stay far below it, but a hosted VST3 (Step 17) can report
anything: linear-phase EQs and look-ahead processors often report several thousand samples, and a
few of them in series on one track can exceed $`C`$. A compensator cannot delay by more than $`C`$, so
in that case not every path can be aligned. The mixer then aligns on the limit instead of the
maximum:

$$
T_{\text{track}} = \min\!\big(\max_u L_u,\ C\big), \qquad
T_{\text{bus}} = \min\!\big(\max_v L_v,\ C\big), \qquad
c_t = \max(0,\ T_{\text{track}} - L_t)
$$

and reports $`L_{\text{total}} = T_{\text{track}} + T_{\text{bus}} + L_{\text{master}}`$. Every path
whose latency is at most the target stays aligned and lands exactly where a bounce expects it; a
path with $`L_t > T_{\text{track}}`$ arrives $`L_t - T_{\text{track}}`$ samples late (audibly out of
time, but still inside the bounce). `Mixer::hasPdcOverflow()` / `Engine::hasPdcOverflow()` is then
true, and the app's mixer shows "PDC limit exceeded, not aligned!" next to the mix latency. The cure
is to remove the offending plugin or to prepare the engine with a larger `maxPdcSamples`.

Why not report the true maximum? Then the reported latency would be larger than the delay applied
to the dry tracks, and a latency-compensated bounce would shift them in front of sample 0 - they
would simply be missing from the file.

### When it is computed

`Mixer::updateLatencies()` runs on the audio thread at the start of every chunk: first every chain
applies its pending parameter changes (so a lookahead change is already in effect), then the sums
and maxima above are computed and written into the compensators - a few dozen integer operations.
With PDC disabled (`setLatencyCompensationEnabled(false)`) all compensators are 0 and the reported
total latency is 0.

## The code

| File | What |
|---|---|
| `LatencyCompensator.h` | `prepare(maxDelay, channels)`, `setDelaySamples()`, `process(block)` |
| `InsertEffect.h` | `getLatencySamples()` per effect; `InsertChain::getLatencySamples()` sums |
| `ChannelStrip.h` | `processInserts()` = inserts, then the compensator |
| `Mixer.h` | `updateLatencies()`, the track-sum compensator, `getTotalLatencySamples()` |
| `EffectBase.h` | `PureDelayEffect`: delays by N and reports N (the measuring instrument) |

## Your turn

`steps/step08-pdc/minidaw.h` is your Step 7 engine plus `getLatencySamples()` in the effect
interface, `InsertChain::getLatencySamples()`, `PureDelayEffect` (`createEffect("PureDelay")`,
parameter 0 = delay in samples), a `LatencyCompensator` after every strip's inserts and one for the
dry track sum, and `processChunk()` calling `mixer_.updateLatencies()` right after
`applyCommands()`.

| | |
|---|---|
| You write | `LatencyCompensator::process()` (the ring buffer, written also at delay 0) and `Mixer::updateLatencies()` (apply pending parameters, the targets, every compensator, the overflow flag, the total) |
| File | `steps/step08-pdc/starter/step08.cpp` |
| Run | `l14_step08_starter`; it prints `[FAIL] l14_step08 (15 of 22 checks failed)` until you are done |
| Solution | `steps/step08-pdc/solution/step08.cpp` (ctest `l14_step08_solution`) |

Expected output (verified) - impulses of 0.25 at timeline 1000 on tracks 1 and 2, each line lists
where (and how loud) something arrives at the output:

```
Step 8: plugin delay compensation (build-along)
  delay line: delay 0 passes through yes; delay 3 -> y[n] = x[n-3] on both channels yes; first sample after 0 -> 3 is x[7] = 8 (expected 8)
  PDC on : track 1 delayed by 100             : 0.3536 at 1100
  PDC off: the flam                           : 0.1768 at 1000 0.1768 at 1100
  tracks 37/0, buses 250/0, master 64         : 0.7071 at 1351
  total latency                               : 351 (expected 351), track sum waits 250 (expected 250)
  limit C = 128, track 1 delayed by 200       : 0.1768 at 1128 0.1768 at 1200
  overflow                                    : yes, reported latency 128 (expected yes, 128)
  delay changed to 30 by its parameter        : 0.3536 at 1030
[PASS] l14_step08 (22 checks passed)
```

The last line is the reason `updateLatencies()` applies pending parameters first: the delay's
latency follows its parameter, like a limiter's lookahead.

## Verify (reference route)

`l14_test_pdc` (verified):

| scenario | expected | result |
|---|---|---|
| impulse at 1000 on tracks 1 and 2, track 1 has a 100-sample PureDelay | one impulse of 2 x 0.25 x 0.7071 at 1100; compensators 0 / 100 | pass |
| same, output-compensated render | the impulse at 1000 | pass |
| PDC off | two impulses, at 1000 and 1100 | pass |
| track 1: 37 samples + send to bus A (250 samples); track 2 sends to bus B (0); master 64 | all four paths land on 1000 + 351; total latency 37 + 250 + 64 | pass |
| real Limiter (5 ms) vs dry | aligned at 1000 + 240; lookahead changed to 2 ms -> 96 samples, still aligned; bypass -> 0 | pass |
| device change 48 kHz -> 96 kHz | the same 5 ms lookahead re-prepared = 480 samples, PDC follows | pass |
| track 1: 3 x 8191 = 24 573 samples (more than $`C`$ = 16 384), track 2 dry, compensated render | overflow flag set, reported latency 16 384; track 2 at 1000, track 1 at 1000 + 8189 (late, but in the file) | pass |
| same, engine prepared with `maxPdcSamples` = 32 768 | no overflow, latency 24 573, one impulse at 1000 | pass |

`l14_pdc_demo` measures and lets you hear it (verified output):

```
Step 8: plugin delay compensation
  insert latency  track 1 (Limiter)          : 240 samples
  insert latency  track 2 (dry)              : 0 samples
  insert latency  bus A (ConvolutionReverb)  : 512 samples
  compensation    track 1 / track 2          : 0 / 240 samples
  compensation    track sum (waits for buses): 512 samples
  total latency (maxTrack + maxBus + master) : 752 samples = 15.67 ms

  click at timeline sample 1000:
    PDC off            : arrivals at 1000 and 1240 (flam of 240 samples), peak 0.177
    PDC on             : first arrival at 1752, second arrival at 25752 (beat 2), peak 0.354
    PDC on + bounce    : first arrival at 1000 (latency removed)

  drums RMS  PDC off -25.98 dBFS, PDC on -20.93 dBFS (comb filter loses energy)
```

Two coherent copies add to twice the amplitude (+6.02 dB, peak 0.177 -> 0.354). With PDC off the
drums are 5.05 dB quieter in RMS (-25.98 vs -20.93 dBFS): misaligned copies only add in power where
they do not overlap, and where they do, the comb's notches at 100, 300, 500 Hz sit right on the kick
and snare fundamentals. The program checks all of this (`[PASS] l14_pdc_demo (7 checks passed)`):
the flam equals the limiter's latency, the PDC-on click arrives at exactly 1000 + 752, the next one
24 000 samples later, the bounce puts it back at 1000, the coherent peak is $2 \cdot 0.5 \cdot
10^{-6/20} \cdot 0.7071$, and the aligned drums are more than 3 dB louder. Listen to `l14_pdc_off.wav` (hollow, flammed) and `l14_pdc_on.wav` (solid), and
`l14_pdc_clicks.wav` (a double click, then a single one).

## What you learned

- Latency is a property every effect must report honestly, and latencies in series add.
- PDC pads every path to the slowest one: tracks to the slowest track, returns to the slowest bus,
  the dry sum by the slowest bus; the master's latency is reported for the output.
- The padding has a limit; beyond it the mixer aligns what it can, reports the latency it really
  applies and raises an overflow flag instead of silently reporting a wrong number.
- Without PDC, identical material turns into a comb filter - an audible, measurable error.
