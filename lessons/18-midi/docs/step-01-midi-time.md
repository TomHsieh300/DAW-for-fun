# Step 1 - MIDI events and musical time

**English** | [繁體中文](step-01-midi-time.zh-TW.md)

[Overview](../README.md) | Next: [Step 2 - Playing a MIDI clip](step-02-midi-clips.md)

## Goal

A note is not sound. It is an instruction - "start middle C, this loud" - that arrives at a moment in the
song, and the song counts in beats while the audio thread counts in samples. Represent the instructions,
put them on a musical timeline, and hand each one to an instrument at the exact sample it belongs to,
whatever size the audio blocks have.

## Design

### Three bytes

MIDI was designed in 1983 for a 31250 baud cable, and its messages are still three bytes or fewer. The
first byte is the *status*: its high bit is always 1, the high nibble says what the message is, the low
nibble names one of 16 channels. The data bytes that follow have their high bit at 0, so they carry 7 bits
each, 0 to 127.

<div align="center">

| Status | Data 1 | Data 2 | Message |
|---|---|---|---|
| `0x8n` | note | velocity | note off |
| `0x9n` | note | velocity | note on - **with velocity 0 it is a note off** |
| `0xBn` | controller | value | control change: 64 sustain pedal, 1 mod wheel, 7 volume, ... |
| `0xEn` | low 7 bits | high 7 bits | pitch bend, 14 bits, 8192 = no bend |

</div>

Two details catch every first MIDI parser. A note on with velocity 0 is a note off: most keyboards send
their releases that way, because it lets them keep the same status byte for a whole performance (*running
status*, Step 5). And pitch bend puts its *low* bits first: `(data2 << 7 | data1) - 8192` gives -8192 to
8191, with a range of the instrument's choosing - usually two semitones.

`MidiMessage` keeps the three bytes as they are and decodes them on demand with `kind()`. The program
change, aftertouch and system messages are `Other` here: the instrument of this chapter does not use them,
and it must not take them for notes.

### Ticks, not samples

Where does a note live? If it lived in samples, like an audio clip, a change of tempo would leave every note
where it was, and the music would fall apart. A note lives in *ticks*, a fraction of a quarter note: 960
to the quarter, as chapter 14's `Transport` already counts. Only when it is played does a tick become a
sample:

```text
samplesPerTick = sampleRate x 60 / (bpm x 960)        120 BPM at 48 kHz: exactly 25
sample(tick)   = round(tick x samplesPerTick)
```

That is the difference this chapter will keep coming back to. The self-check ends with it: bar 3 starts at
sample 192000 at 120 BPM, and the same note at 90 BPM is at 256000. An audio clip at sample 192000 stays
there.

The formula is computed from the tick each time. Adding `samplesPerTick` up one tick at a time looks the
same and is cheaper, but every addition rounds, and the errors add up: after an hour of a song, a note would
be heard a little late. `sampleOfTick(tick)` has no memory, and the check takes it an hour out at 133 BPM
(22.556 samples a tick) against an exact long-double calculation.

### Which events are in this block?

The audio thread asks, block after block: which events belong to samples `[s0, s1)`? The answer must hold
every event exactly once, however the blocks fall. The way to be sure is to turn the block's edges into
ticks with the exact inverse of `sampleOfTick`:

```text
firstTickAtOrAfter(s) = the smallest tick t with sampleOfTick(t) >= s
block [s0, s1) holds the ticks [firstTickAtOrAfter(s0), firstTickAtOrAfter(s1))
```

Consecutive blocks share an edge, so the tick ranges are consecutive too, and no tick is in two of them or
in none. Dividing `s / samplesPerTick` is not enough: rounding sends some ticks just before that point to `s`
as well (at 999 BPM and 22.05 kHz, 1.38 samples a tick, several ticks land on one sample). So
`firstTickAtOrAfter` starts at `ceil(s / samplesPerTick)` - a tick that surely reaches `s` - and steps back
while the tick before it still does.

With the tick range known, `std::lower_bound` finds the first event in the sorted sequence, and the loop
stops at the first one past the range. The audio thread never walks from the start of the song, and with
`reserve()`d space it never allocates.

### The order at one tick

Two events at the same tick still happen in an order. A melody that plays C, then C again, has the first C's
note off and the second C's note on at the same tick. If the note on came first, the note off would end the
*new* note at once: a silent note in the middle of a phrase. So `sortEvents` puts note offs first at each
tick, and leaves everything else in the order it was given - a *stable* sort: three moves of the mod wheel
in one tick must arrive in the order they were made, and `std::sort` does not promise that.

### Sample-accurate

An instrument renders audio and receives events. The lazy way hands it all of a block's events, then renders
the whole block: every note starts at the beginning of the block it falls in. At 512 samples and 48 kHz that
is up to 10.7 ms late, and worse, the error depends on the buffer size the user picked - change it and the
groove changes. So `renderBlock` cuts the block at every event:

```text
render [0, offset1)  handle(event1)  render [offset1, offset2)  handle(event2) ... render [last, n)
```

Several events at one offset: render nothing between them. Chapter 16 did the same for automation, and the
test is the same, too: *block invariance*. The check renders 300 notes with a `CountingInstrument` - its
output is the number of notes held, a staircase that shows exactly when each event took effect - in blocks
of 1, 7, 64, 512 and random sizes, at four tempos and sample rates, and compares every run with a reference
that handles the events sample by sample.

## Your turn

Write the seven functions in `steps/step01-midi-time/starter/step01.cpp`:

1. `MidiMessage::kind()`: the high nibble, and velocity 0.
2. `MidiMessage::pitchBendValue()`: data2 is the high half.
3. `sampleOfTick()`: rounded, from the tick.
4. `firstTickAtOrAfter()`: from the ceiling, step down.
5. `sortEvents()`: stable, note offs first at a tick.
6. `eventsInBlock()`: the tick range, `lower_bound`, the offsets.
7. `renderBlock()`: render up to each event, then hand it over.

When it works, the output ends like this:

```text
Step 1: MIDI events and musical time
  an hour at 133 BPM: tick 7660800 is sample 172800000
  20 runs of 600 events in blocks of 1, 7, 64, 512 and random sizes: 0 differ from the reference
  bar 3 starts at sample 192000 at 120 BPM, at 256000 at 90 BPM
[PASS] l18_step01 (53 checks passed)
```

## What you learned

- A MIDI message is a status byte and up to two 7-bit data bytes; a note on with velocity 0 is a note off.
- Notes live in ticks and follow the tempo; audio lives in samples and does not.
- Compute a sample from its tick every time: a running sum drifts.
- With an exact inverse, consecutive blocks see every event exactly once.
- At one tick a note off goes first, and the rest keep their order.
- Cut the block at every event, or the timing depends on the buffer size.
