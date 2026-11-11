# Chapter 20 - Solutions
**English** | [繁體中文](README.zh-TW.md)

[Back to the chapter](../README.md) | [Exercises](../exercises/README.md)

Run all solution programs with `.\scripts\build.ps1 -Only 20-audio-tempo -TestLabel solution` (add `-Juce` for
Exercises 7-9). Every one ends with `[PASS] l20_exN (... checks passed)`.

---

## Exercise 1 - Why this way?

1. **A tick, not a moved sample.** "The right amount" is computed from the clip's tick - which the clip would not
   have any more. It would have to find it again from its sample, which is rounded: a little off each time, and
   the errors add up over many changes (Step 2 measured 15 samples after 1000). Keeping the tick, each change is
   one rounding from the truth, never a rounding of a rounding. It also makes undo free: a list of clips from
   another tempo is placed again from its ticks.
2. **A counter.** It has to be told where to start again after every **seek** and every **loop's wrap**, and a new
   tempo map changes the position under it: it would have to be *corrected* from the maps anyway, which is the
   computation it was meant to save. And a counter does not know about a **knot** inside a block - a tempo point
   or a marker where the speed changes - so it uses one speed for the whole block. (The rounding is the third
   thing: 31 ms after ten minutes of ramps.)
3. **Dividing by the weights.** The window makes the weights at one fraction add up to a little more or less than
   1, and differently at another fraction. Without the division a steady level comes out larger or smaller from
   sample to sample, as the fraction moves: a ripple, which is noise at the rate the fraction repeats. Reading
   faster the sinc is narrowed - `sinc(c x)` with `c = 1/speed` - and its weights add up to about `1/c`, the
   speed itself: without the division the clip would get louder by the speed.
4. **Natural continuation, divided by the candidate.** The piece before is already in the output; what the new
   piece must fit is what *follows* it in the input, because that is what would have played if nothing had been
   cut - comparing with the piece itself would find a start one hop too early, the piece repeated. Divided by
   the size of the candidate, the score is a cosine of the candidate's shape against the continuation, times a
   constant: a loud candidate does not win for being loud. Divided by the candidate's energy instead (not its
   square root), a quiet one would win for being quiet.
5. **Each channel its own fit.** The two channels would be cut at different places, a few samples apart, and the
   delay between them would jump at every piece. A delay between left and right is how a sound's direction is
   heard: the stereo image would wobble 94 times a second, or smear into a wide, phasey sound.
6. **Every finished render, in order.** A slow render of an older tempo may finish after a quick one of the newest
   tempo. Given back in order, it would replace the newer one: the clip would play at the old tempo while the
   song is at the new one, until the next change - out of time, and nothing would correct it.
7. **Markers of the map of now.** They say "as it plays at this moment, it is in time". That is wrong for audio
   that does not play in time right now: a loop dropped in at another tempo, or a take recorded before the tempo
   was changed. The user should first make it fit: change the tempo back to what it was made at, turn following
   on, and change the tempo again - or, for a take whose old tempo is known, Exercise 7; for a loop, Exercise 9.

## Exercise 2 - By the numbers

[`solutions/ex2_numbers.cpp`](ex2_numbers.cpp)

- (a) The speed is the tempo now over the tempo the loop was made at: $`120/128 = 0.9375`$ and
  $`140/128 = 1.0938`$. In semitones $`12 \log_2 0.9375 = -1.117`$ and $`12 \log_2 1.0938 = +1.551`$. A singer is
  more than a semitone out of tune in both; a drum loop is fine.
- (b) $`k = 60/15360`$ BPM a tick, and $`\max|f''| = \frac{60}{960} \cdot k / 60^2 = 6.78 \cdot 10^{-8}`$ s/tick²
  at the slow end. $`n = \lceil 15360 \sqrt{6.78 \cdot 10^{-8} / (8 \cdot 0.001)} \rceil = 45`$ pieces of 341 ticks,
  46 markers, each piece off by at most $`h^2/8 \cdot \max|f''| = 0.988`$ ms. The first piece, where the ramp is
  slowest, is really 0.966 ms off at its middle: the bound is tight there.
- (c) At speed 1 the sinc reaches 16 samples to each side: 33. At speed 2 it is narrowed to cut at half the
  Nyquist frequency and so reaches $`16/0.5 = 32`$ samples to each side: 65, twice the work.
- (d) $`48000/512 = 93.75`$ pieces a second, each searching $`2 \cdot 256 + 1 = 513`$ offsets over $`512`$
  samples with two products each (the cross term and the energy): $`93.75 \cdot 513 \cdot 512 \cdot 2 = 49.2`$
  million a second of output.
