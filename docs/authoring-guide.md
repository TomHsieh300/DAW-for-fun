# Authoring Guide
**English** | [繁體中文](authoring-guide.zh-TW.md)

This document defines the structure of each chapter, the code style and the API contract. Follow it whenever you add a chapter or an exercise, so that the whole course stays consistent.

## 1. Audience and language

- Audience: readers who know basic C++ (classes, templates, `std::vector`, lambdas) but have **not studied DSP**, and whose goal is to be able to write a DAW themselves.
- The course is **bilingual (English / Traditional Chinese), and English is the primary language**:
  - The English version uses the original file name (for example `README.md`, `exercises/README.md`). Code comments and `printf` / `std::cout` output are always written in natural English.
  - The Traditional Chinese version lives in the same folder under the same name with `.zh-TW` added (for example `README.zh-TW.md`, `exercises/README.zh-TW.md`). It must stay in sync with the English version, and it must not contain Simplified Chinese characters.
  - Every Markdown file has a language switcher line right after its first line (the title): English files use `**English** | [繁體中文](xxx.zh-TW.md)` and Chinese files use `[English](xxx.md) | **繁體中文**`. Links in an English file point to other English files; links in a Chinese file point to the corresponding `.zh-TW.md` files. Links to code, images and LICENSE files are the same in both versions.
  - In the Chinese version, proper nouns and technical terms stay in English instead of being translated literally: sample rate, buffer, block, filter, cutoff, Q, biquad, gain, dB, envelope, attack, release, threshold, ratio, knee, lookahead, aliasing, oversampling, waveshaper, plugin, host, callback, thread, lock-free, and so on. On first use you may add a Chinese explanation in parentheses.
  - Program output quoted in the Chinese version stays in the English the program actually prints. Output quoted in a README must always be what the program really prints when run.
  - **Chapter 14 is no longer a special case**: all code in the course (comments, UI text, output) is in English, and every chapter follows the same rules. The PASS/FAIL output of `Check.h` is also in English.
- Identifiers in code are in English.
- Math uses the LaTeX supported by GitHub: inline math as ``$`...`$`` and display math in a fenced ` ```math ` block (both keep the TeX away from the Markdown parser, which would otherwise eat backslashes such as `\\`, `\,` and `\;` or pair underscores across formulas). Inside a list item GitHub shows a ` ```math ` block as plain code, so a display formula there is written on its own line as ``$`\displaystyle ...`$``. Avoid macros GitHub rejects, such as `\operatorname` (use `\mathrm`).
- Diagrams: signal flows, block diagrams, state machines and sequences are ` ```mermaid ` blocks (labels in the edition's language); curves (responses, transfer curves, envelopes, waveforms) are SVG figures computed by the chapter's `images/make_figures.py` with `docs/tools/plotstyle.py`. Never draw diagrams with box-drawing characters, arrows or CJK text in code blocks: they misalign on GitHub. `python docs/tools/check_mermaid.py` checks that every Mermaid block renders.
- Layout: an image alone on its line is centered, `<p align="center"><img src="images/name.svg" alt="..."></p>`, and a top-level table is wrapped in `<div align="center">` (with a blank line after the opening and before the closing tag, so GitHub still parses the table as Markdown). Display math in ` ```math ` and Mermaid diagrams are centered by GitHub already; code blocks always span the full width. SVG text uses a sans-serif font list ending in the generic `sans-serif` (set in `plotstyle.py`), so every browser renders it without serifs.
- Mermaid labels: write subscripts and superscripts with `<sub>`/`<sup>` (`z<sup>−1</sup>`, `s<sub>i</sub>`), since `_` and `^` show up literally; a node whose whole label is `+`, `-` or `*` is parsed as a Markdown list, so write the sign as an entity code (`(("#43;"))`).

## 2. Directory structure

<div align="center">

