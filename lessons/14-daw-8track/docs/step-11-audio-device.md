# Step 11 - The audio device and the callback

[Overview](../README.md) | Previous: [Step 10](step-10-realtime-safety.md) | Next: [Step 12](step-12-main-window.md) | Back to the [roadmap](../README.md#4-the-twenty-one-steps)

Stage 2 starts here: from now on we build **DawLab 8**, a JUCE application around the engine of
Steps 1-10. The first thing a DAW needs is sound coming out of the speakers.

## Goal

Open the system's default audio output, and on every buffer the device asks for, call
`Engine::processBlock()` - with **no copy** between JUCE's buffers and our `AudioBlock`, and with
nothing in the callback that could block. Measure how much of the available time we use (the
"CPU" display), and keep the same path usable without any device for tests.

## Design

### Who calls whom

```
 audio driver (WASAPI / ASIO / DirectSound)          high-priority thread, every N / fs seconds
        |
 juce::AudioDeviceManager  ---- owns the AudioIODevice, mixes registered callbacks
        |
 AudioHost : juce::AudioIODeviceCallback
        |   audioDeviceAboutToStart(device)   -> Engine::prepare(fs, N)   (no callback running)
        |   audioDeviceIOCallbackWithContext  -> Engine::processBlock(AudioBlock(outputs, 2, N))
        |   audioDeviceStopped()
        v
 dawlab::engine::Engine (stage 1)
```

`AudioDeviceManager::initialise(numInputs, numOutputs, nullptr, true)` picks the default device
type and device and opens it at the device's preferred sample rate and buffer size. Adding our
callback (`addAudioCallback`) makes JUCE call `audioDeviceAboutToStart()` and then, from the
driver's thread, the I/O callback.

### Zero-copy: JUCE's channel array *is* an AudioBlock

JUCE hands us `float* const* outputChannelData`: one pointer per channel, non-interleaved. That is
exactly the layout `dawlab::AudioBlock` views (chapter 00). So the whole adapter is one line:

```cpp
engine_.processBlock(AudioBlock(outputs, std::min(numOutputChannels, 2), numSamples));
```

The engine renders straight into the driver's memory. Devices with more than two outputs get
silence on the others (we must write every channel: the buffer may contain garbage); a mono
device gets the left channel (the engine supports a 1-channel `out`).

### The two moments that matter

| moment | thread | what may happen there |
|---|---|---|
| `audioDeviceAboutToStart` | the thread that starts the device (normally the message thread) | allocation: `Engine::prepare()` (which also re-prepares every insert, Step 1), `SpectrumAnalyzer::prepare()` (Step 20) |
| `audioDeviceIOCallbackWithContext` | the driver's real-time thread | only `processBlock()`, a few atomics, no allocation / lock / I/O |

If the sample rate or buffer size does not change (for example when the export re-attaches the
engine, Step 19), `prepareInternal()` skips `Engine::prepare()`: re-preparing would reset every
effect and cut reverb tails for no reason.

When the device *does* come back at another sample rate, everything measured in samples is wrong:
clip positions and lengths, fades, the loop range - and the clip audio itself, which now plays at
the wrong speed and pitch (48 -> 44.1 kHz: 8 % slower, 1.47 semitones lower). Every clip source must
be resampled once (shared buffers stay shared) and every position scaled by
$f_{\text{new}} / f_{\text{old}}$. Resampling is slow - a few seconds for a 3-minute stereo file,
even in a Release build - so it must neither run on the message thread (the GUI would freeze) nor
leave the engine playing the old clips meanwhile:

```
device thread (audioDeviceAboutToStart)      message thread                     loader thread
Engine::prepare(newRate)
onEnginePrepared(old, new):
  transport.stop()  (remember "was playing")
  callAsync -------------------------------> scaleTransportPositions(old, new)
                                             startClipConversion():
                                               loader_.convertBuffers(sources) -> resampleBuffer() x N
                                             finishClipConversion()  <--callAsync--+
                                               Session::applySampleRateConversion():
                                               swap sources, scale positions, setTrackClips
                                               transport.play() if it was playing
```

`Session` tracks the *clip rate* (`getClipSampleRate()`): the rate the clip lists are expressed
in. It equals the engine's rate except during such a conversion. While the conversion runs, Space
only arms "play when done" and an export is refused, so nothing ever plays old-rate clips at the new
rate. If something changed while the loader worked (another device change, a new project, the demo
song), `finishClipConversion()` throws the result away and starts again from the current state; a
file that finished loading meanwhile (at the old clip rate, Step 18) is converted in one more
round. `Session::handleSampleRateChange(old, new)` is the synchronous version of the whole thing
for code that has no message loop or knows the clips are at `old` (tests, Exercise 11).

### CPU load

The callback measures its own duration with `juce::Time::getHighResolutionTicks()`:

$$
\text{load} = \frac{t_{\text{callback}}}{N / f_s}, \qquad
\overline{\text{load}}_k = \overline{\text{load}}_{k-1} + 0.1\,(\text{load}_k - \overline{\text{load}}_{k-1})
$$

The one-pole smoothing (coefficient 0.1, about 10 callbacks) keeps the display readable. 100 %
means "the deadline is hit exactly"; anything close to it will drop out on the next hiccup. The
value is an `std::atomic<float>`: the transport bar reads it 30 times per second.

### Denormals

`juce::ScopedNoDenormals` at the top of the callback sets the FTZ/DAZ flags (flush-to-zero /
denormals-are-zero: the CPU treats tiny denormal numbers as 0) of the CPU for the
duration of the callback, so decaying filter states and reverb tails never slow down to
denormal arithmetic (chapters 02 and 12 explain why that matters).

## Code

`app/Source/AudioHost.h`, `AudioHost.cpp`. The interesting function in full:

```cpp
void AudioHost::render(float* const* outputs, int numOutputChannels, int numSamples) noexcept {
    if (numOutputChannels <= 0 || numSamples <= 0)
        return;
    const juce::ScopedNoDenormals noDenormals;
    const auto t0 = juce::Time::getHighResolutionTicks();

    const int engineChannels = std::min(numOutputChannels, engine::kNumChannels);
    engine_.processBlock(AudioBlock(outputs, engineChannels, numSamples));
    for (int ch = engineChannels; ch < numOutputChannels; ++ch)
        if (outputs[ch] != nullptr)
            std::fill_n(outputs[ch], numSamples, 0.0f);

    // ... push (L+R)/2 into the spectrum analyzer (Step 20) ...
    // ... CPU load, smoothed, into an atomic ...
}
```

`audioDeviceIOCallbackWithContext()` and `processOffline()` both call `render()`. That is the
point of `processOffline()`: the smoke test, the `--snapshot` mode and the exercises drive *the
same function* the driver drives, from the main thread, with JUCE-shaped channel arrays.

Opening the device (`openDefaultDevice`) and the fallback (`MainComponent` constructor):

```cpp
juce::String deviceError;
if (mode_ == Mode::Live)
    deviceError = audioHost_.openDefaultDevice();          // -> aboutToStart -> Engine::prepare
if (mode_ == Mode::Offline || deviceError.isNotEmpty())
    audioHost_.prepareOffline(kOfflineSampleRate, kOfflineBlockSize); // 48 kHz, 512
```

A machine without an audio output still starts the app ("No audio device ... running offline").

## Verify

`DawLab 8.exe --smoke-test` (ctest `l14_app_smoke_test`) plays the demo song through
`AudioHost::processOffline()` with **three** output channels and compares it with the offline
bounce of Step 9:

```
4. device callback, 512-sample blocks, 3 outputs: max |callback - bounce| = 0, CPU load 68.1%
4. device callback, 480-sample blocks, 3 outputs: max |callback - bounce| = 0, CPU load 81.5%
```

- The difference is exactly 0 for both block sizes (the bounce's last `latency` samples are
  excluded: the renderer plays them with the transport already stopped). So the callback path
  is zero-copy *and* adds nothing: what you hear is what you bounce.
- The third channel, pre-filled with garbage by the test, is all zeros afterwards.
- The CPU figure is from a Debug build rendering all 19 inserts of the demo song as fast as it
  can; it says "this Debug build needs roughly 60-80 % of real time" - the demo song's convolution
  reverb and oversampled effects dominate, and the figure varies from run to run and machine to
  machine. A Release build needs a fraction of that.

Section 7 of the same test simulates a device restart at 44.1 kHz: `Engine::prepare(44100, 512)`
followed by `Session::handleSampleRateChange(48000, 44100)`:

```
7. device change 48 -> 44.1 kHz: bass clip 3 moved 96000 -> 88200 samples (bar 2 = 88200)
```

Section 10 does the same through the application's own wiring, while the transport plays -
`AudioHost::prepareOffline(44100, 512)` on a `MainComponent`, then the message loop delivers
`onEnginePrepared`'s `callAsync`, the loader thread converts, and the test polls until
`MainComponent::isConvertingSampleRate()` is false:

```
10. GUI device change 48 -> 44.1 kHz (loader thread): engine 44100 Hz, transport stopped at once: yes, bass clip 3 96000 -> 88200,
    converted in 5010 ms with 457 message-loop turns meanwhile, playback resumed: yes, status "Sample rate changed to 44100 Hz: clips converted"
```

The clip that started at bar 2 still starts at bar 2 ($96000 \cdot 44100 / 48000 = 88200$), its
1-second source buffer now has 44 100 samples, and the converted song renders without NaN. The
transport stopped the moment the device restarted and started again after the conversion; the
message loop kept running all the time (the number of turns and the milliseconds depend on the
machine; this is a Debug build converting the demo song's 28 buffers).

To hear it: start `DawLab 8.exe` (no arguments), press Space.

## Your turn

From this step on there are no snapshots: a GUI cannot be split into ten small console programs.
Instead you rebuild one function of the finished app at a time and let the app's own automated
test judge it (see "How to use this chapter" in the [chapter README](../README.md)). Copy the file
first (`AudioHost.cpp` -> `AudioHost.cpp.orig`) so that you can compare and go back.

| | |
|---|---|
| You write | the body of `AudioHost::render()` in `app/Source/AudioHost.cpp`, up to the spectrum feed: wrap the device's pointer array in an `AudioBlock` of at most `engine::kNumChannels` channels, call `engine_.processBlock()`, clear every further device channel (keep the spectrum and CPU-load code below it as it is) |
| Build + check | `.\scripts\build.ps1 -Only 14-daw-8track -Juce -TestLabel test` (builds `DawLab8` and runs `l14_app_smoke_test`) |
| Expected | section 4 prints `max |callback - bounce| = 0` for the 512- and 480-sample blocks, the third output channel is checked to be all zeros, and the run ends with `[PASS] l14_app_smoke_test` |

Then start `DawLab 8.exe` without arguments and press Space: you hear your callback.

## What you learned

- JUCE's `AudioIODeviceCallback` is the host-side twin of `Engine::processBlock()`; the adapter is
  a view, not a copy.
- `audioDeviceAboutToStart()` is the one place, besides construction, where the audio side may
  allocate - the device is stopped.
- A device change is a sample-rate change for the whole project: everything in samples must be
  converted - on a worker thread, with the transport stopped until the converted clips are in.
- Keep a device-free path to the same render function, so tests can prove what the device would
  play.
