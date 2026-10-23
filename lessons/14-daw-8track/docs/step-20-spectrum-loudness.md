# Step 20 - Spectrum analyzer and loudness display
**English** | [繁體中文](step-20-spectrum-loudness.zh-TW.md)

[Overview](../README.md) | Previous: [Step 19](step-19-export.md) | Next: [Step 21](step-21-shortcuts-and-tests.md) | Back to the [roadmap](../README.md#4-the-twenty-one-steps)

## Goal

Next to the master strip: a live spectrum of the master output (20 Hz - 20 kHz, logarithmic) and
the EBU R 128 loudness - momentary, short-term, integrated - plus the mix latency. All of it from
chapter 11's classes, all of it fed by the audio thread without locks.

## Design

### Two measurement points, one rule

| display | measured by | where | how the GUI gets it |
|---|---|---|---|
| strip meters, master meter | `dsp::LevelMeter` in every `ChannelStrip` | audio thread, `ChannelStrip::meter()` | atomics, 30 Hz poll |
| LUFS M / S / I | `dsp::LoudnessMeter` in every `ChannelStrip` | audio thread | atomics, 30 Hz poll |
| spectrum | `dsp::SpectrumAnalyzer` in `AudioHost` | audio thread pushes samples | `computeSpectrum()` on the message thread |

The rule from chapter 11: the audio thread only *writes* (samples into a lock-free FIFO, results
into atomics); everything expensive or allocating happens on the reading side.

### The spectrum: push mono samples, compute on the GUI side

After `Engine::processBlock()` the device callback has the master output in the device buffer.
`AudioHost::render()` forms the mono sum $`m[n] = \tfrac12(L[n] + R[n])`$ in a pre-allocated
scratch buffer and calls `SpectrumAnalyzer::pushSamples(m, n)` - a copy into the analyzer's ring
buffer. On the message thread `SpectrumView::update()` calls `computeSpectrum(magnitudesDb)`, which
windows the newest 4096 samples, runs the FFT of chapter 11 and returns $`N/2 + 1 = 2049`$ bins in
dB (or `false` when no new data arrived, so a stopped transport costs nothing). With
`prepare(sampleRate, 12)` the FFT has $`2^{12} = 4096`$ points: the bin spacing is
$`f_s / N = 48000 / 4096 = 11.7`$ Hz, enough to separate the bass notes (A1 = 55 Hz, F1 = 43.7 Hz).

### Drawing on a log axis

Musicians hear octaves, so the x axis is logarithmic:

$$
x(f) = \frac{\ln(f / 20)}{\ln(20000 / 20)}, \qquad f(x) = 20 \cdot 1000^{x}
$$

At the low end one pixel covers less than one bin (below about 480 Hz for the 285-pixel-wide view
in the snapshot, README section 3.11); at the high end one pixel covers about 20 bins.
`SpectrumView::paint()` therefore walks the pixel columns, takes the **maximum** of all bins whose
frequency falls into the column (so a narrow peak is never lost between pixels), and reuses the
next bin above where a column contains none - which is why the trace looks like a staircase at the
low end. Display ballistics on top of the analyzer's own
smoothing: a value may rise immediately but fall by at most 1.5 dB per frame (45 dB/s at 30 Hz) -
calm, but never late on transients. The y axis spans 0 .. -90 dB with lines every 20 dB. The labels (0 dB at the top edge, then -20,
-40, ... and the frequencies) are drawn last, on a dark backing, so the trace never hides them.

### Loudness

The master strip's `dsp::LoudnessMeter` (BS.1770 K-weighting, 400 ms momentary, 3 s short-term,
gated integrated) publishes three atomics. `LoudnessDisplay` shows them with one decimal ("-inf"
below -70 LUFS, the absolute gate). It repaints only when a value changed by 0.05 LU (loudness units; 1 LU is a difference of 1 dB in LUFS). The
integrated value accumulates from the last engine reset (new project, demo song, export).
Streaming platforms normalize to about -14 LUFS; the demo song's master sits around -15.5 LUFS
integrated (the stage-1 mix report, Step 9).

### Mix latency

Below the loudness the mixer shows `Engine::getLatencySamples()` in samples and milliseconds -
752 samples = 15.67 ms for the demo song (512 for the convolution reverb on track 8 + 240 for the
master limiter's 5 ms lookahead). Insert a plugin with latency (Step 17) and watch it grow. If the
inserts on one track or bus add up to more than the PDC limit (16 384 samples, Step 8), the line
turns red and adds "PDC limit exceeded, not aligned!" (`Engine::hasPdcOverflow()`).

## Code

- `app/Source/AudioHost.cpp`: the analyzer feed in `render()`, `prepareInternal()` (analyzer
  prepared with FFT order 12 while the device is stopped; `isAnalyzerReady()` guards the GUI side).
- `app/Source/Meters.h/.cpp`: `MeterBar`, `LoudnessDisplay`, `SpectrumView`
  (`frequencyToProportion`, `proportionToFrequency`, `update`, `paint`).
- `app/Source/MixerView.cpp`: layout of the master section, `update()`.

## Verify

- `l14_app_smoke_test`, section 8, checks the display path with numbers: a sine exactly on bin 85
  of the 4096-point FFT ($`85 \cdot 48000 / 4096 = 996.09`$ Hz) at amplitude 0.5 must appear in
  `SpectrumView` at that frequency and at $`20 \log_{10} 0.5 = -6.02`$ dBFS (the analyzer divides by
  the window's coherent gain, chapter 11), and the log axis must put the geometric mean of the
  two ends, $`\sqrt{20 \cdot 20000} = 632.5`$ Hz, exactly in the middle:

  ```
  8. spectrum view: 996.09 Hz sine at 0.5 -> peak at 996.09 Hz, -6.02 dBFS (-6.02 expected); 632.5 Hz at x = 0.500
  ```

  It also checks `isAnalyzerReady()` after the device-callback simulation (section 4).
- The snapshot (Step 12) shows the spectrum after 1.5 bars of the demo song. Only drums, bass,
  pad and the reverb tail of the first hit play at that point (the arp and the shaker start at
  bar 3): energy from 40-50 Hz (kick and bass) up to about 2 kHz, then falling towards 10 kHz
  where only the drum loop's noisy hats and snare remain. The snapshot prints the loudness it
  displays: M -17.2, S -15.8, I -15.9 LUFS at bar 2 beat 3 (these figures depend on the DSP
  implementations of chapters 01-12).
- In the app, solo the bass: the spectrum collapses to below 300 Hz; solo the shaker: only highs.

## Your turn

| | |
|---|---|
| You write | `SpectrumView::frequencyToProportion()` and `proportionToFrequency()` in `app/Source/Meters.cpp` (the log axis between 20 Hz and 20 kHz, clamped at both ends) |
| Build + check | `.\scripts\build.ps1 -Only 14-daw-8track -Juce -TestLabel test` (`l14_app_smoke_test`) |
| Expected | section 8: `peak at 996.09 Hz, -6.02 dBFS (-6.02 expected); 632.5 Hz at x = 0.500`; `[PASS] l14_app_smoke_test`; in the snapshot the spectrum's grid lines (100 Hz, 1 kHz, 10 kHz) sit where they were |

## What you learned

- Measurements are produced on the audio thread and *displayed* elsewhere; the hand-over is a
  FIFO of samples or a set of atomics.
- A log-frequency display needs per-pixel aggregation (max), not per-bin drawing.
- Ballistics are part of a meter's design, not an afterthought.
