# Step 21 - Keyboard shortcuts, menus and automated tests of the app

[Overview](../README.md) | Previous: [Step 20](step-20-spectrum-loudness.md) | Back to the [roadmap](../README.md#4-the-twenty-one-steps)

## Goal

Space plays and stops - always, wherever the mouse is. Every menu item shows its shortcut, and
every action exists exactly once in the code. And finally: a GUI application that a script can
test, so that `ctest` guards the app like it guards the engine.

## Design

### Commands: one action, three ways to trigger it

JUCE's `ApplicationCommandManager` is a registry of *commands* (an integer ID with a name, a
description, a category, default key presses and an "active / ticked" state) and *targets*
(objects that perform them). `MainComponent` is the target for all the app's commands
(`AppCommands.h`):

| category | command | default key |
|---|---|---|
| File | New project / Load demo song / Load audio into selected track... / Export mixdown... / Audio settings... / Quit | Ctrl+N / Ctrl+D / Ctrl+O / Ctrl+E / Ctrl+, / Ctrl+Q |
| Transport | Play / Stop, Return to start, Loop, Back / Forward one bar | Space, Home, L, Left, Right |
| Track | Select previous / next track, Mute / Solo selected track | Up, Down, M, S |
| View | Zoom in / out, Keyboard shortcuts... | = or +, -, F1 |

(Ctrl is Cmd on macOS: `ModifierKeys::commandModifier`.)

```cpp
commandManager_.registerAllCommandsForTarget(this);     // asks getAllCommands + getCommandInfo
commandManager_.setFirstCommandTarget(this);
addKeyListener(commandManager_.getKeyMappings());        // keys -> commands
setApplicationCommandManagerToWatch(&commandManager_);   // the menu bar shows shortcuts / ticks
```

- The menu bar items are `PopupMenu::addCommandItem(&commandManager_, id)`: name, shortcut text,
  tick (Play / Stop, Loop) and enabled state come from `getCommandInfo()`.
- Keys go through the `KeyPressMappingSet` installed as a key listener on `MainComponent`. Keys
  reach a component's key listeners when no focused child consumed them; that is why every
  button, knob and fader calls `setWantsKeyboardFocus(false)`, and why the main window grabs the
  focus at start. A label being edited (track names) does consume keys - typing "s" in a name must
  not solo the track.
- The transport buttons call the same `togglePlay()` / `returnToStart()` / `toggleLoop()`.
- While an export runs, `getCommandInfo()` marks the editing commands inactive and `perform()`
  swallows them (Step 19).

`getNextCommandTarget()` returns an optional extra target (`setExtraCommandTarget()`), and
`onExtendMenu` lets a caller add items to a menu. The app does not use them itself: Exercises 11
(project save / open) and 13 (a shortcut map) add commands to the running app through them
without modifying it.

### Testing a GUI application

A DAW with a GUI and an audio device looks untestable. Two command-line modes make it testable,
because the app was built for it from Step 11 on (an offline mode, a device-free render path,
public `tick()`):

**`DawLab 8.exe --smoke-test`** (ctest `l14_app_smoke_test`, label `test`). No window, no device.
It builds the real classes and checks them with `DAWLAB_CHECK`:

1. the resampler (length, passband, alias rejection, the alias left in the transition band) - Step 18;
2. the demo song through `Session::applyDemoSong()`: 19 inserts, 0 unknown parameters, GUI slots
   = engine chains, mix latency = max track + max bus + master latency - Steps 16, 18;
3. a 2 s export through `Exporter::renderToFile()`: not silent, no NaN, peak <= ceiling - Step 19;
4. the device callback (`AudioHost::processOffline`, 512 and 480 frames, 3 outputs) equals the
   bounce exactly - Step 11;
5. a hosted plugin with latency on the master: PDC, bit-identical bounce, parameter, cleanup -
   Step 17;
   5b. when chapter 13 was built in the same tree: its real DawLab Compressor VST3 on the drums,
   and its DawLab Limiter VST3 (latency 240) on the track with the largest latency: mix latency
   +240, bounce unchanged;
6. the GUI offline: strip count, fader -> engine, solo button, generated panel -> engine, Space /
   L through the key mappings, the transport display and the playhead once beat 2 is heard
   (latency-compensated: transport position minus mix and device latency) - Steps 12-16, 21.
7. a device change to 44.1 kHz: `Engine::prepare()` + `Session::handleSampleRateChange()` move a
   clip from 96 000 to 88 200 samples (bar 2 at the new rate) and resample its buffer - Step 11;
8. the spectrum display: a bin-centred sine appears at its frequency and level, the log axis is
   right - Step 20;
9. File > Export mixdown through `MainComponent::exportMixdown()`: the `ExportJob` thread, the
   disabled commands, the result posted back through the message loop, the 19 s file - Step 19;
10. a device restart at 44.1 kHz through `MainComponent`'s own wiring, while playing: `AudioHost`
    -> `onEnginePrepared` (transport stopped at once) -> `callAsync` -> `startClipConversion()` ->
    `MediaLoader::convertBuffers()` on the loader thread -> `finishClipConversion()` ->
    `Session::applySampleRateConversion()`, playback resumed - Step 11.

