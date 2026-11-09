# Step 2 - Playing a MIDI clip

**English** | [繁體中文](step-02-midi-clips.zh-TW.md)

[Overview](../README.md) | Previous: [Step 1 - MIDI events and musical time](step-01-midi-time.md) | Next: [Step 3 - One voice](step-03-one-voice.md)

## Goal

Play clips of notes under a transport that loops, jumps and stops, and while the user mutes and deletes
clips, so that every note on gets exactly one note off - and controllers have the right value wherever
playback starts.

## Design

### A note has a start and a length

Step 1 played a list of events. An editor does not show events, it shows notes - a bar with a start and a
length - and a clip holds them in its own time, from the clip's zero:

```text
Note            start, length (ticks from the clip's zero), pitch, velocity, channel
ControllerPoint tick, number, value, channel
MidiClip        start (timeline tick), length, offset, muted, notes, controllers
```

The clip is a *window* on its material, as chapter 14's audio clips are: it shows the material from `offset`
to `offset + length`, at timeline tick `start`. Dragging the clip's left edge to the right raises `offset`
and `start` together; the notes stay where they were in the song. `MidiClip::place()` decides where a note
plays:

- a note that starts inside the window plays at `start + note.start - offset`;
- a note longer than the window ends at the window's end;
- a note that starts before the window - before a trimmed start - is not played at all.

The last rule is a choice. The note's beginning is cut off, and a piano note without its attack is not the
same note. Most DAWs offer to play such notes anyway ("chase notes"); this chapter keeps it simple.

### Who ends a note?

A list of events has a note off for every note on. Playback does not. These cases end a note that the list
says is still sounding:

<div align="center">

| What happens | Where the note off goes |
|---|---|
| the note is longer than its clip | at the clip's end |
| the loop wraps, the user seeks | at the sample of the jump |
| the transport stops | in the stop's block |
| the clip is muted or deleted | at the start of the next block |

</div>

Miss one and the instrument plays the note forever - a *hanging note*, the oldest MIDI bug there is (every
synth has a panic button for it). The cure is bookkeeping: the player keeps a table of what it started -
clip, channel, pitch, and the tick where it will end - and ends those notes itself. A note off that the
player did not start is never sent, so a note that was hidden by a trim or a mute cannot end a note of
another clip.

The table has a fixed size, 128 notes: `process()` runs on the audio thread. When it is full, a new note is
not played and `droppedNotes()` counts it - the opposite choice (ending the oldest note) is a voice
allocator's, and that is Step 4.

A note whose clip is edited while it sounds keeps the end it had when it started. The table knows clips by
an id that stays the same through edits, so a moved or trimmed clip is still the same clip, and a deleted
one - or one replaced by another with a new id - is gone.

### Controllers are chased

A controller is not an event with a start and an end. It is a value that holds until the next move. Stop
in the middle of a filter sweep, seek back to the start, and the synth still has the filter where the sweep
left it: what it plays from the start is wrong until the clip's next move.

So after every jump the player *chases* the controllers: for each clip under the new position, the last
value of each controller before it - also from points before the window's start, which still describe what
the material sounds like there - sent before anything else. A point exactly at the new position is not
chased; the segment that starts there sends it, and sending it twice would be harmless but untidy.

Notes are not chased (see above), and that is why the two are different: a value can be set at any time,
a note's beginning cannot be made up later.

### One block, two pieces

The transport hands over a block as one or two *segments*: two when the loop wraps inside it.
`MiniTransport` marks a segment that does not continue the one before as a `jump` - after `play()`,
`seek()` and a wrap. `process()` handles each segment in turn:

```text
jump?                        all notes off, then chase, at the segment's offset
notes of muted/gone clips    off at the segment's offset
the segment's ticks          [firstTickAtOrAfter(start), firstTickAtOrAfter(end))   (Step 1)
  note ons                   notes that start there, into the table
  note offs                  table entries that end there - also notes just started
  controllers                points there, inside the window
in order                     by tick; at one tick offs, then controllers, then ons
```

The order at one tick matters twice. A note played again right where it ended needs its note off first, as
in Step 1. And a controller that changes at the same tick as a note starts must reach the instrument first,
or the note starts with the old value.

To put a segment's events in order without allocating, they are collected in a list reserved once, each
with its tick, a rank for its kind and a sequence number for the order it was found, and sorted with
`std::sort` on all three: equal keys cannot happen, so the result is the same as a stable sort. The list has
a fixed size too, and it always keeps room for a note off of every sounding note: when it is full a
controller is dropped (and counted), never a note off.

### How the check knows

Two kinds of checks:

- **Exactly the clips.** Three random clips - trimmed starts, notes longer than the window, notes of one tick,
  a pitch played again where it ends - written out by hand into events, and played straight through in
  blocks of 64, 512, 1000 and random sizes. The player must give the same events at the same samples.
- **Never a hanging note.** A `StrictInstrument` counts what sounds per channel and pitch, and complains about
  a note off for a note that is not on. Thirty random sessions of 2500 blocks stop, play, seek, loop, mute,
  delete and edit at random; at the end the transport stops, and nothing may sound.

Between them, by hand: a seek into the middle of a bar (both notes end, the mod wheel is chased to its value
there, the long note is not restarted), stop, mute, unmute, delete, a loop that wraps inside blocks, a trim,
a full table and a flooded list.

## Your turn

Write the four functions in `steps/step02-midi-clips/starter/step02.cpp`:

1. `MidiClip::place()`: inside the window or not, and the end cut at the window's end.
2. `ClipPlayer::allNotesOff()`: a note off for each entry of the table, then an empty table.
3. `ClipPlayer::chase()`: the last value of each controller before the position, from a fixed array.
4. `ClipPlayer::process()`: the table above, segment by segment.

When it works, the output ends like this:

```text
Step 2: playing a MIDI clip
  a loop of 36100 samples, 3 wraps: 6 note offs and 6 note ons at the wrap samples
  24 straight runs (2048 events) in blocks of 64, 512, 1000 and random sizes: 0 differ from the clips
  30 random sessions, 1532 note ons: 0 unmatched or misplaced events, 0 notes left sounding (expected 0, 0)
[PASS] l18_step02 (33 checks passed)
```

## What you learned

- A clip is a window on its material; a note that starts outside the window is not played.
- Playback ends notes that the sequence does not: at a clip's end, a jump, a stop, a mute, a delete.
- Keep a table of what you started, and end exactly that.
- Controllers are values: chase them after a jump. Notes are not chased.
- At one tick: note offs, controllers, note ons.
- On the audio thread, sort without allocating, and never drop a note off.
