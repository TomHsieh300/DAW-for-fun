# Chapter 13 Exercises: VST3 Plugins and Hosting
**English** | [繁體中文](README.zh-TW.md)

Difficulty: ★☆☆ basic, ★★☆ intermediate, ★★★ advanced. Solutions are in [`../solutions/README.md`](../solutions/README.md).

The starter code for the hands-on exercises is in `exercises/starter/`. It all compiles but contains `// TODO` markers; running it as is prints `[FAIL]` (this is expected).
After you finish, run the matching `l13_exN_starter`; all `[PASS]` means you got it right. The solution programs `l13_exN_solution` are verified automatically by `ctest -L solution`.

```powershell
.\scripts\build.ps1 -Only 13-vst-hosting -Juce -NoTest
cd build                               # your build folder
.\bin\Debug\l13_ex4_starter.exe
```

> Every hands-on exercise loads the `DawLabCompressor.vst3` / `DawLabLimiter.vst3` built by this chapter (the paths are compiled into the programs at CMake time),
> so you must do one full build first (the `l13_*` targets already have their dependencies set up, so building them builds the plugins first).

---

## Exercise 1 ★☆☆ (calculation): normalized parameter conversion

DawLabCompressor's parameter ranges are as follows (`plugin/compressor/CompressorProcessor.cpp`):

| Parameter | Range | skew |
|---|---|---|
| Threshold | −60 … 0 dB | 1 (linear) |
| Ratio | 1 … 20 | 0.5 |
| Attack | 0.1 … 200 ms | `setSkewForCentre(10)` |
| Release | 5 … 2000 ms | `setSkewForCentre(100)` |

JUCE's conversion formulas: $n = \left(\dfrac{v - s}{e - s}\right)^{k}$, $v = s + (e - s)\, n^{1/k}$.

1. The host wants to set Threshold to −24 dB and Ratio to 8:1. What normalized value must it send for each?
2. `setSkewForCentre(c)` makes $n = 0.5$ land exactly on $v = c$. Derive the formula for the skew $k$, and compute $k$ for Attack and for Release.
3. The user turns the Release knob to the 1/4 position ($n = 0.25$). How many ms is that actually? (Hint: $0.25 = 0.5^2$; the answer has a very neat closed form.)
4. Why does VST3 require the host and the plugin to exchange only values in 0..1, rather than passing "−24 dB" directly?

## Exercise 2 ★★☆ (concepts): threads, scanning and crashes

1. In which thread should each of the following run, and why?
   (a) `PluginHost::scan()`　(b) `HostedPlugin::process()`　(c) `HostedPlugin::setState()`　(d) creating a `PluginWindow`　(e) calling `setParameterValue()` once every 32 samples while automation is playing
2. Why does `HostedPlugin::process()` use `ScopedTryLock` instead of `ScopedLock` to take the plugin's callback lock? What do we do when the lock cannot be taken? What is the cost of doing so?
3. Why do professional DAWs (for example Reaper, Bitwig, Cubase) scan plugins in "a separate process"? This chapter's `PluginHost::scan()` is in-process; what happens if a plugin crashes during a scan? Design an "out-of-process scanning + blacklist" workflow (text or a flowchart is fine).
4. The comment in `PluginWindow` says "you must delete this window before the HostedPlugin is destroyed". What happens if the order is reversed?

## Exercise 3 ★★☆ (calculation): latency and PDC

A 48 kHz project has three tracks feeding the master:

- Track A: DawLabLimiter (lookahead 5 ms)
- Track B: a linear-phase EQ that reports latency = 1024 samples
- Track C: no plugin

1. What is each plugin's latency in samples and in ms?
2. To line up the three tracks at the master, how many extra samples must the host delay each track by? What is the total latency of the whole project?
3. If the host did no PDC, how many ms apart would the kick on Track A and the kick on Track C be? If two identical kicks are stacked but offset by 240 samples, at which frequencies do they cancel completely? (Hint: a comb filter, $f_k = \dfrac{(2k+1)\, f_s}{2D}$)
4. What problem does PDC cause while recording (monitoring the input signal)? How do DAWs usually handle it?

## Exercise 4 ★★☆ (hands-on): host-side automation -- slicing into sub-blocks

Starter code: `exercises/starter/ex4_automation.cpp` (target `l13_ex4_starter`)

Make DawLabCompressor's Threshold fall linearly from 0 dB to −40 dB over an 8-second drum loop. The plugin reads its parameters only once per `processBlock`, so split every 512-sample block into 32-sample sub-blocks and send a new value with `setParameterValue()` at the start of each segment.

Self-check: the result differs from the reference answer ("call `dsp::Compressor` directly and change the threshold every 32 samples") by < 1e-4; the RMS of the first 0.5 s is nearly unchanged (> −1 dB), and the last 1 s is compressed by 6 dB or more.

Further thought: the smaller the sub-block, the better the time resolution, but what is the cost?

## Exercise 5 ★★☆ (hands-on): save a preset to a file and load it back

Starter code: `exercises/starter/ex5_preset_file.cpp` (target `l13_ex5_starter`)

Implement `savePreset()` / `loadPreset()`: store the binary blob from `HostedPlugin::getState()` as base64 in a custom XML file (`dawlab_output/l13_ex5_preset.dawpreset`), together with the plugin name and uid; when loading, check that it is the same plugin and that the file is not corrupted.

Self-check: after loading back, every parameter's error is < 1e-3 and the processed result is identical sample by sample to the result before saving (< 1e-6); loading the compressor's preset into DawLabLimiter, or loading a corrupted file, must return false.

## Exercise 6 ★★★ (hands-on): chain two plugins with AudioProcessorGraph and measure the latency

Starter code: `exercises/starter/ex6_graph_chain.cpp` (target `l13_ex6_starter`)

Use `juce::AudioProcessorGraph` to build `audio in → DawLabCompressor (makeup +12 dB) → DawLabLimiter (ceiling −1 dB) → audio out`.

Self-check: the latency the graph reports = 240 samples (5 ms); the delay measured with an impulse = the reported value; with only the compressor the peak is > 0 dBFS, and the peak of the whole chain is ≤ −1 dBFS.

## Exercise 7 ★★★ (hands-on): a bypass without clicks (latency compensation + crossfade)

Starter code: `exercises/starter/ex7_bypass_crossfade.cpp` (target `l13_ex7_starter`)

Complete `CrossfadeBypass::process()`: first delay the dry signal by the plugin's latency (using chapter 05's `dsp::DelayLine`), then crossfade between dry and wet with a 10 ms `SmoothedValue`. The plugin keeps processing while bypassed.

Self-check (DawLabLimiter, ceiling −20 dB, 440 Hz sine wave): once bypass has settled, the output = the input delayed by 240 samples; the crossfade version's maximum sample-to-sample jump is < 0.025; a hard bypass without compensation jumps by > 0.05.

Extension: DawLabCompressor's Mix parameter (parallel compression) is in fact the same trick -- why does it not need to delay dry? If the compressor had lookahead, how would Mix have to change?