- (e) 4 minutes at speed 0.8 are 300 s: $`300 \cdot 48000 \cdot 2 \cdot 4`$ bytes = 115.2 MB of float audio. The
  positions are a double per output sample: another $`300 \cdot 48000 \cdot 8`$ = 115.2 MB while the render runs.
  A song with forty such clips does not fit in a phone, and computing positions in chunks would halve the peak.
- (f) A beat and a half is 0.75 s at 120 BPM and 1.0 s at 90. The clip's start stays on bar 2, the playhead on its
  beat, so the audio inside the clip jumps 0.25 s ahead - 12000 samples. A clip that does not follow jumps much
  further: it does not move while the playhead does.

```text
Exercise 2 - audio and tempo by the numbers
(a) at 120 BPM: speed 0.9375, -1.117 semitones; at 140 BPM: speed 1.0938, +1.551 semitones
(b) max|f''| = 6.782e-08 s/tick^2: 45 pieces of 341.3 ticks, 46 markers; one piece is off by at most 0.9877 ms
    the first piece at its middle: 0.9661 ms off
(c) the sinc reaches 16 samples to each side at speed 1 (33 in all), 32 at speed 2 (65 in all)
(d) 93.75 pieces a second, 49.2 million multiplications a second of output
(e) 300 s of output: 115.2 MB of audio, and 115.2 MB of positions while it is rendered
(f) 0.750 s into the clip before, 1.000 s after: the audio jumps 0.250 s (12000 samples) ahead
[PASS] l20_ex2 (12 checks passed)
```

## Exercise 3 - The fewest markers for a take

[`solutions/ex3_fewest_markers.cpp`](ex3_fewest_markers.cpp)

The same program as chapter 19's Exercise 5, on a different curve. `fewest[j]` is the fewest markers from the
first candidate to candidate `j`; it is one more than the best `fewest[i]` over the earlier candidates `i` from
which a straight line to `j` keeps every candidate in between within the tolerance. Walking back from the last
candidate gives the list. Greedy - the longest run that fits, again and again - is not always the fewest, for the
same reason as in chapter 19; the check against every choice would catch it.

```text
Exercise 3 - the fewest markers for a take
  150 small takes: 150 with as few markers as the best of every choice, 150 well formed, 150 within the tolerance
  a take of 60 to 120 BPM within 1.0 ms:  33 markers, Step 1's equal pieces  46 (the worst sample 0.9864 ms off, 0.9865 between candidates)
  a take of 60 to 120 BPM within 0.1 ms: 102 markers, Step 1's equal pieces 143 (the worst sample 0.0951 ms off, 0.0951 between candidates)
  a take of up and down   within 1.0 ms:  59 markers, Step 1's equal pieces  87 (the worst sample 0.9911 ms off, 0.9916 between candidates)
  a take of up and down   within 0.1 ms: 187 markers, Step 1's equal pieces 268 (the worst sample 0.1000 ms off, 0.1000 between candidates)
[PASS] l20_ex3 (11 checks passed)
```

About 30 % fewer markers, always. The saving is where the ramp is fast: the curve bends with $`1/B^2`$, so at 120
BPM a piece may be twice as long as at 60 for the same error, and equal pieces sized for the slow end waste the
difference. Between the candidates (5 ms apart here) the error is at most 0.0005 ms more than at them.

Is it worth a dynamic program? It costs the square of the number of candidates; Step 1's formula is one line and
needs no candidates at all. For a project file with a few takes over ramps, 30 % fewer lines do not matter. For
a warp that is edited by hand - where every marker is a handle on the screen - they would.

## Exercise 4 - A fit to a fraction of a sample

[`solutions/ex4_fine_fit.cpp`](ex4_fine_fit.cpp)

The score is smooth around its best offset, so three of its values are enough to find its top: the parabola
through them. The new start is a fraction, and three things follow from it: the natural continuation of the next
piece is a fraction too (read with Hermite before the search), the piece itself is read with Hermite, and the
"is it the natural continuation already?" test compares with a tolerance - at speed 1 the candidate is a whole
sample and the continuation is the start before plus 512, also whole, and nothing may slide.

