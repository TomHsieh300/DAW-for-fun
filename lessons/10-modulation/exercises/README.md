# Chapter 10 Exercises: Modulation -- Chorus, Flanger, Phaser, Tremolo

**English** | [繁體中文](README.zh-TW.md)

Difficulty: ★☆☆ basic / ★★☆ intermediate / ★★★ advanced.
The implementation problems (3–6, 8) each come with starter code `exercises/starter/exN_xxx.cpp` (it compiles and leaves `// TODO` markers; running it prints FAIL, which is expected) and a complete solution `solutions/exN_xxx.cpp`.
Solutions and explanations (including the conceptual and calculation problems) are in [`../solutions/README.md`](../solutions/README.md). Try to finish a problem on your own before looking.

Build and run (Windows, from the project root):

```powershell
.\scripts\build.ps1 -Only 10-modulation -NoTest     # the default build directory is build\
cd build
.\bin\Debug\l10_ex3_starter.exe      # your version (edit exercises/starter/ex3_vibrato.cpp)
.\bin\Debug\l10_ex3_solution.exe     # the reference solution
```

After editing a starter, just run `build.ps1` again (or, from the project root, `cmake --build build --config Debug --target l10_ex3_starter`).
The WAV files the programs write go into `dawlab_output/` under the "current working directory", so `cd build` before running.
Note that the starter and the solution write WAV files with the same name (for example both write `l10_ex3_vibrato.wav`), and whichever runs later overwrites the earlier one.
A starter is done when all of its self-checks pass and it prints `[PASS]`.

<div align="center">

| No. | Type | Difficulty | starter / solution target |
|---|---|---|---|
| 1 | Concept | ★☆☆ | — |
| 2 | Calculation | ★★☆ | — |
| 3 | Implementation: Vibrato | ★★☆ | `l10_ex3_starter` / `l10_ex3_solution` |
| 4 | Implementation: Ring modulator | ★★☆ | `l10_ex4_starter` / `l10_ex4_solution` |
| 5 | Implementation: Stereo chorus and correlation | ★★☆ | `l10_ex5_starter` / `l10_ex5_solution` |
| 6 | Implementation: Tempo-synced tremolo | ★★☆ | `l10_ex6_starter` / `l10_ex6_solution` |
| 7 | Concept + calculation: phaser notch spacing | ★★★ | — |
| 8 | Implementation: Staggered phaser (a different $`f_c`$ per stage) | ★★★ | `l10_ex8_starter` / `l10_ex8_solution` |

</div>

---

## Exercise 1 ★☆☆ (concept) The essential differences between chorus, flanger and vibrato

The code of `Chorus` and `Flanger` is almost the same: both are "a delay pushed by an LFO + the dry signal".

1. What is the delay range of each? Why does the delay length make the ear hear completely different things? Explain once from the **frequency-domain** view (the spacing $`1/d`$ of the comb filter teeth) and once from the **pitch** view (Doppler, $`1 - d'(t)`$).
2. A flanger often uses feedback and a chorus almost never does. Why? (Hint: feedback creates resonances at $`f = k/d`$. What are those resonance frequencies for $`d = 20`$ ms, and what would they sound like?)
3. Vibrato and chorus differ by a single parameter. Which one? Why does vibrato have no comb-filter peaks and valleys?
4. If you change the flanger mix from 0.5 to 0.2, do the notches still reach −∞ dB? How deep do they get?

## Exercise 2 ★★☆ (calculation) Work through the formulas

Use $`f_s = 48`$ kHz throughout.