| Path | Contents |
|---|---|
| `common/include/dawlab/` | Shared utilities (already provided; do not change the API) |
| `common/include/dawlab/AudioBuffer.h` | `AudioBlock` / `AudioBuffer` (non-interleaved float) |
| `common/include/dawlab/WavFile.h` | `readWav` / `writeWav` / `outputPath` |
| `common/include/dawlab/Signals.h` | `makeSine` / `makeLogSweep` / `makeWhiteNoise` / `makeImpulse` / `makeToneBursts` / `makeDrumLoop` / `makePluck` / `makePluckSequence` / `NoiseGenerator` |
| `common/include/dawlab/Analysis.h` | `peakAbs` / `rms` / `hasNanOrInf` / `goertzelAmplitude` / `measureGainDb` / `harmonicLevelsDb` / `thdPercent` |
| `common/include/dawlab/Check.h` | `DAWLAB_CHECK` / `DAWLAB_CHECK_NEAR` / `dawlab::test::summary` |
| `common/include/dawlab/DspMath.h` | `kPi` / `kTwoPi` / `dbToGain` / `gainToDb` / `lerp` / `msToSamples` |
| `common/include/dawlab/Common.h` | Includes all of the above at once |
| `dsp/include/dawlab/dsp/` | DSP components that grow chapter by chapter (header-only, namespace `dawlab::dsp`) |
| `lessons/NN-name/README.md` | Theory notes (the main text); README.zh-TW.md is the Traditional Chinese version |
| `lessons/NN-name/CMakeLists.txt` | The chapter's build file |
| `lessons/NN-name/examples/*.cpp` | Example programs (each one runs on its own, ctest label: example) |
| `lessons/NN-name/exercises/README.md` | Exercises (problems and hints only; README.zh-TW.md alongside) |
| `lessons/NN-name/exercises/starter/*.cpp` | Starter code for the programming exercises (compiles, contains TODOs; built but not run as a test) |
| `lessons/NN-name/solutions/README.md` | Answers and explanations for all exercises, including the conceptual and calculation ones (README.zh-TW.md alongside) |
| `lessons/NN-name/solutions/*.cpp` | Complete solutions to the programming exercises (with self-checks, ctest label: solution) |
| `lessons/NN-name/tests/*.cpp` | (Optional) regression tests for this chapter's dsp components (ctest label: test) |

</div>

## 3. Structure of each chapter README.md

1. **Chapter goals**: what the reader can do after finishing (as a list).
2. **Theory**: from intuition, to math, to diagrams. Derive the key formulas instead of just stating the result, and explain what each parameter means to the ear.
3. **From formula to code**: a walkthrough of the headers this chapter adds under `dsp/include/dawlab/dsp/`, and what each example demonstrates, how to run it, which WAV files it writes and what you should hear.
4. **Real-time considerations**: what to watch for when this chapter's components run on the audio thread (allocation, denormals, parameter smoothing, thread safety, ...).
5. **Common mistakes and debugging**: the pitfalls beginners hit most often, and their symptoms (pops, zipper noise, NaN, aliasing, ...).
6. **Exercises**: a link to `exercises/README.md`.
7. **Further reading**: books, papers, specifications (list only sources that really exist and that you are sure of).
8. **Next chapter**: one sentence linking to the next chapter.

## 4. Coding rules

### 4.1 DSP components (`dsp/include/dawlab/dsp/*.h`)

- Header-only, `#pragma once`, `namespace dawlab::dsp`, depending only on the standard library, the shared utilities under `dawlab/` (and the dsp headers of earlier chapters). **No JUCE dependency**, so that offline tests, plugins and the DAW can all share the same code.
- Common interface:

```cpp
class Xxx {
public:
    void prepare(double sampleRate, int maxBlockSize, int numChannels); // the only place allowed to allocate
    void reset() noexcept;                                              // clear internal state (filter memory, envelope, ...)
    void setSomething(float value) noexcept;                            // parameter setter: may be called between blocks
    float processSample(int channel, float x) noexcept;                 // provided only for per-sample effects
    void process(AudioBlock block) noexcept;                            // process a whole block in place
};
```

- `process()` must **not**: allocate / free, take a mutex, do file or console I/O, or throw an exception.
- Parameter changes must avoid zipper noise / clicks: use `SmoothedValue` (chapter 01) or coefficient interpolation.
- Recursive structures (IIR filters, envelopes) must handle denormals: zero them in `reset()`, and where necessary add a tiny offset or flush tiny values to 0.
- Measurements that the UI reads (for example gain reduction) use `std::atomic<float>` (relaxed ordering is enough).
- Comments explain "why", and the corresponding math is written next to the formula.

### 4.2 Example programs (`examples/*.cpp`)

