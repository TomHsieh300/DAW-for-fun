# Chapter 14 - Exercises
**English** | [繁體中文](README.zh-TW.md)

[Back to the chapter](../README.md) | [Solutions](../solutions/README.md)

Difficulty: ★☆☆ easy, ★★☆ medium, ★★★ hard. Implementation exercises have a starter file in
`exercises/starter/` that compiles and runs; until you complete it, its self-check prints `FAIL`
(that is expected). Build target names are given in brackets.

The chapter's **exercise set is Exercises 1-7** (concept, calculation, design and three
implementation exercises), about the engine (stage 1, no JUCE). **Exercises 8-13 are optional
projects** about the application DawLab 8 (stage 2): larger, open-ended extensions for readers who
want to keep building the DAW after the chapter. They need a JUCE
build (`.\scripts\build.ps1 -Only 14-daw-8track -Juce`). The JUCE exercises are console programs
that link the app library `l14_daw_app` and build the real `MainComponent` in its offline mode, so
no audio device and no clicking is needed to check them. **Exercise 14** came with the mono /
stereo track formats: an engine implementation exercise like 3-5 (no JUCE), placed at the end.

---

## Exercise 1 - Who may touch what? ★☆☆ (concept)

Answer in a few sentences each:

1. A colleague protects the insert chain with a `std::mutex`: the UI locks it to add an effect, the
   audio thread locks it in `processBlock()`. It works on his machine. Explain what can go wrong, and
   name the effect.
2. Why does `Engine::removeInsert()` not simply `delete` the effect after the audio thread has
   unlinked it, but send it back through a second queue?
3. If the garbage queue is full, the audio thread *leaks* the object instead of deleting it. Why is
   that the right choice? Why can it not happen in practice in this engine?
4. Why do fader moves and effect parameters not need the command queue, while adding an effect
   does?
5. A track is muted, but its pre-fader send to the reverb bus is at 0 dB. What do you hear from
   that track, and why did we choose that behaviour?

## Exercise 2 - Timing and latency by hand ★★☆ (calculation)

Compute by hand (show the formulas), then run [`l14_ex2_solution`] to check:

- (a) A project at 44.1 kHz, 96 BPM, 3/4. How many samples are one beat and one bar? Where (in
  samples and seconds) is bar 5, beat 2? Which bar:beat:tick is sample 1 000 000 (960 ticks per beat)?
- (b) The delay on bus B should repeat on dotted eighth notes at 96 BPM. Delay time in ms and in
  samples at 44.1 kHz? What LFO rate in Hz is "one cycle per quarter note"?
- (c) At 48 kHz, tracks 1-4 have inserts with latencies 0, 64, 240 and 1024 samples (tracks 5-8 have
  none), bus A has none, bus B has a 512-sample convolution reverb, the master a 240-sample limiter.
  Give every compensator delay (tracks, buses, track sum) and the total latency in samples and ms.
- (d) A track fader at -6 dB with pan +0.5, constant-power law: the left and right gains, linear
  and in dB. And on a strip in balance mode?
- (e) Each PDC delay line holds up to 16 384 samples (+1), stereo, `float`. How much memory do all
  of them use (11 strips + the track-sum line)?

## Exercise 3 - Volume and pan automation ★★☆ (implementation)

[`l14_ex3_starter`, file `exercises/starter/ex3_automation.cpp`]

Implement a breakpoint envelope ("automation lane") and use it sample-accurately inside the engine:

- `AutomationLane::valueAt(t)`: linear interpolation between sorted breakpoints; before the first /
  after the last point the first / last value holds; an empty lane returns its default.
- `AutomationLane::render(start, out, n)`: the same for a whole block, but with one binary search and
  then a forward-moving cursor instead of one search per sample.
- `VolumeAutomationEffect::processDsp()`: lane values are dB; multiply every channel by
  $`10^{v/20}`$. (The effect receives the block's timeline segments through `setPlayHead()`; the base
  class already turns them into one value per sample, so loops work too.)

The self-check renders a DC clip through a fade 0 dB (0.5 s) -> -24 dB (1.5 s) and requires exactly
-12.00 dB at 1.0 s, identical output for block sizes 512 and 37, an identical second loop pass, and a
constant-power pan sweep.

