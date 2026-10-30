# Chapter 03 Exercises: Parametric EQ
**English** | [繁體中文](README.zh-TW.md)

Difficulty: ★☆☆ basic / ★★☆ intermediate / ★★★ advanced.
The starter code for the programming exercises is in [`starter/`](starter/) and compiles as-is. When you run it, the self-check prints `[FAIL]`; that is expected. Fill in the `// TODO`s until it prints `[PASS]`. Complete solutions are in [`../solutions/`](../solutions/), with explanations in [`../solutions/README.md`](../solutions/README.md).

Build and run (using Chapter 03 as the example):

```powershell
.\scripts\build.ps1 -Only 03-eq                      # build + run the tests for the examples and solutions
cd build
.\bin\Debug\l03_ex2_starter.exe                      # run your own starter code
```

<div align="center">

| # | Type | Difficulty | Starter code | starter target | solution target |
|---|---|---|---|---|---|
| 1 | Calculation | ★☆☆ | — | — | (the numbers are verified in `l03_ex2_solution`) |
| 2 | Programming | ★☆☆ | `starter/ex2_q_bandwidth.cpp` | `l03_ex2_starter` | `l03_ex2_solution` |
| 3 | Programming | ★☆☆ | `starter/ex3_cascade_sum.cpp` | `l03_ex3_starter` | `l03_ex3_solution` |
| 4 | Programming | ★★☆ | `starter/ex4_graphic_eq.cpp` | `l03_ex4_starter` | `l03_ex4_solution` |
| 5 | Programming | ★★☆ | `starter/ex5_tilt_eq.cpp` | `l03_ex5_starter` | `l03_ex5_solution` |
| 6 | Programming | ★★★ | `starter/ex6_switchable_highcut.cpp` | `l03_ex6_starter` | `l03_ex6_solution` |
| 7 | Concept | ★★☆ | — | — | — |

</div>

---

## Exercise 1: Work out A, Q, bandwidth, and the RBJ coefficients by hand (calculation, ★☆☆)

No computer (a calculator is fine). Write out your calculations:

1. The $`A`$ of an RBJ peak / shelf: what is $`A`$ for gain = +6 dB and for −12 dB? What is the **linear** gain at the center of a +6 dB peak?
2. What Q corresponds to a bandwidth of 2/3 octave? How many octaves does Q = 2 correspond to?
3. For a peak with $`f_0`$ = 1 kHz and Q = √2, where are the two bandwidth edges (the "midpoint gain" edges)? With +12 dB, what is the gain in dB at the edges?
4. A peak with $`f_s`$ = 48 kHz, $`f_0`$ = 12 kHz, Q = 1, gain = +6 dB: compute the normalized $`b_0, b_1, b_2, a_1, a_2`$, and verify that the DC gain is 0 dB and the gain at $`f_0`$ is +6 dB.
5. A +6 dB peak and a −6 dB peak (both at 1 kHz, Q = 1) are cascaded. What is the overall curve, and why?

> Hint: Sections 2.4 and 2.5 of the README. For part 4, $`\omega_0 = \pi/2`$ makes $`\cos\omega_0 = 0`$, which simplifies the arithmetic a lot. At $`f_0`$, $`z = e^{j\pi/2} = j`$, so $`z^{-1} = -j`$ and $`z^{-2} = -1`$.

---

## Exercise 2: Q ↔ bandwidth conversion and verification (programming, ★☆☆)

Starter code: [`starter/ex2_q_bandwidth.cpp`](starter/ex2_q_bandwidth.cpp)

1. Implement three functions: `qToBandwidthOctaves(q)`, `bandwidthOctavesToQ(bw)`, and `bandEdges(f0, q, fLow, fHigh)` (formulas in README Section 2.4).
2. The self-check verifies: 1 octave ↔ Q = √2, 1/3 octave ↔ Q ≈ 4.3185, round-trip conversions return to the starting value, $`f_1 f_2 = f_0^2`$, and $`f_2 - f_1 = f_0/Q`$.
3. Complete the TODOs in `main()`: use `BiquadCoefficients::makePeak(48000, 1000, √2, 12)` to verify that the gain at the edges is +6 dB (the midpoint gain), and use `makeBandpass` to verify that the same edges are at −3 dB. Then use `measureGainDb` to measure it once by feeding in actual sine waves.

> Hint: `std::asinh` and `std::sinh` are in `<cmath>`.

---

## Exercise 3: Verify that cascaded peaks add in dB (programming, ★☆☆)

Starter code: [`starter/ex3_cascade_sum.cpp`](starter/ex3_cascade_sum.cpp)

1. Complete `Cascade2::processSample`: `y = H2(H1(x))`.
2. Two peaks: +6 dB @ 500 Hz (Q 1) and −4 dB @ 2 kHz (Q 2). At 8 frequencies, compare: each one's dB, the sum of the dB values, the dB of $`|H_1 H_2|`$ (multiply the complex values from `response()`), and the measured value (`measureGainDb`).
3. Reverse the cascade order and confirm that the measured result does not change.
4. Bonus: cascade two +6 dB, Q = √2 peaks @ 1 kHz. The center should be +12 dB and 1414 Hz should be +6 dB.

