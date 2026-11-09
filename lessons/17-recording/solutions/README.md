# Chapter 17 - Solutions
**English** | [繁體中文](README.zh-TW.md)

[Back to the chapter](../README.md) | [Exercises](../exercises/README.md)

Run all solution programs with `.\scripts\build.ps1 -Only 17-recording -TestLabel solution`. Every one
ends with `[PASS] l17_exN (... checks passed)`.

---

## Exercise 1 - Why so careful?

1. **The file.** `fwrite` copies into a buffer until the buffer is full, then calls the operating
   system, which may wait for the disk: a spinning-up drive, a virus scanner looking at the new file, a
   network folder, a full page cache. That takes milliseconds or seconds, and the audio thread has a few
   milliseconds per block, every block. The ring lets the audio thread hand the frames over without
   waiting, whatever the disk does; the disk thread does the waiting.
2. **Whole blocks, and silence.** If the part of a block that fits went in, the take would lose the
   middle of the block, and everything after it would be early by the lost frames - the rest of the take
   would be out of time with the song. Whole or nothing keeps the accounting simple: a gap is always a
   number of whole frames at a known place. And the gap becomes silence of the same length, not nothing,
   for the same reason: every frame after it stays where it was played.
3. **Who says Idle.** The audio thread may be in the middle of `process()`, pushing a block, at the very
   moment the UI calls `stop()`. If the UI declared Idle and started reading or preparing the ring, both
   threads would use it at once: a data race, and the last frames lost or written into a ring that was
   just reset. Only the audio thread knows that it has left the take, so only it says so - with its next
   block.
4. **After a crash.** Sizes of 0 and the `.recording` name both say "not finished": no program mistakes
   the file for a finished take, and the next start knows exactly which files to recover. The real
   length comes from the file's size, not from the header, so nothing that was written is lost. Syncing
   is slow - it waits until the disk has the data - and after every write it would make the disk thread
   fall behind. Once a second bounds the loss in a power cut to a second; a crashed *program* loses
   only what was still in the ring, because every write already went to the operating system, which
   writes it out even after the program is gone.
5. **No cut.** The block that arrives when the transport reaches the punch-in holds sound that was
   played $`L = L_{in} + L_{out} + L_{mix}`$ samples *earlier*. The frames that belong to the punch-in
   arrive $`L`$ later, somewhere inside a later block. A cut at the block where the transport reached it
   would be $`L`$ too early, and rounded to a block boundary as well.
6. **Mute, and stay muted.** Feedback is a loop whose gain is above 1. Turning down "a little" may leave
   it above 1, or bring it back with the next loud note; only muting breaks the loop for sure. And the
   loop is quiet *because* it is muted: a guard that unmuted when it got quiet would start the howl again,
   over and over. The user has to change something - headphones, the microphone's place - and then reset.

## Exercise 2 - Recording by the numbers

- **(a)** $`L = 480 + 480 + 240 = 1200`$ samples = 25 ms, so the take starts at $`96000 - 1200 = 94800`$
  (1.975 s). With a real round trip of 380 ms (18 240 samples) the take lands $`18240 - 1200 = 17040`$
  samples = **355 ms** late: **0.71 beats** at 120 BPM - not a subtle flam, an obviously wrong
  recording. A take started at 0 would start at $`-1200`$: those frames were played before the song
  began, so the clip skips them (`skipFrames = 1200`) and starts at 0.
- **(b)** $`48000 \cdot 2 \cdot 4 = 384\,000`$ bytes a second, **23.04 MB** a minute, **1.38 GB** an hour.
  `kMaxDataBytes` = $`2^{32} - 1 - 100`$ = 4 294 967 195 bytes: $`/ 384\,000 = 11\,185`$ s = **3.11 h**
  of stereo (twice that in mono). Then the recorder stops with a full take, as a full disk does.
- **(c)** 250 ms = 12 000 frames, plus one block, because a block goes in whole: **12 512 frames**,
  $`12512 \cdot 2 \cdot 4`$ = **100 096 bytes** stereo. DawLab 17's ring of 10 s is 3.84 MB - large, but
  a disk that stalls for a second or two is not rare.
- **(d)** Directly: input + output = 960 samples = **20 ms**. Through the inserts: + the mix's 240 =
  **25 ms**. At 64 samples each way: 128 samples = **2.67 ms**, less than standing a metre from a
  speaker.
- **(e)** The take starts at frame $`480000 + 1200 = 481\,200`$ of the stream; minus the preroll of
  48 000, **433 200 frames** are never written: **1.73 MB** of mono.

