# Step 4 - The engine

**English** | [繁體中文](step-04-engine.zh-TW.md)

[Overview](../README.md) | Previous: [Step 3 - Instrument latency](step-03-latency.md) | Next: [Step 5 - The app: tempo](step-05-app-tempo.md)

## Goal

Bring Steps 1 to 3 into DawLab's engine: a tempo map in the transport, segments that carry their ticks, a MIDI
player that follows them, and instruments whose latency the delay compensation waits for - with chapter 18's tests
passing unchanged.

## Design

### Copy first, then change

As in chapters 15 to 18, the first commit of this step is chapter 18's engine and tests as they are; the second one
changes them. `scripts/check_copies.py` now covers chapter 19, so a file the step does not change stays the same as
chapter 18's.

### The map: a value, three buffers

`TempoMap.h` is Steps 1 and 2's map as the engine needs it: a fixed-size value - up to 256 points in a
`std::array`, their starts in seconds next to them - so that copying it allocates nothing, and neither does making one or setting it to one tempo. `setTempo(bpm)` is now a
map of one point, and everything that converted with "the" tempo converts through the map.

The UI thread gives the transport a new map at any time; the audio thread plays a block with one. A map is 8 KB: no
atomic can carry it. A seqlock could, but its reader reads memory the writer writes, which C++ calls a data race
(and ThreadSanitizer reports). So the transport keeps **three** maps and passes them around:

```text
   UI thread                         audio thread
   writes its BACK map               plays its FRONT map
   exchange(back <-> middle | new)   at a block's start: if middle is new, exchange(front <-> middle)
```

Each map belongs to one thread at a time; the hand-over is one atomic exchange. Nobody ever reads a map that is
being written, and nothing is copied on the audio thread. The UI keeps a fourth copy for its own conversions (bars
and beats, the grid). The four maps live on the heap: 32 KB more would not fit an Engine on a test's stack - which
the first run of the tests found, as chapter 18's did for its instruments.

The three maps and the exchange are a small class of their own, `TripleBuffer<T>` in `TripleBuffer.h`: `write()`
for the one writer, `take()` and `front()` for the one reader. It works for any value that must cross whole: the
loop range travels in one too, its start, end and unit (ticks or samples) together, so that no block reads a tick
as a sample.

### The transport plays Step 2

`Transport::advance()` gives every `TimelineSegment` its ticks, `tickFrom`, `tickTo` and `ticksJump`, as Step 2's
transport did. A new map is taken at the start of a block, with the playhead kept at its musical place. The loop
can be set two ways now:

- `setLoopRange(samples)` - as before: it stays where it is when the tempo changes, and its edges' ticks are found
  from samples. Every test of chapters 14 to 18 sets its loops this way, and they pass unchanged.
- `setLoopRangeTicks()` and `setLoopRangeBars()` - in ticks: the loop follows the tempo, and its edges are exact.

`samplesToBarsBeats()` now finds the tick that plays at a sample - the last one whose sample is not after it -
instead of dividing by a tempo and adding an epsilon: a beat lands on its tick, whatever rounding did.

### The MIDI player follows the ticks

Chapter 18's `ClipPlayer` found its ticks from the samples and called a segment that did not start where the last
one ended a jump. Now it takes the segment's ticks as they are and jumps when `ticksJump` says so. That is what lets
a held note survive a tempo change: the samples jump, the ticks do not, and the player has no reason to end the
note. A player that has just been reset still starts with a chase.

### Instrument latency in the plan

`Instrument::getLatencySamples()` (0 unless an instrument says otherwise) joins the track's latency,
`Track::getLatencySamples()` = inserts + instrument. Chapter 14's `Mixer::updateLatencies()` uses it for the
largest track latency and every track's compensation, and the automation lanes' shift uses it too. With
`Mixer::setLowLatencyMonitoring(true)`, an armed track skips the plan, as in Step 3.

## Checks

Chapter 18's 14 engine tests pass unchanged. `tests/test_tempo.cpp` is new:

```text
l19_test_tempo: a tempo map and instrument latency in the engine
  32 beats over a map that speeds up, jumps and slows down: every note on at its sample in blocks of 512, random and 7: yes
  a loop of ticks 960-1920 where two ticks share a sample, 7 passes in random blocks: 7 and 7 note ons, 0 misplaced;
    the note at 959 before it once: 1
  a tempo change half way through a held note: 1 note on, 1 note off, at 335872 (wanted 335872)
  a sampler 1000 samples late next to an instant synth: mix latency 1000, both heard at 25000
  the sampler armed, low-latency monitoring: mix latency 0, the synth at 24000, the sampler on its own at 25000
  the synth armed next to the sampler, low-latency monitoring: the synth at 24000, the sampler at 25000
  an insert's play head through a ramp from 60 to 120 BPM: 60.0 at the start, 120.0 at the end, never down
  29 new maps while a loop plays: 0 allocations on the audio thread
  N maps of up to 200 points from the UI thread while the audio thread plays 2001 blocks: 0 torn
[PASS] l19_test_tempo (18 checks passed)
```

## What you learned

- A large value crosses threads in a triple buffer: each copy belongs to one thread, the hand-over is one exchange.
- Old interfaces keep their meaning (a loop in samples, `setTempo`), new ones get their own names.
- A MIDI player that trusts the transport's ticks keeps notes through a tempo change.
- An instrument's latency is part of the track's, and the plan of chapter 14 needs no other change.
