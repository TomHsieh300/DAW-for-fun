# Chapter 17 - Exercises
**English** | [繁體中文](README.zh-TW.md)

[Back to the chapter](../README.md) | [Solutions](../solutions/README.md)

Difficulty: ★☆☆ easy, ★★☆ medium, ★★★ hard. Implementation exercises have a starter file in
`exercises/starter/` that compiles and runs; until you complete it, its self-check prints `FAIL`
(that is expected). Build target names are given in brackets. None of them needs JUCE:

```powershell
.\scripts\build.ps1 -Only 17-recording
.\build\bin\Debug\l17_ex3_starter.exe
```

Exercises 3 and 6 build on Step 4's model (`exercises/model.h`: Step 4's `minidaw.h` with its functions
solved). Exercises 4 and 5 stand alone. Do the steps first.

---

## Exercise 1 - Why so careful? ★☆☆ (concept)

Answer in a few sentences each.

1. Why may the audio thread not write the take to the file itself, even with `fwrite`, which only
   copies into a buffer most of the time?
2. Step 1 puts a block into the ring whole or not at all, and a block that did not fit becomes silence
   of the same length in the take. Why not keep the part that fits? Why silence, and not nothing?
3. Step 2's `stop()` does not make the recorder Idle; only the audio thread does. What could go wrong
   if the UI set Idle itself and then read the ring?
4. Step 3 leaves the WAV header's sizes at 0 while recording and names the file `.recording`. What do
   these two things buy after a crash? Why sync to the disk once a second, and not after every write?
5. Step 4 does not cut the take on the audio thread at the punch-in, although the audio thread knows
   exactly when the transport reaches it. Why would that cut be in the wrong place?
6. Step 5's feedback guard mutes the monitor and stays muted until the user resets it. Why not turn it
   down a little, and why not unmute when the howl is gone?

## Exercise 2 - Recording by the numbers ★★☆ (calculation)

Compute by hand (show the formulas), then run [`l17_ex2_solution`] to check. 48 kHz throughout.

- (a) The transport is at 2 s when the first input block arrives. The driver reports 480 samples of
  input and 480 of output latency, the mix has 240. Where does the take start on the timeline? Step 7's
  laptop measured a real round trip of about 380 ms: how far off would its takes be, in samples, in ms
  and in beats at 120 BPM? A take started at 0: what happens to its first frames?
- (b) A stereo take in 32-bit float: bytes per second, MB per minute, GB per hour. The `data` chunk's
  size is 32 bits (`kMaxDataBytes`): how long can one take be?
- (c) The disk stalls for 250 ms while the audio thread pushes blocks of 512 frames. How many frames
  must the ring hold so that nothing is lost? How much memory is that in stereo? And DawLab 17's ring of
  10 s?
- (d) What does the musician hear of themselves, in ms, with Step 5's direct monitoring and through
  the inserts? At a buffer of 64 samples (input and output)?
- (e) With Exercise 3: a punch at 10 s, the recording started at 0, a preroll of 1 s, a mono take. How
  many frames are never written, and how many MB?

## Exercise 3 - Write only what a take can use ★★☆ (implementation)

[`l17_ex3_starter`, file `exercises/starter/ex3_preroll.cpp`]

Step 4 writes everything from the moment the transport plays, and its docs leave a question open: a
punch at bar 41 with the transport started at bar 1 writes 80 seconds that no take will ever use.
Drop them on the audio thread, before the ring - but keep a *preroll* before the first take, so that the
user can still move its left edge.

- `prerollFor(stamped, latencies, range, preroll)`: how many of the stream's first frames to drop, and
  the stamped position of the first frame that is kept. Dropping frames is the same as a stream that
  started later, so Step 4's `takesOf()` stays as it is - with one catch in a loop.
- `StreamGate`: drops them, block by block, on the audio thread.

The self-check requires, for 20 000 random recordings (loops, punches, both, neither, negative trims),
that the shortened stream gives exactly the same takes, each with its preroll, and that the gate drops
exactly `skip` frames whatever the block sizes are.

## Exercise 4 - Measure the latency, and know when not to trust it ★★☆ (implementation)

