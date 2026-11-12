# Step 4 - A time-stretcher

**English** | [繁體中文](step-04-stretch.zh-TW.md)

[Overview](../README.md) | Previous: [Step 3 - Varispeed](step-03-varispeed.md) | Next: [Step 5 - The engine](step-05-engine.md)

## Goal

Change the speed of audio and keep its pitch. Build the simplest stretcher that works - WSOLA - and measure
what it does well and what it does badly.

## Design

### Pieces, not samples

Varispeed changes how fast every sample goes by, and the pitch is how fast samples go by. To keep the pitch the
samples must go by at their own speed. So the audio is cut into short pieces that overlap, each piece is played
as it is, and the pieces are put down closer together or further apart than they were taken.

```text
input    [  A  ]                      pieces taken every 256 samples ...
            [  B  ]
               [  C  ]

output   [  A  ]                      ... and put down every 512: twice as long,
                [  B  ]                   every piece at its own pitch
                       [  C  ]
```

This is **overlap-add** (OLA). A piece is a window of 1024 samples faded in and out by a Hann curve, and a new
piece starts every 512 samples of the output. Two Hann halves that overlap add up to exactly 1, so at speed 1 -
pieces taken every 512 samples too - the output is the input.

Where a piece is taken comes from Step 3: the piece whose middle is at output sample $`c`$ is taken around the
input position `positions[c]`. The stretcher does not know about tempo maps or markers; it gets a list of
positions, one per output sample, and that list can change its slope wherever it likes.

### The pieces do not fit

At any other speed two neighbouring pieces were not neighbours in the input. Where they overlap, the waves of
one are somewhere in the waves of the other. They cancel in one overlap and add up in the next, and a steady
tone comes out as something else: the self-check's 440 Hz through plain OLA has more of what is not the tone
than of the tone.

### Let a piece slide: WSOLA

A piece does not have to be taken exactly where the positions say. A few milliseconds earlier or later is the
same moment of the music - and somewhere in that range there is a place where it fits.

What would fit perfectly is known: the input that came right after the first half of the last piece, its
**natural continuation**. So among the starts within `search` samples of the candidate, take the one whose
first half looks most like it:

```math
score(d) = \frac{\sum_i a_i \, b_i}{\sqrt{\sum_i a_i^2}} \qquad a = \text{input from } candidate + d, \quad b = \text{the natural continuation}
```

The numerator is the correlation. Dividing by the size of the candidate makes the score about the shape: without
it a loud place wins for being loud, and dividing by the full energy instead would let a nearly silent place
win. Of equal scores the nearest wins.

This is **waveform-similarity overlap-add**. Two things make it safe:

- A piece is never more than `search` samples (5 ms here) from where the tempo wants it, and that error does not
  add up: the next candidate comes from the positions again, not from where the last piece ended up.
- At speed 1 the candidate *is* the natural continuation, nothing slides, and the output is still the input.

### What it does well

A tone. The self-check stretches 440 Hz to half and to twice its speed: the pitch stays within one hundredth of
a semitone, the level within 0.01 dB, and what is not the tone is 31 to 40 dB below it. That rest has a simple
cause: a piece slides by whole samples, so it fits to within half a sample, and half a sample of 440 Hz is a
small step of phase at every joint. Fitting to a fraction of a sample (Step 3 knows how to read there) would
take it further down.

### What it costs

A sound that happens **once**. A drum hit is in every piece that covers it - two of them at speed 1, where they
put it back on the same output sample. Stretched to twice the length, the pieces that hold the hit are put down
further apart than they were taken, and the hit lands on two places: it is heard twice, a flam. Squeezed to half
the length, pieces are taken a whole window apart; a hit near the edge of its piece is faded out by the window,
and the self-check loses 4 clicks of 8.

Nothing in WSOLA knows what a transient is. Real stretchers find the transients first and treat them apart, or
work on the spectrum instead of the waveform, and that is why they are long programs. For DawLab the choice
is per clip: **varispeed** for drums and for anything where pitch does not matter, **stretch** for what has a
pitch. Both follow the tempo exactly as far as Step 1's markers go.

The search costs time: 513 offsets of 512 multiplications for every piece, some 50 million a second of output.
That is nothing for a render ahead of time, and too much to do on the audio thread for every clip - Step 5.

The window and the search are lengths at 48 kHz. A piece is a time, though: the same 1024 samples at 96 kHz are
half as long, too short for a low tone's cycles, and a 55 Hz tone came out an octave low. The engine's stretcher
(`stretchSettingsFor()`, Step 5) scales them with the sample rate, so that a tone sounds the same at every rate -
and keeps whole, at every rate, the cycles of tones down to about 60 Hz. Lower bass needs a longer window and
search: more time, and drums that smear more.

## Your turn

Write `bestOffset()` and `stretch()` in `steps/step04-stretch/starter/step04.cpp`. Build and run:

```powershell
.\scripts\build.ps1 -Only 20-audio-tempo
.\build\bin\Debug\l20_step04_starter.exe
```

In the self-check's lines, "plain OLA" is the same stretcher with a search of 0, and a negative number of dB
means that what is not the tone is louder than the tone.

```text
Step 4: a time-stretcher
  noise at speed 1: at most 0 from the input
  440 Hz at speed 0.50: 4.0 s long, +0.72 cents, what is not the tone 30.9 dB below it, the level -0.00 dB (plain OLA: -25.2 dB, -2.17 dB)
  440 Hz at speed 0.80: 2.5 s long, +0.29 cents, what is not the tone 38.8 dB below it, the level +0.00 dB (plain OLA:  -1.4 dB, -0.08 dB)
  440 Hz at speed 1.25: 1.6 s long, -0.36 cents, what is not the tone 36.9 dB below it, the level -0.00 dB (plain OLA: -13.4 dB, -0.63 dB)
  440 Hz at speed 2.00: 1.0 s long, +0.25 cents, what is not the tone 39.6 dB below it, the level +0.00 dB (plain OLA: -19.4 dB, -1.79 dB)
  at speed 0.6 the level of noise changes by -0.56 dB, of a chord by -0.30 dB (plain OLA: -1.01 dB)
  at speed 1.5 the level of noise changes by -0.63 dB, of a chord by -0.28 dB (plain OLA: -1.49 dB)
  speed 0.75 for a second, then 1.5: 440 Hz in both (+0.36 and -0.72 cents, 36.9 and 30.9 dB)
  8 clicks at speed 0.5: 8 heard, 8 of them twice, the loudest copy at most 480 samples from its place
  8 clicks at speed 0.9: 8 heard, 0 of them twice, at most 53 samples off
  8 clicks at speed 2.0: 4 heard, 0 of them twice, at most 112 samples off
[PASS] l20_step04 (22 checks passed)
```

## What you learned

- To keep the pitch, audio is moved in pieces that each play at speed 1; overlapping Hann windows add up to 1.
- Pieces taken from other places do not fit; letting each slide to where it looks like the natural continuation
  of the last one does.
- The slide is bounded and does not add up, so the stretched audio stays on the tempo.
- WSOLA is good for tones and bad for single hits: stretched they double, squeezed they can vanish.
