# Chapter 07 Exercises: Distortion

**English** | [繁體中文](README.zh-TW.md)

Difficulty: ★☆☆ basic / ★★☆ intermediate / ★★★ advanced.
The implementation problems (3, 4, 5) all come with starter code `exercises/starter/exN_xxx.cpp` (it compiles, leaves `// TODO`s, and prints FAIL when run -- that is expected) and a complete solution `solutions/exN_xxx.cpp`.
Solutions and explanations (including the conceptual, calculation, and design problems) are in [`../solutions/README.md`](../solutions/README.md); we recommend finishing them on your own before looking.

Build and run (Windows, from the project root):

```powershell
.\scripts\build.ps1 -Only 07-distortion -NoTest
cd build
.\bin\Debug\l07_ex3_starter.exe      # your version (edit exercises/starter/ex3_wavefolder.cpp)
.\bin\Debug\l07_ex3_solution.exe     # reference solution
```

After editing a starter, go back to the project root and run `build.ps1` again (or, inside the build directory, run `cmake --build . --config Debug --target l07_ex3_starter`).
A starter is done when all of its self-checks pass and it prints `[PASS]`. The WAV files are written under `dawlab_output/` in the directory you run from.

| # | Type | Difficulty | Topic | File |
|---|---|---|---|---|
| 1 | Calculation | ★☆☆ | Quantization step and SNR for N bits | — |
| 2 | Conceptual | ★☆☆ | Why full-wave rectify goes up an octave | — |
| 3 | Implementation | ★★☆ | Wavefolder (triangle folder, sine folder, oversampling) | `starter/ex3_wavefolder.cpp` |
| 4 | Implementation | ★★☆ | A simple cabinet simulator built from biquads | `starter/ex4_cab_sim.cpp` |
| 5 | Implementation | ★★☆ | Auto gain compensation (output RMS = input RMS) | `starter/ex5_auto_gain.cpp` |
| 6 | Calculation | ★★☆ | Where the aliases of hard clip and sample & hold land | — |
| 7 | Design | ★★★ | Why the order of the amp chain stages matters | — |

---

## Exercise 1 ★☆☆ (Calculation) Quantization step and SNR

Use $`\Delta = 2/2^N`$ and $`\text{SNR} \approx 6.02N + 1.76 + 20\log_{10}A`$ dB from README Section 2.6.1.

1. Compute the step $`\Delta`$ and the theoretical SNR of a full-scale sine ($`A = 1`$) for $`N = 4, 8, 12, 16, 24`$.
2. What is the theoretical SNR when an 8-bit quantizer quantizes a −20 dBFS sine? Compare it with the numbers printed by Example 3.
3. A piece of material peaks at about −12 dBFS. What is the minimum number of bits needed for its quantization SNR to be at least 60 dB?
4. What is the maximum positive output of a 4-bit mid-tread quantizer? What happens to the positive peak when a 0 dBFS sine is fed in? What does this mean for the "measured 2.6 dB lower than theory" at 4 bit in Example 3?
5. `Distortion::setBitDepth()` accepts fractional values. What is the step when `bits = 5.5`? What is the theoretical SNR? What does this mean audibly (why should a plugin allow fractional bits)?
6. Starting from $`\sigma_e^2 = \Delta^2/12`$, derive for yourself where the numbers 6.02 and 1.76 come from.

## Exercise 2 ★☆☆ (Conceptual) Why does full-wave rectify raise the pitch an octave?

1. Explain it from the viewpoint of the period: what is the period of $`|\sin\omega t|`$?
2. Explain it from the viewpoint of the Fourier series: which frequency components does $`|\sin\omega t|`$ have? What is the coefficient of the fundamental ($`\omega`$)?
3. Why does half-wave rectify ($`\max(\sin\omega t, 0)`$) **not** go up an octave? Which components does it have beyond the original sine?
4. If the input carries a DC offset before the rectifier (for example $`0.3 + \sin\omega t`$), is the pitch after full-wave rectify still exactly an octave up? Why?
5. Why does an octave fuzz sound good on single-note solos but often terrible on chords?

## Exercise 3 ★★☆ (Implementation) Wavefolder

Open `exercises/starter/ex3_wavefolder.cpp` and complete:

- (a) `float foldTriangle(float u, float threshold)`: fold back the part beyond ±threshold, as many times as needed (a triangle wave of period $`4T`$, with $`y = u`$ for $`|u| \le T`$). With `threshold = 1` it must match `Distortion::foldback()` exactly (the self-check compares over $`u \in [-10, 10]`$).
- (b) `float foldSine(float u)`: $`y = \sin(\pi u / 2)`$.
- (c) `class Wavefolder`: the standard interface + `setDriveDb` + `setThreshold`, using the Chapter 06 `Oversampler` (4x) to run `foldTriangle` at the high sample rate.

Self-check: the numeric points of (a) and (b); THD < 0.1% at drive 0 dB and amplitude 0.25 ($`|u| < T`$ is perfectly transparent); THD > 20%, peak < 1.1, and no NaN at drive 18 dB; latency > 0.

Hints:

