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

**Exercises 1-7** are the chapter's exercise set, about the steps (no JUCE). **Exercises 8-11 are
optional projects** that extend DawLab 17. They need a JUCE build
(`.\scripts\build.ps1 -Only 17-recording -Juce`); like chapter 16's they are console programs that
build the real `MainComponent` in its offline mode, and none of them changes the app. They record
through the real paths - the disk thread, the take files, the clips - with the input coming from the
self-check instead of a device (`exercises/app_rig.h`, the smoke test's way).

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

## Exercise 8 - A "Measure Latency" button ★★☆ (implementation, JUCE)

[`l17_ex8_starter`, file `exercises/starter/ex8_latency_button.cpp`]

DawLab 17 asks for the latency trim as a number. Make it a button that measures it, as the device test
does (Step 7) - Exercise 4's search is given, as `chirpLags()`.

- `LatencyMeter::measure()`: plays the probe (8 chirps) through the device function it was given,
  takes the median of the heard lags as the round trip, and sets the trim to the round trip minus what
  the driver reports. Not while recording; not when fewer than 3/4 of the chirps were heard; not when
  they disagree by more than 1 ms. The status line says which, and the old trim stays.
- `LatencyMeter::revert()`: the trim from before the last measurement that set one, once.
- `LatencyMeter::addToMenu(menu)`: "Measure Latency..." and "Undo Latency Measurement".

The self-check simulates rooms: a round trip of 1234 samples with nothing reported - and then records a
click through the app to see it land exactly where it was played - a driver that reports part of it,
latencies that move by 48 and 49 samples and by 80 ms (Step 7's laptop), 6 and 5 chirps of 8, a muted
microphone, a measurement while recording, and the menu.

Think about it: the probe is played straight to the device, not through the mix. Why is the mix's
latency then not part of the trim, although a take is placed with it (Step 2)?

## Exercise 9 - Say what is wrong with a take ★★☆ (implementation, JUCE)

[`l17_ex9_starter`, file `exercises/starter/ex9_take_warnings.cpp`]

The first real take was three seconds of zeros, and the app said "Recorded 1 take". Exercise 5 watches
the input on the disk thread; here the cheaper half, after the fact.

- `analyse(audio, channels)`: silent (every sample exactly 0), the peak in dBFS, clips (3 or more
  full-scale samples of one sign in a row, per channel). A mono track's take is loaded as two equal
  channels: count it once.
- `describe(report, name)`: one sentence per problem for the status line.
- `TakeWatcher::attach()`: wraps the recorder's `onMessage`; each recording that is new on a track is
  analysed once, and the app's handler gets "Recorded 1 take. Track 1: the take is exactly 0 - is the
  input muted?" as an alarm.

The self-check analyses buffers - runs across a change of sign, at the end of a channel, across two
channels, -50 dBFS on both sides - then records a muted input, a good take after an undo and redo, three
tracks at once (stereo, mono, quiet), and a silent loop of four passes, which is one recording.

## Exercise 10 - Comping in the app ★★★ (implementation, JUCE)

[`l17_ex10_starter`, file `exercises/starter/ex10_comping_ui.cpp`]

Put Exercise 6 on the track. A comp is stored in the clips themselves: one clip per segment on the
recording's buffer, neighbours overlapping by their crossfade - saved, undone and played like any clips.
Exercise 6's `chooseTake()` and `compClips()` are given (do Exercise 6 first).

- `Comper::takeOf(clip, takes)`: which take a clip plays a part of.
- `Comper::compOf(track, clip)`: the comp read back from the clips; half of each crossfade belongs to
  each side; the comp's two ends fade over 5 ms, as a new take does.
- `Comper::choose(track, clip, from, to, take)`: the new comp's clips in place of the old, one undo step
  "Comp Take 3"; the range is first limited to what that take has.
- `Comper::addToMenu(menu, track, clip)`: "Take N Between the Locators".

The self-check records a loop of 3.5 passes that starts in the middle of the loop (so the first pass
begins later than the others), comps the second pass into the middle, chooses the last pass over
everything, then makes 60 random choices from any of the recording's clips, and after
each one compares the comp with Exercise 6's and mixes the clips frame by frame: the fades add up to 1,
and every frame comes from the take the comp has there.

## Exercise 11 - Pre-roll ★★☆ (implementation, JUCE)

[`l17_ex11_starter`, file `exercises/starter/ex11_pre_roll.cpp`]

Record starts at the playhead, at once. Give the player a bar or two: the transport starts earlier,
and the take starts exactly at the playhead.

- `PreRoll::startFor(position)`: whole bars of the tempo and meter before, not before 0.
- `PreRoll::record()`: with the punch on, the pre-roll goes before the punch-in. Without it, the punch is
  lent for a moment - locators at [playhead, far away), punch on, record, and both given back at once:
  `TakeRecorder::start()` takes its range when it starts. A loop is its own pre-roll.
- `PreRoll::addToMenu(menu)`: "No Pre-roll", "Pre-roll 1 Bar", "Pre-roll 2 Bars".

The self-check works out the start at 60 BPM in 3/4 and at 133 BPM, records two bars of pre-roll over an
old take and requires the take at the playhead to the sample, the old take intact before it and the
pre-roll in the file; then a punch, a loop, no pre-roll (with and without the punch), and a record that cannot start, which must leave
the playhead, the locators and the punch switch as they were.

---

[Back to the chapter](../README.md) | [Solutions](../solutions/README.md)
