# Step 1 - A warp map

**English** | [繁體中文](step-01-warp-map.zh-TW.md)

[Overview](../README.md)

## Goal

Say where the audio of a clip belongs in the music, so that it can follow the tempo: which sample of the file
is on which tick of the song. From that and the tempo map, find what a player needs - the sample to play now, and
how fast.

## Design

### Markers

A note follows the tempo because it is stored as a tick. A clip of audio is thousands of samples, and each of
them belongs somewhere in the music - but nobody stores a tick per sample. A **warp map** stores a few pairs,
the **warp markers**:

```text
(source, tick)        "this sample of the file belongs to this tick of the song"
```

Between two markers the audio is spread evenly over the ticks. Before the first and after the last marker the
line of the nearest pair goes on, so a clip that is trimmed or extended still knows where it is.

```text
source
(samples)                         . (96000, bar 3)
   |                         .
   |                    .           a loop of one bar, put on bar 2:
   |               .                two markers, one straight line
   |          .
   +-----o-----------------------> ticks
      (0, bar 2)
```

Both the source position and the tick are fractions. A marker is rarely on a whole tick, and at 120 BPM and
48 kHz one tick is 25 samples - far too coarse to place audio.

### Two maps, two questions

The warp map ties samples of the file to ticks. The tempo map of chapter 19 ties ticks to time. Together they
answer what a player asks:

```text
song sample --tempo map--> tick --warp map--> source sample       what do I play now?
source sample --warp map--> tick --tempo map--> seconds           when is this heard?
```

Neither map knows the other. Change the tempo map, and the same markers put the same audio on the same beats, at
new times: that is "following the tempo".

### The speed

How fast the file is read is the product of two slopes: the warp map's source samples a tick, and the tempo
map's ticks a song sample.

```math
speed = \frac{\Delta source}{\Delta tick} \cdot \frac{B \cdot 960}{60 \cdot rate}
```

A loop made at 120 BPM has 25 samples a tick at 48 kHz. At 120 BPM the song has 1/25 tick a sample, and the speed
is 1: the loop plays as it is. At 60 BPM the speed is 0.5. The speed is the ratio of the tempo now to the tempo
the audio was made at - Steps 3 and 4 are two ways to play at a speed that is not 1.

### The markers of a loop

A loop that is known to be four beats long needs two markers: its first sample on the tick where it is put, and
the sample after its last one four beats later. Its own tempo, $`beats \cdot 60 \cdot rate / length`$, is
never stored: the two markers say it.

### The markers of a take

A take was recorded over a tempo map, starting at some sample of the song. It should play exactly as it was
recorded while the map stays, and follow when the map changes later. So its markers copy the map it was
recorded over: the sample $`n`$ of the take was heard at $`(start + n) / rate`$ seconds, and the tempo map says
which tick that was.

While the tempo holds, the ticks of a take are a straight line of its samples. A marker at the take's start,
one at every tempo point under the take and one at its end are then **exact**: the lines between them are the
map's own.

On a ramp the line is a curve, and straight pieces only come close. How close is a classic result: a curve
$`f`$ replaced by its chord over a piece of width $`h`$ is off by at most

```math
\frac{h^2}{8} \max |f''|
```

Here $`f(t)`$ is the time of tick $`t`$ on the ramp, with $`f' = \frac{60}{960} / B(t)`$ and
$`f'' = -\frac{60}{960} \, k / B(t)^2`$, where $`k`$ is the ramp's slope in BPM a tick. $`|f''|`$ is largest at the
slow end of the ramp. A ramp of $`T`$ ticks cut into $`n`$ equal pieces has $`h = T / n`$, so a tolerance
$`\varepsilon`$ in seconds needs

```math
n = \left\lceil T \sqrt{\frac{\max |f''|}{8 \, \varepsilon}} \right\rceil
```

pieces. The square root is the good news: ten times the accuracy costs about three times the markers. A ramp
from 60 to 120 BPM over four bars needs 46 markers to stay within 1 ms, and 143 for 0.1 ms.

A marker's error does not add up along the take: every marker is computed from the tempo map itself, not from
the marker before it.

## Your turn

Write `WarpMap::sourceAt()` and `tickAt()`, `sourceAtSample()`, `songSecondsOf()`, `speedAt()`,
`markersForLoop()` and `markersForTake()` in `steps/step01-warp-map/starter/step01.cpp`. Build and run:

```powershell
.\scripts\build.ps1 -Only 20-audio-tempo
.\build\bin\Debug\l20_step01_starter.exe
```

The self-check puts a loop on bar 2 and slows the song down under it, goes both ways through 200 random warp
maps, makes takes over random tempo maps and asks whether every sample is where it was recorded, and then changes
the map under the takes:

```text
Step 1: a warp map
  a loop of one bar on bar 2: its third beat at 3.0 s at 120 BPM, at 4.0 s with 60 BPM from bar 2 (speed 0.50)
  200 random warp maps: 0 ticks not back from their source, 0 steps backwards
  200 takes over maps of jumps: 0 refused, 0 with a marker too many or too few, 0 not where they were recorded
    (worst 4.3e-14 s), 0 places with a speed other than 1
  a take of a ramp from 60 to 120 BPM over 4 bars within 10.00 ms:  16 markers, the worst sample 8.3299 ms off
  a take of a ramp from 60 to 120 BPM over 4 bars within  1.00 ms:  46 markers, the worst sample 0.9661 ms off
  a take of a ramp from 60 to 120 BPM over 4 bars within  0.10 ms: 143 markers, the worst sample 0.0985 ms off
  a take of a ramp from 60 to 120 BPM over 4 bars within  0.01 ms: 449 markers, the worst sample 0.0099 ms off
  150 takes over maps of jumps and ramps within 0.5 ms: 0 refused, 0 off by more, 0 with more markers than needed (8871 markers in all)
  100 takes, then a new map: 0 refused, 0 samples not at half their time at twice the tempo,
    0 places where the speed is not new tempo / old tempo
[PASS] l20_step01 (22 checks passed)
```

The worst sample of each ramp is close to its tolerance, not far below it: the bound is tight where the ramp is
slow, and equal pieces spend more markers than needed where it is fast. Chapter 19's Exercise 5 would find the
fewest; the formula is one line and never wrong.

## What you learned

- A warp map is a few pairs of a file's sample and a song's tick, with straight lines between them.
- The warp map and the tempo map do not know each other; a new tempo map moves the audio without touching a marker.
- The speed is the product of two slopes: the tempo now over the tempo the audio was made at.
- A take over jumps has exact markers at the tempo points; a ramp needs $`\sqrt{1/\varepsilon}`$ of them.
