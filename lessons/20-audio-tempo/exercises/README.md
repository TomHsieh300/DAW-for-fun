# Chapter 20 - Exercises
**English** | [繁體中文](README.zh-TW.md)

[Back to the chapter](../README.md) | [Solutions](../solutions/README.md)

Difficulty: ★☆☆ easy, ★★☆ medium, ★★★ hard. Implementation exercises have a starter file in
`exercises/starter/` that compiles and runs; until you complete it, its self-check prints `FAIL`
(that is expected). Build target names are given in brackets. Exercises 1-6 need no JUCE:

```powershell
.\scripts\build.ps1 -Only 20-audio-tempo
.\build\bin\Debug\l20_ex3_starter.exe
```

**Exercises 1-6** are the chapter's exercise set, about the steps (no JUCE). **Exercises 7-9 are optional
projects** that extend DawLab 20. They need a JUCE build (`.\scripts\build.ps1 -Only 20-audio-tempo -Juce`); like
chapter 19's they are console programs that build the real `MainComponent` in its offline mode
(`exercises/app_rig.h`), and none of them changes the app.

Exercise 3 builds on Step 1 (`steps/warp_map.h` has it solved), Exercise 4 on Step 4 (`exercises/wsola.h` has it
solved), Exercise 5 on Steps 2 and 3. Do the steps first.

---

## Exercise 1 - Why this way? ★☆☆ (concept)

Answer in a few sentences each.

1. A clip whose start follows the tempo keeps a tick and derives its sample (Step 2). Why not keep the sample and
   move it by the right amount at every change of the tempo?
2. The position in the file is computed from the song's sample in every block (Step 3). Besides rounding, name
   two things that would go wrong with a position that is counted up by the speed.
3. The sinc divides its sum by the sum of its weights (Step 3). What would a steady signal do without that, and
   why is it worse when the sinc reads faster than 1?
4. WSOLA scores a candidate against the *natural continuation* of the piece before, not against that piece itself,
   and divides by the size of the candidate only (Step 4). Why each?
5. In a stereo stretch both channels slide by the same amount (Step 5). What would you hear if each found its own
   best fit?
6. A render that a newer request overtook is not given back, even when it is finished (Step 5). What could the
   user hear if `collect()` gave back every finished render, in the order they finished?
7. Turning following on gives a clip the markers of "a take recorded over the map of now" (Step 6). When is that
   wrong, and what can the user do then?

## Exercise 2 - By the numbers ★★☆ (calculation)

Compute by hand (show the formulas), then run [`l20_ex2_solution`] to check.

- (a) A two-bar drum loop made at 128 BPM, in a song at 120 BPM and at 140 BPM. At what speed does it play, and
  by how many semitones does varispeed move it?
- (b) A take of a ramp from 60 to 120 BPM over four bars, to stay within 1 ms. What is $`\max|f''|`$, how many
  equal pieces and markers does Step 1 give, and how far off is one piece at most? How far off is the first piece
  really, at its middle?
- (c) How many samples does Step 3's sinc read for one output sample at speed 1, and at speed 2?
- (d) How many multiplications a second of output does Step 4's search cost, with pieces of 1024 samples and a
  search of 256 at 48 kHz?
- (e) A 4-minute stereo clip stretched to speed 0.8 at 48 kHz: how much memory is its render, and how much does
  Step 5's list of positions need while it is made?
- (f) A clip whose start follows the tempo is on bar 2. The song plays at 120 BPM and is a beat and a half into the
  clip when the tempo becomes 90 BPM. By how much does the audio inside the clip jump?

## Exercise 3 - The fewest markers for a take ★★★ (implementation)

[`l20_ex3_starter`, file `exercises/starter/ex3_fewest_markers.cpp`]

Step 1 cuts a ramp into equal pieces, as many as the chord bound asks for at the ramp's slow end: never wrong, and
wasteful where the ramp is fast. Chapter 19's Exercise 5 found the fewest tempo points with a dynamic program.
Write `fewestMarkers()`: of a set of candidate markers (every `gridSamples` of the take, and at its tempo points),
the fewest with which every candidate plays within a tolerance of where it was recorded.

