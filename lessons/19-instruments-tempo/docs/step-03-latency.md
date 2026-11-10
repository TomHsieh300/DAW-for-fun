# Step 3 - Instrument latency

**English** | [繁體中文](step-03-latency.zh-TW.md)

[Overview](../README.md) | Previous: [Step 2 - A transport on a tempo map](step-02-transport.md) | Next: Step 4 - The engine (coming)

## Goal

Make a song's notes come out together when its instruments answer late - a sampler that reads ahead, a synth that
works on blocks of its own - and know what that costs a musician who plays live, and what it does not change about
a recording.

## Design

### An instrument's latency is a track's latency

Chapter 18's synth answers a note in the sample it comes in. Many instruments do not, and report a latency: their
sound for a note comes that many samples later. That is the same kind of delay as an effect's, and chapter 14 has
the answer for it - plugin delay compensation. A track's latency is now its instrument's and its inserts':

```math
L_t = L_{instrument} + L_{inserts}, \qquad total = \max_t L_t, \qquad compensation_t = total - L_t
```

Every track waits $`compensation_t`$ more, so a note at sample $`S`$ on any track is heard at $`S + total`$: all of
them together, the whole mix late by the same amount. For a song that is all that is needed - chapter 14's playhead
already shows where the sound is, not where the engine is rendering.

| Track | Instrument | Inserts | $`L_t`$ | Waits |
|---|---|---|---|---|
| Synth | 0 | 0 | 0 | 4096 |
| Sampler | 256 | 0 | 256 | 3840 |
| Synth + limiter + filter | 64 | 32 | 96 | 4000 |
| Streaming sampler, armed | 4096 | 0 | 4096 | 0 |

### What the plan cannot make right: a key played now

A note from a keyboard has not happened yet when the block before it is rendered. It goes through everything:

```math
heard = delay_{scheduled} + L_t + compensation_t + L_{output}
```

On the streaming sampler that is $`1024 + 4096 + 0 + 512 = 5632`$ samples, 117 ms - its own latency, nothing to
do about it but choose another instrument. On the synth it is the same 5632 samples, although the synth answers at
once: the synth *waits* for the sampler on another track, because the plan delays every track to the slowest. The
musician on the synth feels a latency that is not theirs.

### Low-latency monitoring

So a DAW lets an armed track skip the plan while the musician plays: its compensation is 0, and it does not count
for `total`. Arm the synth with the sampler left alone, and the synth's key is heard after
$`1024 + 0 + 0 + 512 = 1536`$ samples, 32 ms. The armed track is now early against the others by what its
compensation would have been - and nobody hears that while playing along. When the take plays back, the plan is
whole again. Arm the sampler instead, and low-latency monitoring helps the rest: `total` drops from 4096 to 256,
and the song no longer waits 85 ms for one instrument.

### Recording does not move

A recorded note belongs where the musician heard the *song* when they pressed the key (chapter 18, Step 6). The song
came out $`total + L_{output}`$ after it was rendered, so a key pressed while the engine renders $`R`$ belongs at

```math
R - total - L_{output}
```

The armed instrument's own latency is not in it. It makes the note *feel* late to the musician, and a musician
adapts to a late sound by playing early - but the note is placed where they heard the beat, wherever that was. The
self-check places a note played exactly on the beat with the armed sampler's latency at 0, 256, 4096 and 20000
samples: always on the beat.

## Your turn

Write `planLatency()`, `heardLive()`, `placeRecorded()`, `Mix::setTracks()` and `Mix::render()` in
`steps/step03-latency/starter/step03.cpp`. `LatentInstrument` (an impulse `latency` samples after every note on)
and `DelayLine` are given. Build and run:

```powershell
.\scripts\build.ps1 -Only 19-instruments-tempo
.\build\bin\Debug\l19_step03_starter.exe
```

```text
Step 3: instrument latency
  latencies 0, 256, 64+32 and 4096 (armed): total 4096, waits 4096 3840 4000 0; with low-latency monitoring: total 256, waits 256 0 160 0
  a key to the ear on the sampler: 5632 samples (117 ms) either way; on the synth 5632 (117 ms), with low-latency monitoring 1536 (32 ms)
  a note played on the beat it heard: at 96000 whatever the armed sampler's latency (low-latency total 256)
  a chord on the four tracks at 10000: one impulse of 4 at 14096; with low-latency monitoring 3 at 10256 and the armed one at 14096
  100 random sessions in random blocks: 0 where the tracks did not arrive together at the note + total
[PASS] l19_step03 (18 checks passed)
```

## What you learned

- An instrument's latency is part of its track's latency, and chapter 14's compensation lines the tracks up.
- Compensation is free for a song and costly for a key played now: every track waits for the slowest.
- Low-latency monitoring takes an armed track out of the plan while the musician plays.
- A recorded note is placed by when the song was heard; the instrument's own latency does not move it.
