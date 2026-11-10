# Step 2 - A transport on a tempo map

**English** | [繁體中文](step-02-transport.zh-TW.md)

[Overview](../README.md) | Previous: [Step 1 - A tempo map](step-01-tempo-map.md) | Next: [Step 3 - Instrument latency](step-03-latency.md)

## Goal

Play a song whose tempo changes - inside a block, across a loop's wrap, and while it plays because the user moved a
tempo point - with every tick played exactly once, at its sample, in blocks of any size.

## Design

### Two clocks: samples and ticks

The engine's timeline stays in samples. Audio clips, automation, recording and the delay compensation all count
samples, and a tempo change must not move a recorded take: it was played at those samples. Notes are in ticks.
Chapter 14's transport describes a block as **segments**, ranges of timeline samples; now each segment also says
which **ticks** it plays, $`[tickFrom, tickTo)`$, and whether they follow on from the last segment's (`ticksJump`).

Normally a segment's ticks are Step 1's $`[\mathrm{first}(s_0), \mathrm{first}(s_1))`$, and since the next segment
starts at $`s_1`$, its ticks start where these end. The transport makes that a rule rather than a coincidence:
in a stretch that goes on, `tickFrom` is the last segment's `tickTo`. Two places need it.

### A loop in ticks

The user sets a loop in bars. Kept in samples, it would stay where it was when the tempo changes, and no longer
start on a bar. So the loop is kept in **ticks**, and its samples come from the map for every block:
$`[\mathrm{sampleOfTick}(start), \mathrm{sampleOfTick}(end))`$.

At its edges rounding to samples bites. At 999 BPM and 8 kHz two ticks share a sample, and
$`\mathrm{first}(\mathrm{sampleOfTick}(end))`$ may be a tick before `end`: from samples alone, a pass would lose a
tick at its end and play one twice at its start. So the segment that ends at the loop's end ends at tick `end`, and
the one after the wrap starts at tick `start`, given, not found again from a sample. Every pass plays exactly the
loop's ticks.

### A new map while it plays

The user drags a tempo point. What should the playhead do? It is at some bar and beat, and that is where the user
expects it to stay: the **musical place** is kept. Its exact, fractional tick under the old map is found with
Step 1's `tickAt()`, and the playhead moves to that tick's sample under the new map.

For the audio that is a jump - the samples go somewhere else, as after a seek. For the notes it is not: the ticks
go on from where the last segment ended, `ticksJump` stays false, and chapter 18's MIDI player keeps the notes that
sound. A note held across the change is held on, at the new tempo.

```text
    old map:   ...|  bar 3   |...         the playhead at bar 3, beat 2 (tick 7926)
    new map:   ...|      bar 3      |...  the same tick, at its new sample; the ticks go on
```

What moves when the tempo changes is what is in ticks: notes, the loop, the bars and beats, the metronome. What
does not is what is in samples: audio clips and automation. A drum loop recorded at 120 BPM does not speed up with
the song - that would need time-stretching, which is a subject of its own.

### A metronome

`clicksIn()` is the first thing that plays the map: a click on every beat in a segment's ticks, the first of every
bar accented, at its beat's sample. It accelerates through a ramp because the beats do, and it is exactly as right
as the ticks are - after a wrap it starts on the loop's first beat, not one tick early.

## Your turn

Write `Transport::setMap()`, `Transport::advance()` and `clicksIn()` in `steps/step02-transport/starter/step02.cpp`.
Build and run:

```powershell
.\scripts\build.ps1 -Only 19-instruments-tempo
.\build\bin\Debug\l19_step02_starter.exe
```

The self-check plays a song that speeds up, jumps and slows down - once in a single block and once in blocks of 1 to
1500 samples - loops over its tempo changes, loops where two ticks share a sample, and moves a tempo point while it
plays:

```text
Step 2: a transport on a tempo map
  8 bars that speed up, jump and slow down: 30720 ticks once each at their samples, 32 clicks; the same in blocks of 1-1500
    beats 1-4 at samples 0, 30736, 59194, 85687; beats 29-32 at 876026, 917169, 958312, 999455
  bars 2-4 looped 5 times: each pass the loop's 7680 ticks, the ticks jump at each wrap; the same in blocks of 1-1500
  two ticks a sample, ticks 960-1920 looped 7 times, both edges on a shared sample: every pass exactly its 961 ticks
  a tempo point put in while it plays: the playhead stays at tick 7926, no tick lost or played twice, no jump;
    a bar after it is 192000 samples later (it was 96000)
[PASS] l19_step02 (18 checks passed)
```

## What you learned

- The timeline stays in samples; a segment carries its ticks too, and the transport keeps them following on.
- A loop set in bars is kept in ticks, and its edges are given in ticks: rounding to samples cannot move them.
- A tempo change keeps the playhead's musical place; for the audio it is a jump, for the notes it is not.
- What is in ticks follows the tempo; what is in samples does not.