```text
Exercise 4 - a fit to a fraction of a sample
  noise at speed 1: at most 0 from the input
  220 Hz at speed 0.50: what is not the tone  96.4 dB below it (whole samples: 36.9 dB), +0.000 cents
  220 Hz at speed 2.00: what is not the tone  96.6 dB below it (whole samples: 43.3 dB), -0.000 cents
  440 Hz at speed 0.50: what is not the tone  91.0 dB below it (whole samples: 30.9 dB), -0.000 cents
  440 Hz at speed 2.00: what is not the tone  90.4 dB below it (whole samples: 39.6 dB), +0.000 cents
  the fit to a fraction is at least 51 dB better on these 8 tones
  1 kHz at speed 0.80 (a period of 48 samples): whole samples 155 dB, a fraction 83 dB
[PASS] l20_ex4 (19 checks passed)
```

From 31-43 dB to 90-97 dB: what Step 4 called "its" error was almost all the half sample. What is left now is the
error of reading between samples with a cubic - Step 3 measured it at 49 dB for 5 kHz, and it is smaller the
lower the frequency, which is why 220 Hz comes out better than 440.

The last line is the price. A tone whose period is a whole number of samples - 1 kHz at 48 kHz is 48 of them -
was fitted *exactly* by whole offsets; the parabola's top is a hair off a whole sample, the reading between
samples begins, and the result is 83 dB: still far below hearing, and worse than before. Reading with Step 3's
sinc instead of Hermite would take it back up, for the sinc's price. Most real audio is not a tone of a whole
number of samples, and the fit to a fraction wins.

## Exercise 5 - A fade over a jump

[`solutions/ex5_declick.cpp`](ex5_declick.cpp)

The class keeps two numbers between calls - where the reading would go on, and the speed it had - and, during a
fade, the same two for the old path. A jump is a first position too far from the first number. Then for
`fadeSamples` output samples both paths are read; the new one is weighted by $`\sin(t\pi/2)`$ and the old one by
$`\cos(t\pi/2)`$, which add up in power, $`\sin^2 + \cos^2 = 1`$, the right sum for two parts of a recording that
are not related. The fade may run over several calls, so its progress is a member, not a loop variable.

```text
Exercise 5 - a fade over a jump
  120 to 90 BPM while a 440 Hz clip whose start follows plays: the largest step from one sample to the next 0.953 without a fade, 0.029 with one
    (the tone's own: 0.029)
[PASS] l20_ex5 (16 checks passed)
```

Without the fade the wave steps by almost its whole peak-to-peak (0.95 of 1.0) from one sample to the next: a click. With
10 ms of fade no step is larger than the tone's own.

Two things are worth seeing:

- **Who jumps.** A clip that follows by varispeed or stretch does not: its position is a function of the tick, and
  the playhead keeps its tick. The check proves it in one line before it makes the jump with a clip whose start
  follows. Of Step 2's two clips, the one that does not follow at all jumps even further.
- **Where it belongs.** In DawLab the audio thread reads clips sample for sample (`renderClip()`), and a jump
  happens between blocks there - on a seek, at a loop's wrap, at a tempo change. A Declicker per clip on the
  audio thread is what a DAW does; it costs two readings for 10 ms after a jump, and the state is a handful of
  numbers, no allocation. Chapter 14's engine does not do it, which is why Step 2 said "a seek still clicks".

## Exercise 6 - Transients

