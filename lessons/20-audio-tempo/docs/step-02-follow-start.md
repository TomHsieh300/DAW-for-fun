# Step 2 - A clip whose start follows the tempo

**English** | [繁體中文](step-02-follow-start.zh-TW.md)

[Overview](../README.md) | Previous: [Step 1 - A warp map](step-01-warp-map.md) | Next: [Step 3 - Varispeed](step-03-varispeed.md)

## Goal

The cheapest way to follow the tempo: a clip that keeps its place in the music with its **start** only, and
plays at its own speed. Find out what that needs, what it is right for, and where it stops being enough.

## Design

### The tick is the truth

A clip that follows keeps the tick where it belongs, `startTick`, and its start in samples is **derived** from
it whenever the tempo map changes:

```math
start = \mathrm{round}(\mathrm{secondsAt}(startTick) \cdot rate)
```

Never the other way round. The start in samples is rounded, and a tick found again from a rounded sample is not
quite the tick it came from. Do that once and nobody notices; do it at every tempo change and the clip walks
away from its bar line. The self-check sends a clip on bar 9 through 1000 tempo maps and back: the clip that
keeps its tick is where it was, to the sample; one whose tick is found again each time is 15 samples off.

This is the rule of chapter 18 for notes and of chapter 19 for a loop set in bars, now for audio: what is
counted in beats is stored in ticks.

The tick is a fraction, as Step 1's markers are. A clip is rarely on a whole tick, and turning following on
must move nothing: `startTick` becomes the exact tick of the sample where the clip is.

### Two ways to move it

A free drag puts the clip on a sample; its tick is found from that sample, once. A drag that snaps to the grid
puts it on a tick, and that tick is kept exactly - not the tick of the sample the grid line was rounded to. A
clip that does not follow just gets the sample of the tick, and forgets the tick.

### The length stays

Only the start follows. The clip is as long as its audio, in samples, so where it **ends** in the music depends
on the tempo: two seconds are a bar at 120 BPM and half a bar at 60.

```text
120 BPM   bar 2            bar 3            bar 4
          [=== clip ======]
 60 BPM   bar 2                             bar 3
          [=== clip ======]                              the same 2 s, half a bar
```

That is right for audio that has no beat of its own: a cymbal on bar 17, a spoken line, a sound effect.

### Seams

It is wrong for audio that was cut. Two halves of one take lie end to start; when both follow and the tempo
goes down, the second half moves away and leaves a gap of silence. When the tempo goes up it moves into the
first half, and the engine adds clips that overlap (chapter 14 made crossfades of that), so the overlap is heard
twice.

```text
120 BPM   [== first half ==][== second half ==]
 60 BPM   [== first half ==]                  [== second half ==]      a gap of 4 s
240 BPM   [== first half ==]
                   [== second half ==]                                 2 s heard twice
```

A take of a band, or a loop, has a beat in every bar of it. For those the start is not enough: everything in the
clip has to follow, and that means playing it at another speed. Step 1's `speedAt()` says which.

### While it plays

When the map changes while the song plays, the playhead keeps its musical place (chapter 19, Step 2), and the
samples jump. A clip that follows jumps too, with its start. But the playhead inside the clip does not land on
the audio it was playing: the time from the clip's start to the playhead is another one at another tempo.

At 120 BPM, one beat into a clip is half a second of its audio. Change to 60 BPM, and one beat into the clip is
a whole second: the audio jumps half a second forward. A clip that does not follow jumps further, by 2.5 s here,
because the playhead moved and the clip did not. Either way it is a seek for the audio, and it can click as any
seek can: DawLab's engine does not fade over a jump of the playhead.

## Your turn

Write `setFollow()`, `startUnder()`, `retime()`, `moveTo()`, `moveToTick()`, `endTick()`, `gaps()` and
`jumpInClip()` in `steps/step02-follow-start/starter/step02.cpp`. Build and run:

```powershell
.\scripts\build.ps1 -Only 20-audio-tempo
.\build\bin\Debug\l20_step02_starter.exe
```

```text
Step 2: a clip whose start follows the tempo
  two clips on bar 2, then 60 BPM instead of 120: the one that follows starts at 4.0 s, the other at 2.0 s
  following turned on for 400 clips on random maps: 0 moved
  a clip on bar 9 through 1000 tempo maps and back: 0 samples from where it was, 0 times off its bar;
    with its tick found again from its sample each time: 15 samples
  300 clips moved to a tick and to a sample: 0 wrong
  8 clicks on 8 bars, then a map with a ramp: 8 of the clips that follow are on their bars (bar 8 at 18.806 s);
    of those that do not, 8 are where they were (bar 8 was at 14.000 s)
  a take cut in two at bar 3, both halves follow: at 60 BPM a gap of 4.0 s, at 240 BPM 2.0 s heard twice
  120 to 60 BPM while beat 2 of bar 2 plays: the audio of a clip that follows jumps by 0.5 s, of one that does not by 2.5 s
[PASS] l20_step02 (23 checks passed)
```

(The 15 samples of the clip that forgets its tick depend on the last digits of `log1p` and `expm1`; another
compiler's library may give a slightly different number. The 0 of the clip that keeps its tick does not.)

## What you learned

- A clip that follows the tempo stores a tick and derives its sample, every time, from the tick.
- Turning following on moves nothing, and a snap keeps the grid's own tick.
- With only its start following, a clip keeps its length in samples: right for single sounds, wrong for cut
  takes and loops, which come apart or overlap.
- A tempo change while playing is a seek inside every audio clip, whether it follows or not.
