# Step 2 - The transport: play, stop, position, loop, tempo, bars:beats

**English** | [繁體中文](step-02-transport.zh-TW.md)

[Overview](../README.md) | Previous: [Step 1](step-01-skeleton.md) | Next: [Step 3 - Clips](step-03-clips.md)

## Goal

Give the engine a clock. The transport knows whether we play, where the playhead is, the tempo and
time signature, and the loop. Once per block the audio thread asks it "which part of the timeline
does this block cover?" - and the answer must be exact to the sample, also when the loop end falls
in the middle of a block.

## Design

### Samples are the truth, music is derived

The position is an `int64_t` number of samples. Musical time is computed from it:

$$
N_{\text{beat}} = \frac{60\, f_s}{B}, \qquad N_{\text{bar}} = n \cdot N_{\text{beat}}
$$

with tempo $`B`$ in beats per minute, sample rate $`f_s`$, numerator $`n`$. A beat is the note value
named by the denominator (in 7/8 a beat is an eighth note), which is what DAWs display.

Positions are 1-based like on screen, with 960 ticks per beat (a common PPQ resolution; PPQ = pulses per quarter note, the number of
timing ticks a sequencer divides one beat into):

$$
\text{beats}(b, k, t) = (b-1)\,n + (k-1) + \frac{t}{960}, \qquad
\text{samples} = \mathrm{round}\big(\text{beats} \cdot N_{\text{beat}}\big)
$$

The inverse takes the floor of the beat count; a tiny epsilon ($`10^{-9}`$ beats) keeps exact grid
positions from becoming "tick 959 of the previous beat" through floating-point rounding.

Worked example (120 BPM, 4/4, 48 kHz): bar 3, beat 2, tick 480 is
$`(2 \cdot 4 + 1 + 0.5) \cdot 24000 = 228\,000`$ samples = 4.75 s.

### Thread safety: atomics and a pending seek

Every setter (`play`, `stop`, `setTempo`, `setLoopRange`, ...) writes an `std::atomic`, so the UI may
call it at any time. A **seek** is special: if the audio thread saw the position change in the
middle of processing a block, half of the tracks would play the old position and half the new one.
So `setPosition()` writes a *pending* value, and `advance()` - the first thing the audio thread does
for a block - exchanges it in:

```cpp
const int64_t seek = pendingSeek_.exchange(-1, std::memory_order_acq_rel);
if (seek >= 0) position_.store(seek, std::memory_order_relaxed);
```

`getPosition()` returns the pending value if there is one, so the UI shows the new position
immediately.

### Timeline segments and the loop

`advance(numSamples, segments, samplesCovered)` returns a list of **segments**: "block samples
`[blockOffset, blockOffset + numSamples)` are timeline samples `[timelineStart, ...)`". Normally
there is one segment. When the loop end is inside the block:

```
loop [1000, 1300), position 1256, block of 100 samples

block:     |0 ............ 43|44 ............. 99|
timeline:   1256 ...... 1299   1000 ......... 1055
segments:  {1256, offset 0, 44} {1000, offset 44, 56}
position after the block: 1056
```

The tracks render each segment separately, so the loop is seamless and sample-exact. A loop
shorter than `block / 8` would need more than `kMaxSegmentsPerBlock = 8` segments; `advance()` then
covers only part of the block, reports how much in `samplesCovered`, and `Engine::processChunk()`
simply calls it again for the rest. Any loop length down to one sample works.

Rules that match common DAW behaviour:

- the loop is active when enabled **and** `end > start`;
- if the playhead is already beyond the loop end when looping is switched on, it plays on linearly;
- while stopped, `advance()` returns no segments but still applies a pending seek.

## The code

`engine/include/dawlab/engine/Transport.h`, section by section:

| Section | What it does |
|---|---|
| `TimeSignature`, `BarsBeats`, `TimelineSegment` | plain structs; `BarsBeats` is 1-based |
| tempo & meter | `setTempo()` clamps to 20..999 BPM; `setTimeSignature()` |
| play state | `play()`, `stop()`, `setPlaying()`, `isPlaying()` |
| seek | `setPosition()`, `setPositionSeconds()`, `setPositionBarsBeats()`; pending until `advance()` |
| loop | `setLoopRange()`, `setLoopRangeBars(firstBar, numBars)`, `setLoopEnabled()` |
| conversions | `getSamplesPerBeat()`, `barsBeatsToSamples()`, `samplesToBarsBeats()`, `toSeconds()` |
| `advance()` | the only audio-thread method: seek, segments, position update |

The heart of `advance()`:

```cpp
while (offset < numSamples && count < kMaxSegmentsPerBlock) {
    int64_t n = numSamples - offset;
    if (loopActive && pos < loopEnd)
        n = std::min<int64_t>(n, loopEnd - pos);     // stop exactly at the loop end
    segments[count++] = {pos, offset, static_cast<int>(n)};
    pos += n;
    offset += static_cast<int>(n);
    if (loopActive && pos == loopEnd)
        pos = loopStart;                             // wrap
}
samplesCovered = offset;
```

The engine side (`Engine::processChunk`):

```cpp
while (done < n) {
    TimelineSegment segments[Transport::kMaxSegmentsPerBlock];
    int covered = 0;
    const int numSegments = transport_.advance(n - done, segments, covered);
    mixer_.process(out.getSubBlock(done, covered), segments, numSegments, transport_.getTempo());
    done += covered;
}
```

