# Chapter 18 - Exercises
**English** | [繁體中文](README.zh-TW.md)

[Back to the chapter](../README.md) | [Solutions](../solutions/README.md)

Difficulty: ★☆☆ easy, ★★☆ medium, ★★★ hard. Implementation exercises have a starter file in
`exercises/starter/` that compiles and runs; until you complete it, its self-check prints `FAIL`
(that is expected). Build target names are given in brackets. None of them needs JUCE:

```powershell
.\scripts\build.ps1 -Only 18-midi
.\build\bin\Debug\l18_ex3_starter.exe
```

**Exercises 1-7** are the chapter's exercise set, about the steps (no JUCE). **Exercises 8-12 are
optional projects** that extend DawLab 18. They need a JUCE build
(`.\scripts\build.ps1 -Only 18-midi -Juce`); like chapter 17's they are console programs that build the real
`MainComponent` in its offline mode (`exercises/app_rig.h`), and none of them changes the app. Exercises 8 and 12
use the hook the engine got after the chapter: another instrument in the synth's place, and the synth's settings
changed while it plays (Step 7, "Added after the chapter").

Exercises 2, 3 and 5 build on Step 1's model (`exercises/model.h`: Step 1's `minidaw.h` with its functions
solved), Exercise 6 on Step 4's solved voice parts (`Saw`, `Adsr`). Exercise 4 stands alone. Do the steps
first.

---

## Exercise 1 - Why so careful with notes? ★☆☆ (concept)

Answer in a few sentences each.

1. An audio clip lives in samples, a MIDI clip in ticks (Step 1). What would go wrong with notes stored in
   samples when the user changes the tempo? And why does Step 1 compute `sampleOfTick()` from the tick
   every time instead of adding `samplesPerTick` up?
2. At one tick, Step 1 plays note offs before note ons, and Step 2 plays controllers between them. Give a
   case where the other order is audibly wrong, for each of the two rules.
3. After a seek, Step 2 chases the controllers (the pedal, the bend) but not the notes. What would a DAW
   have to do to chase a note correctly - and why might the user not even want it?
4. Step 4 steals a voice with a 2 ms fade, so the new note starts 2 ms late. Why not cut the old note at
   once? Why not drop the new note when all voices are busy?
5. Step 5 can play a keyboard's events as soon as possible or scheduled, a fixed delay after they arrived.
   Scheduled is always later. Why is it still the better default, and when would you choose as soon as
   possible?
6. Recording audio (chapter 17) subtracts the input latency; recording MIDI (Step 6) does not. Why not?
   Which latencies does it subtract, and why those?
7. Step 6's quantize never changes the notes as played; it computes where they play. What would be lost if
   it moved the notes themselves?

## Exercise 2 - MIDI by the numbers ★★☆ (calculation)

Compute by hand (show the formulas), then run [`l18_ex2_solution`] to check.

- (a) A song at 97 BPM, 44.1 kHz, 960 ticks a quarter: how many samples is a tick? At which sample does
  bar 33 (4/4) start? A program adds `samplesPerTick` to a `float` once a tick for an hour: guess how far off
  it ends up, then let the solution tell you.
- (b) A MIDI cable carries 31 250 bits a second, 10 bits a byte. How long does a note on take? A chord of 10
  notes, with running status and without? How many samples at 48 kHz lie between the first note and the
  last?
- (c) Blocks of 512 samples at 48 kHz, an output latency of 512. How much jitter does "as soon as possible"
  add, and how long is Step 5's scheduled delay of two blocks plus the output latency? At 128 samples?
- (d) How long is a tick in samples and milliseconds at 120 BPM and at 200 BPM (48 kHz)? How many samples
  is a 1/16 note at 120 BPM?
- (e) A naive sawtooth on C7 (MIDI note 96) at 48 kHz. Which harmonic is the first above Nyquist, and where
  does it fold to? Which is the first alias below 10 kHz, and how loud is it compared with the fundamental?
- (f) Step 6's quantize on a 1/8 grid (480 ticks) with swing 0.2: how late is every second line, in ticks,
  samples and milliseconds at 120 BPM? Where does strength 0.5 move a note at tick 990? Where does full
  strength with that swing move a note at tick 1430?

## Exercise 3 - Standard MIDI Files ★★★ (implementation)

[`l18_ex3_starter`, file `exercises/starter/ex3_smf.cpp`]

Every DAW reads and writes `.mid` files. Write both directions for format 0 and 1 (the header explains the
format in full):