1. **Finding the hits.** In short windows (5-10 ms), how much *new* energy appears - the rise of the energy, or
   better the spectral flux: the sum over frequency bands of the increase in each band, so that a hit under a loud
   bass is still seen. An onset is a peak of that curve above a threshold. A fixed threshold finds every hit of a
   loud part and none of a quiet one: make it adaptive (a multiple of the curve's running median), and add a
   minimum distance between onsets (30-50 ms) so that one hit with a rattle is not three.
2. **One piece per hit.** Cut the pieces at the onsets, not every half window. From each onset copy the attack
   (the first 10-30 ms) at speed 1 and put it exactly where the onset's tick is now - so the hit is on its beat
   and is heard once. All the stretching happens in the rest of the segment, up to the next onset: WSOLA there,
   with a stronger ratio, because the attack took none.
3. **Stretch and squeeze.** Stretched, the segment's decay is longer - WSOLA repeats pieces of the sustain, or, if
   there is none (a dry snare), there is silence before the next hit, which is what a drummer playing slower
   would do. Squeezed, the decay is cut: the segment may become shorter than its attack, and the attack is then
   faded out where the next one begins. Nothing is dropped, and each hit stays whole; the tail of a cymbal is the
   price.
4. **Several microphones.** They must share the onsets - found once, on the sum of the clips (or the loudest), and
   applied to all - and the cuts: otherwise the kick in the close microphone and in the overheads are moved by a
   few milliseconds against each other, and the kit sounds hollow (comb filtering). That is the same reason both
   channels of a stereo clip slide together in Step 5.
5. **Where and when.** Once per clip, ahead of time, when the clip is made to follow: the audio does not change,
   only the tempo does. It is O(n) in the clip's length with an FFT per window - cheaper than the stretch - and its
   result is stored with the clip: a list of source samples. Each onset is a natural warp marker, because a hit
   should land on a beat: with the onsets as candidates, Exercise 3 would choose the markers and Exercise 8
   quantizes them.

---

## Exercise 7 - Takes that follow as they were recorded

[`solutions/ex7_follow_as_recorded.cpp`](ex7_follow_as_recorded.cpp)

The whole difference to the app's own "follow" is one argument: the map passed to `markersForTake()`. A clip that
stays keeps its start in samples, so the sample where the take was recorded is still its start; given the map of
that moment, the markers say which tick each of its samples was heard on - and the session places the clip under
the map of now. The list is changed as a whole and performed once: one undo step for every take on the track.

```text
Exercise 7 - takes that follow as they were recorded
  recorded at 120, the tempo set to 90, then the app's "follow": 16 clicks, up to 3167 ms off the beats of 90
  the same take following as it was recorded (over 120): 16 clicks, at most 1 samples off the beats of 90
[PASS] l20_ex7 (25 checks passed)
```

With the app's markers the take plays exactly as before the tempo change, at its old samples, and the 16th beat is
three seconds off. With the markers of 120 BPM every beat is on a beat of 90.

What it shows is that the app should have kept the map. A take knows when it was recorded; the tempo map of that
moment is 256 points at most. Stored with the take (`Session::registerTakes()` already keeps a record of each),
the clip menu could offer "follow as recorded" by itself. The exercise does it from outside, with the map passed
in.

## Exercise 8 - Audio quantize

[`solutions/ex8_quantize_audio.cpp`](ex8_quantize_audio.cpp)

`findOnsets()` is the simplest detector that works on clean hits: a level relative to the range's peak, and a
quiet stretch before a new hit. Exercise 6 says why a real one looks at the spectrum.

`quantizeClip()` needs one idea: *where is a source sample heard now?* For a clip that already follows by its
markers it is their tick; for any other clip it is the tick of its song sample. Then each onset's tick moves
towards the nearest line by `strength`, exactly as chapter 18 moved a note's tick - only now the markers move,
and the audio between them is read faster or slower.

```text
Exercise 8 - audio quantize
  16 hits played by hand at 120 BPM, up to 24.3 ms off the beats; quantized onto quarter notes: 0 samples off
[PASS] l20_ex8 (27 checks passed)
```

Two hits that would land on one line keep the first: a marker cannot have the tick of the one before it. Under a
coarse grid most hits are left out that way - quantizing to whole bars is not what anybody wants of a drum take,
and the result shows it.

Varispeed changes the pitch of what is between two hits by the ratio of how far they moved. For a hit 25 ms late
over a beat of 500 ms that is 5 %, almost a semitone, for a moment: audible on a tom, not on a hi-hat. Stretch
keeps the pitch. Whether it doubles a hit (Step 4) depends on how far a stretch goes; here it is a few percent, and
the check does not listen for doubled hits.

## Exercise 9 - A loop that fits

[`solutions/ex9_loop_fits.cpp`](ex9_loop_fits.cpp)

The guess compares in logarithms because a loop played at 0.8 of its speed is as far off as one at 1.25: the
middle between 4 and 8 beats is $`\sqrt{32} = 5.66`$, not 6. The drop then is Step 1's two markers and the session's
own `withClipOnTop()`, which cuts what is under the loop - for a clip that follows, in its own terms (Step 6).

```text
Exercise 9 - a loop that fits
  a loop of 7.5 s (4 bars at 128 BPM) dropped on bar 2 at 120 BPM: taken as 16 beats, 16 clicks, at most 0 samples off the beats
[PASS] l20_ex9 (23 checks passed)
```

The loop has no fade in. A file rarely starts at a zero crossing, which is why the app fades every clip it makes
by 5 ms - but a loop starts on its downbeat, and a fade would swallow the first hit, the most important one.

**Double time.** A loop of four bars at 170 BPM lasts as long as two bars at 85, and at a song tempo of 120 it is
guessed as the second: played at the right speed, but with its bars half as long as they should be. Nothing in
the length can tell the two apart; an app shows the guess ("8 beats") and offers to double or halve it - or
listens for where the kick drum is.