- `std::fmod` returns a negative remainder for negative numbers; remember to bring it back into $`[0, 4T)`$.
- Think about whether the **derivative** of the sine folder in (b) is continuous at the fold points, and what that means for how fast the harmonics decay (the solution prints a harmonic table for both).

## Exercise 4 ★★☆ (Implementation) A simple cabinet simulator from biquads

Open `exercises/starter/ex4_cab_sim.cpp` and complete `CabSim`:

$$
H_\text{cab} = H_\text{HPF}(70\text{ Hz}, Q = \tfrac{1}{\sqrt 2}) \cdot H_\text{peak}(100\text{ Hz}, Q = 1.5, +4\text{ dB}) \cdot H_\text{LPF}(4.5\text{ kHz}, Q = \tfrac{1}{\sqrt 2})^2
$$

All four are the `Biquad` from Chapter 02 (`BiquadCoefficients::makeHighpass` / `makePeak` / `makeLowpass`). You also need to write `double magnitudeDb(double freq) const`, which adds up the `magnitudeDb()` of the stages.

Self-check:

- The actual processing (`measureGainDb`) and the analytic form (`magnitudeDb`) differ by < 0.2 dB at 9 frequencies.
- −6.02 ± 0.3 dB at 4.5 kHz (the signature of an LR4).
- At 9 kHz the value must match the one corrected for the **frequency warping of the bilinear transform** (not the analog LR4's −24.6 dB).
- 1 kHz within ±0.5 dB, 100 Hz between +2.5 and +4.5 dB, 30 Hz < −12 dB, 15 kHz < −40 dB.

Food for thought: why does the digital filter fall further than the analog prototype at 9 kHz? (Hint: $`\tan(\pi f / f_s)`$.)

## Exercise 5 ★★☆ (Implementation) Auto gain compensation

Open `exercises/starter/ex5_auto_gain.cpp` and complete `AutoGain::process(const AudioBlock& dry, AudioBlock wet)`:

$$
\overline{x^2}[n] = a\,\overline{x^2}[n-1] + (1-a)\,x^2[n], \qquad a = e^{-1/(\tau f_s)}
$$

Compute a mean square for each of dry and wet ($`\tau`$ = 100 ms), and then

$$
g[n] = \mathrm{clamp}\left(\sqrt{\frac{\overline{x_\text{dry}^2}[n]}{\max(\overline{x_\text{wet}^2}[n], 10^{-12})}},\; 0.01,\; 100\right), \qquad y[n] = g[n] \cdot x_\text{wet}[n]
$$

Self-check:

1. −20 dBFS sine, HardClip 30 dB: after reaching steady state the output RMS is within 0.2 dB of the input RMS, and the gain is about −22.4 ± 0.5 dB (estimate this number on your own first: for a clipped sine, the RMS is a bit smaller than the 1 of a pure square wave).
2. Pluck sequence, Fuzz 30 dB: at least 6 dB louder without compensation; within 1 dB of the input with compensation.
3. Completely silent input: the output must be all 0 with no NaN.

Food for thought: what happens if the time constant $`\tau`$ is too short (for example 1 ms)? Too long (for example 2 s)?

## Exercise 6 ★★☆ (Calculation) Where do the aliases land?

Sample rate 48 kHz.

1. A 7 kHz sine goes through a hard clip (odd harmonics only). List the frequencies of the 3rd, 5th, 7th, 9th, 11th, 13th, and 15th harmonics, and the frequencies at which they actually appear at 48 kHz ($`f_\text{alias} = |f - k f_s|`$, $`k = \mathrm{round}(f/f_s)`$). Which are "true harmonics" and which are aliases?
2. The same signal, with the hard clip done at 4x oversampling (192 kHz) instead. Where does the 27th harmonic fold to? Assuming the hard clip is already close to a square wave, roughly how many dB below the fundamental is it?
3. A 3 kHz sine goes through the bitcrusher's sample & hold with downsample factor = 4. What is the effective sample rate $`f_s'`$? Does the 3 kHz itself alias? How many dB are the output components at 3 kHz and 9 kHz relative to the original 3 kHz? (Use the sinc of the ZOH: $`|\sin(\pi f/f_s') / (\pi f/f_s')|`$.)
4. Why does the situation in part 3 neither "need" nor "deserve" oversampling?

## Exercise 7 ★★★ (Design) Why does the order of the amp chain stages matter?

The order of `AmpChain` is: input HPF → pre-EQ → preamp (tanh) → tone stack → power amp (hard clip) → cabinet → output gain.

1. State in one line of math why "two linear filters" can be swapped but a "filter and a waveshaper" cannot.
2. If the input HPF is moved **after** the preamp, what difference would you hear on a chord played on the low strings? Why? (Keyword: intermodulation.)
3. How does the role of the bass knob differ between putting the tone stack **between** the preamp and the power amp and putting it at the very **end** of the chain?
4. What happens if the cabinet is moved **before** the power amp?
5. Where should the noise gate (Chapter 08) go? Why not after the cabinet?
6. Design problem: you are building a distortion for **bass guitar** and want the low end to stay clean and full while only the mids and highs distort. Draw the signal flow (parallel paths are allowed) and explain roughly how to set the parameters of each stage.