- `writeVlq()` and `readVlq()`: the variable-length numbers that every delta time and length uses.
- `writeSmf()`: a song - a tempo, a time signature, named tracks of events - into a file of format 1 at 960
  ticks a quarter, with a conductor track and running status. The self-check compares the exact bytes.
- `readSmf()`: a file of format 0 or 1 at any division back into a song in ticks of 960. Running status,
  meta events, SysEx, unknown chunks and a header longer than 6 bytes must all work.

A file is untrusted input. The self-check feeds the reader 23 broken files, every truncation of a good one
and 20 000 randomly damaged ones: it must refuse what it cannot read, with a reason, and never read past the
end. Build it with AddressSanitizer too (`-DDAWLAB_SANITIZE=address,undefined` on Linux), or in Debug with
MSVC, whose checked iterators catch the same mistakes.

## Exercise 4 - A tempo map ★★☆ (implementation)

[`l18_ex4_starter`, file `exercises/starter/ex4_tempo_map.cpp`]

DawLab 18 keeps one tempo. Give it a tempo map - a list of changes, each a tick and a tempo - and write the
two conversions the audio thread needs: `sampleOfTick()` and its exact inverse `firstTickAtOrAfter()`, as in
Step 1, but across changes. `prepare()` computes where each change starts in samples.

The self-check compares with a reference for 300 random maps, requires that every tick of a song with a
ritardando into every beat falls into exactly one block of random size, and finds out whether you rounded
where a change starts: after 2000 changes, that is 480 samples (11 ms) off.

## Exercise 5 - An arpeggiator ★★★ (implementation)

[`l18_ex5_starter`, file `exercises/starter/ex5_arpeggiator.cpp`]

An arpeggiator is a MIDI effect between the keyboard and the instrument: it plays the held chord one note
at a time, on a grid in musical time - Up, Down, UpDown or in the order the keys were played, over up to
four octaves, each note a `gate` of its step long. Write `process()`, which turns a block of keyboard events
into the block's notes, and `reset()` for a stop or a jump (the header has every rule).

It is Steps 1 and 2 again in a new shape. Every step lands on its exact sample at any tempo; a key pressed
at a step's own sample is in that step; a note ends at its gate even when its key is already up, and before
the next note when the gate is 1; controllers pass through. And it runs on the audio thread: the output is
kept in order as it is written, without a sort that allocates. The self-check plays 400 random songs once
in a single block and once in blocks of 1 to 700 samples: the output must not change, and no note may hang.

## Exercise 6 - Mono, legato and glide ★★☆ (implementation)

[`l18_ex6_starter`, file `exercises/starter/ex6_mono_synth.cpp`]

A bass line or a lead is often played on one voice. Write `MonoSynth`, built from Step 3's saw and envelope:

- **Last-note priority:** the newest key sounds; when it goes up, the newest key still down sounds again.
- **Legato:** a note that overlaps another does not restart the envelope (with legato off it does, from the
  level it is at).
- **Glide:** the pitch slides to the new note in a straight line in semitones, in the same time whatever
  the interval - but only between overlapping notes.

The self-check follows the pitch sample by sample: half way through a glide of 4 semitones it must be
exactly 2 semitones up, and it must reach the note in exactly 2400 samples, not one sooner.

## Exercise 7 - A VST3 instrument on a track ★★☆ (design)

DawLab 18's instrument is its own synth (Step 7). Chapter 13 hosts VST3 *effects*; Exercise 12 will put a
VST3 *instrument* where the synth is. Before that, design it in writing. Cover:

1. **Events.** Step 7 hands the synth a `MidiBlock`, sliced per chunk. What does a plugin get instead, and
   how do the clip's notes, the chased controllers and the live keyboard get into it? What becomes of a
   controller or a pitch bend in VST3, which has no controller events?
2. **No hanging notes.** Step 2 keeps a table of the notes it started. Is it still needed when the plugin
   has its own voices? What does the host send on a stop, a seek or a loop wrap, given that VST3 has no
   "all notes off"?
3. **Latency.** A sampler that reads ahead, or a synth with look-ahead, reports a latency. Where does it go
   in chapter 14's delay compensation, and what does it do to the keyboard's delay (Step 5) and to where a
   recorded note is placed (Step 6)?
4. **The tail.** The transport stops and the plugin is still releasing a note. Does the engine keep calling
   it? What about a muted track, a bypassed one, and the end of an offline render?
