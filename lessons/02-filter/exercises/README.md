# Chapter 02 Exercises: Filter
**English** | [繁體中文](README.zh-TW.md)

There are 7 problems: 2 calculation problems (problems 1 and 5; problem 5 is also an implementation), 5 implementation problems (problems 2-6), and 1 conceptual problem (problem 7).
The starter code for the implementation problems is in `starter/` and compiles as is; running it before you finish the TODOs prints `[FAIL]`, which is expected. When you are done, all the self-checks should print `[PASS]`.

```powershell
.\scripts\build.ps1 -Only 02-filter -NoTest      # build (including the starters)
cd build
.\bin\Debug\l02_ex2_starter.exe                  # run your version
.\bin\Debug\l02_ex2_solution.exe                 # compare with the solution
```

Solutions and explanations: [`../solutions/README.md`](../solutions/README.md).

---

## Exercise 1 ★☆☆ (calculation): compute the one-pole coefficients by hand

$`f_s = 48000`$ Hz. Use a calculator (or Python) to answer:

1. For the exp-version one-pole lowpass ($`a = e^{-2\pi f_c/f_s}`$), what are $`a`$ and $`1 - a`$ at $`f_c = 1`$ kHz? And at $`f_c = 100`$ Hz?
2. Continuing from that, what is the time constant $`\tau`$ of this filter at $`f_c = 1`$ kHz, in samples? In ms? (Hint: $`a = e^{-1/\tau}`$, with $`\tau`$ in samples.)
3. What is the gain of the exp version at Nyquist ($`z = -1`$) in dB ($`f_c = 1`$ kHz)? Why is it not $`-\infty`$?
4. For the bilinear version with the same $`f_c = 1`$ kHz: what are $`g = \tan(\pi f_c/f_s)`$, the pole $`(1-g)/(1+g)`$, and $`b_0 = b_1 = g/(1+g)`$? How far is the pole from the exp version's $`a`$?
5. For the exp version with $`f_c = 10`$ kHz, where is the actual −3 dB point? (Hint: $`\cos\omega_{3\text{dB}} = 1 - (1-a)^2/(2a)`$ from section 2.8.)

**Check your answers:** run `l02_filter_response` and look at the table in [1] and the two "Hand-calculation check" lines.

---

## Exercise 2 ★★☆ (implementation): DC blocker

File: `starter/ex2_dc_blocker.cpp` → target `l02_ex2_starter`

Implement a first-order DC blocker:

```math
y[n] = x[n] - x[n-1] + R\,y[n-1],\qquad H(z) = \frac{1 - z^{-1}}{1 - R z^{-1}}
```

1. Complete `processSample()` (update `x1` and `y1`).
2. Complete `magnitudeDb()`: substitute $`z = e^{j\omega}`$ into $`H(z)`$.
3. Verify (the self-checks are already written in the starter): for an input of "0.5 of DC + a 0.25 1 kHz sine", the output mean is < 0.001 and the 1 kHz gain is within ±0.05 dB; with $`R = 0.995`$ the −3 dB point ≈ $`f_s(1-R)/2\pi`$ ≈ 38.2 Hz, and the measured value (`measureGainDb`) is also −3.01 dB.

**Further thought:** What are the benefits and drawbacks of making $`R`$ closer to 1? How long does a suddenly appearing DC (a step) take to be "eaten up"? (The solution file measures the step response.)

---

## Exercise 3 ★★☆ (implementation): two biquads = a 4th-order Butterworth lowpass

File: `starter/ex3_butterworth4.cpp` → target `l02_ex3_starter`

1. Work out the Q of each of the two stages of a 4th-order Butterworth ($`Q_k = 1/(2\cos((2k+1)\pi/(2N)))`$ for even $`N`$, or simply use `dawlab::dsp::butterworthQ(4, k)`).
2. In `setCutoff()`, set the two `makeLowpass` stages with the same $`f_c`$ and their own Qs.
3. `processSample()` chains the two stages; `magnitudeDb()` adds the dB values of the two stages.
4. Verify: −3.01 dB at the cutoff and about −24.1 dB at $`2f_c`$ (tolerance 0.3 dB), with the measurement agreeing with theory.