- `#include <dawlab/Common.h>` and this chapter's dsp headers.
- **It must run with no arguments and exit successfully (return 0)**: generate the input signal with `Signals.h`. It may optionally accept `argv[1]` as an input WAV path.
- Write the "before / after" WAV files for listening with `dawlab::writeWav(dawlab::outputPath("lNN_xxx.wav"), buffer, sampleRate)`, and print meaningful numbers with printf (peak, RMS, frequency-response table, gain reduction, ...).
- Default to a 48000 Hz sample rate and a block size of 512 (simulating an audio callback that processes one block at a time); do not process the whole signal in one go.
- Every program starts with a comment block: what it demonstrates, how to run it, which files it writes and what you should hear.

### 4.3 Exercises

- Each chapter has 5–8 exercises (chapter 08 has 8, so that Gate, Expander, De-esser and Transient shaper each get a programming exercise; the exception is chapter 14, which has 7 engine exercises plus 6 optional application projects, labelled Exercises 1–7 and optional Projects 8–13). They mix **conceptual questions**, **calculation questions** (working out dB, coefficients, time constants, ... by hand) and **programming exercises** (at least 3). Difficulty is marked ★☆☆ / ★★☆ / ★★★.
- Programming exercises provide `exercises/starter/exN_xxx.cpp`: it compiles and leaves `// TODO`s; when run it prints FAIL because the self-check fails (this is expected).
- `solutions/exN_xxx.cpp`: the complete solution. It uses `DAWLAB_CHECK` / `DAWLAB_CHECK_NEAR` to **verify the result with numbers** (for example, using `measureGainDb` to verify that the gain at the cutoff is -3 dB), and ends with `return dawlab::test::summary("lNN_exN");`.
- `solutions/README.md`: every exercise has an answer and an explanation (full explanations for conceptual questions, the working for calculation questions, and for programming exercises the key ideas plus a pointer to the solution file).
- A solution file may bring along the classes the exercise needs (for example the exercise "write your own RMS detector"); they do not have to go into `dsp/`.

### 4.4 CMake

Each chapter's `CMakeLists.txt` uses only the functions from `cmake/DawLab.cmake`, and target names are prefixed with the chapter number to avoid collisions:

```cmake
dawlab_add_example(l02_biquad_response   examples/biquad_response.cpp)
dawlab_add_starter(l02_ex3_starter       exercises/starter/ex3_highpass.cpp)
dawlab_add_solution(l02_ex3_solution     solutions/ex3_highpass.cpp)
dawlab_add_test(l03_test_parametric_eq  tests/parametric_eq_test.cpp)   # optional: component regression test
```

### 4.5 Building and testing (Windows)

```powershell
.\scripts\build.ps1                                  # all pure C++ chapters
.\scripts\build.ps1 -Only 02-filter                  # only chapter 02
.\scripts\build.ps1 -Only 13-vst-hosting -Juce       # a JUCE chapter
.\scripts\build.ps1 -Only 13-vst-hosting,14-daw-8track -Juce   # several chapters, comma-separated
```

The warning level is MSVC `/W4`; new code should produce no warnings.

### 4.6 Building and testing (Linux / macOS)

```bash
./scripts/build.sh                                   # all pure C++ chapters
./scripts/build.sh --only 02-filter                  # only chapter 02
./scripts/build.sh --only 13-vst-hosting,14-daw-8track --juce   # JUCE chapters
```

GCC / Clang use `-Wall -Wextra -Wpedantic`; new code should produce no warnings there either (the only expected ones are unused helper functions in starter files, which the learner is meant to use). The course is verified on Ubuntu 24.04 + GCC 13.3; when you change something, check that it still builds and passes its tests on both MSVC and GCC.

## 5. DSP API contract

Later chapters (especially the chapter 13 plugin and the chapter 14 DAW) use these classes directly, so **the names and the methods listed below must exist and have the same semantics**. You may add extra methods or classes.

<div align="center">

