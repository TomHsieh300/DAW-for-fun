# Step 3 - Varispeed

**English** | [繁體中文](step-03-varispeed.zh-TW.md)

[Overview](../README.md) | Previous: [Step 2 - A clip whose start follows the tempo](step-02-follow-start.md)

## Goal

Let everything in a clip follow the tempo by playing it at another speed, the way a tape does: read the audio
faster or slower. Know where to read for every sample of the song without ever drifting, how to read between two
samples of the file, and what reading faster does to the top of the spectrum.

## Design

### Speed and pitch

Step 1 gave the speed: the tempo now over the tempo the audio was made at. Reading a file at speed $`r`$ plays
everything in it $`r`$ times as fast - and $`r`$ times as high:

```math
semitones = 12 \log_2 r
```

Twice as fast is an octave up. A loop made at 120 BPM and played at 126 is 0.84 semitones sharp. Nobody minds
that on a drum loop; a singer or a piano is out of tune with everything else in the song. Varispeed is the right
tool for drums and for small changes, and Step 4 is for the rest.

### The position comes from the song, not from a counter

At a speed that is not 1 the position in the file is a fraction. The obvious way to get it - start somewhere and
add the speed for every sample - is wrong twice. A sum of a million rounded steps drifts, and after a seek or a
loop's wrap the sum would have to be found again anyway. So the position is computed from the sample of the song,
through the two maps of Step 1, for every block:

```math
position(s) = \mathrm{warp.sourceAt}(\mathrm{map.tickAt}(s / rate))
```

The self-check plays ten minutes of a song that ramps from 90 to 150 BPM in every bar. Counted up with the speed
at the start of each block, the position is 1484 samples (31 ms) off at the end. Computed from the song's own
samples it is never more than 0.003 samples off, at the end as at the start.

### Inside a block

`position(s)` for every single sample costs a binary search and an `exp()` each. It does not have to: while the
tempo holds, the position is a straight line between its **knots** - the tempo points of the map and the markers
of the warp map. So a block computes the exact position only at its corners and draws lines between them:

- the block's first sample and the sample after its last one,
- every knot inside the block (a knot is rarely on a whole sample),
- and every sample of the song that is a multiple of 64.

The last one is for ramps, where the position bends everywhere. Step 1's bound says how much: a chord over
$`h`$ samples is off by at most $`h^2/8 \cdot \max|position''|`$. Over a block of 512 samples of a steep ramp
that was 0.7 samples in the middle of the block - not much, but it bends the pitch a little at every block's
edge. Pieces of 64 samples are 64 times closer: 0.016 samples at worst over the self-check's random ramps. The
grid is counted from sample 0 of the song, not from the block, so the corners do not depend on where the blocks
happen to start.

### Between two samples

Three ways to read a file at a fraction, in the order of their cost:

- **Linear**: the line between the two neighbours. Two samples, one multiplication.
- **Hermite**: a cubic through the two neighbours whose slopes are those of *their* neighbours (Catmull-Rom).
  Four samples, a handful of multiplications.
- **Sinc**: what the sampling theorem says the signal between the samples *is* - a sum of
  $`\sin(\pi x)/(\pi x)`$ around the position. The sum is infinite; here it is cut at 16 zero crossings to each
  side and faded out with a Blackman window. About 33 samples and as many sines.

The error grows with the frequency. For a 5 kHz tone the self-check measures what is left after the tone itself
is taken out:

```text
linear    35 dB below the tone       audibly dull and dirty in the top octaves
Hermite   49 dB
sinc     100 dB and more             below what 16 bits can hold
```

At 440 Hz Hermite is already 100 dB down. Which one to use is a budget: Step 5 renders ahead of time, where the
sinc costs nothing that matters.

### Faster than 1 is sampling again

Reading a file twice as fast is sampling it again at half the rate. What was above the new Nyquist frequency
does not vanish: it comes back down as an alias. A 15 kHz tone read twice as fast should be 30 kHz, which a
48 kHz stream cannot hold - it is heard at 18 kHz, at almost full level.

The sinc is a low-pass, and it can be its own anti-alias filter: stretched by the speed it cuts at
$`Nyquist / speed`$ instead of Nyquist (and reaches $`speed`$ times as far, so it costs more). Hermite and
linear cannot do that. The self-check: the alias is 3 dB below the tone with Hermite, 78 dB with the sinc.

The sinc's sum is divided by the sum of its weights. The window makes the weights of a given fraction add up to
a little more or less than 1, and without the division a steady signal would ripple with the fraction.

### A clip is a range of its source

A warped clip plays a range of its source, `[sourceFrom, sourceTo)`, and covers the song samples whose position
is in that range - wherever the two maps put them. Two halves of one take share the cut: one ends at the source
position where the other starts. So the first song sample of the second half is the sample after the last one of
the first, at every tempo. Step 2's gap of 4 s and its overlap are gone, and the cut take is, sample for sample,
the uncut one.

## Your turn

Write `semitonesOf()`, `readAt()`, `positionsFor()`, `firstSample()`, `endSample()` and `renderWarped()` in
`steps/step03-varispeed/starter/step03.cpp`. Build and run:

```powershell
.\scripts\build.ps1 -Only 20-audio-tempo
.\build\bin\Debug\l20_step03_starter.exe
```

```text
Step 3: varispeed
  a loop made at 120 BPM played at 126: 0.84 semitones up; at 60: -12
  5 kHz read at speed 0.7 and 1.3, linear : the error is  35.1 and  35.1 dB below the tone
  5 kHz read at speed 0.7 and 1.3, Hermite: the error is  48.9 and  48.9 dB below the tone
  5 kHz read at speed 0.7 and 1.3, sinc   : the error is 100.4 and 116.3 dB below the tone
  15 kHz read twice as fast: Hermite leaves an alias at -3.0 dB, the sinc -78.3 dB
  600 blocks of up to 512 samples over maps of jumps  : the worst position 2.8e-09 samples off, 0 blocks not exact at their start
  600 blocks of up to 512 samples over maps with ramps: the worst position 0.016 samples off, 0 blocks not exact at their start
  ten minutes of ramps in blocks: the worst position 0.003 samples off; counted up from the speed instead: 1484 samples (31 ms)
  300 clips over random maps: 0 with a first or a last sample that is not the range's
  a take cut in two at bar 3, at 60 BPM: no gap (the halves meet at 8.0 s), the cut is 0 away from the uncut take,
    and what plays is 220 Hz, an octave down (everything else 102 dB below)
  16 clicks recorded at 120 BPM, played over 100, 150 and a ramp from 70 to 130: 16 on their beats (the last at 7.861 s, was 7.500 s)
[PASS] l20_step03 (32 checks passed)
```

The last digits of these numbers may differ a little with another compiler's math library.

## What you learned

- Varispeed follows the tempo with everything in the clip, and moves the pitch by $`12 \log_2`$ of the speed.
- The position in the file is computed from the song's sample in every block; a counter drifts.
- Inside a block the position is a line between exact corners: the block's ends, the knots, and a grid for ramps.
- Linear, Hermite and sinc are three prices for the same question; only the sinc can also stop aliasing when
  reading faster than 1.
- Clips that are ranges of one source meet at every tempo.
