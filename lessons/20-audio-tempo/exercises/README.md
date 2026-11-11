# Chapter 20 - Exercises
**English** | [繁體中文](README.zh-TW.md)

[Back to the chapter](../README.md) | [Solutions](../solutions/README.md)

Difficulty: ★☆☆ easy, ★★☆ medium, ★★★ hard. Implementation exercises have a starter file in
`exercises/starter/` that compiles and runs; until you complete it, its self-check prints `FAIL`
(that is expected). Build target names are given in brackets. None of these exercises needs JUCE:

```powershell
.\scripts\build.ps1 -Only 20-audio-tempo
.\build\bin\Debug\l20_ex3_starter.exe
```

Exercise 3 builds on Step 1 (`steps/warp_map.h` has it solved), Exercise 4 on Step 4 (`exercises/wsola.h` has it
solved). Do the steps first.

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
