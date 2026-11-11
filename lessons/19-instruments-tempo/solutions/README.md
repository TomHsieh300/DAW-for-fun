# Chapter 19 - Solutions
**English** | [繁體中文](README.zh-TW.md)

[Back to the chapter](../README.md) | [Exercises](../exercises/README.md)

Run all solution programs with `.\scripts\build.ps1 -Only 19-instruments-tempo -TestLabel solution` (add `-Juce`
for Exercises 7-9). Every one ends with `[PASS] l19_exN (... checks passed)`.

---

## Exercise 1 - Why this way?

1. **Seconds.** A map in samples would have to be converted when the rate changes, every point rounded to a
   whole sample - and rounded again at the next change, so that 44.1 → 48 → 44.1 kHz does not give the same map
   back. In seconds the map is a property of the song; the rate comes in at the very end, in one rounding per
   tick. It also costs nothing: the audio thread multiplies once more.
2. **The loop's edges.** Where two ticks share a sample (a fast tempo, a low rate), the first tick at or after
   the loop's end *sample* can be the tick before the loop's end: found from samples, a pass would stop one tick
   short, and the next pass would start one tick early - a note on that tick is lost at the end of every pass
   or played twice at the start. Given in ticks, a pass is exactly the loop's ticks, whatever the samples do.
3. **Musical place.** The other choice keeps the *sample*: the playhead stays where it is on the screen's time
   axis, and lands on another bar and beat - under the new map that sample is somewhere else in the music. The
   audio would play on without a jump, but the notes would skip or repeat, and the bar counter would jump.
   Keeping the musical place, the notes go on (held ones keep sounding) and the audio clips jump - which is
   what the user asked for by dragging a tempo point: the music under the playhead, at a new speed.
4. **Three buffers.** An atomic holds a word, not 8 KB. A mutex would make the audio thread wait for the UI
   thread, which may be descheduled while it holds the lock: a dropout. A seqlock's reader copies memory while
   the writer may be writing it; it throws the copy away when the version changed, but the read itself is a
   data race, which C++ leaves undefined (and ThreadSanitizer reports), and the audio thread would copy 8 KB on
   every retry. With three buffers each one belongs to one thread at a time, and the hand-over is one exchange.
5. **Early by its compensation.** While recording, the musician plays *to what they hear* of the other tracks,
   and hears their own instrument as soon as possible; their sound being early in the mix by some milliseconds
   is not something they play to. The recorded notes are placed by when the song was heard, not by how the
   instrument sounded. On playback nobody is playing: the take must line up with the others exactly, and only
   the whole plan does that.
6. **Playing early.** The notes land early - where the keys went down against the song they heard. That is
   right for the *notes*: a note is when the musician meant it, and a faster instrument, or the same one
   compensated on playback, plays it there. On playback the plan delays every other track by the sampler's
   latency, so the sampler's sound comes exactly on the note's time: the early playing was the musician's
   compensation for a delay that playback no longer has. It will sound a little ahead - which is why a
   musician should monitor through a fast instrument, and why DAWs offer low-latency monitoring.
7. **Time.** It keeps how long the step takes: the tempo is chosen so that the step lasts exactly as long as the
   ramp does over those ticks. A reading program does nothing with a tempo but turn ticks into time, so the
   times of the step's edges are all it can see; the "true" tempo in the middle of the step would give every
   step a slightly wrong length, and the errors add up (Exercise 2 (e): 1.9 ms after one beat).

## Exercise 2 - Tempo by the numbers

[`solutions/ex2_tempo_numbers.cpp`](ex2_tempo_numbers.cpp)

- **(a)** $`k = 60 / 15360`$ BPM per tick, so the ramp takes
  $`\frac{60}{960} \cdot \frac{\ln(120/60)}{k} = 16 \ln 2 = `$ **11.09 s**. The bar line in the middle, where the
  tempo is 90: $`16 \ln 1.5 = `$ **6.49 s** - later than half the time, since the first half is the slow one.
  Half the time (5.545 s) is where $`\ln(B/60) = \frac{1}{2}\ln 2`$: $`B = 60\sqrt{2} = `$ **84.85 BPM**, at tick
  $`15360(\sqrt{2} - 1) = `$ **6362** (bar 2, 2.6 beats in).
- **(b)** Two bars at 120 BPM are 4 s; two bars at 90 are $`8 \cdot 60 / 90 = 5.33`$ s: bar 5 starts at
  **9.333 s**, sample **448 000** at 48 kHz and **411 600** at 44.1 kHz. One map, two rates.
