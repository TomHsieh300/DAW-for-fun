# Step 9 - Offline render / bounce to WAV

**English** | [繁體中文](step-09-offline-render.zh-TW.md)

[Overview](../README.md) | Previous: [Step 8](step-08-pdc.md) | Next: [Step 10 - Real-time safety](step-10-realtime-safety.md)

## Goal

Render a song to a WAV file as fast as the CPU allows - "bounce" or "export mixdown" - with exactly
the sound of real-time playback, effect tails included, latency removed, and print a mix report
(peak, RMS, loudness per strip).

## Design

### Same code, different clock

An offline render is the playback loop without the sound card: the render loop calls
`processBlock()` one block after another, into consecutive sub-blocks of a big buffer. Because it is
the *same* function, "what you hear is what you bounce" holds by construction - provided the
engine does not depend on the block size (Step 10 tests that) and nothing depends on wall-clock
time.

```
applyPendingCommandsOffline()   clips and inserts queued during setup are linked in now
requestReset()                  tails, meters and smoothers start from zero
seek(start); play()
render songSamples              (the block that crosses the song end is cut there)
stop()
render tail + latency samples   tracks are silent, reverb / delay keep ringing, delayed audio flushes out
drop the first `latency` samples
restore the transport           the user's play position and loop survive the bounce
```

### Length bookkeeping

With song length $N_s$, tail $N_t = \operatorname{round}(T_{\text{tail}} f_s)$ and mix latency $L$:

$$
N_{\text{render}} = N_s + N_t + L, \qquad \text{file} = \text{render}[L,\; L + N_s + N_t)
$$

so file sample 0 is timeline sample `startSample`, and the file is exactly $N_s + N_t$ long.
For the chapter's song: 8 bars at 120 BPM = $8 \cdot 96\,000 = 768\,000$ samples, 3 s tail = 144 000,
latency 752, so 912 752 samples are rendered and 912 000 written (19.00 s).

### Thread rules

The render runs on the calling thread and plays the role of the audio thread. It must not run
while an audio device also calls `processBlock()` on the same engine. A GUI app either stops the
device during the bounce, or builds a second `Engine` from the project (Exercise 4) and bounces that
on a worker thread while playback continues.

### The mix report

Because `requestReset()` cleared every meter, after the render each strip's exact statistics
(`ChannelStrip::getStats()`: max peak, RMS over the whole render, integrated LUFS from chapter 11's
`LoudnessMeter`) describe exactly this bounce.

## The code

`Renderer.h`:

```cpp
struct RenderOptions {
    int64_t startSample = 0;        // timeline position to start from
    int64_t numSamples = 0;         // 0 = up to the end of the last clip
    double tailSeconds = 0.0;
    int blockSize = 512;
    bool resetBeforeRender = true;
    bool compensateLatency = true;
    bool restoreTransport = true;
};
AudioBuffer renderOffline(Engine&, const RenderOptions&);
bool bounceToWav(Engine&, const std::string& path, const RenderOptions&, int bitDepth = 24,
                 AudioBuffer* renderedOut = nullptr);
```

`renderOffline()` calls `engine.collectGarbage()` after every block, so a render with commands in
flight never fills the garbage queue.

### The song (`examples/render_song.cpp`)

| Track | Material (Signals.h) | Inserts | Fader / pan | Sends |
|---|---|---|---|---|
| 1 Drums | `makeDrumLoop` 2 bars x 4 clips | TransientShaper, Compressor | -2 dB / C | reverb -18 |
| 2 Bass | `makePluck` A1 F1 C2 G1, 16 clips | ParametricEQ (low cut 35 Hz, +3 dB @ 120 Hz), Saturation | -3 / C | - |
| 3 Arp | `makePluckSequence` from bar 3, 1-bar fade-in | Gain -3, Phaser | -2 / L 0.35 | reverb -14, delay -8 |
| 4 Pad | detuned harmonic chords Am F C G | ParametricEQ (150 Hz low cut, 5 kHz high cut), Chorus | -4 / C | reverb -8 |
| 5 Wind | low-passed noise, bars 5-8 | Tremolo (8 Hz square), Gate | -10 / R 0.5 | reverb -12 |
| 6 Lead | vibrato sine melody, bars 5-8 | Distortion (fuzz), Flanger | -5 / R 0.2 | reverb -12, delay -6 |
| 7 Shaker | high-passed noise bursts, 16ths | Expander, DeEsser | -8 / R 0.6 | reverb -20 |
| 8 Hits | noise bursts on bars 1, 3, 5, 7 | ConvolutionReverb | -6 / L 0.6 | - |
| Bus A | | Reverb (100 % wet, 2.2 s) | -2 | |
| Bus B | | Delay (100 % wet, dotted 1/8 = 375 ms, ping-pong) | -4 | |
| Master | | Compressor (2:1 at -12 dB), Limiter (-1 dBFS, 5 ms) | 0 | |

17 effect types appear once or more - all of them except Filter and AmpChain, which were added to
the catalog (Step 5) after this song was written.

## Your turn

`steps/step09-offline-render/minidaw.h` is your Step 8 engine plus `Engine::requestReset()`,
`Engine::applyPendingCommandsOffline()`, `Mixer::getSongEndSample()`, `RenderOptions` (without
the finished engine's `resetBeforeRender` / `restoreTransport` switches: the snapshot always resets
and restores) and `bounceToWav()`. This is the complete mini engine.