Hint: why automate in dB rather than in linear gain? Compare what you hear halfway through a linear
gain ramp 1 -> 0.063 with a dB ramp 0 -> -24 dB.

## Exercise 4 - Save and load a project as JSON ★★★ (implementation)

[`l14_ex4_starter`, file `exercises/starter/ex4_project_json.cpp`]

The starter gives you a minimal JSON parser (`parseJson`, `Json`) and writer helpers (`jsonString`,
`num`, `boolean`). Write:

- `saveProject(engine, pool)`: transport (tempo, time signature, loop), the PDC flag, and for all 11
  strips: name, fader, pan, pan law, pan mode, mute, solo, sends (level, pre/post), inserts (type
  name, bypass, every parameter **by name**), and for tracks the format (Mono / Stereo, Step 4) and
  the clips (media id, start, length, offset, gain, fades, shape, muted). Audio is not stored in the file: clips refer to a `MediaPool`
  id.
- `loadProject(engine, text, pool, &error)`: the reverse, using `createEffect(name)`,
  `setParameterByName()`, `Engine::addInsert()` and `Engine::setTrackClips()`. Restore a track's
  format *before* its pan mode: `Track::setFormat()` also sets the pan mode. Unknown parameter
  names are skipped; an unknown effect type or missing media is an error with a message.

The self-check saves a project, loads it into a fresh engine, saves again and requires **identical
JSON text** and a **bit-identical re-render** - so think about how many digits a `float` needs.

## Exercise 5 - Moving clips and crossfades ★★☆ (implementation)

[`l14_ex5_starter`, file `exercises/starter/ex5_clip_crossfade.cpp`]

- `snapToGrid(position, gridSamples)`: round to the nearest grid line (no snapping when the grid is 0).
- `moveClip(clips, index, newStart, gridSamples)`: move with snapping, never before 0, keep the vector
  sorted by start, return the new index.
- `crossfade(a, b, overlap, shape)`: move clip `b` so that it overlaps the end of `a` by `overlap`
  samples and give both matching fades.

The self-check measures four crossfades through the engine. Before running it, predict: for two
copies of the *same* audio and for two *independent* noises, what is the level in the middle of a
Linear and of an EqualPower crossfade?

## Exercise 6 - Designing per-track input recording ★★☆ (design)

Add recording to the engine on paper (no code required; pseudo-code welcome). Your design must
answer:

1. Where does the input audio enter `processBlock()`, and how do you avoid allocating while a take
   of unknown length grows?
2. Which thread writes the WAV file, and how does the audio reach it without locks?
3. The interface has an input latency $`L_{\text{in}}`$ and output latency $`L_{\text{out}}`$, and the mix
   has PDC latency $`L_{\text{pdc}}`$. The musician played along with what they heard. At which timeline
   position must the recorded clip be placed?