- **(c)** The total is the largest, **4096**; the tracks wait 4096, 3840, 4000 and 0. A key on the first:
  $`1024 + 0 + 4096 + 512 = 5632`$ samples = **117 ms**. With the first track armed and low-latency monitoring:
  $`1024 + 0 + 0 + 512 = 1536`$ = **32 ms**, and it is 4096 samples (85 ms) early against the others.
- **(d)** $`60\,000\,000 / 97.5 = 615\,384.6`$, stored as **615 385**, which is 97.499 939 BPM. Over ten minutes:
  $`600 \cdot (97.5 / 97.499939 - 1) = `$ **0.375 ms** late. Small - but two files of one song, converted from
  different tempos, do not stay together for an hour.
- **(e)** $`\frac{60}{960} \cdot \frac{\ln(60/120)}{-60/960} = \ln 2 = `$ **0.6931 s**. With the middle tempos
  112.5, 97.5, 82.5 and 67.5: $`15 \cdot (1/112.5 + 1/97.5 + 1/82.5 + 1/67.5) = 0.6912`$ s, **1.93 ms** early: the
  time of a step is the mean of $`1/B`$ over it, not $`1/B`$ at its middle.

## Exercise 3 - A meter map

[`solutions/ex3_meter_map.cpp`](ex3_meter_map.cpp)

`prepare()` adds up the bars of each change: $`start_i = start_{i-1} + (bar_i - bar_{i-1}) \cdot ticksPerBar_{i-1}`$.
`tickOfBar()` finds the last change at or before the bar with `std::upper_bound`; `positionOf()` finds the change
by its start in ticks, then divides twice - by the bar's length, by the beat's.

The one trap is the division. C++ rounds towards zero: $`-1 / 3840`$ is 0, and tick -1 would be "bar 0, beat 0,
tick -1". A `floorDiv` that rounds down puts it in bar -1, beat 3, tick 959.

```text
Exercise 3 - a meter map
  one meter: 4/4 bars of 3840 ticks, 6/8 bars of 2880; a tick before 0 is in bar -1
  4/4, 4/4, 2/4, then 6/8, 7/16 from bar 11: bar lines at 0, 3840, 7680, 9600, 12480 ... 29760
  300 random maps (1-21 changes, 1/2 to 12/16): 0 bar lines wrong, 0 positions that do not come back, 0 outside their
    bar, 0 out of order
[PASS] l19_ex3 (16 checks passed)
```

In DawLab the meter map would sit next to the tempo map in the transport: the ruler asks it for the bar
lines, the tempo map for where each of those ticks is in samples. The two never need each other.

## Exercise 4 - A tempo map from beats

[`solutions/ex4_beats_to_map.cpp`](ex4_beats_to_map.cpp)

Between beat $`k`$ and beat $`k + 1`$ there are 960 ticks and $`d_k = t_{k+1} - t_k`$ seconds, so the tempo there is
$`60 / d_k`$: a jump at tick $`960k`$. A beat whose tempo is the one before it needs no point - a steady band is
one point, not two hundred.

`tapTempo()` sorts the intervals and takes the middle one. With a missed tap the intervals are 0.5, 0.5, 1.0, 0.5,
0.5: the mean says 100 BPM, the median 120.

```text
Exercise 4 - a tempo map from beats
  beats 0.6, 0.6, 0.5, 0.5, 0.4 s apart: 100 BPM, 120 from beat 3, 150 from beat 5; every beat within 0.0e+00 s
  300 random bands of up to 200 beats: 0 refused, 0 with a beat off its time, 0 with a point where the tempo did not change
  five taps with one missed: the median says 120.0 BPM, the mean would say 100.0
[PASS] l19_ex4 (22 checks passed)
```

A real band's beats all differ by a little, and every beat becomes a point: Exercise 5 thins such a map without
moving a beat by more than a tolerance.

## Exercise 5 - Thin a tempo list

[`solutions/ex5_thin_tempo.cpp`](ex5_thin_tempo.cpp)

Draw the song as a curve: ticks along, seconds up. A list of jumps is a line with a corner at every point, and
thinning it means drawing a line with fewer corners, from corner to corner of the old one, that never leaves it
by more than the tolerance. A run from point $`i`$ to point $`j`$ *fits* if the straight line between them - one
tempo, $`\frac{60}{960} \cdot \frac{tick_j - tick_i}{t_j - t_i}`$ - passes every point between within the
tolerance. Only the corners need a look: between two of them both lines are straight.

