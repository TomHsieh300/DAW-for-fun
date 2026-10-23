# Step 3 - Clips on a track: sample-accurate placement, clip gain, fades

**English** | [繁體中文](step-03-clips.zh-TW.md)

[Overview](../README.md) | Previous: [Step 2](step-02-transport.md) | Next: [Step 4 - Mixer](step-04-mixer.md)

## Goal

Put audio on the timeline. A **clip** is a piece of audio at a position; a **track** owns a list of
clips and, for each block, adds every clip that overlaps the block into its buffer - at exactly the
right sample, with clip gain and fades. The UI must be able to replace the clip list while playing,
without locks.

## Design

### A clip is a window, not a copy

```
timeline:  0 ........ start ........................ start+length .......
source:              [sourceOffset ............. sourceOffset+length)
```

```cpp
struct Clip {
    std::shared_ptr<const AudioBuffer> source; // immutable audio, shared
    int64_t start, length, sourceOffset;
    float gainDb;
    int fadeInSamples, fadeOutSamples;
    FadeShape fadeShape;                       // Linear or EqualPower
    bool muted;
    std::string name;                          // UI only
};
```

The audio is shared and immutable: eight copies of a drum loop are eight small `Clip` structs
pointing at one buffer. Trimming the start of a clip is `start += d; sourceOffset += d;
length -= d;` - nothing is copied. Because the buffer is `const` and never changes while it is in
use, the audio thread can read it without any synchronization.

### The overlap

For a timeline segment $[T_0, T_0 + N)$ and a clip $[S, S + L)$:

$$
\text{from} = \max(T_0, S), \qquad \text{to} = \min(T_0 + N, S + L)
$$

If $\text{to} \le \text{from}$ the clip is not in this block. Otherwise, for
$T \in [\text{from}, \text{to})$:

$$
\text{dest}[T - T_0] \mathrel{+}= \text{src}[O + T - S] \cdot g_{\text{clip}} \cdot g_{\text{fade}}(T - S)
$$

All integers: no rounding, no drift, the same result for every block size. Mono sources feed both
channels; a clip longer than its source pads with silence.

### Fades

For clip-internal index $i$ and remaining samples $r = L - 1 - i$:

$$
g_{\text{fade}}(i) = \underbrace{\text{shape}\Big(\frac{i+1}{F_{\text{in}}+1}\Big)}_{i < F_{\text{in}}}
\cdot \underbrace{\text{shape}\Big(\frac{r+1}{F_{\text{out}}+1}\Big)}_{r < F_{\text{out}}},
\qquad \text{shape}(x) = \begin{cases} x & \text{Linear} \\ \sin(\pi x / 2) & \text{EqualPower} \end{cases}
$$

The $+1$ in numerator and denominator means a fade never contains an exact 0 or an exact 1 inside
the fade region (a 4-sample fade-in is 0.2, 0.4, 0.6, 0.8), and it makes a fade-out and a fade-in of
the same length complementary: at overlap index $k$ the arguments are $\frac{F-k}{F+1}$ and
$\frac{k+1}{F+1}$, which sum to 1. Hence linear crossfades sum to exactly 1 and equal-power
crossfades satisfy $\cos^2 + \sin^2 = 1$ (Exercise 5).

Even a few milliseconds of fade matter: a clip that starts or ends in the middle of a waveform is a
step in the signal, and a step contains all frequencies - a click.

### Swapping the clip list without locks

The track holds `std::atomic<const std::vector<Clip>*>`. The UI never edits the list in place:

1. UI thread: build a new `std::vector<Clip>` (allocation is fine here);
2. send it to the audio thread in a `ReplaceClips` command (Step 7);
3. audio thread: `old = clips_.exchange(newList)` - one atomic pointer swap between two blocks;
4. audio thread: push `old` into the garbage queue; the UI thread deletes it later.

The audio thread therefore never sees a half-edited list and never frees memory.

## The code

### `Clip.h`

| Function | Purpose |
|---|---|
| `Clip::fromBuffer(buffer, start, name)` | a clip that plays the whole buffer |
| `fadeGain(shape, t)` | $t$ or $\sin(\pi t/2)$ |
| `clipFadeAt(clip, i)` | the fade gain of clip sample $i$ |
| `renderClip(clip, timelineStart, dest)` | adds the overlapping part into `dest`; real-time safe |

### `Track.h`

```cpp
class Track : public ChannelStrip {
public:
    const std::vector<Clip>* exchangeClips(const std::vector<Clip>* newClips) noexcept; // audio thread
    const std::vector<Clip>* getClips() const noexcept;                                  // any thread, see below
    int64_t getEndSample() const noexcept;                                               // song length
    void renderClips(AudioBlock dest, const TimelineSegment* segments, int numSegments) noexcept;
};
```

