# Chapter 06 Exercises: Saturation and Aliasing

**English** | [繁體中文](README.zh-TW.md)

Difficulty: ★☆☆ Basic  ★★☆ Intermediate  ★★★ Challenge

The starter code for the programming exercises is in `exercises/starter/` and compiles as-is; until you complete the `// TODO`s it prints `[FAIL]` (this is expected). Complete solutions are in `solutions/`, with explanations in [`solutions/README.md`](../solutions/README.md).

Build and run (PowerShell, from the project root):

```powershell
.\scripts\build.ps1 -Only 06-saturation -NoTest
cd build
.\bin\Debug\l06_ex4_starter.exe      # your version
.\bin\Debug\l06_ex4_solution.exe     # the solution
```

<div align="center">

| # | Type | Difficulty | Starter target | Solution target |
|---|---|---|---|---|
| 1 | Concept | ★☆☆ | — | — |
| 2 | Calculation | ★☆☆ | — | `l06_ex2_solution` |
| 3 | Calculation | ★★☆ | — | `l06_ex3_solution` |
| 4 | Implementation | ★★☆ | `l06_ex4_starter` | `l06_ex4_solution` |
| 5 | Implementation | ★★☆ | `l06_ex5_starter` | `l06_ex5_solution` |
| 6 | Implementation | ★★★ | `l06_ex6_starter` | `l06_ex6_solution` |
| 7 | Implementation | ★★☆ | `l06_ex7_starter` | `l06_ex7_solution` |

</div>

---

## Exercise 1 (★☆☆, concept): Symmetry, even harmonics, DC, and latency alignment

1. `tanh` is an odd function. Using the half-wave symmetry of a sine wave, explain why tanh produces only odd harmonics and no DC.
2. Why does the `Asymmetric` curve always produce DC? Give two practical problems a DC offset causes in a mix.
3. Why is the cutoff of a DC blocker usually set to 5–20 Hz rather than 50 Hz or 1 Hz? What is the drawback of each?
4. Someone wrote a 4x-oversampled saturator that adds the "current block's dry" directly to the "wet" in the dry/wet mix, and the result sounds hollow. What is the cause? Roughly at what frequency is the first notch? (The latency of 4x is 36 samples @ 48 kHz.)

**Hint**: README sections 2.4 and 2.10.

---

## Exercise 2 (★☆☆, calculation): Harmonics of $`x^2`$, $`x^3`$, and the cubic soft clip by hand

Let the input be $`x = A\sin\theta`$.

1. Expand $`x^2`$ with a trigonometric identity. Which frequency components appear, and with what amplitudes?
2. Expand $`x^3`$. How many dB is the third harmonic relative to the fundamental? Does this ratio depend on $`A`$?
3. For $`y = x - x^3/3`$ ($`|x| \le 1`$), write the formulas for $`H_1`$ and $`H_3`$, and compute $`H_3/H_1`$ (dB) and THD (%) for $`A = 0.5`$ and $`A = 1`$.
4. For $`y = x + k x^2`$, write the DC component and $`H_2/H_1`$. What are they in dB when $`k = 0.5`$ and $`A = 0.5`$?

**Verification**: the solution program `l06_ex2_solution` measures each value with a Goertzel filter.

---

## Exercise 3 (★★☆, calculation): Alias frequencies

Sample rate $`f_s = 44.1`$ kHz. A 5 kHz sine wave passes through a very heavy hard clip (close to a square wave: only odd harmonics, with the $`k`$-th harmonic having amplitude ≈ $`1/k`$ of the fundamental).

1. What are the frequencies of the 3rd, 5th, 7th, 9th, 11th, 13th, and 15th harmonics? Which exceed Nyquist? Where do they land after folding?
2. At what frequency does the 9th harmonic land after folding? How many dB is it relative to the fundamental? Why is this alias particularly unpleasant to hear?
3. Switch to 2x oversampling (hard clip at 88.2 kHz, then downsample to 44.1 kHz). Which of the harmonics above are filtered out during downsampling? Which one still remains as an alias? Which one falls in the halfband filter's transition band and is attenuated only partially (and where does it land after downsampling)?

**Hint**: $`f_{\text{alias}} = \left| ((f + f_s/2) \bmod f_s) - f_s/2 \right|`$. For 2x, first fold once using $`2f_s`$; anything still above 22.05 kHz after folding is blocked by the downsampling filter.

**Verification**: `l06_ex3_solution` prints the table and uses a real hard clip to measure the aliases at 900 Hz and 9.1 kHz.

---

## Exercise 4 (★★☆, implementation): Chebyshev harmonic exciter

Starter code: `exercises/starter/ex4_chebyshev_exciter.cpp`

1. Implement `chebyshev(n, x)` with the recurrence $`T_{n+1}(x) = 2x\,T_n(x) - T_{n-1}(x)`$.
2. Implement `ChebyshevExciter::processSample()`: $`y = x + \sum_{n=2}^{5} g_n T_n(\text{clamp}(x, -1, 1))`$.
3. Verify: for a 1 kHz sine wave of amplitude 1 with $`g_2 = 0.1`$, $`g_3 = 0.05`$, $`g_5 = 0.02`$, the measured H2 / H3 / H5 must equal −20.00 / −26.02 / −33.98 dB (±0.1 dB), and H4 and H6 must be absent.
4. By hand: when the input amplitude is changed to 0.5, what are the DC and H2 amplitudes of the output? Fill your answers into TODO 3.