The fewest runs is a shortest path. `fewest[j]` is the fewest runs from the first point to point $`j`$:

```math
fewest_j = \min_{i < j,\ (i, j)\ fits} fewest_i + 1
```

and `from[j]` remembers the $`i`$ that won, to walk back from the last point. The last point is then added as it
is. That is $`n^2 / 2`$ pairs with up to $`n`$ points each - for 250 beats two and a half million comparisons,
once, on the message thread.

Why not the longest run that fits, again and again? Because "fits" is not inherited: a run can fit although a
part of it, from a later start to the same end, does not. A wobble of ±9 ms around a steady tempo fits a
tolerance of 10 ms from one calm beat to another; from a beat that was 9 ms late, the line starts 9 ms off and
the early beats are 18 ms from it. The greedy run ends where it must, perhaps on such a beat, and pays for it
with the next run. The self-check's 400 short lists catch a greedy solution.

```text
Exercise 5 - thin a tempo list
  beats of 0.50, 0.52, 0.48, 0.50 s within 15 ms: 117.65 BPM, 122.45 from beat 3; the worst tick is 10.0 ms off
  400 random lists of up to 12 points against every choice: 0 refused, 0 that move a tick too far, 0 not the fewest (240 got shorter)
  250 beats within  0.0 ms: 250 points (worst tick 0.00 ms off); every 1. point instead: 0 ms off
  250 beats within  1.0 ms: 166 points (worst tick 0.98 ms off); every 2. point instead: 71 ms off
  250 beats within  5.0 ms:  44 points (worst tick 4.80 ms off); every 6. point instead: 394 ms off
  250 beats within 20.0 ms:  27 points (worst tick 19.63 ms off); every 10. point instead: 643 ms off
[PASS] l19_ex5 (23 checks passed)
```

The last column is DawLab 19's import: with the same number of points it is off by tens of times as much,
because a point that stays keeps its own tempo for the beats of the ones that went. Exercise 9 puts
`thinTempo()` in its place.

## Exercise 6 - Audio that follows the tempo

1. **Two meanings.** *The start follows*: the clip's start is a tick, not a sample; when the map changes the
   clip moves and plays at its own speed. It needs nothing but the tick, and it is right for what has no beat
   of its own or must not change: a spoken line, a sound effect on bar 17, a cymbal. *Everything follows*: every
   beat in the clip stays on its beat of the song, so the audio is played faster or slower. That needs to know
   where the beats in the audio are, and a time-stretcher; it is right for loops and for takes of a band. A
   third choice is cheap and honest: play faster by resampling, as a tape does - pitch goes with the speed. Fine
   for drums, wrong for a singer.
2. **What the clip stores.** Its start and length in ticks, and a list of pairs *(sample in the file, tick in
   the song)* - warp markers. Between two markers the audio is spread evenly over the ticks. A loop of known
   tempo needs two markers; a take recorded over a map gets a marker at every point of the map under it (and a
   ramp enough of them: Exercise 5 says how few, for a tolerance). Storing only "it was recorded at 120 BPM" is
   the special case of two markers, and cannot describe a take over a ramp.
3. **Where the stretcher runs.** *On the audio thread*: a new tempo is heard at once, but a stretcher works on
   windows of some tens of milliseconds, so it has latency - which the track must report, as an instrument does
   in Step 3 - and its cost is paid on every block, for every clip, in the thread that can least afford it. Its
   ratio must be changeable per block. *Ahead of time*: a worker thread renders the stretched audio to a buffer,
   and the engine gets it in a command, as it gets everything else; the audio thread plays a plain clip, with no
   latency and no extra cost. The price: after a tempo edit the clip is stale until the render is done (play the
   old render moved to its new start, or the clip muted, and swap when ready), and memory for a second copy. For
   DawLab ahead of time is the fit: `renderClip()` does not change, and the hand-over is the one the engine
   already has. A DAW that lets you drag the tempo while listening needs the real-time one, usually with a
   cache of the rendered result.
4. **No drift.** A segment carries its ticks (Step 2). For a block the player asks the markers which file
   samples belong to `tickFrom` and `tickTo`, and feeds the stretcher exactly that many - the position is
   computed from ticks every block, never counted up by a ratio. A ramp inside the clip is then nothing special:
   the ratio just differs from block to block.