[`l17_ex4_starter`, file `exercises/starter/ex4_measure_latency.cpp`]

Step 2's `measureRoundTrip()` finds one click by its first loud frame - fine through a cable. Through the
air it fails: Step 7's device test found a laptop whose latency moved by tens of milliseconds within one
take. Write the core of that test:

- for each chirp played at a known position, the lag with the largest $`|\text{correlation}|`$ between the
  chirp and the recording, in $`[-early, +late]`$;
- *heard* when that peak is more than 8 times the median;
- the median lag of the heard chirps (the trim to set), their spread, and a verdict: trust it only if at
  least 3/4 of the chirps were heard and they agree within 1 ms.

The self-check simulates a cable, a quiet room with an echo, a driver that reports too much, an
inverted microphone, a lost chirp, a door slamming next to a chirp (Step 2's way finds the door), Step
7's laptop, a muted microphone, three, five and six chirps heard of eight, and spreads of 48 and 49 samples.

## Exercise 5 - Tell the user what is wrong with the input ★★☆ (implementation)

[`l17_ex5_starter`, file `exercises/starter/ex5_input_health.cpp`]

DawLab 17's first real take was 3 seconds of exact zeros - the microphone was muted - and nothing said
so. Write `InputHealth`, which the disk thread feeds with every block it writes:

- every sample exactly 0: the input is muted or blocked;
- a stretch of exact zeros of 0.1 s or more between sound: the input stopped;
- a peak below -50 dBFS: far too quiet;
- clipping: 3 or more samples in a row at full scale, of one sign, on one channel - also across the edge
  of a block.

`warnings()` turns that into sentences for the status line. The self-check feeds Step 7's muted and
quiet takes, a sine at 1.5 (2000 clips, the same for any block sizes), full-scale samples that are not
clips, two channels, a 0.2 s dropout, and silence before and after a take that is not a dropout.

Think about it: a noise gate in an audio interface outputs exact zeros between notes. Which of these
rules would it trip, and how would you tell it from a muted input?

## Exercise 6 - Comping ★★★ (implementation)

[`l17_ex6_starter`, file `exercises/starter/ex6_comping.cpp`]

A loop recorded four passes of a solo. The first bars were best in pass 2, the middle in pass 4, the end
in pass 1. A *comp* says which pass plays where, and becomes clips on the one take file of Step 4:

- `chooseTake(comp, from, to, take)`: the comp with `take` in $`[from, to)`$ of the window - segments cut
  where needed, neighbours with the same take merged. A comp is a value, so undo is keeping the last
  one (`CompHistory`, provided).
- `compClips(comp, takes, fade)`: one clip per segment, playing its own pass, with a linear crossfade of
  `fade` frames centred on every boundary - shorter where a segment is short or a pass has no more
  frames, and always even, so that both clips reach exactly half of it past the boundary.

The self-check runs 3000 random choices against a plain model, then mixes the clips frame by frame and
requires the gains to add up to exactly 1 everywhere and every clip to play only its own pass - with
crossfades of 960 frames, around a segment of 300, at the end of a pass that stopped early, odd and zero.

## Exercise 7 - Two clocks ★★☆ (design)

DawLab 17 refuses an input and an output that cannot run at one sample rate (Step 7). But a USB
microphone and the computer's speakers can both run at 48 kHz and still not agree: each device has its
own crystal. Design, in writing, what a DAW does about it. Cover:

1. **How much.** The microphone's clock is 50 ppm faster than the speakers'. After a 10-minute take, how
   many samples - and milliseconds - is its end off from the timeline?
2. **The ring.** With one audio callback for both devices, one of them has to give or take frames.
   What happens to Step 1's ring, or to the driver, if nobody corrects it?
3. **Correct it when?** While recording (an adaptive resampler on the input, steered by how full a
   buffer is), or afterwards (stretch the finished take by the measured ratio)? What does each cost, and
   what must the take's file then store?
4. **Measure it how?** Which numbers, counted on which thread, give the ratio?
5. **The user.** What should the app say, and what is the real answer for someone who records music?

---

[Back to the chapter](../README.md) | [Solutions](../solutions/README.md)
