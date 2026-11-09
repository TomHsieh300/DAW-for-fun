# Step 6 - Recording MIDI

**English** | [繁體中文](step-06-recording.zh-TW.md)

[Overview](../README.md) | Previous: [Step 5 - A keyboard](step-05-keyboard.md) | Next: Step 7 - The engine with instruments (coming)

## Goal

Record what is played on a keyboard into a clip: each note on the tick where it was played, pass after pass
of a loop in one of two modes, and quantized - to a grid, by a strength, with swing - without ever losing
what was played.

## Design

### Where was the player?

The player presses a key in time with what they *hear*. Chapter 17 placed an audio take at

```text
P - (L_in + L_out + L_mix)
```

because the sound came back late through the input as well. A MIDI message has no input latency to
speak of: Step 5's driver stamps it with the time it *arrived* - a millisecond after the key, on a USB
keyboard. So only what stands between the song and the player's ears counts, and that is in two numbers the
audio thread has for every block:

- `blockTime`: when the block's first sample will be heard (Step 5) - the device's output latency is in it;
- `mixLatency`: plugin delay compensation (chapter 14) - what is rendered at song sample `S` comes out of the
  mix that much later.

So the song sample the player heard at time `t` is

```text
songSample + (t - blockTime) x sampleRate - mixLatency
```

from *any* block of the same stretch of playing: the song moves at the sample rate, and the clock the time
stamps and `blockTime` share is the same. The nearest tick to that sample (`tickAt`) is where the note goes.
The check plays 200 notes exactly in time with what is heard, through 12.3 ms of output latency and 752
samples of mix latency (chapter 14's demo song), measures them from arbitrary blocks, and requires every one
on its tick.

The thru delay of Step 5 plays no part here. A scheduled thru makes the player hear their *own* note 26 ms
late, but the note is placed by when the key went down, not by when its sound came out.

### The recorder

`NoteRecorder` gets key downs and key ups with their tick and pairs them into notes. A keyboard can surprise
it:

- a note on with velocity 0 is a key up (Step 1);
- a second note on for a key that is still down ends the first note there;
- a key up for a key that is not down (it went down before recording started) is ignored;
- keys still down when recording stops are closed there.

Pitch *and* channel name a key: a split keyboard sends the same note number on two channels.

### Loops: passes, overdub and replace

Loop recording plays the same bars again and again while the player records. The recorder is given
*unwrapped* ticks - song time as it was played, going on past the loop's end - and wraps them itself: pass 0
runs from where recording started to the loop's end, pass 1 is the first whole time round, and so on. A note
held over the loop's end belongs to the pass it started in, and ends at the loop's end. When recording stops,
the recorder knows what each pass *covered*: the last pass may have covered only part of the loop.

What the clip keeps is the record mode's decision:

- **Overdub** keeps everything: the clip's notes and every pass. Build a drum pattern pass by pass - kick,
  then snare, then hi-hat.
- **Replace** lets each pass erase what it plays over. A note survives unless a *later* pass covered its
  start: the clip's old notes (as pass -1) unless any pass did, a note of pass `p` unless a pass after `p`
  did. Keep playing a phrase until it is right, then stop; what is left in each place is the last attempt
  there. A last pass stopped half-way erases only the half it played.

Without a loop the same rule makes a plain take replace the notes between where it started and where it
stopped, and nothing else.

### Quantize, and keep what was played

Quantize moves notes towards a grid:

```text
n      = start / grid, rounded to the nearest            the nearest grid line
target = n x grid, + swing x grid if n is odd            swing delays every second line
start  = start + strength x (target - start)              strength 0..1: how far towards it
```

A grid of 240 ticks is a sixteenth. Strength 1 puts everything on the grid; 0.5 halves every distance and
keeps some of the feel. Swing 1/3 delays every second sixteenth by a third of a sixteenth: the triplet
shuffle of a lot of grooves.

The essential property is not in the formula: `quantize()` is a *function of the played notes*. The clip
keeps what was played, and the quantized notes are computed from it - so strength, grid and swing can be
changed any time, any number of times, and "unquantize" is strength 0. A destructive quantize, that overwrote
the starts, could never go back to the original timing, and two quantizes in a row would compound.
Chapter 16's automation thinning made the same choice for the same reason.

## Your turn

Write the seven functions in `steps/step06-recording/starter/step06.cpp`:

1. `songSampleAt()`: the anchor, the time, the mix latency.
2. `tickAt()`: the nearest tick.
3. `NoteRecorder::noteOn()`: velocity 0, a key down twice, a new open note.
4. `NoteRecorder::noteOff()`: the open note, its pass, its end within the pass.
5. `NoteRecorder::stop()`: close the keys, then what each pass covered.
6. `mergeTake()`: overdub, or replace by later coverage.
7. `quantize()`: the nearest line, swing, strength, from the played notes.

When it works, the output ends like this:

```text
Step 6: recording MIDI
  200 notes played in time with what was heard (latency 12.3 ms, mix 752 samples): 0 off their tick
[PASS] l18_step06 (39 checks passed)
```

## What you learned

- A MIDI note is placed by when the key went down: output and mix latency count, input latency does not.
- Any block's anchor maps a time to a song sample.
- A recorder pairs keys into notes and survives keyboards that send odd things.
- Loop recording records passes; overdub keeps them all, replace keeps the last that covered each place.
- Quantize is computed from the played notes, so it can always be changed or undone.
