# Step 19 - Export mixdown
**English** | [繁體中文](step-19-export.zh-TW.md)

[Overview](../README.md) | Previous: [Step 18](step-18-file-loading.md) | Next: [Step 20](step-20-spectrum-loudness.md) | Back to the [roadmap](../README.md#4-the-twenty-one-steps)

## Goal

"File > Export mixdown..." (Ctrl+E) writes the song plus 3 s of tails to a 24-bit WAV file,
latency-compensated, faster than real time, without freezing the window - and the result must be
what the user hears when pressing Play.

## Design

### Reuse the engine's bounce

Step 9 built `renderOffline()` / `bounceToWav()`: apply pending commands, reset, play from the
start, call `processBlock()` as fast as possible, stop at the song end, render the tail, drop the
first `getLatencySamples()` samples. Since `processBlock()` is also what the device calls,
"what you hear is what you bounce" holds by construction. The app adds two things around it: a
safe hand-over from the device, and a thread.

### Rule 1: only one caller of `processBlock()`

The engine is a single-consumer object: one thread may call `processBlock()` (and thereby
consume commands). While the device runs, that is the audio thread. So before rendering:

```cpp
audioHost_.setEngineAttached(false);   // AudioDeviceManager::removeAudioCallback(this)
```

`removeAudioCallback()` takes JUCE's audio callback lock: when it returns, no callback is running
and none will start. The device keeps running and outputs silence. After the export,
`setEngineAttached(true)` re-adds the callback; `audioDeviceAboutToStart()` sees the same sample
rate and buffer size and *skips* `Engine::prepare()` (Step 11), so nothing is reset needlessly.
The renderer restores the transport (position, loop, play state) itself.

### Rule 2: the render thread owns the queues meanwhile

`renderOffline()` applies the command queue and calls `collectGarbage()` - it is the consumer of
the command queue *and* of the garbage queue. Both are single-consumer queues. Hence, while the
export runs:

- `MainComponent::tick()` skips `collectGarbage()`;
- an `ExportOverlay` covers the window and swallows mouse clicks, `perform()` ignores commands,
  and the commands report themselves inactive (`setActive(false)`), so nobody edits the project;
- editor windows are closed before the export starts.

Values (fader positions...) could still change through atomics without harm, but there is no way
to reach them.

### A thread with a callback

`ExportJob : juce::Thread` runs `Exporter::renderToFile()` and posts the `ExportResult` back with
`MessageManager::callAsync`, guarded by an `alive` flag like the loader of Step 18.
`MainComponent::finishExport()` removes the overlay, re-attaches the engine and shows
"Exported ...: 19.00 s, peak -1.00 dBFS, -15.5 LUFS, rendered in ... s" in the status line.

The file is written with JUCE's `WavAudioFormat` (`Exporter::writeWavFile`, 16 / 24-bit PCM or
32-bit float): `juce::File` handles non-ASCII paths on Windows, which a `std::ofstream` opened
with a UTF-8 `std::string` does not.

### The numbers in the result

`ExportResult` reports the length, the wall-clock render time, the sample peak of the file,
the master's integrated loudness of this render (the meters were reset at its start, so the value
describes exactly the exported audio) and the latency that was removed.

## Code

- `app/Source/Exporter.h/.cpp`: `ExportOptions`, `ExportResult`, `Exporter::renderToFile()`,
  `Exporter::writeWavFile()`, `ExportJob`.
- `app/Source/MainComponent.cpp`: `exportMixdownInteractive()` (save dialog), `exportMixdown()`,
  `finishExport()`, `ExportOverlay`.

## Verify

`l14_app_smoke_test`, section 3, exports the first 2 s of the demo song with the same function:

```
3. export: ok, 2.00 s, peak -1.00 dBFS, RMS -20.3 dBFS, ...\dawlab_output\l14_app_smoke.wav
```

Checks: the file exists, exactly 96 000 samples, no NaN / Inf, peak above -30 dBFS (not silent)
and at most -1.00 dBFS + 0.05 dB (the master limiter's ceiling), RMS above -45 dBFS. Section 4
(Step 11) then proves that playing through the device callback gives the same samples.

Listen to `dawlab_output/l14_app_smoke.wav`: drums, bass and pad of bar 1, with the hit through
its convolution reverb.

Section 9 of the same test goes through the GUI's own path: an offline `MainComponent` loads the
demo song and calls `exportMixdown(file)` - overlay, `ExportJob` thread, result via
`MessageManager::callAsync`, `finishExport()`. Because the result can only arrive through the
message loop, the smoke test finishes *asynchronously*: `runSmokeTest()` returns after starting
the export, `DawLabApplication::initialise()` returns, JUCE's message loop runs, and the callback
checks the result and quits the application with the exit code.

```
9. GUI export (ExportJob thread): ok, 19.00 s, peak -1.00 dBFS, -15.5 LUFS integrated, latency 752 removed;
   rendered in 14.0 s while the message loop kept running; status line "Exported ..."
```

Checked: right after `exportMixdown()` the job runs and the editing commands report themselves
disabled; afterwards the WAV file `dawlab_output/l14_app_export.wav` has exactly
$19 \cdot 48000 = 912\,000$ stereo frames (16 s of song = 8 bars at 120 BPM, plus the 3 s tail),
the peak is at the limiter's ceiling, the removed latency equals `Engine::getLatencySamples()`,
the status line starts with "Exported", and the transport is back at 0, stopped. The -15.5 LUFS
are the same as stage 1's mix report (Step 9): the GUI export *is* the engine's bounce. The
render time (Debug build) varies with the machine.

## Your turn

| | |
|---|---|
| You write | `Exporter::renderToFile()` in `app/Source/Exporter.cpp`: fill `engine::RenderOptions` from the `ExportOptions`, call `engine::renderOffline()` (Step 9), fill the `ExportResult` (latency, length, peak, integrated LUFS of the master), refuse an empty or NaN render, write the WAV |
| Build + check | `.\scripts\build.ps1 -Only 14-daw-8track -Juce -TestLabel test` (`l14_app_smoke_test`) |
| Expected | section 3: `export: ok, 2.00 s, peak -1.00 dBFS`; section 9: the whole song, 19.00 s, -15.5 LUFS integrated, latency 752 removed; `[PASS] l14_app_smoke_test` |

## What you learned

- A bounce is the playback code run by another driver - but only one driver at a time.
- Single-consumer queues make "who may call this now?" a question you must answer explicitly
  during every mode change.
- Long work on a thread, results via the message thread, a modal overlay instead of a frozen
  window.