1. Flanger, fixed delay 0.5 ms, mix 0.5, feedback 0: what are the frequencies of the first three notches and the first three peaks (including 0 Hz)?
2. You want the **first** notch of a flanger to sweep between 200 Hz and 1000 Hz. What should `setDelayMs` and `setDepthMs` be? (The delay range of this chapter's `Flanger` is $`[\text{delay}, \text{delay} + \text{depth}]`$.)
3. Flanger with mix 0.5 and feedback +0.7: what is the gain in dB at the peaks and at the former notch positions? What about feedback −0.7?
4. Chorus with depth 5 ms and rate 1 Hz: what is the maximum pitch deviation in cents? If the depth stays at 5 ms and you want a maximum deviation of only ±10 cents, what is the largest rate you can set?
5. A first-order allpass with $`f_c = 1`$ kHz: what is the coefficient $`a`$? What is the phase at 500 Hz, in degrees?
6. A 4-stage phaser with $`f_c = 1`$ kHz in every stage, mix 0.5, feedback 0: where are the two notches?
7. Tremolo: a 1 kHz sine (amplitude 0.5), sine LFO at 6 Hz, depth 0.6. Which lines does the output spectrum contain, and what is the amplitude of each?
8. Tempo sync: for a dotted eighth note at 128 bpm, what is the LFO rate in Hz? How many ms is one cycle?

## Exercise 3 ★★☆ (implementation) Vibrato

Starter code: `exercises/starter/ex3_vibrato.cpp`

Use `FractionalDelayLine` from Chapter 09 and `Lfo` from this chapter to write a `Vibrato`: the delay is $`d(t) = \text{centre} + \text{depth}\cdot m(t)`$, and the output is **only** the delayed signal. Complete the three TODOs:

1. In `prepare()`, allocate the delay line (maximum delay = `kMaxCentreMs + kMaxDepthMs`, converted to samples, with a few extra for interpolation).
2. In `advanceModulation()`, compute the delay (in samples) for this sample and clamp it to the legal range.
3. In `processSample()` / `process()`: push, then read with `readCubic`, and **do not** mix in the dry signal.

Self-checks:

- 1 kHz sine, rate 5 Hz, depth 1 ms → the maximum / minimum instantaneous frequency is $`1000(1 \pm 2\pi \cdot 5 \cdot 0.001) = 1031.4 / 968.6`$ Hz (±2 Hz).
- The mean frequency is 1000 Hz (±1 Hz); the output RMS equals the input RMS (±0.3 dB).
- depth 0 → the frequency stays at 1000 Hz.

The program writes `l10_ex3_vibrato.wav` (plucked arpeggio + vibrato). Listen and work out how depth and rate combine to sound like a singer's natural vibrato (hint: 5–6 Hz, ±20–50 cents).

## Exercise 4 ★★☆ (implementation) Ring modulator

Starter code: `exercises/starter/ex4_ringmod.cpp`

$`y = (1 - \text{mix})\,x + \text{mix}\cdot x \cdot c(t)`$, where the carrier $`c(t) = \sin(2\pi f_c t)`$ is produced by an `Lfo` (sine) running in the audible range.

Self-checks:

- 1000 Hz sine ($`A = 0.5`$) × 150 Hz carrier, mix 1: the amplitudes at 850 and 1150 Hz ≈ 0.25 (±0.005); both 1000 Hz and 150 Hz are below −60 dB.
- Carrier 0 Hz, phase 0.25 ($`\sin = 1`$) → output = input.
- mix 0.5: 1000 Hz keeps 0.25 and 850 Hz is 0.125.

The program writes `l10_ex4_ringmod_30hz.wav` and `l10_ex4_ringmod_500hz.wav`. Compare 30 Hz (like a very deep tremolo) with 500 Hz (a metallic bell), and explain why a 30 Hz carrier sounds like a swell "60 times per second".

## Exercise 5 ★★☆ (implementation) Stereo chorus and left/right correlation

Starter code: `exercises/starter/ex5_stereo_chorus.cpp`

Write a `StereoChorus` with only 1 voice but one LFO per channel; the right-channel LFO starting phase = `phaseOffset` (0.25 = 90°). After completing the TODOs, measure the Pearson correlation coefficient $`\rho = \sum LR / \sqrt{\sum L^2 \sum R^2}`$.

Self-checks:

- phase offset 0 → $`\rho > 0.999`$.
- phase offset 0.25, mix 1 → $`\rho < 0.5`$.
- phase offset 0.25, mix 0.5 → $`\rho`$ lies between the two values above.
- The phase difference really is 90°: rate 0, impulse input → the left-channel impulse appears at 15 ms (centre) and the right-channel one at 18 ms (centre + depth·sin 90°). The $`\rho`$ of white noise only shows "whether left and right are offset", not "by how much", so this check is needed.
- Mono compatibility: the RMS of $`(L+R)/2`$ relative to the RMS of a single channel ≈ −1.25 dB (±0.5), and not below −3 dB. First derive for yourself where the −1.25 dB figure comes from.
- The $`\rho`$ differs from that of `dawlab::dsp::Chorus` (1 voice, mix 1, `setStereoSpread(1)`) by < 0.05.

Further thought: the $`\rho`$ of white noise is close to 0 at both 90° and 180°, so why is 90° the usual choice for a stereo chorus?

## Exercise 6 ★★☆ (implementation) Tempo-synced tremolo

Starter code: `exercises/starter/ex6_tempo_tremolo.cpp`

`TempoSyncTremolo` wraps this chapter's `Tremolo`. For every block, the "host" gives you the song position `ppq` (in quarter-note beats) at the start of the block. Complete the two TODOs:

1. `getRateHz()`: $`r = \text{bpm} / 60 / \text{beats}`$.
2. `process()`: at the start of every block, set the LFO phase to $`\mathrm{frac}(\text{ppq}/\text{beats} + \text{phaseOffset})`$.

Self-checks:

- `getRateHz()`: 120 bpm eighth → 4 Hz; dotted eighth → 2.6667 Hz; 90 bpm quarter → 1.5 Hz.
- Input DC 1.0 (output = the gain curve), sine, depth 1, phaseOffset 0.25: the gain is > 0.999 on every eighth-note beat and < 0.001 midway between beats, with 8 troughs in 2 seconds.
- Starting playback from ppq = 3.3 differs from playing from the start up to the same position by a gain difference of at most < $`10^{-3}`$.
- After the tempo changes from 120 → 90 bpm, the spacing between troughs = 0.3333 s (±2 ms).

Think about it: if you only set the rate and do not realign the phase every block, what happens when playback starts in the middle of the song?

## Exercise 7 ★★★ (concept + calculation) Why are a phaser's notches not evenly spaced?

1. A flanger's notches are evenly spaced (spacing $`1/d`$) and a phaser's are not. Explain why, from the relationship between the phase difference of the two paths and frequency.
2. Prove: for an $`N`$-stage phaser with the same $`f_c`$ in every stage, under the low-frequency approximation $`\tan(\pi f/f_s) \approx \pi f/f_s`$ the notches satisfy, in pairs, $`f_k \cdot f_{N/2-1-k} \approx f_c^2`$. (Hint: $`\tan\theta \cdot \tan(\pi/2 - \theta) = 1`$.)
3. Use the 6-stage notches printed by Example 4 (268.3 / 1000.0 / 3665.4 Hz) to check part 2. Why is the product not exactly $`10^6`$?
4. If the $`f_c`$ of the four allpass stages are set to **different** values (for example 300, 600, 1200, 2400 Hz), what does the total phase become? Does the number of notches change? Roughly which way do the notch positions move?
5. If you want a phaser's notches to be "evenly spaced", what can you do? What is the cost?

The answer to part 4 can be checked by actually measuring it in Exercise 8.

## Exercise 8 ★★★ (implementation) Staggered phaser: each allpass stage has its own $`f_c`$

Starter code: `exercises/starter/ex8_staggered_phaser.cpp`

The `Phaser` in this chapter has all stages share the same $`f_c`$, so the notch positions have a closed-form solution (README 2.6). In this exercise you write a `StaggeredPhaser`: each stage has its own base $`f_c`$, and the LFO multiplies all the $`f_c`$ by the same factor $`2^{\text{depth}\cdot\text{octaves}\cdot m(t)}`$ (the ratios between stages stay unchanged). With a different $`f_c`$ per stage there is no closed form, but the total phase is still the sum over the stages: $`\Phi(f) = \sum_i \varphi_i(f)`$, falling monotonically from 0 to $`-N\pi`$, with notches at $`\Phi(f) = -(2k+1)\pi`$. Complete the three TODOs:

1. `theoryPhase(f, scale)`: $`\sum_i \varphi_i(f)`$, where the $`f_c`$ of stage $`i`$ = scale × base $`f_c`$ (you can use `Phaser::allpassPhase` directly).
2. `predictNotchHz(k, scale)`: solve $`\Phi(f) = -(2k+1)\pi`$ by bisection on $`[1, f_s/2 - 1]`$ Hz.
3. `advanceModulation()`: compute this sample's scale factor and set **each stage's own** first-order allpass coefficients (`Phaser::makeFirstOrderAllpass`).

Self-checks (mix 0.5, no feedback, LFO parked, sweep 50 Hz–16 kHz to find the notches automatically):

- All 4 stages at 1000 Hz: `predictNotchHz` agrees with the closed form of `Phaser::notchFrequencyHz` (414.7 / 2397.8 Hz, ±0.01 Hz); white noise through `StaggeredPhaser` and through `dawlab::dsp::Phaser` (rate 0, depth 0, feedback 0, mix 0.5) gives the same output sample by sample (largest difference < $`10^{-5}`$).
- 300 / 600 / 1200 / 2400 Hz: there are still **2** measured notches, within 0.5% of `predictNotchHz`, with depth < −30 dB.
- Verify the prediction of Exercise 7, part 4: the first notch is below the 414.7 Hz of the 4 × 1000 Hz case, and the second is above 2397.8 Hz.
- depth 1, octaves 1, LFO parked at +1 (rate 0, phase 0.25) → all $`f_c`$ double, and the measured notches = `predictNotchHz(k, 2)` (±0.5%).

The program writes `l10_ex8_uniform.wav` and `l10_ex8_staggered.wav` (white noise, rate 0.2 Hz, ±1.5 octaves). Compare how the two sweeps sound: what changes once the notches are farther apart?