5. **State.** What does the project file keep, how is it restored before the first note, and which thread
   may call `setStateInformation()`?
6. **Trust.** Step 7's tests prove the synth never allocates on the audio thread. What can the host prove
   about a plugin, and what does it do instead?

## Exercise 8 - A synth settings panel ★★☆ (implementation, JUCE)

[`l18_ex8_starter`, file `exercises/starter/ex8_synth_panel.cpp`]

The synth had one sound. After the chapter the engine got a hook for it (Step 7, "Added after the chapter"):
`Engine::getTrackSynth(track)->setSettings()` may be called on the UI thread while the synth plays, and the next
block hands the values to the voices. Build the panel in front of it:

- `SynthPanel::showTrack()` shows a track's cutoff, resonance, envelope and level on seven sliders - and sends
  nothing while it does. A track that is not an instrument, or is gone, turns the sliders off and says why.
- Moving a slider sends the whole settings to that track's synth. The panel finds the synth by the track's id every
  time: a deleted track's slot belongs to the next track, and the panel must not change its sound.
- `toText()` and `fromText()`: the settings as one line, `cutoff=4000 resonance=0.9 ...`, with the fewest digits that
  come back exactly - what a project file would keep. Every key once, every value inside its slider's range.

The self-check plays a long note and moves the cutoff from 8000 to 200 Hz: the sound must get darker by more than
ten times, and level 0 must silence it from the next block while the note still sounds.

## Exercise 9 - MIDI files in DawLab 18 ★★☆ (implementation, JUCE)

[`l18_ex9_starter`, file `exercises/starter/ex9_midi_files.cpp`]

Exercise 3 reads and writes events; DawLab keeps notes in clips. Write the step between (Exercise 3's reader and
writer are given, solved, in `exercises/smf_model.h`):

- `songOf()`: the project as it plays - every instrument track with something to play, each clip that is not muted,
  what sounds inside its window after quantize, cut at the clip's end.
- `importSong()`: every track of a file as a new instrument track with one clip. A note off ends the note of its key
  and channel that started first; a note never ended ends with the track; pitch bend and program changes are
  counted, not kept. The file's tempo becomes the project's. All of it is one undo step, "Import MIDI".

The self-check exports a quantized clip with an offset and a muted copy, imports overlapping notes of one key and a
note that never ends, takes it all back with one undo, and sends 198 random notes through a file into another
project: they must come back the same.

## Exercise 10 - The computer keyboard as a MIDI keyboard ★☆☆ (implementation, JUCE)

[`l18_ex10_starter`, file `exercises/starter/ex10_computer_keyboard.cpp`]

`a w s e d f t g y h u j k o l p` play C4 to D#5, `z` and `x` change the octave, `c` and `v` the velocity, and
the notes go where a MIDI keyboard's go: `AudioHost::pushMidi()`. A computer keyboard is not a MIDI keyboard in
three ways, and each one is a hanging note if it is forgotten: a held key repeats, JUCE does not say which key went
up, and a key released while the window is in the background is never seen. And a key that goes up after an octave
change must end the note it started.

## Exercise 11 - Edit many notes at once ★★☆ (implementation, JUCE)

[`l18_ex11_starter`, file `exercises/starter/ex11_note_editor.cpp`]

Select a clip's notes by a rectangle; transpose them, move them, duplicate them after themselves, delete them - each
one undo step - and the keys for all of it. The selection is the hard part: it belongs to the notes, so a note that
the sort moves to another place in the list stays selected; and it belongs to the clip as the editor last saw it,
so an undo clears it instead of leaving it on other notes.

## Exercise 12 - A VST3 instrument on a track ★★★ (implementation, JUCE)

[`l18_ex12_starter`, file `exercises/starter/ex12_plugin_instrument.cpp`]

Exercise 7's design, built: `PluginInstrument` wraps a JUCE `AudioProcessor` - which is what chapter 13's
`PluginHost` gives for a VST3 file - as an `engine::Instrument`, and `Engine::setTrackInstrument()` puts it in a
track's synth's place. The block's events go into a `MidiBuffer` at their offsets, the processor's output is
added to the track, and the state can be kept as base64 text. `render()` runs on the audio thread: the self-check
counts the allocations in it, and there must be none.

The self-check's instrument is a small processor of its own, since no VST3 instrument can be assumed where the
check runs. To hear a real one, load a VST3 instrument you have with `PluginInstrument::load()` in a program of your
own, or wire it into DawLab 18's track menu.
