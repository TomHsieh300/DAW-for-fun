# Step 8 - The app with instruments

**English** | [繁體中文](step-08-app.zh-TW.md)

[Overview](../README.md) | Previous: [Step 7 - The engine with instruments](step-07-engine.md)

## Goal

Bring the chapter into DawLab: instrument tracks in the arrangement, MIDI clips to draw and move, a piano roll for
their notes, a keyboard that plays the synth, MIDI recording with overdub and replace, quantize - all saved, all
undoable.

## Design

### Copy first, then change

As in chapters 15 to 17, the first commit of this step is chapter 17's app with only its name changed to DawLab 18,
so that the commits after it show what instruments change. They come in four parts, each built and tested before the
next:

1. the model: instrument tracks and MIDI clips in the session, two new edits, the project file;
2. the arrangement and the piano roll;
3. the keyboard and MIDI recording;

and between them one fix in the engine, found by the app (below).

### A clip keeps what was played

`MidiModel.h` is the app's MIDI clip: the engine's clip (a window on notes, Step 2), plus the notes **as played** and
how they are quantized. The engine gets the quantized notes - Step 6's `quantizeNotes()` - computed every time the
clip changes:

```cpp
engine::MidiClip toEngine() const { ...; c.notes = quantizeNotes(played, quantize); ... }
```

So quantize is a setting of the clip, not a change to its notes: the menu can switch it off again, the project file
saves what was played, and a second quantize does not compound the first. A clip's id is how the engine's player
knows it through edits (Step 2); the session hands ids out and never reuses one, and an edit of a clip keeps its id.

### Two edits, and a track that comes back whole

Everything about MIDI is undoable with two new edits next to chapter 15's: `SetMidiClips` replaces a track's whole
clip list (a note drawn, a clip moved, a recording, a quantize - each one an edit of the list), and `SetInstrument`
turns a track into an instrument or back. A track's snapshot - what deleting it keeps for undo - now holds its flag
and its clips, so a deleted instrument track comes back with its synth and its clips, under the same clip ids.

### The project file, format 6

```text
instrument 17
midiclip 17 1 3840 15360 0 0 "Bass" 240 1 0
note 1 0 480 36 100 0
cc 1 0 64 127 0
```

All of it is in ticks. A project opened at another sample rate converts its audio positions (chapter 14) and leaves
its notes alone: they follow the tempo, not the samples. `tests/test_project_format.cpp` reads and writes it, and
refuses every line that is out of range.

### The arrangement and the piano roll

An instrument track's lane shows its MIDI clips with their notes as bars, where they play after quantize. A double
click on nothing makes a clip of one bar and opens it; a drag moves a clip (one undo step, snapped to the beat); a
double click opens a clip in the **piano roll**; a right click has a menu: edit, quantize (off, 1/4, 1/8, 1/16, half
strength, swing), mute, delete. Track > Add instrument track (Ctrl+Alt+T) adds one.

The piano roll edits the notes as played and draws where quantize makes them play as outlines: click to add a note on
the grid and drag out its length, drag a note to move it, drag its end, Alt-drag for velocity, right click to delete.
Each gesture is one undo step: every step of a drag carries the same merge key, as chapter 16's automation points do.

The mouse handlers do nothing but translate an event into `press()`, `dragTo()` and `release()`. The smoke test calls
those three directly - draw, move in time and pitch, resize, velocity, delete, six undo steps back to an empty clip -
so the roll is tested without a mouse, and without a window.

### The keyboard

Arming an instrument track opens the MIDI inputs instead of the audio input. The driver calls back on its own thread
with JUCE's message, already parsed (Step 5's parser does the same job) and stamped on the clock that
`Time::getMillisecondCounterHiRes()` reads. `AudioHost` pushes it into **two** FIFOs:

- the audio thread drains one before every block, *scheduled* (Step 5): a fixed delay after it arrived - two blocks
  and the output latency - so every note is late by the same amount, not by however the block periods fell;