```text
Exercise 2 - recording by the numbers
(a) L = 1200 samples = 25.0 ms; the take starts at 94800 (1.9750 s)
    with a real round trip of 18240: 17040 samples (355 ms) too late = 0.71 beats at 120 BPM
(b) 384000 bytes a second, 23.04 MB a minute, 1.3824 GB an hour; a WAV take ends after 11185 s = 3.11 h
(c) a 250 ms stall: 12000 frames + one block = 12512 frames = 100096 bytes stereo
    DawLab 17's ring of 10 s: 3.84 MB stereo
(d) monitoring: directly 20.0 ms, through the inserts 25.0 ms; at 64-sample buffers 2.67 ms
(e) the take starts at frame 481200 of the stream; 433200 frames = 1.73 MB of mono are not written
[PASS] l17_ex2 (15 checks passed)
```

## Exercise 3 - Write only what a take can use

[`solutions/ex3_preroll.cpp`](ex3_preroll.cpp)

The first take begins where `takesOf()` would put it: the stream frame that was heard at the visible
window's start, $`window.from - (stamped - L)`$. Everything up to a preroll before it can go:
$`skip = \max(first - preroll, 0)`$. Moving the stamped position by `skip` changes nothing in
`takesOf()` - every heard position moves by the same amount - except one thing: `loopsFrom()` asks
whether the stamped position is before the loop's end. A position moved past it would no longer loop, so
in a loop `skip` stops at $`loopEnd - 1 - stamped`$. An empty window (a punch outside the loop) has no
first take: nothing to gain.

`StreamGate` counts down: `drop(n)` returns $`\min(remaining, n)`$ and takes it off.

```text
Exercise 3 - write only what a take can use
  a punch at 10 s, recorded from 0: 433200 frames (9.03 s) never written, the take at frame 48000 of the file
  20000 random recordings: 0 wrong (expected 0), 5 % of the frames not written
[PASS] l17_ex3 (23 checks passed)
```

`prerollFor()` is computed on the audio thread, when the first block stamps the position, so it may not
allocate - it does not, it is a few comparisons. The disk thread must use the moved stamped position for
the take's header, or a recovered take would land `skip` frames too early.

## Exercise 4 - Measure the latency, and know when not to trust it

[`solutions/ex4_measure_latency.cpp`](ex4_measure_latency.cpp)

For each chirp, the correlation at every lag in $`[-early, late]`$; its absolute value, because a
microphone may invert the polarity. The largest gives the lag; the median of all of them is the level
of "nothing here", so a peak 8 times above it is a chirp. The median of the heard lags is robust against
one chirp that a door slammed into, and the spread of the heard lags says whether there is one latency
at all.

```text
Exercise 4 - measure the latency, and know when not to trust it
  through a cable                              heard 8 of 8, lag     37, spread     0: set the trim
  through the air, quiet                       heard 8 of 8, lag   1234, spread     0: set the trim
  the driver says too much                     heard 8 of 8, lag   -300, spread     0: set the trim
  the microphone inverts                       heard 8 of 8, lag     37, spread     0: set the trim
  one chirp lost                               heard 7 of 8, lag     37, spread     0: set the trim
  a door slams after the third chirp           heard 8 of 8, lag     37, spread     0: set the trim
    (the first loud frame says 2400)
  a latency that moves (Step 7's laptop)       heard 8 of 8, lag  17236, spread  4525: do not trust it
  a muted microphone                           heard 0 of 8, lag      0, spread     0: do not trust it
  three chirps of eight                        heard 3 of 8, lag     37, spread     0: do not trust it
  five chirps of eight                         heard 5 of 8, lag     37, spread     0: do not trust it
  six chirps of eight                          heard 6 of 8, lag     37, spread     0: set the trim
  lags 48 samples apart                        heard 8 of 8, lag     37, spread    48: set the trim
  lags 49 samples apart                        heard 8 of 8, lag     37, spread    49: do not trust it
[PASS] l17_ex4 (15 checks passed)
```

Why a sweep and not a click: a click's energy is spread over all frequencies, and a laptop's speaker
plays only some of them; a sweep puts all of its energy where the speaker and the microphone work, and it
correlates with itself in one sharp peak.

## Exercise 5 - Tell the user what is wrong with the input

[`solutions/ex5_input_health.cpp`](ex5_input_health.cpp)

- Each channel keeps its current run at full scale as a signed count: positive for a run at +1, negative
  at -1. A sample of the other sign starts a new run; a quieter one ends it. Because the count lives in
  the object, a run that crosses the edge of a block goes on - and the self-check feeds the same signal
  in random block sizes to prove it.
