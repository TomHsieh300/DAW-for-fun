# Chapter 19 - Exercises
**English** | [繁體中文](README.zh-TW.md)

[Back to the chapter](../README.md) | [Solutions](../solutions/README.md)

Difficulty: ★☆☆ easy, ★★☆ medium, ★★★ hard. Implementation exercises have a starter file in
`exercises/starter/` that compiles and runs; until you complete it, its self-check prints `FAIL`
(that is expected). Build target names are given in brackets. None of them needs JUCE:

```powershell
.\scripts\build.ps1 -Only 19-instruments-tempo
.\build\bin\Debug\l19_ex3_starter.exe
```

**Exercises 1-6** are the chapter's exercise set, about the steps (no JUCE); 5 and 6 are still to come.
**Exercises 7-9 will be optional projects** that extend DawLab 19.

Exercises 2 and 4 build on Step 1's tempo map (`steps/step02-transport/minidaw.h` has it solved). Exercise 3
stands alone. Do the steps first.

---

## Exercise 1 - Why this way? ★☆☆ (concept)

Answer in a few sentences each.

1. Step 1 keeps the tempo map in seconds, not samples. What would have to happen to a map in samples when the
   audio device changes its rate - and what would it cost?
2. A loop set in bars is kept in ticks (Step 2), and its two edges are *given* in ticks to the segments instead
   of being found from samples. Which tick would a pass lose or play twice otherwise, and when?
3. When the tempo map changes while the song plays, the playhead keeps its musical place and the samples jump.
   Give the other choice, and say what the user would see and hear with each.
4. Step 4 passes the map from the UI thread to the audio thread through three buffers. Why not an atomic, why
   not a mutex, and what is wrong with a seqlock here although it "works"?
5. With low-latency monitoring, the armed track is early against the others by its compensation. Why does
   nobody mind while recording, and why must the plan be whole again for playback?
6. A recorded note is placed by where the song was heard, whatever the latency of the instrument it was
   played on (Step 3). A musician on a slow sampler plays early to make up for it. Where do their notes land,
   and is that right?
7. Exporting a MIDI file, Step 5 writes a ramp as steps whose tempo is *not* the ramp's tempo anywhere in
   the step. What does it keep instead, and why is that what matters to the program that reads the file?

## Exercise 2 - Tempo by the numbers ★★☆ (calculation)

Compute by hand (show the formulas), then run [`l19_ex2_solution`] to check.

- (a) A ramp from 60 to 120 BPM over four bars of 4/4. How long does it take? When does the bar line in its
  middle come? At which tick has half its time passed, and what is the tempo there?
- (b) 120 BPM, and 90 from bar 3. When does bar 5 start, and at which sample at 48 kHz and at 44.1 kHz?
- (c) Tracks with latencies 0, 256, 64 + 32 and 4096 samples at 48 kHz. The total, and how long each track
  waits. A key on the first track, with the keyboard scheduled 1024 samples late and an output latency of 512:
  when is it heard, with and without low-latency monitoring?
- (d) A MIDI file stores a tempo as a whole number of microseconds a beat. What does 97.5 BPM become, and how
  far has a song drifted after ten minutes?
- (e) A ramp from 120 to 60 BPM over one beat, written as four steps of a sixteenth. How long is the beat?
  How long with each step at the tempo of its middle?

## Exercise 3 - A meter map ★★☆ (implementation)

[`l19_ex3_starter`, file `exercises/starter/ex3_meter_map.cpp`]

The chapter gave the tempo a map and kept one time signature. Give the meter its map too: a list of changes,
each a bar and a time signature that holds from that bar on. Write `prepare()`, `tickOfBar()`, `positionOf()`
(the bar, the beat and the ticks into the beat of a tick) and its inverse `tickOf()`.

It is all whole numbers - nothing to round - but a 6/8 bar is not six quarters long, a beat is whatever the
denominator names, and a tick before 0 is in bar -1, not in bar 0 counted backwards. The self-check compares
with a reference that counts bar after bar for 300 random maps.

## Exercise 4 - A tempo map from beats ★★☆ (implementation)

[`l19_ex4_starter`, file `exercises/starter/ex4_beats_to_map.cpp`]

A band recorded without a click, and the grid fits nowhere. Turn it around: from the times where the beats
*are* - tapped along, or found by a beat detector - build the tempo map that puts the grid there.

- `mapFromBeats()`: a jump at each beat to the tempo that makes the next beat arrive on time - but no point
  where the tempo just goes on. It refuses beats that do not come one after another, tempos no map can hold,
  and more points than a map has.
- `tapTempo()`: one tempo from a few taps, by the median interval, so that a missed tap does not halve it.

The self-check builds maps for 300 random bands and asks Step 1's map where each beat plays: within a tenth
of a microsecond of where it was heard.