| Chapter | Header | Must provide |
|---|---|---|
| 01 | `SmoothedValue.h` | `class SmoothedValue`: `reset(double sampleRate, double rampSeconds)`, `setCurrentAndTargetValue(float)`, `setTargetValue(float)`, `float getNextValue()`, `float getCurrentValue() const`, `float getTargetValue() const`, `bool isSmoothing() const`, `void skip(int numSamples)` (linear ramp) |
| 01 | `Gain.h` | `class Gain`: standard interface + `setGainDb(float)`, `setGainLinear(float)`, `float getGainDb() const`, `setRampSeconds(double)` (default about 0.05 s) |
| 01 | `Panner.h` | `enum class PanLaw { Linear, ConstantPower, Minus4_5dB }`; `class Panner`: standard interface + `setPan(float)` (-1 = left, 0 = centre, +1 = right), `setPanLaw(PanLaw)`, `static void computeGains(float pan, PanLaw law, float& left, float& right)`; `process()` handles stereo blocks (document how a mono input is treated; `process()` applies the pan law to a stereo block, with -3 dB at the centre, while a bus that is already stereo normally needs balance instead, see chapter 01) |
| 02 | `OnePole.h` | `class OnePole`: `enum class Type { Lowpass, Highpass }`, standard interface + `setType(Type)`, `setCutoff(float hz)`, `processSample(int ch, float x)` |
| 02 | `Biquad.h` | `struct BiquadCoefficients { double b0, b1, b2, a1, a2; }` (a0 normalized to 1), static factories (RBJ Audio EQ Cookbook): `makeLowpass(sr, f, q)`, `makeHighpass(sr, f, q)`, `makeBandpass(sr, f, q)` (0 dB peak), `makeNotch(sr, f, q)`, `makeAllpass(sr, f, q)`, `makePeak(sr, f, q, gainDb)`, `makeLowShelf(sr, f, q, gainDb)`, `makeHighShelf(sr, f, q, gainDb)`; members `std::complex<double> response(double f, double sr) const` and `double magnitudeDb(double f, double sr) const`. `class Biquad`: standard interface + `setCoefficients(const BiquadCoefficients&)`, `getCoefficients()`, `processSample(int ch, float x)` (Transposed Direct Form II) |
| 03 | `ParametricEq.h` | `enum class EqBandType { LowCut, LowShelf, Peak, HighShelf, HighCut, Notch }`; `struct EqBand { EqBandType type; float frequency; float q; float gainDb; bool enabled; }`; `class ParametricEq`: `static constexpr int kNumBands = 6`, standard interface + `setBand(int, const EqBand&)`, `const EqBand& getBand(int) const`, `double getMagnitudeDb(double freq) const` (the whole EQ curve, for the UI to draw). Parameter changes must not click |
| 04 | `EnvelopeFollower.h` | `enum class DetectorMode { Peak, Rms }`, `enum class SmoothingMode { Branching, Decoupled }` (default Branching); `class EnvelopeFollower`: `prepare(double sr)`, `reset()`, `setAttackMs`, `setReleaseMs`, `setMode`, `setSmoothingMode(SmoothingMode)` (the chapter 08 Expander uses Decoupled), `float processSample(float x)` (takes the raw sample and returns the linear amplitude envelope) |
| 04 | `Compressor.h` | `class Compressor`: standard interface + `setThresholdDb`, `setRatio`, `setKneeDb`, `setAttackMs`, `setReleaseMs`, `setMakeupGainDb`, `setStereoLink(bool)`, `float getGainReductionDb() const` (a **positive** value, e.g. 6 means it is currently compressing by 6 dB; thread-safe), `static float computeOutputDb(float inputDb, float thresholdDb, float ratio, float kneeDb)` (the static gain computer) |
| 05 | `DelayLine.h` | `class DelayLine`: `prepare(int maxDelaySamples, int numChannels)`, `reset()`, `void push(int ch, float x)`, `float read(int ch, int delaySamples) const` |
| 05 | `Limiter.h` | `class Limiter`: standard interface + `setCeilingDb`, `setInputGainDb`, `setReleaseMs`, `setLookaheadMs` (the upper bound is fixed in prepare; the default maximum is 20 ms and can be changed before prepare with `setMaxLookaheadMs(float)`), `int getLatencySamples() const`, `float getGainReductionDb() const` (positive, thread-safe). Guarantee: for any input, the output sample peak is ≤ the ceiling |
| 06 | `Oversampler.h` | `class Oversampler`: `explicit Oversampler(int factorLog2 = 1)` (factor = 2^factorLog2), `prepare(double sr, int maxBlockSize, int numChannels)`, `reset()`, `int getFactor() const`, `AudioBlock upsample(const AudioBlock& input)` (returns a view of the internal high-rate buffer), `void downsample(AudioBlock output)`, `int getLatencySamples() const` (in samples at the original rate); `static constexpr int kMaxFactorLog2 = 3` (up to 8×) |
| 06 | `Saturation.h` | `enum class SaturationCurve { Tanh, Cubic, Arctan, Asymmetric }`; `class Saturation`: standard interface + `setCurve`, `setDriveDb`, `setMix(float 0..1)`, `setOutputGainDb`, `setOversamplingFactorLog2(int)` (call before prepare), `static float shape(SaturationCurve, float x)`, `int getLatencySamples() const` |
| 07 | `Distortion.h` | `enum class DistortionType { HardClip, Foldback, Fuzz, Rectify, Bitcrush }`; `class Distortion`: standard interface + `setType`, `setDriveDb`, `setToneHz` (post-stage lowpass), `setMix`, `setOutputGainDb`, `setBitDepth(float)`, `setDownsampleFactor(int)`, `setOversamplingFactorLog2(int)` (call before prepare; default 2 = 4×), `int getOversamplingFactor() const` (after prepare, reports the factor actually in effect), `int getLatencySamples() const` |
| 08 | `Gate.h` | `class Gate`: standard interface + `setThresholdDb`, `setHysteresisDb` (close threshold = threshold − hysteresis), `setRangeDb` (attenuation when closed, negative, e.g. -80), `setAttackMs`, `setHoldMs`, `setReleaseMs`, `process(AudioBlock)`, `process(AudioBlock main, const AudioBlock& sidechain)` (detects from the sidechain), `float getGainReductionDb() const` (positive, thread-safe), `bool isOpen() const` |
| 08 | `Expander.h` | `class Expander` (downward expander): standard interface + `setThresholdDb`, `setRatio` (2 means every 1 dB below the threshold becomes 2 dB), `setKneeDb`, `setRangeDb`, `setAttackMs`, `setReleaseMs`, `float getGainReductionDb() const`, `static float computeOutputDb(float inputDb, float thresholdDb, float ratio, float kneeDb)` |
| 08 | `DeEsser.h` | `class DeEsser`: standard interface + `setFrequency(float hz)`, `setThresholdDb`, `setRangeDb` (maximum attenuation, positive), `setSplitBand(bool)` (true = attenuate only the high band), `setListen(bool)` (monitor the detection signal), `float getGainReductionDb() const` |
| 08 | `TransientShaper.h` | `class TransientShaper`: standard interface + `setAttackDb(float)` (-24..+24, emphasizes or softens transients), `setSustainDb(float)` |
| 09 | `FractionalDelayLine.h` | `class FractionalDelayLine`: `prepare(int maxDelaySamples, int numChannels)`, `reset()`, `void push(int ch, float x)`, `float readLinear(int ch, float delaySamples) const`, `float readCubic(int ch, float delaySamples) const` (3rd-order Lagrange or Hermite), `int getMaxDelaySamples() const`. Read convention: after a `push()`, `read*(ch, 0)` is the sample just written and `read*(ch, D)` is the sample from D pushes ago; `readCubic` clamps delays below 1 to 1 |
| 09 | `Delay.h` | `class Delay` (stereo echo): `setMaxDelayMs(float)` (call before prepare, default 2000), standard interface + `setDelayMs(float)` (changes are smoothed), `setFeedback(float 0..0.99)`, `setMix(float 0..1)`, `setPingPong(bool)`, `setLowpassHz(float)`, `setHighpassHz(float)` (filtering inside the feedback loop), `static float noteToMs(double bpm, double beats)` (beats = 0.75 means a dotted eighth note) |
| 10 | `Lfo.h` | `enum class LfoShape { Sine, Triangle, Saw, Square, SampleAndHold }`; `class Lfo`: `prepare(double sampleRate)`, `reset()`, `setShape(LfoShape)`, `setRateHz(float)`, `setPhase(float 0..1)`, `float getNextValue()` (outputs -1..1) |
| 10 | `Chorus.h` | `class Chorus`: standard interface + `setRateHz`, `setDepthMs`, `setCentreDelayMs`, `setVoices(int)`, `setMix`, `setStereoSpread(float 0..1)` |
| 10 | `Flanger.h` | `class Flanger`: standard interface + `setRateHz`, `setDepthMs`, `setDelayMs` (base delay), `setFeedback(float -0.99..0.99)`, `setMix` |
| 10 | `Phaser.h` | `class Phaser`: standard interface + `setRateHz`, `setDepth(float 0..1)`, `setCentreHz`, `setFeedback`, `setStages(int)` (an even number from 2 to 12), `setMix` |
| 10 | `Tremolo.h` | `class Tremolo`: standard interface + `setRateHz`, `setDepth(float 0..1)`, `setShape(LfoShape)`, `setAutoPan(bool)` (when true, left and right are in opposite phase = autopan) |
| 11 | `Fft.h` | `enum class WindowType { Rectangular, Hann, Hamming, BlackmanHarris }`; `class Fft`: `explicit Fft(int order)` (size = 2^order; twiddle factors are precomputed in the constructor), `int getSize() const`, `void forward(std::complex<float>* data) const`, `void inverse(std::complex<float>* data) const` (in place; the inverse includes 1/N), `void forwardReal(const float* input, std::complex<float>* output) const` (outputs N/2+1 bins), `void inverseReal(const std::complex<float>* input, float* output) const`, `static void makeWindow(std::vector<float>& window, int size, WindowType type)`. Note: `forwardReal` / `inverseReal` use an internal scratch buffer, so one `Fft` object must not be used by two threads at the same time |
| 11 | `LevelMeter.h` | `class LevelMeter`: `prepare(double sampleRate, int numChannels)`, `reset()`, `setPeakHoldMs`, `setDecayDbPerSecond`, `setRmsWindowMs`, `void process(const AudioBlock&)` (audio thread), `float getPeakDb(int ch) const`, `float getRmsDb(int ch) const`, `float getPeakHoldDb(int ch) const` (thread-safe), `void resetPeakHold()` |
| 11 | `LoudnessMeter.h` | `class LoudnessMeter` (ITU-R BS.1770 / EBU R128, K-weighting): `prepare(double sampleRate, int numChannels)`, `reset()`, `void process(const AudioBlock&)` (must not allocate), `float getMomentaryLufs() const` (400 ms), `float getShortTermLufs() const` (3 s), `float getIntegratedLufs() const` (with the -70 LUFS absolute gate and the -10 LU relative gate); all getters are thread-safe |
| 11 | `SpectrumAnalyzer.h` | `class SpectrumAnalyzer`: `prepare(double sampleRate, int fftOrder = 11)`, `void pushSamples(const float* x, int n)` (audio thread, lock-free), `bool computeSpectrum(std::vector<float>& magnitudesDb)` (UI thread; returns the dB values of N/2+1 bins, with windowing and smoothing; returns false when there is no new data), `double getBinFrequency(int bin) const` |
| 12 | `Reverb.h` | `class Reverb` (algorithmic; choose either an FDN or a Freeverb architecture and explain it): standard interface + `setDecaySeconds` (RT60), `setPreDelayMs`, `setDampingHz`, `setSize(float 0..1)`, `setWidth(float 0..1)`, `setMix(float 0..1)`, `setFreeze(bool)`. Teaching-oriented building blocks (`CombFilter`, `AllpassDiffuser`, etc.) may live in `ReverbBuildingBlocks.h` |
| 12 | `ConvolutionReverb.h` | `class ConvolutionReverb` (uniform partitioned FFT convolution, using the chapter 11 `Fft`): `prepare(double sampleRate, int maxBlockSize, int numChannels)`, `reset()`, `void setImpulseResponse(const AudioBuffer& ir)` (not on the audio thread; may allocate), `setMix(float 0..1)`, `process(AudioBlock)`, `int getLatencySamples() const`, `static AudioBuffer makeSyntheticImpulseResponse(double sampleRate, double rt60Seconds, int numChannels, uint32_t seed = 1)` |

</div>

Chapter 13 (JUCE) provides a VST3 plugin built around `dawlab::dsp::Compressor` (`DawLabCompressor`), plus reusable plugin-hosting code (which the chapter 14 DAW uses to insert VST3 plugins on tracks). Chapter 14 separates the engine (pure C++, testable offline) from the JUCE app (audio device, GUI, VST3 inserts), and offers two routes to building the 8-track DAW from scratch. In the build-along route the reader writes the code step by step: Steps 1–10 each have, under `steps/stepNN-*/`, a stand-alone compilable engine snapshot containing only the features up to that step (`minidaw.h`; `starter/` leaves TODOs and FAILs when run, `solution/` uses `DAWLAB_CHECK` to verify only that step's features, the ctest names are `l14_stepNN_solution`, and each step builds on the previous step's solution), while Steps 11–21 specify, in the "Your turn" section of each step document, which functions in `app/` to delete and rewrite, and the checks that must pass afterwards (the corresponding section of `l14_app_smoke_test`, a `--snapshot` image, or an exercise solution). In the reference route you simply read and run the finished `engine/` and `app/`.
