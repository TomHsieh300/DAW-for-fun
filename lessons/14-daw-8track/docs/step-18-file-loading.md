# Step 18 - Loading files and the demo song
**English** | [繁體中文](step-18-file-loading.zh-TW.md)

[Overview](../README.md) | Previous: [Step 17](step-17-vst3-inserts.md) | Next: [Step 19](step-19-export.md) | Back to the [roadmap](../README.md#4-the-twenty-one-steps)

## Goal

"Load..." on a track strip (or Ctrl+O for the selected track) reads an audio file - WAV, AIFF,
FLAC, Ogg Vorbis, Opus or MP3, any sample rate, mono or stereo - converts it to the engine's
sample rate and places it as a clip at bar 1. Audio files dragged from the file browser onto a
lane load the same way. "File > Load demo song" (Ctrl+D) builds the 8-track demo song of Step 9.
None of these may freeze the GUI, and none may touch the audio thread except through a finished
clip list.

## Formats and drag and drop

`MediaLoader` reads through `juce::AudioFormatManager::registerBasicFormats()`. Which formats that
registers is decided at compile time by JUCE options, set on chapter 13's host library
(`lessons/13-vst-hosting/host/CMakeLists.txt`): `JUCE_USE_FLAC`, `JUCE_USE_OGGVORBIS`,
`JUCE_USE_OPUS` and `JUCE_USE_MP3AUDIOFORMAT` are all 1, next to the always-present WAV and AIFF.
The codecs are compiled into the app (libFLAC, libogg/libvorbis and Opus are BSD-licensed; the MP3
decoder is JUCE's own), so nothing has to be installed. MP3 can be read but not written; mixdowns
are still exported as WAV (Step 19). On Windows, JUCE also registers its Windows Media reader
(`JUCE_USE_WINDOWS_MEDIA_FORMAT`, on by default there), so WMA files load as well; the smoke test
prints the full list: `*.wav;*.bwf;*.aiff;*.aif;*.flac;*.ogg;*.opus;*.mp3;*.wmv;*.asf;*.wm;*.wma`.

`MediaLoader::getWildcard()` asks the same format manager for its file patterns, so the file
chooser always lists exactly what the reader understands. `MediaLoader::canRead(file)` checks the
extension the same way; the arrangement view uses it for drag and drop:

| `juce::FileDragAndDropTarget` callback | `ArrangementView` does |
|---|---|
| `isInterestedInFileDrag(files)` | true if at least one file has a readable extension |
| `fileDragEnter` / `fileDragMove(files, x, y)` | highlights the lane under the mouse (`laneAt(y)`) |
| `fileDragExit` | removes the highlight |
| `filesDropped(files, x, y)` | keeps the readable files and calls `onFilesDropped(track, files)` |

The view only routes; `MainComponent::loadDroppedFiles()` loads the first file into the lane it
was dropped on and further files into the tracks below it (files beyond track 8 are reported in the
status bar), each through the same `loadAudioFile()` as the button.

## Design

### Three threads, one direction

```
message thread                   loader thread (juce::ThreadPool, 1 worker)       audio thread
--------------                   ---------------------------------------------    ------------
Load... -> FileChooser
loader_.loadFile(t, file, fs) -> read with juce::AudioFormatReader
                                 mono -> stereo, resampleBuffer(fileRate -> fs)
                                 shared_ptr<const AudioBuffer>
on the message thread  <-------- MessageManager::callAsync(...)
Session::setTrackAudio()  -----------------------------------------------------> setTrackClips
                                                                                  (pointer swap)
```

The loader never touches the session or the engine; it only produces an immutable buffer and
posts a closure to the message thread. If the GUI is gone by then (the app closed while a file
was loading), a shared `alive` flag, checked through a `std::weak_ptr`, drops the result. The
destructor of `MediaLoader` waits for a running job.

`Session::setTrackAudio()` gives the clip 5 ms fades (files rarely start and end at a zero
crossing; Step 3 explains the click) and names the track after the file.

### Resampling: band-limited interpolation

A 44.1 kHz file played by a 48 kHz engine would be 8.8 % too fast and 1.5 semitones too high
($`12\log_2(48000/44100) = 1.47`$). It must be converted. Output sample $`m`$ lies at the input
position $`t_m = m \cdot f_{\text{in}} / f_{\text{out}}`$, between two input samples. The ideal
reconstruction of a band-limited signal (sampling theorem) is a sum of sinc functions; we
evaluate it at $`t_m`$ with a windowed, low-passed sinc kernel:

$$
y[m] = \sum_k x[k]\; g(t_m - k), \qquad
g(x) = 2f_c\,\mathrm{sinc}(2 f_c x)\; w\!\left(\frac{x}{W}\right), \qquad
\mathrm{sinc}(u) = \frac{\sin \pi u}{\pi u}
$$

- $`f_c`$ is the cutoff in cycles per input sample, $`f_c = 0.5 \cdot \min(1, f_{\text{out}}/f_{\text{in}}) \cdot 0.92`$:
  just below the *lower* of the two Nyquist frequencies. When downsampling ($`f_{\text{out}} < f_{\text{in}}`$)
  this removes what would alias; when upsampling it removes the spectral images. "Removes" is
  approximate: the windowed kernel has a transition band of about $`5.5 / (2W) = 0.23 f_c`$ around
  $`f_c`$, so its stopband starts near $`1.115 f_c = 0.513 f_{\text{out}}`$, just above the output's
  Nyquist frequency. A component between $`0.5`$ and $`0.513 f_{\text{out}}`$ (22.05-22.6 kHz for a
  44.1 kHz output) still folds back, into 21.5-22.05 kHz (the top 2.6 % of the band), at least about 54 dB down
  (README section 3.10 derives the numbers).
- $`W = Z / (2 f_c)`$ is the half-width in input samples, with $`Z = 24`$ zero crossings of the
  sinc on each side; $`w`$ is a Blackman window on $`[-1, 1]`$.
- $`2 f_c`$ normalizes the kernel's DC gain to 1.

The kernel is tabulated once (512 points per zero crossing, linear interpolation between them);
each output sample costs about $`2W \approx 52`$ multiply-adds when going from 44.1 to 48 kHz. That is
too slow for the audio thread and irrelevant on the loader thread. (This is a teaching resampler;
production libraries use polyphase filters - same mathematics, different bookkeeping.)

### The demo song as data

`engine/include/dawlab/engine/DemoSong.h` describes the same 8-track song that `l14_render_song`
(Step 9) builds by hand, but as data: `makeDemoSong(sampleRate)` generates all audio and the clip
lists, the insert list (effect type + parameters *by name*), faders, pans and sends - a pure
function, run on the loader thread (it allocates about 40 MB at 48 kHz). The message thread then
calls `Session::applyDemoSong(song)`, which puts every insert into the next free GUI slot of its
strip and sends everything to the engine. `applyDemoSong(engine, song)` in the same header does
the same without a GUI (for console programs).

Because the song is generated at the engine's sample rate, a 44.1 kHz device gets a 44.1 kHz
song - no resampling needed. If the device changes while the song is being generated, the result
is thrown away and generated again. Files get the same protection: `loadAudioFile()` asks for the
session's *clip rate* (`Session::getClipSampleRate()`, normally the engine's rate, Step 11), and
its callback compares `media.targetSampleRate` with the clip rate when the result arrives. If the
device changed the rate in the meantime, the file is simply read again for the new rate - otherwise
audio at the old rate would end up in the project and play at the wrong pitch forever.

## Code

- `app/Source/MediaLoader.h/.cpp`: `resampleBuffer()`, `MediaLoader::readAudioFile()` (the
  synchronous part, usable from tests), `loadFile()`, `loadDemoSong()`, `convertBuffers()` (the
  same resampler on the loader thread, used when the device's sample rate changes, Step 11).
- `app/Source/Session.cpp`: `setTrackAudio()`, `applyDemoSong()`, `applySampleRateConversion()`,
  `handleSampleRateChange()` (Step 11).
- `app/Source/MainComponent.cpp`: `chooseAudioFileForTrack()`, `loadAudioFile()`,
  `loadDemoSongAsync()` (startup and Ctrl+D), `loadDemoSongNow()` (synchronous, for tests).

## Verify

`l14_app_smoke_test`, section 1:

```
1. resampler: 1 kHz 44.1k->48k amplitude 0.5000 (0.5 expected, -0.000 dB), length 48000;
   20 kHz 48k->22.05k alias at 2050 Hz: -122.2 dB below the input
   22.3 kHz 48k->44.1k (transition band) alias at 21800 Hz: -54.3 dB relative to the input
   file 44.1 kHz mono -> 2 channels, 48000 samples at 48 kHz, 1 kHz amplitude 0.5000
```

- 1 s at 44.1 kHz becomes exactly 48 000 samples, and a 1 kHz sine keeps its amplitude
  (passband error < 0.05 dB).
- A 20 kHz sine cannot exist at 22.05 kHz (Nyquist 11.025 kHz). Without the lowpass it would fold
  to $`22050 - 20000 = 2050`$ Hz; measured with Goertzel at 2050 Hz it is 122 dB below the input
  (the check requires more than 60 dB).
- 22.3 kHz converted from 48 to 44.1 kHz lies in the kernel's transition band, just above the new
  Nyquist frequency of 22.05 kHz: it folds to $`44100 - 22300 = 21800`$ Hz at -54.3 dB. That is the
  honest limit of this kernel (the check requires -50 to -80 dB, so a change of the kernel shows up).
- `MediaLoader::readAudioFile()` - the loader thread's function - reads a mono 44.1 kHz WAV (written
  by the test) and returns a stereo buffer of exactly 48 000 samples at 48 kHz with the tone intact.

Section 2 loads the demo song through `Session::applyDemoSong()` (the GUI's path): 19 inserts, 0
unknown parameter names, mix latency 752 samples - the same numbers as `l14_render_song`.

## Your turn

| | |
|---|---|
| You write | `resampleBuffer()` in `app/Source/MediaLoader.cpp`: output length, the cutoff $`f_c = 0.46 \min(1, f_{\text{out}}/f_{\text{in}})`$, the windowed-sinc kernel of "Resampling: band-limited interpolation" (keep `ResamplerSettings::kZeroCrossings` and `kCutoffScale` from `MediaLoader.h`) |
| Build + check | `.\scripts\build.ps1 -Only 14-daw-8track -Juce -TestLabel test` (`l14_app_smoke_test`) |
| Expected | section 1 prints the four resampler lines of this page: 1 kHz amplitude 0.5000 and length 48 000, the 20 kHz alias more than 60 dB (author: 122.2 dB) down, the 22.3 kHz transition-band alias between -50 and -80 dB (author: -54.3 dB); `[PASS] l14_app_smoke_test` |

## What you learned

- Slow work goes to a worker thread; results come back as immutable objects via the message
  thread; the audio thread only ever sees finished data.
- Resampling is filtering: the cutoff follows the lower Nyquist frequency, and a finite kernel
  needs a transition band - measure where it ends instead of assuming a brick wall.
- Describe content as data, so that one description can be applied by a GUI and by a test.
