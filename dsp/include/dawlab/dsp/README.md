# dawlab::dsp
**English** | [繁體中文](README.zh-TW.md)

The DSP components implemented step by step across the chapters: header-only, in `namespace dawlab::dsp`, depending only on the standard library and the shared utilities in `common/` (**no JUCE dependency**), so the offline examples, the chapter 13 VST3 plugins and the chapter 14 DAW engine all share the same code.

- The common interface (`prepare` / `reset` / setters / `processSample` / `process`), the real-time rules and the API contract of each class are in sections 4 and 5 of [docs/authoring-guide.md](../../../../docs/authoring-guide.md).
- To use them, link `dawlab::dsp` in CMake (it brings in `dawlab::common` as well), then `#include <dawlab/dsp/Xxx.h>` in your code.
- The theory, derivations and line-by-line walkthrough of each header are in the README of the corresponding chapter.

## Listed by chapter

<div align="center">

| Chapter | Header | Purpose |
|---|---|---|
| [01](../../../../lessons/01-gain/README.md) | `SmoothedValue.h` | Linear-ramp smoothing of parameters, avoiding clicks and zipper noise when a parameter changes |
| 01 | `Gain.h` | A gain component set in dB or as a linear factor, with smoothing |
| 01 | `Panner.h` | Mono pan and stereo balance with three pan laws (linear / constant power / −4.5 dB) |
| [02](../../../../lessons/02-filter/README.md) | `OnePole.h` | First-order lowpass / highpass (the basis of DC blockers, smoothing and tone controls) |
| 02 | `Biquad.h` | Second-order IIR filter with RBJ Audio EQ Cookbook coefficients and Transposed Direct Form II |
| 02 | `StateVariableFilter.h` | TPT (zero-delay feedback) state variable filter that stays stable under fast cutoff modulation |
| [03](../../../../lessons/03-eq/README.md) | `ParametricEq.h` | 6-band parametric EQ (low cut, shelf, peak, high cut, notch) with parameter smoothing, and it can compute the whole curve for a UI |
| [04](../../../../lessons/04-compressor/README.md) | `EnvelopeFollower.h` | Peak / RMS envelope detector and attack / release smoother |
| 04 | `Compressor.h` | Feedforward, log-domain compressor (threshold, ratio, knee, makeup, stereo link) |
| [05](../../../../lessons/05-limiter/README.md) | `DelayLine.h` | Integer-sample circular-buffer delay line |
| 05 | `Limiter.h` | Lookahead brickwall limiter that guarantees the output sample peak never exceeds the ceiling |
| [06](../../../../lessons/06-saturation/README.md) | `Oversampler.h` | 2× / 4× / 8× polyphase halfband FIR oversampling (reduces aliasing from nonlinear processing) |
| 06 | `Saturation.h` | Waveshaper saturation (tanh / cubic / arctan / asymmetric) with built-in oversampling |
| [07](../../../../lessons/07-distortion/README.md) | `Distortion.h` | Five distortion types (hard clip, foldback, fuzz, rectify, bitcrush) with tone filter and mix |
| 07 | `AmpChain.h` | A simple guitar amp simulation chain (HPF → pre-EQ → preamp saturation → tone stack → power amp distortion → cabinet) |
| [08](../../../../lessons/08-gate-expander/README.md) | `Gate.h` | Noise gate with hysteresis, hold, range and sidechain |
| 08 | `Expander.h` | Downward expander (signal below the threshold is pushed down further according to the ratio) |
| 08 | `DeEsser.h` | Detects the sibilance band and attenuates it (wideband or split-band, with a monitorable detection signal) |
| 08 | `TransientShaper.h` | Boosts or cuts attack and sustain separately, based on the difference between a fast and a slow envelope |
| [09](../../../../lessons/09-delay/README.md) | `FractionalDelayLine.h` | Circular buffer supporting non-integer delays (linear / cubic Hermite / Lagrange interpolation) |
| 09 | `Delay.h` | Stereo echo: smoothed delay time, filtering inside the feedback loop, ping-pong, tempo sync |
| [10](../../../../lessons/10-modulation/README.md) | `Lfo.h` | Low-frequency oscillator (sine, triangle, saw, square, sample & hold) |
| 10 | `Chorus.h` | Multi-voice, LFO-modulated short delay with stereo spread |
| 10 | `Flanger.h` | Very short modulated delay with feedback, forming a sweeping comb filter |
| 10 | `Phaser.h` | Phaser built from cascaded first-order allpass filters with LFO-swept notches |
| 10 | `Tremolo.h` | LFO amplitude modulation, switchable to autopan |
| [11](../../../../lessons/11-metering-fft/README.md) | `Fft.h` | Radix-2 FFT / IFFT, real FFT and window functions |
| 11 | `LevelMeter.h` | Sample-peak meter (with peak hold and decay) and sliding-window RMS meter, with thread-safe getters |
| 11 | `LoudnessMeter.h` | ITU-R BS.1770 / EBU R128 loudness (momentary, short-term, integrated LUFS) |
| 11 | `SpectrumAnalyzer.h` | The audio thread pushes samples through a lock-free FIFO and the UI thread computes the smoothed spectrum |
| [12](../../../../lessons/12-reverb/README.md) | `ReverbBuildingBlocks.h` | Teaching-oriented reverb building blocks: `CombFilter`, `AllpassDiffuser`, `TappedDelay`, the RT60 measurement `measureRt60`, `nextPrime` |
| 12 | `Reverb.h` | 8-line FDN algorithmic reverb (decay, pre-delay, damping, size, width, freeze) |
| 12 | `ConvolutionReverb.h` | Uniform partitioned FFT convolution reverb, which can generate a synthetic impulse response |

</div>

Chapter 13 (JUCE plugins and hosting) and chapter 14 (the 8-track DAW) add no headers to this folder: the chapter 13 code is in `lessons/13-vst-hosting/plugin/` and `host/`, and the chapter 14 engine is in `lessons/14-daw-8track/engine/include/dawlab/engine/`.