It writes `dawlab_output/l14_app_smoke.wav`, `l14_app_export.wav` and `l14_app_smoke_44k.wav` (the
44.1 kHz mono WAV that section 1 feeds to the file loader) and exits with 0 when all
checks pass. Sections 1-8 run synchronously inside `initialise()`; sections 9 and 10 need the
message loop, so `runSmokeTest()` takes a completion callback, returns after starting the export, and
the last callback (on the message thread, after the export and the sample-rate change) sets the
exit code and calls
`quit()`. This is the usual shape of a test for asynchronous GUI code: start the operation,
return to the event loop, check in the completion handler.

**`DawLab 8.exe --snapshot <file.png>`** (ctest `l14_app_snapshot`, label `example`) renders the
window with the demo song at bar 2 beat 3 and the drum compressor's panel (Step 12).

Both modes run inside `JUCEApplication::initialise()`: the message manager exists, components can
be created and painted, and the process ends with `setApplicationReturnValue(code); quit();`.

## Code

- `app/Source/AppCommands.h` - IDs and categories.
- `app/Source/MainComponent.cpp` - `getAllCommands`, `getCommandInfo`, `perform`,
  `getMenuBarNames`, `getMenuForIndex`, `setExtraCommandTarget`, `showKeyboardShortcuts` (F1: a
  list generated from the key mappings).
- `app/Source/SmokeTest.h/.cpp`, `app/Source/Main.cpp`.
- `app/CMakeLists.txt` - the two `add_test()` calls (working directory = the build directory, like
  every other chapter).

## Verify

```
DawLab 8 smoke test (no audio device)

1. resampler: 1 kHz 44.1k->48k amplitude 0.5000 (0.5 expected, -0.000 dB), length 48000;
   20 kHz 48k->22.05k alias at 2050 Hz: -122.2 dB below the input
   22.3 kHz 48k->44.1k (transition band) alias at 21800 Hz: -54.3 dB relative to the input
   file 44.1 kHz mono -> 2 channels, 48000 samples at 48 kHz, 1 kHz amplitude 0.5000
2. demo song: 140 clips on 28 distinct buffers, 19 inserts in GUI slots, 0 unknown parameter names,
   mix latency 752 samples
3. export: ok, 2.00 s, peak -1.00 dBFS, RMS -20.3 dBFS, ...\dawlab_output\l14_app_smoke.wav
4. device callback, 512-sample blocks, 3 outputs: max |callback - bounce| = 0, CPU load 68.1%
4. device callback, 480-sample blocks, 3 outputs: max |callback - bounce| = 0, CPU load 81.5%
5. test plugin on the master (latency 100): mix latency 752 -> 852, |with - without| = 0, gain 0.25 -> -6.02 dB, 192 process calls
5b. chapter 13's DawLab Compressor not built in this tree: VST3 file test skipped
5b. chapter 13's DawLab Limiter not built in this tree: VST3 PDC test skipped
6. GUI offline: 11 strips, position 1.2.000 (playhead 24000, transport 24752 = + mix latency 752),
   master peak -11.8 dBFS, status "Demo song loaded"
7. device change 48 -> 44.1 kHz: bass clip 3 moved 96000 -> 88200 samples (bar 2 = 88200)
8. spectrum view: 996.09 Hz sine at 0.5 -> peak at 996.09 Hz, -6.02 dBFS (-6.02 expected); 632.5 Hz at x = 0.500
9. GUI export (ExportJob thread): ok, 19.00 s, peak -1.00 dBFS, -15.5 LUFS integrated, latency 752 removed;
   rendered in 14.0 s while the message loop kept running; status line "Exported ..."
10. GUI device change 48 -> 44.1 kHz (loader thread): engine 44100 Hz, transport stopped at once: yes, bass clip 3 96000 -> 88200,
    converted in 5010 ms with 457 message-loop turns meanwhile, playback resumed: yes, status "Sample rate changed to 44100 Hz: clips converted"

[PASS] l14_app_smoke_test (92 checks passed)
```

(Debug build; the CPU figures and the render time in section 9 vary with the machine; the master
peak in line 6 and the loudness in line 9 depend on the DSP implementations of chapters 01-12.
Everything that is checked exactly - lengths, latencies, zero differences - does not vary.)
Exercise 13 adds a conflict check over the 18 commands of the app plus its own 10 (28 in all):
the default map has none.

## Your turn

| | |
|---|---|
| You write | the transport commands in `app/Source/MainComponent.cpp`: their `case`s in `getCommandInfo()` (name, description, category "Transport", default keys Space, Home, L, Left and Right arrows) and in `perform()` (`playStop`, `returnToStart`, `toggleLoop`, `seekBackOneBar`, `seekForwardOneBar`) |
| Build + check | `.\scripts\build.ps1 -Only 14-daw-8track -Juce -TestLabel test` (`l14_app_smoke_test`) |
| Expected | the smoke test's key presses pass (Space toggles the transport, L the loop, also through `invokeDirectly`); `[PASS] l14_app_smoke_test`; in the app the Transport menu and the F1 list show your names and keys |

To go further, add a command of your own (an ID in `AppCommands.h`, an entry in `getAllCommands()`,
`getCommandInfo()` and `perform()`). Take it out again before you run `l14_ex13_solution`: Exercise
13 counts exactly the app's 18 commands plus its own 10 (32 rows in its shortcut editor).

## What you learned

- Route every user action through a command registry; menus, keys and buttons then stay in sync.
- Keyboard focus decides where keys go - keep it away from widgets that would swallow Space.
- Design the app for testing: an offline mode, a device-free render path and command-line modes
  turn "click around and listen" into numbers a CI server can check.
