# Chapter 19 - Solutions
**English** | [繁體中文](README.zh-TW.md)

[Back to the chapter](../README.md) | [Exercises](../exercises/README.md)

Run all solution programs with `.\scripts\build.ps1 -Only 19-instruments-tempo -TestLabel solution`. Every one
ends with `[PASS] l19_exN (... checks passed)`.

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