**Further thought:** If both stages use Q = 0.707, how many dB is the response at the cutoff? What is this kind of filter called, and where is it used?

---

## Exercise 4 ★★☆ (implementation): build a bandpass from an HPF + LPF

File: `starter/ex4_bandpass_cascade.cpp` → target `l02_ex4_starter`

Cascade a Butterworth highpass ($`f_\text{low} = 500`$ Hz) with a Butterworth lowpass ($`f_\text{high} = 2`$ kHz) to make a bandpass that "keeps only 500 Hz–2 kHz".

1. Complete `setBand()`, `processSample()` and `magnitudeDb()`.
2. Verify: about −3 dB at the two edges; about −0.53 dB at the center of the passband (1 kHz) (why is it not 0 dB?); about −24 dB two octaves outside the edge (125 Hz).
3. Compare with RBJ's second-order `makeBandpass` (center $`\sqrt{500\cdot2000} = 1`$ kHz, $`Q = f_0/\text{BW} = 1000/1500`$): how different are the slopes? What are the pros and cons of each?

---

## Exercise 5 ★★☆ (calculation + implementation): compute the poles from the coefficients and judge stability

**Hand-calculation part:** For each of the following denominator coefficient sets ($`1 + a_1z^{-1} + a_2z^{-2}`$), compute the poles and $`|p|`$ and decide whether it is stable; then check again with the triangle condition $`|a_2| < 1`$ and $`|a_1| < 1 + a_2`$.

| | $`a_1`$ | $`a_2`$ |
|---|---:|---:|
| (a) | −1.8 | 0.81 |
| (b) | −1.9 | 1.0 |
| (c) | −2.1 | 1.1025 |
| (d) | 0.5 | −0.6 |

**Implementation part:** File `starter/ex5_pole_stability.cpp` → target `l02_ex5_starter`

1. `computePoles()`: solve $`z^2 + a_1 z + a_2 = 0`$ with `std::complex<double>`.
2. `isStable()`: both poles have $`|p| < 1`$.
3. `tailToHeadRatio()`: feed an impulse into a `Biquad` and return "the peak of the last 10% of samples ÷ the peak of the first 10%" -- a stable filter should approach 0.
4. Verify: the results agree with `BiquadCoefficients::poles()` / `isStable()`.

**Further thought:** What do the radius $`r`$ and the angle $`\theta`$ of a conjugate pole pair each determine? Work out the resonant frequency and the decay time constant for a 1 kHz, Q = 8 LPF.

---

## Exercise 6 ★★☆ (implementation): FIR moving average vs. one-pole

File: `starter/ex6_moving_average.cpp` → target `l02_ex6_starter`

1. Implement an $`N`$-point moving average with a **running sum**: for each sample "add the new one and subtract the one from $`N`$ samples ago", so the cost is independent of $`N`$.
2. `magnitudeDb()`: $`|H| = \left|\dfrac{\sin(N\omega/2)}{N\sin(\omega/2)}\right|`$.
3. Verify ($`N = 8`$): the −3 dB point ≈ $`0.443 f_s/N`$; a null at $`f_s/N = 6`$ kHz; about −12.96 dB at $`1.5f_s/N`$ (9 kHz) (close to the peak of the first sidelobe: the peak is at about 8.6 kHz, −12.8 dB); and the step response settles in exactly $`N`$ samples.
4. Compare with a one-pole that has the same −3 dB point: how do the passband, nulls, sidelobes, step response and phase differ? And how do they differ to the ear?

---

## Exercise 7 ★☆☆ (concept): why Transposed Direct Form II?

Answer in your own words (you may refer to the output of `l02_direct_forms`):

1. How many state variables do DF-I, DF-II and TDF-II each need? What quantity does each one's state "represent"?
2. Why is DF-II problematic for a lowpass with a low cutoff? What are the consequences in fixed-point and in floating-point?
3. Which structure clicks when the coefficients are changed between blocks? Why?
4. Besides choosing TDF-II, what other decision did `dawlab::dsp::Biquad` make to bring the error of a 30 Hz lowpass from about −70 dB down to below −130 dB? Is doing only half of it (for example switching only the state to double) enough?
5. If the cutoff must be modulated on every sample (for example a synthesizer's filter envelope), which structure would you choose? Why?