4. What happens on the audio thread if the disk is too slow for a while?
5. How does input monitoring (hearing yourself through the track's inserts) interact with PDC?

## Exercise 7 - Tempo changes and time-stretching ★★★ (design)

So far clips are in samples, so changing the tempo moves the grid but not the audio: a drum loop
recorded at 120 BPM no longer fits at 100 BPM.

1. What must a clip store instead (or in addition) to follow tempo changes? Distinguish
   "musical" (beat-based) clips from "absolute" (sample-based) ones - which material needs which?
2. Why is resampling (playing faster/slower) not acceptable for most material, and what does a
   time-stretch algorithm do instead? Name two approaches and their typical artefacts.
3. Time-stretching is expensive and has latency. Where would you run it - on the audio thread per
   block, or elsewhere? Sketch a design that stays real-time safe and still reacts when the user
   changes the tempo while playing.
4. With a tempo *map* (tempo changing over time), how do you convert a timeline sample to a beat
   position? What data structure makes this fast?

---

# Optional projects (Exercises 8-13): the application (JUCE)

These six projects are not part of the chapter's core exercise set; pick the ones that interest
you. Each has a starter, a complete solution with self-checks, and a write-up in the solutions.

## Exercise 8 - An automation lane in the GUI ★★★ (implementation)

[`l14_ex8_starter`, file `exercises/starter/ex8_automation_lane.cpp`]

Exercise 3 automated a fader from code. Now the user draws the automation. Complete:

- `AutomationLaneData::valueAt(t)`: linear interpolation between sorted breakpoints (dB values);
  the first / last value holds outside them.
- `AutomatedGainEffect::process()`: the gain for every sample from the timeline position given by
  the PlayHead segments (the hand-over of a new lane to the audio thread is already written - read
  it: an atomic pointer in, an `SpscQueue` back, delete on the UI thread).
- `AutomationLaneComponent`: `valueToY` / `yToValue` (+6 dB at the top, -48 dB at the bottom, linear
  in dB), `pointFromPosition` (x through the arrangement's `xToSample()` and `snapToBeat()`),
  `hitTest` (6-pixel radius), and the three gestures: click = select or add, drag = move but never
  past the neighbours, right-click = remove.

The self-check builds a real `ArrangementView` (one beat = 125 px), performs the gestures with
coordinates, and renders a DC clip through the effect: exactly -12.00 dB halfway between a 0 dB
and a -24 dB point, identical output for block sizes 512 and 37, and no lane object leaked after
200 publishes during playback. Hint: why must dragging clamp the point between its neighbours
instead of re-sorting the list?

## Exercise 9 - Recording audio input into a track ★★★ (implementation)

[`l14_ex9_starter`, file `exercises/starter/ex9_recording.cpp`]

Turn the design of Exercise 6 into code. `RecordingCallback` is a `juce::AudioIODeviceCallback`
that renders the engine like `AudioHost` and gives the device inputs to an `InputRecorder`:

- `pushInput()` (audio thread): copy the block into a pre-allocated `juce::AbstractFifo` ring
  (two regions when it wraps). If there is not enough free space: count an overrun and drop the
  block - never wait.
- `drain()` (disk thread): move everything that is ready into the growing take.
- `clipStart()`: where the take goes on the timeline, given the input / output latencies of the
  device and the PDC latency of the mix.

The test calls the callback from a second thread with a 440 Hz tone (no device needed) while a
`juce::Thread` drains. Required: the take equals the input sample for sample through a FIFO that
wraps many times, zero overruns, the clip at 96 000 - (64 + 96 + 240), and the exact number of
overruns when nobody drains a 0.25 s FIFO.

## Exercise 10 - A MIDI track with a simple synthesizer ★★★ (implementation)

[`l14_ex10_starter`, file `exercises/starter/ex10_midi_synth.cpp`]

`SimpleSynth` is an *instrument insert*: it sits in a track's first slot and adds its voices to
the (empty) track. Its parameters (Waveform, Attack, Decay, Sustain, Release, Gain) get an
auto-generated panel in the app like any effect. Complete:

- `notesFromMidi()`: a `juce::MidiMessageSequence` in ticks (960 per beat) -> `NoteEvent`s in
  samples, note-ons paired with their note-offs (`updateMatchedPairs`).
- `noteOn()`: voice allocation - a free voice, otherwise steal the oldest.
- `envelope()`: a linear ADSR.

The event loop (sample-accurate note starts inside the PlayHead segments, release on a loop jump)
and the PolyBLEP oscillators are given. The self-check requires the first non-zero sample at
24 000 or 24 001 for a note on beat 2, 440 Hz at least 30 dB above 466 Hz for A4, three voices
during a triad, silence after the release, identical output for block sizes 512 and 100, and the
same note in both passes of a loop. Questions: why must a jump (seek, loop) release the voices?
What would a "chasing" DAW do with a note that started before the seek point?

## Exercise 11 - Save and open projects from the File menu ★★☆ (implementation)

[`l14_ex11_starter`, file `exercises/starter/ex11_project_file.cpp`]

Exercise 4 saved the *engine* as JSON. Now save the *application's* project, and make it a real
menu command:

- `ProjectFile::stripToTree()` / `stripFromTree()`: one `juce::ValueTree` per strip (name, fader,
  pan, pan law, track format and pan mode, mute, solo, sends, inserts with all parameters by name or a VST3 file +
  base64 state, clips). The media writing (one 32-bit float WAV per distinct buffer) and the file
  framing are given.
- `ProjectCommands`: "Save project..." (Ctrl+S) and "Open project..." (Ctrl+Shift+O) as commands of
  an extra `ApplicationCommandTarget` (`MainComponent::setExtraCommandTarget`), added to the File
  menu through `MainComponent::onExtendMenu` - without modifying the app.

The self-check finds both items in the File menu and Ctrl+S in the key mappings, saves a modified
demo song, opens it in a fresh `MainComponent`, requires a **bit-identical** 4-bar render, and
saving again must give the **identical XML text**. Why 32-bit float WAVs, and why 9 significant
digits for floats?

## Exercise 12 - A third FX bus ★★☆ (implementation + concept)

[`l14_ex12_starter`, file `exercises/starter/ex12_third_bus.cpp`, links `l14_daw_app_3bus`]

The number of buses is the compile-time constant `engine::kNumBuses`
(`-DDAWLAB_ENGINE_NUM_BUSES=3` builds a second copy of the app library, see `app/CMakeLists.txt`).

1. (Concept) List every place in the engine and in the GUI whose size follows `kNumBuses` (hint:
   `ChannelStrip`, `Mixer`, `Engine::allChannels`, `Session`, `MixerView`, `DemoSong`). Why is a
   compile-time constant a reasonable choice for an engine that must not allocate on the audio
   thread? What would a run-time bus count require?
2. (Implementation) `configureThirdBus()`: make bus C a "Doubler" - Chorus at 100 % wet plus a
   low-cut EQ - fed by the Pad and the Lead through **pre-fader** sends at -6 dB.

The self-check counts 12 strips and 3 send knobs per track, moves the C knob and reads the engine,
mutes everything but the doubler path, pulls the Pad and Lead faders to $`-\infty`$ and still hears
bus C (pre-fader!), then turns the sends off and requires exact silence.

## Exercise 13 - A keyboard shortcut map ★☆☆ (implementation)

[`l14_ex13_starter`, file `exercises/starter/ex13_shortcut_map.cpp`]

- `findConflicts()`: every pair of commands that share a key press.
- `ShortcutCommands`: keys 1-8 select track 1-8; Ctrl+Left / Ctrl+Right move the playhead to the
  previous / next beat *line* (not "one beat from here").
- `saveShortcuts()` / `loadShortcuts()`: store only the user's changes
  (`KeyPressMappingSet::createXml(true)`) and restore them.

The self-check requires no conflicts among the 28 commands, detects a deliberately created one,
presses the new keys, remaps Play/Stop from Space to P, saves, restores the file into a fresh app
and checks that P plays and Space does nothing. It also renders JUCE's `KeyMappingEditorComponent`
for the same mapping set to `dawlab_output/l14_ex13_editor.png`. Question: why do the knobs and
buttons of DawLab 8 refuse the keyboard focus?

## Exercise 14 - Mono and stereo tracks by hand ★★☆ (implementation)

[`l14_ex14_starter`, file `exercises/starter/ex14_track_format.cpp`; engine only, no JUCE]

Write the format-dependent part of a track (Step 4, "Mono and stereo tracks"; Step 5, "Inserts on
a mono track") as two free functions, without the engine around them:

- `trackPanGains(format, pan, left, right)`: Mono -> the constant-power pan law
  $`p_L = \cos\theta`$, $`p_R = \sin\theta`$ with $`\theta = \frac{\pi}{4}(p+1)`$; Stereo -> balance
  $`p_L = \min(1, 1-p)`$, $`p_R = \min(1, 1+p)`$.
- `processTrackBlock(block, format, insert, gainDb, pan)`, in place on a stereo block:
  Mono -> fold to $`(L+R)/2`$, run `insert` (may be null) on `block.getChannelSubset(0, 1)` only,
  copy channel 0 to channel 1; Stereo -> run `insert` on both channels. Then multiply each side by
  $`10^{\text{dB}/20}`$ times its pan gain.

The self-check compares your gains with `ChannelStrip::computeStripGains()`, puts a mono file
(L = R = 0.5) on both formats and requires the **3.01 dB** centre difference, requires a stereo
source (0.6 | 0.2) to keep L != R under balance (0.3 | 0.2 at +0.5) and to fold to 0.4 x 0.7071 on a
mono track, runs a chorus with full Stereo Spread and requires L = R on the mono track but a wide
output on the stereo track, and finally compares your result with what the real engine renders for
both formats. Before running it, predict: on which of the two formats is the same mono file louder
at the centre, and by how much? Why?