The self-check compares with every possible choice for 150 small takes, and counts against Step 1's equal pieces
on two ramps.

## Exercise 4 - A fit to a fraction of a sample ★★★ (implementation)

[`l20_ex4_starter`, file `exercises/starter/ex4_fine_fit.cpp`]

Step 4's pieces slide by whole samples, and fit to within half a sample: that is where the 31 to 40 dB between a
stretched tone and the rest come from. Find the top of the score between whole offsets with a parabola through
three of them (`refine()`), and read the pieces at their fraction (`readHermite()`, `stretchFine()`).

The self-check stretches tones of 220 and 440 Hz at four speeds and compares with Step 4 - and a tone of 1 kHz,
whose period is a whole number of samples.

## Exercise 5 - A fade over a jump ★★☆ (implementation)

[`l20_ex5_starter`, file `exercises/starter/ex5_declick.cpp`]

When the tempo changes while the song plays, a clip that reads by the sample - one that does not follow, or follows
with its start only - jumps to another place in its audio (Step 2), and the step in the wave is a click. A clip that
follows by varispeed or stretch reads by the tick, which the playhead keeps, and does not jump. Write a
`Declicker`: it remembers where the reading would have gone on, and when a block starts somewhere else it fades
that old path out and the new one in, over a few milliseconds, with equal-power curves.

The self-check changes the tempo under a 440 Hz clip whose start follows, and measures the largest step from one
output sample to the next, with and without the fade; it also reads the old path back out of the output.

## Exercise 6 - Transients ★★★ (design)

No code. Step 4's WSOLA doubles a drum hit when it stretches and can lose one when it squeezes. Design a stretcher
that keeps every hit once, on its beat, in a page:

1. How does the program find the hits in a clip? What goes wrong with a fixed threshold, and what do you do about
   it?
2. A hit should be in exactly one piece, played at speed 1. What does that do to where the pieces are cut, and to
   the steady part between two hits?
3. Stretched to twice the length, what fills the time between two hits? Squeezed to half, what is given up?
4. A drum kit recorded with several microphones is several clips. What must they share so that a hit stays one hit?
5. Where does the detection run, how often, and what does it cost? How do the hits relate to Exercise 3's markers?

---

## Exercise 7 - Takes that follow as they were recorded ★★☆ (optional project, JUCE)

[`l20_ex7_starter`, file `exercises/starter/ex7_follow_as_recorded.cpp`]

DawLab 20 records a take as a clip that stays, and turning following on later gives it the markers of the map of
*now*. If the tempo was changed in between, that is the wrong map. Write `followAsRecorded()`: every recording on a
track follows the tempo with the markers of the map it *was* recorded over - all in one undo step.

The self-check records a take at 120 BPM, sets the tempo to 90, and plays it both ways: with the app's markers and
with yours.

## Exercise 8 - Audio quantize ★★★ (optional project, JUCE)

[`l20_ex8_starter`, file `exercises/starter/ex8_quantize_audio.cpp`]

Chapter 18 quantized MIDI notes by moving their ticks. Do it to audio: find the hits in a clip (`findOnsets()`),
put a warp marker on each at the tick where it is heard now, and move that tick towards the nearest line of the
grid (`quantizeClip()`).

The self-check plays 16 hits by hand, each up to 25 ms early or late, quantizes them onto quarter notes, half way,
and onto eighths with a stretch - and changes the tempo afterwards.

## Exercise 9 - A loop that fits ★★☆ (optional project, JUCE)

[`l20_ex9_starter`, file `exercises/starter/ex9_loop_fits.cpp`]

A drum loop from a library says neither its tempo nor its bars. Guess its beats from its length at the song's
tempo - a power of two, the closest as a ratio (`guessBeats()`) - and drop it on a bar with the markers of a loop
of that many beats (`dropLoop()`): in time at once, and after every tempo change.

The self-check guesses loops made at seven tempos, drops a 128 BPM loop into a 120 BPM song and plays it, and puts
a loop over a clip that is already there.