> Hint: $`\log(ab) = \log a + \log b`$.

---

## Exercise 4: 10-band graphic EQ (programming, ★★☆)

Starter code: [`starter/ex4_graphic_eq.cpp`](starter/ex4_graphic_eq.cpp)

Build a 10-band graphic EQ: center frequencies $`31.25 \times 2^k`$ Hz ($`k = 0..9`$, i.e. 31.25, 62.5, 125 … 16000 Hz), each band 1 octave wide, and the user can only move the gain (±12 dB).

1. Use the formula from Exercise 2 to turn "1 octave" into `kQ`.
2. Complete `setGainDb` (recompute that band's coefficients), `getMagnitudeDb` (sum the dB of all bands), `processSample`, and `process`.
3. Look at the numbers the self-check prints: with only 1 kHz pushed +6 dB, how much are 500 Hz and 2 kHz lifted? With **all** bands pushed +6 dB, what is the level at 1 kHz? Why isn't it +6?
4. Apply a "smiley curve" to the drum loop and write the output to `l03_ex4_graphic_smile.wav`.

> Hint: the "skirts" of adjacent bands add up (README Section 2.11).

---

## Exercise 5: Tilt EQ (programming, ★★☆)

Starter code: [`starter/ex5_tilt_eq.cpp`](starter/ex5_tilt_eq.cpp)

A tilt EQ has only two parameters: the pivot frequency and the tilt (in dB). The curve behaves like a seesaw: 0 dB at the pivot, $`-t/2`$ toward the low end, and $`+t/2`$ toward the high end.

1. Use one low shelf (gain $`-t/2`$) plus one high shelf (gain $`+t/2`$), both placed at the pivot with Q = 0.707.
2. Complete `getMagnitudeDb` and `updateCoefficients`.
3. The self-check verifies: 0 dB at the pivot, ∓3 dB at the two ends (tilt = +6), antisymmetry about the pivot, a monotonic curve, and that $`-t`$ is the mirror image of $`+t`$.
4. Bonus: make `setTiltDb` smooth with `SmoothedValue`, and have `process()` recompute the coefficients every 32 samples. The pluck sequence switches from +6 to −6 dB at the 2-second mark and must not click. Write the output to `l03_ex5_tilt.wav`.

> Hint: why is the pivot exactly 0 dB? See README Section 2.6: "the $`f_0`$ of a shelf is the midpoint gain".

---

## Exercise 6: A high cut switchable between 12 / 24 dB/oct (programming, ★★★)

Starter code: [`starter/ex6_switchable_highcut.cpp`](starter/ex6_switchable_highcut.cpp)

1. 12 dB/oct = one Butterworth lowpass (Q = 1/√2); 24 dB/oct = two cascaded lowpasses, using `butterworthQ(4, 0)` and `butterworthQ(4, 1)` for Q (`Biquad.h` from Chapter 02).
2. Implement the theoretical digital Butterworth response $`|H|^2 = 1/\left(1 + \left(\frac{\tan(\pi f/f_s)}{\tan(\pi f_c/f_s)}\right)^{2N}\right)`$, and verify that the measured response of both slopes matches it, and that both are −3.01 dB at $`f_c`$.
3. Switching the slope must not click: run both chains at the same time and use `SmoothedValue` for a 20 ms crossfade. The self-check compares the largest sample-to-sample jump of a 1 kHz (= $`f_c`$) sine wave under "direct switching" versus "crossfade".
4. Listen: drum loop, high cut at 2 kHz, 12 dB/oct for the first 2 seconds and 24 dB/oct for the last 2 seconds. Write the output to `l03_ex6_highcut_switch.wav`.

> Hint: why does direct switching click? Think about the phase of the 2nd-order and 4th-order Butterworth at $`f_c`$.

---

## Exercise 7: EQ and phase (concept, ★★☆)

Answer in your own words (a few sentences each):

1. Why must an IIR EQ that changes the magnitude also change the phase? What is minimum phase?
2. For a +6 dB @ 1 kHz, Q = 1 peak, roughly what is the phase at 500 Hz and at 2 kHz? In which direction (leading / lagging)? What if it were −6 dB? (You can support your answer with the output of the example `l03_eq_phase_cramping`.)
3. With an IIR EQ on a single track, the phase shift is usually inaudible. When does it become a problem?
4. What are the costs of a linear-phase EQ? How many ms of latency does a 4097-tap linear-phase FIR have at 48 kHz?
5. What is pre-ringing? Why is it especially noticeable on kick and snare?
6. When mastering, you want a very narrow cut at 40 Hz. Do you use linear-phase or minimum-phase? Explain your trade-off.