- the UI thread reads the other, for recording.

### Recording: where was the player?

Step 6 placed a note by an anchor: when a block is heard, which song sample it rendered, the mix latency. The audio
thread publishes one every block, through a **seqlock** (a version that is odd while it writes; the reader retries
until it reads an even version that did not change), so the UI thread always gets one consistent block.

A loop adds a problem that Step 6 did not have: the UI reads at 30 Hz, so it may never see the recording's first
block, and a later block's song position has wrapped. So the anchor also says how many samples have been rendered
while playing - a count that only grows, through wraps too - and the recording keeps that count at its start:

```text
unwrapped = startSample + (anchor.renderedBefore - renderedAtStart) + (time - anchor.blockTime) x rate - mix
```

From any block of the recording, that is where the key went down, counted on without wrapping; Step 6's recorder
wraps it into passes. On stop, each armed instrument track's take becomes one new clip over what was recorded:

- **Overdub** keeps every pass and the old clips;
- **Replace** (Transport > MIDI Loop Replaces) erases what a later pass played over - in the take's own earlier passes
  and in the old clips.

All tracks' clips change in one undo step, "Record MIDI". An armed instrument track records MIDI, not audio: the
engine now starts its audio Recorders only on armed audio tracks, and both kinds can record at once.

### Found by the app

The first run of the smoke test found a bug in Step 7's engine: removing a track cleared its instrument flag on the
audio thread. But the UI reuses a slot at once, and by the time the audio thread took the removal, the UI had given
the slot to a new instrument track - which the removal then turned back into an audio track. Undo and redo of adding
an instrument track gave a silent track; so did opening a project. The flag is the UI's value, like a fader:
`Engine::addTrack()` resets it now, and the audio thread never touches it. Step 7's document tells the story, and its
test covers it.

## Checks

The smoke test (`DawLab 18.exe --smoke-test`) has three new parts:

- **The model:** an instrument track and a clip as undo steps; the synth plays the clip; quantize changes what the
  engine plays and not what the clip keeps; an instrument switched off and back; a deleted track brought back whole;
  saved and opened.
- **Editing:** a clip added and dragged (one undo step), found by the arrangement where it plays; the piano roll's
  gestures as above; quantize and mute from the menu; the roll of a deleted clip.
- **Recording the keyboard:** a player at a keyboard against the offline device with a test clock. Four notes, one off
  the beat, are recorded on their exact ticks; the synth is heard while recording; only the instrument track is
  armed, so no audio take is made. A loop of 2.5 passes over an old clip, in both modes, and undo.

```text
instruments and MIDI clips: recording the keyboard
  4 notes played against a device in 512-sample blocks: recorded at ticks 0, 960, 1920, 3001
```

## What you have to try yourself

CI has no keyboard and no ears. These are for you:

1. **Play.** Add an instrument track (Ctrl+Alt+T), arm it (R in its header) and play a MIDI keyboard: the status line
   names the keyboards it found. The delay should feel constant; at a large buffer size it is longer, but still
   constant.
2. **Draw.** Double click in the track's lane, draw notes in the piano roll, play.
3. **Record.** Press R, play two bars, press R again. Then a loop with MIDI Loop Replaces on and off.
4. **Quantize.** Right click a clip, choose 1/16, hear it, choose Off.
5. **Save and open,** and undo everything you did.

## What you learned

- A clip that keeps what was played can be quantized, unquantized and saved without loss.
- Two edits of whole lists make every MIDI change undoable, and a snapshot brings a track back whole.
- Testing a component through the functions its mouse handlers call needs no mouse.
- A seqlock hands a few values from the audio thread to the UI without a lock and without tearing.
- A count that only grows unwraps a loop for a reader that sees only some of the blocks.
- The app is the engine's best test: it found the slot-reuse bug that the engine's own tests did not.