- A run is counted once, when it reaches 3 samples; the longest is kept for the message.
- A run of all-zero frames is a gap only when something was heard before it *and* something after it
  ends it. So silence before the first note and after the last one never counts.
- A take of nothing but zeros gets only one sentence: everything else about it would be noise.

```text
Exercise 5 - tell the user what is wrong with the input
  muted:   Every sample was exactly 0: the input is muted or blocked. Check the system's microphone settings and privacy settings.
  quiet:   The input is very quiet: its peak is -63.0 dBFS. Raise the input gain.
  hot:     The input clipped 2000 times, the longest for 13 samples: lower the input gain.
  gap:     The input stopped for 0.200 s in the middle of the take: a cable or an interface?
[PASS] l17_ex5 (14 checks passed)
```

**A noise gate.** A gate in the interface outputs exact zeros between notes, so a pause of 0.1 s or more
trips the "stopped" rule - a false alarm. A muted input is different: *every* sample is zero. To tell a
gate from a dropout, look at what came just before the zeros: a gate closes after the signal has faded
to almost nothing, a dropout cuts it off in the middle, at full level. Remembering the last few samples'
level before each run of zeros is enough.

## Exercise 6 - Comping

[`solutions/ex6_comping.cpp`](ex6_comping.cpp)

- `chooseTake()` walks the segments once. Each gives up to three
  pieces - before the range, inside it, after it - and a `push()` drops empty pieces and merges a piece
  into the last one when both have the same take. That one rule keeps the comp in its normal form,
  whatever the user chose before.
- `compClips()` works out the crossfade at every boundary first, because each clip needs the one before
  it and the one after it. The limits are the two segments' lengths and how far each pass reaches past
  the boundary: the earlier pass must have frames up to $`b + x/2`$, the later one from $`b - x/2`$.
  Then each clip runs from $`from - fadeIn/2`$ to $`to + fadeOut/2`$.

Why the sum is exactly 1: in the crossfade around $`b`$, the earlier clip's gain is $`(b + x/2 - p)/x`$ and
the later one's $`(p - (b - x/2))/x`$; they add up to $`x/x = 1`$. With an odd $`x`$ the halves would be
fractions of a frame, which is why it is made even.

```text
Exercise 6 - comping
  3000 random choices: 0 wrong (expected 0), 8 segments at the end
  three passes, 960-frame crossfades: gains off by 0.0e+00, 0 frames from the wrong pass
[PASS] l17_ex6 (28 checks passed)
```

A linear crossfade keeps the *amplitude* constant, which is right for two passes of the same part played
alike - they are strongly correlated. Between two very different takes an equal-power crossfade
($`\sin`$ / $`\cos`$) avoids a dip in the middle; a DAW lets the user choose (chapter 14's clip fades have
both).

## Exercise 7 - Two clocks

One design; others are possible.

1. **How much.** $`48000 \cdot 600 \cdot 50 \cdot 10^{-6} = 1440`$ samples = **30 ms** after 10 minutes - a
   flam at the end of a take that started in time. At the start it is nothing; it grows linearly.
2. **The ring.** If the microphone delivers more frames than the callback consumes, they pile up in the
   driver's buffer or in the ring until something overflows: about one 512-frame block is lost every 3.5
   minutes, a click in the take each time. A slower microphone runs dry instead: gaps. Some systems hide
   it - Windows' shared mode and macOS's aggregate devices can resample with drift correction - and then
   the timing is right but the quality depends on their resampler.
3. **When to correct.**
   - *While recording*: an adaptive resampler on the input, whose ratio is nudged by how full a buffer
     is (a slow control loop, a PLL). Monitoring stays right and the take is right as it is written, but
     it costs CPU on the audio thread and adds a little latency.
   - *Afterwards*: record the input as it comes and stretch the finished take by the measured ratio when
     it is sealed. Exact and cheap, nothing on the audio thread, but the live waveform and the monitor
     drift during the take. The file must store the ratio (in the `dlab` chunk, next to the latencies),
     so that a recovered take is stretched the same way.
4. **How to measure.** Count the input frames received and the output frames rendered, both on the
   audio thread, together with a clock (JUCE passes a host time with each callback). The ratio of the
   two counts over a long window is the drift. 50 ppm is 2.4 frames a second at 48 kHz, so it takes tens
   of seconds to measure well - which is fine, because it changes only with temperature.
5. **The user.** Say it plainly when input and output are different devices: "Recordings may drift
   against the song by up to 3 ms a minute." The real answer for music is one interface for both input
   and output - one clock - or an aggregate device with drift correction, or devices locked to a shared
   word clock.

---

[Back to the chapter](../README.md) | [Exercises](../exercises/README.md)