## Your turn

Build the transport yourself in the Step 2 snapshot. `steps/step02-transport/minidaw.h` is your
Step 1 engine (solved) plus the `Transport` class with its atomics; the engine's
`processChunk()` already asks the transport for segments and plays the "tape" at those timeline
positions. Three functions are only declared:

| | |
|---|---|
| You write | `Transport::barsBeatsToSamples()`, `Transport::samplesToBarsBeats()`, `Transport::advance()` (pending seek, segments split at the loop end, `samplesCovered`) |
| File | `steps/step02-transport/starter/step02.cpp` |
| Run | `l14_step02_starter` (from the build directory: `.\bin\Debug\l14_step02_starter.exe`); it prints `[FAIL] l14_step02 (27 of 31 checks failed)` until you are done |
| Solution | `steps/step02-transport/solution/step02.cpp` (ctest `l14_step02_solution`) |

The check covers only Step 2: the grid (including 7/8 and 96 BPM at 44.1 kHz, where a beat is
27 562.5 samples), a stopped transport that still applies a seek, the loop split worked out above,
a 10-sample loop that needs more than 8 segments, and two engine renders with a "ramp tape"
(sample $`i`$ has the value $`i`$), so that every output sample says which timeline position was
played - 441-sample blocks across the loop end, and a 7-sample loop with random block sizes.
Expected output (verified):

```
Step 2: the transport (build-along)
  120 BPM, 4/4, 48 kHz: 24000 samples per beat, 96000 per bar
     bar:beat:tick    samples    back
      1:1:000            0    1:1:000
      1:2:000        24000    1:2:000
      1:2:480        36000    1:2:480
      2:1:000        96000    2:1:000
      3:2:480       228000    3:2:480
      9:1:000       768000    9:1:000
  7/8 at 90 BPM: bar 2 = 224000 samples (expected 224000)
  96 BPM at 44.1 kHz: beat 2 = 27563, bar 2 = 110250 (expected 27563, 110250)
  loop [1000, 1300), position 1256, 100-sample block: 2 segments {1256, offset 0, 44} {1000, offset 44, 56}, position after 1056
  10-sample loop, 512-sample block: 8 segments cover 80 samples (expected 8, 80)
  engine, loop [1000, 1300) from 900 with 441-sample blocks: 0 misplaced samples (expected 0)
  engine, 7-sample loop with random blocks 1..600: 0 misplaced of 10000 (expected 0)
[PASS] l14_step02 (31 checks passed)
```

## Verify (reference route)

Run the programs from the build directory, as ctest does, so that the WAV files land in
`build\dawlab_output\`:

```powershell
cd build                                       # or: .\scripts\build.ps1 -Only 14-daw-8track
.\bin\Debug\l14_test_timeline.exe
.\bin\Debug\l14_transport_clips.exe
```

`l14_test_timeline` checks (among others): 24 000 samples per beat at 120 BPM; bar 3 beat 2 tick 480
= 228 000 and back; 7/8 at 90 BPM = 32 000 samples per beat and 224 000 per bar; a stopped transport
returns no segments; the split `{1256, 44} {1000, 56}` above; a 10-sample loop in a 512 block covers
80 samples with 8 segments; and a 7-sample loop played with random block sizes reproduces the
expected timeline sample at every one of 10 000 output samples.

`l14_transport_clips` prints the grid (verified output):

```
Musical grid at 120 BPM, 4/4, 48 kHz: 24000 samples per beat, 96000 per bar
   bar:beat:tick    samples    seconds
     1:1:000           0     0.000    (back: 1:1:000)
     1:2:000       24000     0.500    (back: 1:2:000)
     1:2:480       36000     0.750    (back: 1:2:480)
     2:1:000       96000     2.000    (back: 2:1:000)
     3:2:480      228000     4.750    (back: 3:2:480)
     9:1:000      768000    16.000    (back: 9:1:000)
7/8 at 90 BPM (an eighth note is the beat): 32000 samples per beat, 224000 per bar
```

and later loops bars 1-2 with 441-sample blocks (192 000 is not a multiple of 441):

```
   wrap 1 in the block at output 191835..192275: segment 1 = 165 samples (191835..191999), segment 2 = 276 samples from 0
   wrap 2 in the block at output 383670..384110: segment 1 = 330 samples (191670..191999), segment 2 = 111 samples from 0
   wrap 3 in the block at output 575946..575999: one segment of  54 samples (191946..191999) ending exactly at the loop end; the position wraps to 0
Looped 3 x 192000 samples, 3 wraps, max |pass 2 - pass 1| = 0
```

The third wrap is the special case of a block that ends exactly on the loop end: the block needs
only one segment, and the position wraps to the loop start for the *next* block. Pass 2 of the loop is bit-identical to pass 1. Listen to `l14_transport_loop.wav`: no gap, no click
at the loop point.

## What you learned

- Keep time as integer samples; derive bars and beats; round once.
- UI writes go to atomics; a seek is applied by the audio thread at a block boundary.
- A block is not necessarily one contiguous piece of the timeline: timeline segments make loops
  sample-accurate for any block size and any loop length.