**Hint**: $`T_2(A\cos\theta) = 2A^2\cos^2\theta - 1`$, then use the double-angle identity.

---

## Exercise 5 (★★☆, implementation): Latency-aligned dry/wet mix and the comb notches

Starter code: `exercises/starter/ex5_aligned_dry_wet.cpp`

(You already implemented a DC blocker in [Chapter 02, Exercise 2](../../02-filter/exercises/README.md); `Saturation` internally uses the Chapter 02 `OnePole` highpass, see README section 2.4. This exercise focuses on the other pitfall from README section 2.10: dry/wet alignment.)

The starter code already contains a parallel saturator: the wet path is a 4x `Oversampler` + $`\tanh(g x)/g`$ (divided by the drive so that dry and wet are equally loud for small signals), and the dry path goes through a `DryDelay` whose delay $`D`$ can be set. For small signals the whole system is

```math
y[n] = (1 - m)\,x[n - D] + m\,x[n - L],\qquad |H(f)| = \left|(1 - m) + m\,e^{-j2\pi f (L - D)/f_s}\right|
```

1. Implement `DryDelay::processSample()`: a ring buffer of length `maxDelay + 1`; write $`x[n]`$ first, then read $`x[n - D]`$; when $`D = 0`$ the output equals the input (TODO 1).
2. Implement `predictedNotchHz(k, Δ, fs)`: the frequency of the $`k`$-th notch when $`\Delta = L - D`$ (TODO 2).
3. Complete $`(1 - m)\cdot\text{dry} + m\cdot\text{wet}`$ inside `ParallelSaturator::process()` (TODO 3).
4. Verify (−40 dBFS sine wave, 256-sample blocks):
   - The latency of 4x is $`L = 36`$.
   - **Misaligned** ($`D = 0`$), mix 50%: the three notches at 667 / 2000 / 3333 Hz are all below −40 dB; 1333 Hz, halfway between two notches, is at 0 dB; sweeping 500–850 Hz in 2 Hz steps, the deepest point falls at 667 Hz ± 2 Hz. With mix set to 30%, 667 Hz is only down to $`20\log_{10}|1 - 2m| = -7.96`$ dB.
   - **Aligned** ($`D = L`$): 667 Hz, 2 kHz, 3.33 kHz, and 10 kHz are all within 0 dB ± 0.02 dB.
   - **Off by one sample** ($`D = L - 1`$): the first notch is at 24 kHz, but 10 kHz is already down by $`20\log_{10}|\cos(\pi f/f_s)| = -2.01`$ dB.

**Think about it**: Is it still necessary to delay the dry signal when mix = 100%? If the wet path's latency is not an integer (for example the nonlinear phase of an IIR halfband), can an integer delay still align it perfectly?

---

## Exercise 6 (★★★, implementation): First-order ADAA hard clip

Starter code: `exercises/starter/ex6_adaa.cpp`

1. Write the antiderivative $`F(x)`$ of the hard clip $`f(x) = \text{clamp}(x, -1, 1)`$, and confirm that $`F' = f`$ and that $`F`$ is continuous at $`|x| = 1`$.
2. Implement $`y[n] = \dfrac{F(x[n]) - F(x[n-1])}{x[n] - x[n-1]}`$, and fall back to $`f\big((x[n] + x[n-1])/2\big)`$ when $`|x[n] - x[n-1]|`$ is very small.
3. For a 7 kHz sine wave with peak 2 (48 kHz), compare the total alias energy of naive clipping and ADAA. ADAA must improve it by at least 6 dB.
4. (Think) What filter is ADAA equivalent to in the linear region? How much does it attenuate the 7 kHz fundamental? How much delay does it add?

**Bonus**: put ADAA inside the 2x loop of the `Oversampler` and compare it with naive 2x.

---

## Exercise 7 (★★☆, implementation): Oversampling quality and cost

Starter code: `exercises/starter/ex7_oversampling_cost.cpp`

Use `dawlab::dsp::Saturation` (Tanh, drive +18 dB, output −6 dB):

1. Implement `aliasDb(log2)`: after processing a 7 kHz sine wave with peak 0.5, compute the total power at all alias positions (1, 3, 5, …, 23 kHz, excluding 7 and 21 kHz) in dB relative to the fundamental. Skip the first 0.1 seconds.
2. Implement `timeMs(log2, noise)`: the time to process 10 seconds of stereo noise (`std::chrono::steady_clock`, run 3 times and take the minimum; do not include copying the buffer or `prepare`).
3. Print a table for 1x / 2x / 4x / 8x. From a quality / cost standpoint, which factor would you pick? Would the answer change if the curve were a hard clip?

**Hint**: Why do all the aliases land on odd kHz values? See README section 2.5.
