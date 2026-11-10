# Step 6 - The app: instruments

**English** | [繁體中文](step-06-app-instruments.zh-TW.md)

[Overview](../README.md) | Previous: [Step 5 - The app: tempo](step-05-app-tempo.md)

## Goal

Make a track's instrument part of the project: a panel for the built-in synth's sound that undo and the project
file know, and a VST3 instrument in the synth's place - loaded, saved, undone and compensated like everything else.

## Design

### One snapshot, one edit

What a track plays its notes with is two things: the built-in synth's settings, and - if one is in the synth's
place - a plugin, by its file and its own state. `InstrumentSnapshot` is both as values, the way chapter 15's
`InsertSnapshot` is an effect as values. One edit, `SetInstrumentSound`, replaces a track's snapshot and returns the
old one; a track's own snapshot (what deleting it keeps) holds it too, so a deleted track comes back with its
sound.

`Session::setInstrumentSound()` carries it out:

- the synth's settings go to the engine at once (chapter 18's hook: atomics, taken by the next block);
- **another plugin file** builds that plugin and hands it to `Engine::setTrackInstrument()`; an empty file puts the
  synth back, and the old plugin comes back as garbage;
- **the same file** only takes the state: undoing a change inside a plugin does not build it again.

If the plugin cannot be built, nothing changes and the edit does not apply - History's rule since chapter 15.

The synth's settings are kept while a plugin is in its place. Take the plugin off, and the track sounds as it did.

### The synth panel

Seven sliders for the selected track (Track > Synth settings). Moving one changes the sound while the synth plays -
the engine never stops, and nothing is allocated on the audio thread. Three rules, each from an earlier chapter:

- **A drag is one undo step.** Every step of it carries the same merge key (chapter 16's automation points).
- **Showing is not changing.** When the session changes - an undo, another track selected - the sliders are set
  without a notification. With one, the panel would send the values back as a new edit: opening a panel would dirty
  the project, and an undo would be followed by its own redo.
- **The panel finds its track by id every time.** A track with a plugin, an audio track, a deleted one: the sliders
  go off and the panel says why.

### A plugin as an instrument

`PluginInstrument` is chapter 18's Exercise 12 in the app: a `juce::AudioProcessor` - what chapter 13's host gives
for a `.vst3` - behind the engine's `Instrument` interface. A block's events go into a `MidiBuffer` at their
offsets, the processor's output is added to the track. Step 3 arrives here: the wrapper reports the processor's
**latency**, the track's latency includes it, and the delay compensation lines every other track up with it. The
status line says so when a plugin is loaded.

Only what can be an instrument is taken: a plugin that accepts no MIDI or has no output is refused with a reason
(chapter 13's compressor is, in the smoke test).

What is not done: the plugin's own editor window (an insert's is shown, chapter 13), and the tail of a plugin after
the song's end in an export.

### The project file

```text
synth 17 300 0.9 0.005 0.2 0.6 0.3 1
plugininstrument 17 "C:/Plugins/Pianos.vst3" "<base64>"
```

Still format 7: this chapter's format. A `synth` line is written only for a sound that is not the default, so a
project that never opened the panel reads as before. Opening a project where the plugin is not installed is not an
error: the track plays the built-in synth with its own settings, and the user gets a warning that names the plugin.

### A factory that a test can replace

The session does not know chapter 13's host. It has a function, `createPluginInstrument`, that `MainComponent` sets
to "load the file, check that it is an instrument, wrap it" - as chapter 15 did for VST3 inserts. No VST3
instrument can be assumed on the machine that runs CI, so the smoke test sets its own: a small processor with a
latency of 256 samples whose state is its gain. Everything after the factory - the edit, undo, the track's
snapshot, the project file, the compensation - is the real code.

## Checks

```text
instruments: the synth's sound and plugin instruments
  the synth panel: a drag is one undo step, the engine follows, a deleted track comes back with its sound
  a plugin in the synth's place: 1 note on, mix latency 240 -> 496, the built-in synth silent
  its state saved, undone, redone, deleted with its track and opened again: gain 0.5 each time (6 plugins built);
    where it is not installed: "track "Synth 9": plugin instrument Test Synth.vst3 not loaded (no such plugin); it plays the built-in synth"
```

`tests/test_project_format.cpp` reads and writes the two new lines and refuses what is out of range, twice, or on a
track that does not exist.

## What you have to try yourself

CI has no VST3 instrument and no ears.

1. **The panel.** Add an instrument track, draw a few notes, play, and open Track > Synth settings. Turn the cutoff
   and the release while it plays. Undo.
2. **A VST3 instrument.** Track > Load VST3 instrument, with any instrument plugin you have. Play the same notes; arm
   the track and play a keyboard. Read the latency on the status line, and compare with the built-in synth.
3. **Save, close, open.** The plugin and what you set inside it should be back.

## What you learned

- A thing that can be two things is one snapshot and one edit; what is not in use is kept, not thrown away.
- Showing a value and changing it must be told apart, or every display becomes an edit.
- A dependency behind a function is a place a test can stand in.
- A missing plugin is a warning and a working track, not an error.