| | |
|---|---|
| You write | `renderOffline()`: length bookkeeping, save / restore the transport, reset, render the song (cut at its end), stop, render tail + latency, drop the first `latency` samples |
| File | `steps/step09-offline-render/starter/step09.cpp` |
| Run | `l14_step09_starter`; it prints `[FAIL] l14_step09 (11 of 14 checks failed)` until you are done |
| Solution | `steps/step09-offline-render/solution/step09.cpp` (ctest `l14_step09_solution`) |

The test song is 1 s long (impulses of 0.5 and 0.25 at samples 0 and 24 000 on track 1) with a
240-sample delay on the master standing in for a lookahead limiter. Expected output (verified):

```
Step 9: offline render (build-along)
  rendered 72000 samples (expected 48000 + 24000 = 72000), latency 240 (expected 240)
  file[0] = 0.3536, file[24000] = 0.1768 (expected 0.3536, 0.1768: the latency is gone)
  transport after the bounce: position 12345, loop on, stopped (expected 12345, on, stopped)
  wrote dawlab_output/l14_step09.wav: 72000 samples at 48000 Hz
  bounce vs live playback shifted by the latency: max difference 0 (expected 0)
  render from 24000: 24000 samples (expected 24000), file[0] = 0.1768 (expected 0.1768)
  tail: 60000 samples (expected 60000), the echo at 52900 = 0.3536 (expected 0.3536)
[PASS] l14_step09 (14 checks passed)
```

The tail line uses an uncompensated 5000-sample delay on a clip that ends at the song end: its
"echo" sounds 5000 samples later, inside the tail, because the inserts keep running after the
transport stopped. The check also renders with 37-sample blocks and requires the identical file.

## Verify (reference route)

```powershell
cd build; .\bin\Debug\l14_render_song.exe
```

Report from the author's run (Debug build; exact LUFS values depend on the chapter 01-12
implementations, the structure and the limiter's -1.00 dBFS do not):

```
Step 9: offline bounce of an 8-track song (8 bars at 120 BPM + 3 s tail)
  length 19.00 s, rendered in 13.14 s (1.4x real time), mix latency 752 samples (15.67 ms)

  strip     peak     RMS     LUFS   fader   pan    inserts
            dBFS    dBFS      (I)
  Drums     -3.3   -27.4    -24.8    -2.0 dB  0.00   TransientShaper, Compressor
  Bass      -8.9   -22.6    -16.8    -3.0 dB  0.00   ParametricEQ, Saturation
  Arp      -11.9   -33.9    -27.0    -2.0 dB -0.35   Gain, Phaser
  Pad      -19.6   -33.5    -30.5    -4.0 dB  0.00   ParametricEQ, Chorus
  Wind     -20.3   -42.3    -34.7   -10.0 dB  0.50   Tremolo, Gate
  Lead     -12.6   -27.7    -21.4    -5.0 dB  0.20   Distortion, Flanger
  Shaker   -17.9   -47.1    -38.8    -8.0 dB  0.60   Expander, DeEsser
  Hits     -16.6   -46.0    -31.9    -6.0 dB -0.60   ConvolutionReverb
  Reverb   -19.6   -36.9    -33.8    -2.0 dB  0.00   Reverb
  Delay    -20.7   -39.7    -34.3    -4.0 dB  0.00   Delay
  Master    -1.0   -20.2    -15.5     0.0 dB  0.00   Compressor, Limiter

  file: peak -1.00 dBFS, RMS -20.2 dBFS, integrated loudness -15.5 LUFS
  wrote dawlab_output/l14_render_song.wav
  drums solo: peak -1.56 dBFS, wrote dawlab_output/l14_render_song_drums_solo.wav
  silent strips in the solo bounce: Bass -100 dBFS, Arp -100 dBFS (silenced by the solo)
[PASS] l14_render_song (7 checks passed)
```

The self-checks: no NaN; length exactly 768 000 + 144 000 samples; file peak $\le$ -0.95 dBFS (the
limiter's ceiling); the song is not silent; the solo bounce silences the other strips.

How to read the report: the bass has the highest loudness of the tracks (-16.8 LUFS) although its
peak is moderate - low, sustained energy. The shaker's peak-to-RMS ratio (crest factor) is almost
30 dB: short bursts. The master's integrated loudness of -15.5 LUFS is in the range of a typical
streaming target (-14 LUFS); its RMS of -20.2 dBFS with -1 dBFS peaks is a crest factor of 19 dB -
the master compressor and limiter work gently.

The same song also exists as *data* in `engine/include/dawlab/engine/DemoSong.h`
(`makeDemoSong()` + `applyDemoSong()`), which the stage-2 app loads on a worker thread (Step 18).
`l14_test_demo_song` bounces it and checks that it reproduces this report: 912 000 samples,
752 samples of latency, peak -1.00 dBFS, master -15.5 LUFS, drums -3.3 / -27.4 dBFS, bass
-16.8 LUFS, and that every parameter name in the data exists in its effect.

Listen to `l14_render_song.wav`. The tails at the end ring out for 3 s: the reverb bus, the
ping-pong delay bouncing between left and right, the convolution room of the hits.

## What you learned

- A bounce is the playback loop with the clock taken away from the device - nothing more.
- Reset before, render the tail, render the latency, cut the latency away, restore the transport.
- The meters of chapter 11 turn a render into a numeric mix report.
