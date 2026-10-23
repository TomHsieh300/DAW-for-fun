# Step 12 - The main window and a dark LookAndFeel
**English** | [繁體中文](step-12-main-window.zh-TW.md)

[Overview](../README.md) | Previous: [Step 11](step-11-audio-device.md) | Next: [Step 13](step-13-transport-bar.md) | Back to the [roadmap](../README.md#4-the-twenty-one-steps)

## Goal

A 1400 x 900 window with a menu bar, the transport bar on top, the arrangement in the middle, the
mixer at the bottom and a status line - all in a dark theme - and an application object that
owns everything in the right order. Plus a way to *see* the window without starting the app by
hand: `--snapshot <file.png>`.

![DawLab 8](images/dawlab8_snapshot.png)

## Design

### The object tree

```
DawLabApplication : juce::JUCEApplication          Main.cpp (START_JUCE_APPLICATION)
 '- MainWindow : juce::DocumentWindow               native title bar, resizable, min 1100 x 720
     '- MainComponent                               the content and the controller
          lookAndFeel_   DawLookAndFeel             (1) must outlive all components using it
          pluginHost_    dawlab::host::PluginHost   (2) VST3 format manager (chapter 13)
          engine_        dawlab::engine::Engine     (3) stage 1
          session_       Session                    (4) GUI model of the project (Step 15)
          audioHost_     AudioHost                  (5) Step 11
          loader_        MediaLoader                (6) Step 18
          menuBar_, transportBar_, arrangement_, mixer_, status line, editor windows
```

C++ destroys members in reverse declaration order, and the order above is chosen so that every
object dies before the things it uses: the audio device closes (5) before the engine (3) goes
away, so no callback can touch a dead engine; the engine (which owns the VST3 inserts) dies
before the plugin format manager (2); every component dies before the LookAndFeel (1) - JUCE
asserts if a LookAndFeel is deleted while a component still points at it.
`~MainComponent()` additionally stops the timer, waits for a running export, closes editor
windows and the device explicitly, so the order is visible in the code, not only implied.

### Layout

`MainComponent::resized()` cuts the window from the edges inwards:

| area | height | component |
|---|---|---|
| menu bar | 24 px | `juce::MenuBarComponent` (File, Transport, Track, View) |
| transport | 58 px | `TransportBar` (Step 13) |
| status line | 22 px | two `juce::Label`s: last message, audio device |
| mixer | max(430 px, 55 % of the rest) | `MixerView` (Step 15) |
| arrangement | the rest | `ArrangementView` (Step 14) |

The menu bar is a component inside the window (not the OS menu), so it also appears in the
snapshot and looks the same on every platform.

### The LookAndFeel

JUCE draws standard widgets through a `LookAndFeel`. `DawLookAndFeel` derives from
`LookAndFeel_V4`, sets its 9-role colour scheme to dark greys, and overrides four drawing
functions:

- `drawRotarySlider`: a dark track arc, a cyan value arc, a disc with a pointer. A knob whose
  `bipolar` property is set (pan, +/- dB parameters) draws its arc **from the centre**, so 0 is
  visibly "nothing".
- `drawLinearSlider` (vertical only): a mixing-desk fader - a slot, a filled part below the cap,
  a flat cap with a centre line.
- `drawButtonBackground`, fonts, tooltips.

All colours live in `DawColours` (`DawLookAndFeel.h`), including one colour per track, used by the
clips, the lane headers and the strip headers, so the eye can connect a lane to its strip.

`MainComponent` calls `setLookAndFeel(&lookAndFeel_)`: children inherit it. Top-level windows
(parameter panels) get it explicitly, and reset it to `nullptr` in their destructor.

### Two modes

`MainComponent(Mode::Live)` opens the audio device and starts the 30 Hz timer;
`MainComponent(Mode::Offline)` builds the identical component tree without a device (48 kHz,
512) and without a timer - the caller drives `tick()` and `simulatePlayback()` itself. The
smoke test, the snapshot and all the JUCE exercises use the offline mode.

### `--snapshot`

```cpp
MainComponent main(MainComponent::Mode::Offline);
main.setSize(1400, 900);
main.loadDemoSongNow();                              // Step 18, synchronously
tr.setPosition(0); tr.play();
for (done = 0; done < bar2beat3; done += fs / 30) {  // "play" with 30 UI ticks per second
    main.simulatePlayback(fs / 30);                  // AudioHost::processOffline in 512 blocks
    main.tick();                                     // meters, playhead, spectrum
}
juce::Image image = main.createComponentSnapshot(main.getLocalBounds(), true, 1.0f);
juce::PNGImageFormat().writeImageToStream(image, out);
```

`createComponentSnapshot()` paints the component tree into an image without a window on screen.
One detail made the first snapshot wrong: `Session` notifies the views with
`juce::ChangeBroadcaster`, which delivers *asynchronously* through the message loop - and in the
snapshot no message loop runs, so the mixer still showed "Track 1" and empty slots. `tick()` now
starts with `session_.dispatchPendingMessages()`, which delivers pending change messages
synchronously. (In the running app this changes nothing; it just makes a tick self-contained.)

## Code

- `app/Source/Main.cpp`: `DawLabApplication` (command line: `--smoke-test`, `--snapshot`),
  `MainWindow`. The test modes run inside `initialise()`, set the exit code with
  `setApplicationReturnValue()` and `quit()`. A GUI program on Windows has no console; when it is
  started from a terminal (stdout is not redirected), `AttachConsole(ATTACH_PARENT_PROCESS)` makes
  `printf` visible.
- `app/Source/MainComponent.h/.cpp`: construction order, `resized()`, `tick()`,
  `simulatePlayback()`.
- `app/Source/DawLookAndFeel.h/.cpp`.
- `app/CMakeLists.txt`: `juce_add_gui_app(DawLab8 PRODUCT_NAME "DawLab 8")`. All app classes
  except `main` are in the static library `l14_daw_app`, so the exercises can link them. JUCE is
  not compiled again: chapter 13's `dawlab_plugin_host` already contains the JUCE modules.

## Verify

```powershell
cd build                                      # the build directory (.\scripts\build.ps1 -Juce)
& ".\bin\Debug\DawLab 8.exe" --snapshot dawlab_output/l14_app_snapshot.png
```

```
snapshot written: 1400x900, ...\dawlab_output\l14_app_snapshot.png, position 2.3.000, unknown parameters 0
meters at that point: master peak -9.4 / -9.6 dBFS, loudness M -17.2  S -15.8  I -15.9 LUFS, mix latency 752 samples
panel snapshot: ...\dawlab_output\l14_app_snapshot_panel.png (drum compressor, gain reduction 12.1 dB)
```

ctest runs it as `l14_app_snapshot` (label `example`). The picture at the top of this page is
that file. It shows the playhead at bar 2 beat 3 - the position you *hear*: the snapshot plays
until the transport is 752 samples (the mix latency) past it, see Step 13 - meters with level
(the numbers in the second line are what the transport bar and the master section display), the
master spectrum, and in
the status line "No audio device (offline)". The CPU figure in the transport bar (72 % in this
run, it varies) is honest but means something slightly different here: `simulatePlayback()`
renders as fast as it can, so it is "time spent rendering / duration of the audio rendered" for
this Debug build.

## Your turn

Rebuild the window layout (copy `MainComponent.cpp` first, see Step 11).

| | |
|---|---|
| You write | `MainComponent::resized()` in `app/Source/MainComponent.cpp`: from the top a 24-pixel menu bar and the 58-pixel transport bar, a 22-pixel status row at the bottom (device info on its right), the mixer below with 55 % of the remaining height (at least 430 pixels), the arrangement in what is left, the export overlay over everything |
| Build + check | `.\scripts\build.ps1 -Only 14-daw-8track -Juce`; ctest runs `l14_app_snapshot` (label `example`) |
| Expected | `dawlab_output/l14_app_snapshot.png` (1400 x 900) looks like the picture at the top of the chapter README: transport bar, eight lanes, mixer, status line "No audio device (offline)" - compare the two images side by side |

A layout mistake does not fail a test, it shows in the picture: that is why the app can render
itself into a PNG.

## What you learned

- Member declaration order is lifetime order; write it down when it matters.
- One `LookAndFeel` subclass themes every widget; custom components share its colour table.
- `createComponentSnapshot()` makes a GUI testable and documentable without a screen.
- `ChangeBroadcaster` is asynchronous - fine for a GUI, surprising in a test.