5. **The rest.** *Recording*: a take gets the markers of the map it was recorded over, so that right afterwards
   it plays as it was (ratio 1), and stretches against that when the map changes later. A take made with the
   metronome off gets no markers and follows with its start only. *Project file*: per clip the mode and the
   markers - a new format number, and old files load as "does not follow". *Undo*: nothing new for the tempo:
   the `SetTempoMap` edit already restores the map, and the clips are in ticks, so they follow it back. Changing
   a clip's mode or markers is one more edit. The renders are not saved and not undone: they are a cache, made
   again from the file, the markers and the map.

---

## Exercise 7 - A click track

[`solutions/ex7_click_track.cpp`](ex7_click_track.cpp)

The clip is built before anything changes: a note every 960 ticks, pitch 84 and velocity 127 where `beat % 4`
is 0. Then one `edit::AddTrack` from `makeNewTrack()` carries it in `track.midiClips`, so the track and its clip
are one entry in the history - adding the track and then its clip would be two, and one undo would leave an empty
track called "Click". `perform()` says whether there was room; only then is the result filled in.

```text
Exercise 7 - a click track in DawLab 19
  three bars of click from bar 2: 12 notes; heard from sample 96240 (120 BPM), 192240 (60 BPM), 133324 (a ramp from 60 to 120)
  refused: bar 0, no bars, 1000 bars, and track 129 of a project that holds 128
[PASS] l19_ex7 (18 checks passed)
```

The three numbers are the point of the chapter in one line. The notes were written once, in ticks; 120 BPM, 60 BPM
and a ramp are three maps, and the click is where bar 2 is in each (240 samples later: the mix's latency). A click
made of audio would have to be rendered again for every one of them.

## Exercise 8 - Tap tempo

[`solutions/ex8_tap_tempo.cpp`](ex8_tap_tempo.cpp)

Both functions are a few lines, because the work is done: Exercise 4 computes, and `Session::setTempoPoints()`
records one `edit::SetTempoMap` and tells the engine. What is left is deciding what must *not* happen:

- a tempo outside the map's range is refused before anything is touched (fewer than two taps give 0, which is
  outside it);
- the tempo the map already has returns before the edit: an undo step that changes nothing is a step the user
  has to press through;
- only the first point changes: tapping is "the song's tempo", and a song with a ritardando at its end keeps it.

```text
Exercise 8 - tap tempo in DawLab 19
  taps 0.48 s apart with one missed: 125.0 BPM; 0.47 s apart: 127.7 BPM
  beats 0.6, 0.6, 0.5, 0.5, 0.4 s apart: 3 tempo points, beat 6 at sample 124800
  100 random bands of up to 200 beats: 0 refused, 0 with a beat more than a sample off
[PASS] l19_ex8 (27 checks passed)
```

Rounding to a tenth is not cosmetic. A tap is accurate to some milliseconds, so the digits after the first
decimal are noise - and a tempo of 127.65957 in the tempo field says the opposite.

## Exercise 9 - A better MIDI import

[`solutions/ex9_better_import.cpp`](ex9_better_import.cpp)

`prepareTempos()` first makes a list that Exercise 5 accepts, then asks for as little tolerance as it takes: 0
first, because many files with "too many tempos" only repeat one tempo every bar, and those cost nothing to
drop. The doubling is a search for the smallest tolerance on a coarse scale; each try is the whole dynamic
program again, which is fine for an import and not for a mouse drag. Tempos that `thinTempo()` computed can land
a hair outside 20 .. 999 by rounding, so they are clamped once more before the map sees them.

`importSongBetter()` changes one field of a copy of the song and calls the app's import. Nothing of the tracks,
clips and the single undo step had to be written again.

```text
Exercise 9 - a better MIDI import in DawLab 19
  700 tempos into 256 points: 179 points within 4.0 ms (the worst beat 3.99 ms off)
  700 tempos into  64 points:  44 points within 64.0 ms (the worst beat 63.79 ms off)
  700 tempos into  16 points:  12 points within 512.0 ms (the worst beat 506.31 ms off)
  a file of 700 tempos imported: 179 points, the worst beat 3.99 ms off; every k-th tempo: 234 points, 222 ms off
[PASS] l19_ex9 (34 checks passed)
```

Same file, fewer points, and the worst beat is off by a few milliseconds where every k-th tempo is off by a fifth
of a second. That is why this became the app's import: `app/Source/TempoThinning.h` is `thinTempo()` and the
search for the tolerance, `importSong()` calls it, and the status line says by how much a beat may have moved.
Two things are different there. A tempo between two points that both stay is left bit for bit, so a file that
fits comes in exactly as it is; and `tests/test_tempo_thinning.cpp` tests it without JUCE, in every build. The
last check of this exercise imports the file with the app alone and expects the same number of points.