`renderClips()` loops over the segments from Step 2 and calls `renderClip()` for each clip with
the segment's `timelineStart` into the matching sub-block. With $C$ clips this is $O(C)$ per block;
a real arranger with thousands of clips would keep the list sorted and binary-search the first
candidate.

`getClips()` from the UI thread is safe as long as that same thread is the one that runs
`collectGarbage()`: the list it reads can only be freed by itself.

## Your turn

`steps/step03-clips/minidaw.h` is your Step 2 engine plus `Clip`, `fadeGain()` and a `Track`
that renders its clips segment by segment. The engine now has 8 tracks instead of the tape, and
its output is their plain sum - no fader and no pan until Step 4, so a clip sample of 0.5 comes out
as exactly 0.5. Until Step 7 a track keeps its clips in a plain `std::vector` that is only replaced
while the engine is not running (the lock-free swap above is Step 7's job).

| | |
|---|---|
| You write | `clipFadeAt()` (the two fade formulas) and `renderClip()` (overlap, source index, clip gain, fades, mono -> all channels, `+=`) |
| File | `steps/step03-clips/starter/step03.cpp` |
| Run | `l14_step03_starter`; it prints `[FAIL] l14_step03 (24 of 27 checks failed)` until you are done |
| Solution | `steps/step03-clips/solution/step03.cpp` (ctest `l14_step03_solution`) |

Expected output (verified):

```
Step 3: clips (build-along)
  impulse clip at 12345, block size  512: found at 12345, 1 non-zero samples
  impulse clip at 12345, block size   64: found at 12345, 1 non-zero samples
  impulse clip at 12345, block size    1: found at 12345, 1 non-zero samples
  impulse clip at 12345, block size 1000: found at 12345, 1 non-zero samples
  trimmed clip: out[999] 0.0000, out[1000] 0.0500, out[1299] 0.1995, out[1300] 0.0000 (expected 0, 0.0500, 0.1995, 0)
  fade-in  : 0.20 0.40 0.60 0.80 1.00 (expected 0.20 0.40 0.60 0.80 1.00)
  fade-out : 0.80 0.60 0.40 0.20 (expected 0.80 0.60 0.40 0.20)
  crossfade over 100 samples: max |gOut + gIn - 1| (linear) 0, max |gOut^2 + gIn^2 - 1| (equal power) 1.2e-07
[PASS] l14_step03 (27 checks passed)
```

The trimmed clip is a ramp source (sample $i$ = $i/1000$) with `sourceOffset = 100`,
`length = 300` and -6.02 dB of clip gain: $0.100 \cdot 0.5$ and $0.399 \cdot 0.5$. The check also
tests stereo sources, a clip longer than its source, overlapping clips and a muted clip.

## Verify (reference route)

`l14_test_timeline` (verified, all pass):

- an impulse clip at timeline 12 345 appears at output sample 12 345 - and nowhere else - with block
  sizes 512, 64, 1 and 1000;
- trimming: `sourceOffset = 100`, `length = 300`, clip gain -6.02 dB on a ramp source: first
  output = 0.100 x 0.5 x 0.7071, last = 0.399 x 0.5 x 0.7071, silence right after;
- a stereo clip keeps its channels; a clip longer than its source is silent beyond the source;
- overlapping clips add;
- fades: 0.2, 0.4, 0.6, 0.8, 1; linear crossfade gains sum to 1, equal-power squares sum to 1.

`l14_transport_clips` builds a two-bar arpeggio from 16 clips (4 shared pluck buffers, one clip per
eighth note, clip length = one eighth, 25 ms fade-out, -6 dB except on beat 1) and checks every clip
start: at its start sample $S$, the output must equal exactly `source[0] x clipGain x 0.7071`
(the previous clip has just ended, and a pluck begins with random noise, so a clip one sample off
would show a different value). Verified output:

```
Clip placement (first 6 of 16):
   clip  0 at 1:1:000 = sample      0, length 12000, gain 0 dB
   clip  1 at 1:1:480 = sample  12000, length 12000, gain -6 dB
   ...
Rendered once: 240000 samples, peak -9.04 dBFS, misplaced clips: 0
```

Listen: `l14_transport_clips.wav` - even eighth notes, accent on each bar's first note, no clicks
between notes thanks to the fade-outs.

## What you learned

- A clip is a window into shared, immutable audio; editing clips never copies audio.
- Sample accuracy comes for free when all positions are integers and the overlap is computed per
  segment.
- Fades are designed so that crossfades are complementary.
- Lists the audio thread reads are replaced, never edited: build new, swap the pointer, free the
  old one later on another thread.
