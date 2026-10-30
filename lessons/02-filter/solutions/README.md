# Chapter 02 Exercise Solutions
**English** | [繁體中文](README.zh-TW.md)

The complete programs for the implementation problems are in this folder (`exN_xxx.cpp`). Each one verifies its results numerically with `DAWLAB_CHECK` / `DAWLAB_CHECK_NEAR` and is run automatically by ctest (label `solution`). All the numbers quoted below are what the solution programs (or the example programs) actually print ($`f_s = 48`$ kHz).

```powershell
.\scripts\build.ps1 -Only 02-filter -TestLabel solution
cd build
.\bin\Debug\l02_ex3_solution.exe
```

---

## Exercise 1: compute the one-pole coefficients by hand

**1.** $`f_c = 1`$ kHz:

```math
\frac{2\pi f_c}{f_s} = \frac{2\pi\cdot1000}{48000} = 0.130900,\qquad
a = e^{-0.130900} = 0.877306,\qquad 1 - a = 0.122694
```

$`f_c = 100`$ Hz: the exponent is 0.0130900, $`a = e^{-0.0130900} = 0.986995`$, $`1 - a = 0.013005`$. When the cutoff drops by a factor of 10, $`1-a`$ also drops by roughly a factor of 10 (for a low cutoff $`1 - a \approx 2\pi f_c/f_s`$).

**2.** $`a = e^{-1/\tau}`$ ⇒ $`\tau = -1/\ln a = f_s / (2\pi f_c) = 48000 / 6283.19 = 7.64`$ samples $`= 0.159`$ ms. This is exactly the same as the $`\tau = 1/(2\pi f_c)`$ of an analog RC -- which is precisely what impulse invariance means.

**3.** Substitute $`z = -1`$ into $`H(z) = (1-a)/(1 - a z^{-1})`$:

```math
H(-1) = \frac{1-a}{1+a} = \frac{0.122694}{1.877306} = 0.065357 \;\Rightarrow\; -23.69\text{ dB}
```

It is not $`-\infty`$ because this $`H(z)`$ has no zero at $`z = -1`$ (the numerator is a constant). An analog RC lowpass only reaches 0 at "infinitely high frequency", and impulse invariance merely copies the impulse response at the sampling instants; it does not move "infinitely high frequency" to Nyquist.

**4.** The bilinear version:

```math
g = \tan\frac{\pi\cdot1000}{48000} = \tan(0.065450) = 0.065543,\quad
\text{pole} = \frac{1-g}{1+g} = 0.876976,\quad
b_0 = b_1 = \frac{g}{1+g} = 0.061512
```

The pole differs from the exp version's $`a`$ (0.877306) by only 0.00033, so for a low cutoff the two are almost identical. The main difference is in the numerator: the bilinear version is $`b_0(1 + z^{-1})`$, which adds a zero at $`z = -1`$, so Nyquist is $`-\infty`$ dB, and the DC gain is $`2b_0/(1 - \text{pole}) = 0.123024/0.123024 = 1`$.

**5.** $`f_c = 10`$ kHz: $`a = e^{-1.308997} = 0.270091`$.

```math
\cos\omega_{3\text{dB}} = 1 - \frac{(1-a)^2}{2a} = 1 - \frac{0.532767}{0.540182} = 0.013727
\;\Rightarrow\; \omega = 1.55707 \text{ rad}
\;\Rightarrow\; f = \frac{\omega f_s}{2\pi} = 11895 \text{ Hz}
```

That is 19% higher than the set value.

**Cross-check:** section [1] of `l02_filter_response` prints the `a=exp(..)` column (0.986995, 0.877306, ...), the `exp -3dB` column (1001.4, 11895.1 Hz, ...) and the two "Hand-calculation check" lines ($`a`$ = 0.877306, Nyquist −23.69 dB, $`g`$ = 0.065543, pole = 0.876976, $`b_0`$ = 0.061512), all verified by self-checks.

---

## Exercise 2: DC blocker

Solution: [`ex2_dc_blocker.cpp`](ex2_dc_blocker.cpp) (target `l02_ex2_solution`)

The two key lines:

```cpp
const double y = x - s.x1 + r_ * s.y1;   // y[n] = x[n] - x[n-1] + R y[n-1]
// Theoretical response: H(e^{jω}) = (1 - e^{-jω}) / (1 - R e^{-jω})
return gainToDb(std::abs((1.0 - z1) / (1.0 - r_ * z1)));
```

Results ($`R = 0.995`$):

```
DC: input mean 0.5000 -> output mean -0.000000
1 kHz: input amplitude 0.2500 -> output amplitude 0.2504 (0.015 dB)
-3 dB point: theoretical approximation 38.2 Hz, computed from H(z) 38.1 Hz
Measured: 38.1 Hz = -3.01 dB, 20 Hz = -6.67 dB (theory -6.67 dB)
Step response: τ = 199 samples (4.2 ms), 918 samples to fall to 1% (theory ~ 4.6 τ = 919)
```

**Why the −3 dB point ≈ $`f_s(1-R)/2\pi`$:** at low frequencies $`e^{-j\omega} \approx 1 - j\omega`$, so $`H \approx \dfrac{j\omega}{(1-R) + jR\omega} \approx \dfrac{j\omega}{(1-R) + j\omega}`$. This is a first-order highpass with its cutoff at $`\omega_c = 1-R`$, which in Hz is $`f_s(1-R)/2\pi`$.

**The trade-off in $`R`$:** the closer $`R`$ is to 1, the lower the cutoff and the less it affects bass (for example a 40 Hz kick); but the closer the pole is to $`z = 1`$, the longer the time constant $`\tau = -1/\ln R \approx 1/(1-R)`$, and the longer a suddenly appearing DC offset takes to be "eaten up" (with $`R = 0.995`$, 99% takes about $`4.6\tau`$ = 918 samples ≈ 19 ms; with $`R = 0.999`$, $`\tau`$ is about 1000 samples, which is 5 times longer). In practice $`R`$ is usually 0.995–0.999, or it is derived from the sample rate, $`R = e^{-2\pi f_c / f_s}`$, so that the cutoff does not change with the sample rate.

---

## Exercise 3: 4th-order Butterworth lowpass

Solution: [`ex3_butterworth4.cpp`](ex3_butterworth4.cpp) (target `l02_ex3_solution`)

The key: the two stages use the **same** $`f_c`$ but **different** Qs:

```cpp
for (int k = 0; k < 2; ++k)
    stages_[k].setCoefficients(BiquadCoefficients::makeLowpass(sampleRate_, hz, butterworthQ(4, k)));
```

$`Q_0 = 1/(2\cos(\pi/8)) = 1/(2 \times 0.92388) = 0.5412`$, $`Q_1 = 1/(2\cos(3\pi/8)) = 1/(2\times0.38268) = 1.3066`$. Intuition: the low-Q stage "sags" near the cutoff and the high-Q stage "bulges" near the cutoff, and added together they are exactly maximally flat in the passband.

**Be careful with odd orders:** the angle formula $`(2k+1)\pi/(2N)`$ above only applies to even $`N`$. For odd $`N`$ the pole angle is $`(k+1)\pi/N`$, plus one bilinear one-pole stage (README section 2.13): 3rd order = a one-pole + a biquad with Q = 1.0. At the end the solution file verifies `butterworthQ(3, 0)` = 1.0, `butterworthQ(5, k)` = 0.6180 / 1.6180, and that the 3rd-order cascade is −3.01 dB at $`f_c`$ and −0.067 dB at 500 Hz (matching analog). If you misapply the even-order formula, $`N = 3`$ gives Q = 0.5774, and the response at $`f_c`$ becomes −7.78 dB.

```
  f (Hz)    analog BW4    2-stage dB      measured   single biquad
     500         -0.02         -0.02         -0.02           -0.26
    1000         -3.01         -3.01         -3.01           -3.01
    2000        -24.10        -24.25        -24.25          -12.37
    4000        -48.16        -48.92        -48.92          -24.48
    8000        -72.25        -75.59        -75.59          -37.80
```

- −3.01 dB at the cutoff ✓; −24.25 dB one octave out (the analog theory is $`-10\log_{10}(1+2^8) = -24.10`$ dB, and the extra 0.15 dB is the bilinear cramping); −48.92 dB two octaves out (analog −48.16).
- The passband is much flatter than a single biquad: only 0.02 dB down at 500 Hz (a single biquad is down 0.26 dB).
- With both stages at Q = 0.707: the response at the cutoff is $`2 \times (-3.01) = -6.02`$ dB. This is a **4th-order Linkwitz-Riley (LR4)**: the LR4 LPF and HPF are both −6 dB at the cutoff and in phase, so their sum has a perfectly flat magnitude, which is why it is used in crossovers (multiband processing, speaker crossovers). It is not a Butterworth.

Output WAVs: `l02_ex3_noise_butterworth4.wav`, `l02_ex3_noise_biquad2.wav`. The RMS of the white noise differs by only 0.3 dB (−30.07 vs. −30.38 dB, because most of the remaining energy is in the passband), but listen closely and the 4th-order version has a cleaner "hiss" above 1 kHz.

---

## Exercise 4: build a bandpass from an HPF + LPF

Solution: [`ex4_bandpass_cascade.cpp`](ex4_bandpass_cascade.cpp) (target `l02_ex4_solution`)

```cpp
hp_.setCoefficients(BiquadCoefficients::makeHighpass(sampleRate_, lowHz, BiquadCoefficients::kButterworthQ));
lp_.setCoefficients(BiquadCoefficients::makeLowpass(sampleRate_, highHz, BiquadCoefficients::kButterworthQ));
// Cascade = responses multiply = dB values add
```

```
  f (Hz)    cascade dB      measured    RBJ BPF dB
   125.0        -24.11        -24.11        -14.57
   500.0         -3.03         -3.03         -3.02
  1000.0         -0.52         -0.52          0.00
  2000.0         -3.03         -3.03         -3.04
  8000.0        -25.69        -25.69        -15.39
```

- **About −3 dB at the edges**: the HPF is −3.01 dB at 500 Hz, and the LPF contributes only $`-10\log_{10}(1 + (500/2000)^4) = -0.017`$ dB at 500 Hz, for a total of −3.03 dB.
- **The center is not 0 dB**: at 1 kHz neither the HPF nor the LPF has fully "opened up": each contributes $`-10\log_{10}(1 + 0.5^4) = -0.26`$ dB, about −0.53 dB in total (−0.52 dB measured digitally). The closer the two edges, the deeper the center sags; when the edges are very close you should switch to the RBJ bandpass or a peak filter.
- **Slope**: the cascade is 12 dB/oct on both sides (about −24 dB two octaves outside the edge; −25.7 dB on the 8 kHz side because of cramping); the RBJ second-order bandpass is only 6 dB/oct on each side (−14.6 / −15.4 dB, analog theory −14.56 dB).

**Pros and cons:** the cascade lets you set the lower and upper edges independently, has twice the slope (and you can cascade even more stages), and suits a "telephone effect" or "keep only the vocal band"; the price is two biquads and a center that is not exactly 0 dB. The RBJ bandpass needs just one biquad, is exactly 0 dB at the center, and uses a single Q to control the width, which suits applications where the center frequency is swept (wah, the detector of a de-esser).

Output WAVs: `l02_ex4_noise_cascade.wav`, `l02_ex4_noise_rbj_bpf.wav` (RMS −28.37 vs. −27.27 dB: the steeper version leaves less energy and sounds "narrower").

---

## Exercise 5: compute the poles from the coefficients and judge stability

**By hand:** $`p = \dfrac{-a_1 \pm \sqrt{a_1^2 - 4a_2}}{2}`$

<div align="center">

| | discriminant $`a_1^2 - 4a_2`$ | poles | $`\max\lvert p\rvert`$ | triangle condition | conclusion |
|---|---:|---|---:|---|---|
| (a) $`-1.8, 0.81`$ | $`3.24 - 3.24 = 0`$ | 0.9 (double root) | 0.9 | $`0.81 < 1`$, $`1.8 < 1.81`$ ✓ | stable |
| (b) $`-1.9, 1.0`$ | $`3.61 - 4 = -0.39`$ | $`0.95 \pm 0.3122j`$ | $`\sqrt{a_2} = 1`$ | $`\lvert a_2\rvert = 1`$, fails $`< 1`$ | marginal (an oscillator that does not decay) |
| (c) $`-2.1, 1.1025`$ | $`4.41 - 4.41 = 0`$ | 1.05 (double root) | 1.05 | $`\lvert a_2\rvert > 1`$ ✗ | unstable |
| (d) $`0.5, -0.6`$ | $`0.25 + 2.4 = 2.65`$ | $`0.5639`$, $`-1.0639`$ | 1.0639 | $`0.5 < 0.4`$? ✗ | unstable |

</div>

(b) is interesting: the poles lie exactly on the unit circle, and the impulse response is a sine that never decays, at frequency $`\theta f_s/2\pi = \arccos(0.95)\cdot 48000/2\pi \approx 2.43`$ kHz -- this is the principle of a "digital sine oscillator". (d) is a negative real pole outside the circle: the sign flips on every sample while the amplitude is multiplied by 1.064, blowing up near Nyquist.

**Implementation:** [`ex5_pole_stability.cpp`](ex5_pole_stability.cpp) (target `l02_ex5_solution`)

```cpp
const std::complex<double> disc = std::sqrt(std::complex<double>(a1 * a1 - 4.0 * a2, 0.0));
return {(-a1 + disc) * 0.5, (-a1 - disc) * 0.5};
```

It uses `std::complex`'s `sqrt`, which automatically yields an imaginary result when the discriminant is negative, so no case split is needed. Program output:

```
case                                                         pole 1                 pole 2   |p|max    stable?  tail/head
LPF 1 kHz Q=0.707                                0.9077   +0.0845j     0.9077   -0.0845j   0.9116        yes          0
LPF 1 kHz Q=8                                    0.9834   +0.1292j     0.9834   -0.1292j   0.9919        yes          0
a1=-1.8  a2=0.81 (double real pole 0.9)          0.9000   +0.0000j     0.9000   +0.0000j   0.9000        yes   2.05e-30
a1=-1.9  a2=1.0  (|p| = 1, marginal)             0.9500   +0.3122j     0.9500   -0.3122j   1.0000         NO          1
a1=-2.1  a2=1.1025 (p = 1.05, unstable)          1.0500   +0.0000j     1.0500   +0.0000j   1.0500         NO        inf
a1=0.5   a2=-0.6  (real poles 0.564, -1.064)     0.5639   +0.0000j    -1.0639   +0.0000j   1.0639         NO        inf
```

The tail/head column is exactly as expected: stable cases decay to 0, the marginal one stays at 1, and unstable ones become inf. The program also scans 5861 points of the $`(a_1, a_2)`$ plane, and the triangle condition agrees with "computing the poles directly" on all of them.

**Further thought:** for a conjugate pair $`re^{\pm j\theta}`$, the **angle $`\theta`$ determines the resonant frequency** ($`f \approx \theta f_s / 2\pi`$) and the **radius $`r`$ determines the decay rate** (envelope $`r^n`$, $`\tau = -1/\ln r`$ samples). For the 1 kHz, Q = 8 LPF: $`r = \sqrt{a_2} = 0.991875`$, $`\theta = 0.13065`$ rad → 998.1 Hz, $`\tau = 123`$ samples (2.6 ms). Measured, the impulse response decays by −70.33 dB after 1000 samples, against a theoretical $`20\log_{10}r^{1000} = -70.86`$ dB. For high Q there is a handy approximation: $`r \approx e^{-\omega_0/(2Q)}`$ ($`0.1309/16 = 0.00818`$, $`e^{-0.00818} = 0.99185`$) -- the higher the Q, the closer the poles hug the unit circle and the longer the ringing.

---

## Exercise 6: FIR moving average vs. one-pole

Solution: [`ex6_moving_average.cpp`](ex6_moving_average.cpp) (target `l02_ex6_solution`)

The core of the running sum:

```cpp
sum_[ch] += x - h[idx];   // add the new sample, subtract the one from N samples ago
h[idx] = x;
idx = (idx + 1) % length_;
return static_cast<float>(sum_[ch] / length_);
```

(The accumulator is a double: a float running sum accumulates error after running for a long time, because the rounding of "adding in" and "subtracting out" does not cancel exactly.)

Results ($`N = 8`$, with the one-pole's cutoff set to the moving average's actual −3 dB point, 2676 Hz):

```
   f (Hz)     MA theory   MA measured      one-pole
     1000         -0.39         -0.39         -0.56
     2676         -3.01         -3.01         -2.97
     4000         -7.57         -7.57         -5.00
     6000       -100.00       -100.00         -7.58
     9000        -12.96        -12.96        -10.40
    12000       -100.00       -100.00        -12.34
    20000        -19.01        -19.01        -14.93
```

<div align="center">

| Comparison | moving average (FIR) | one-pole (IIR) |
|---|---|---|
| −3 dB point | 2676 Hz (approximation $`0.443 f_s/N`$ = 2658) | set to the same |
| Passband | flatter (1 kHz −0.39 dB) | starts dropping earlier (−0.56 dB) |
| Stopband | complete nulls at $`kf_s/N`$ (6, 12 kHz), with sidelobes between nulls (peak at about 8.6 kHz, −12.8 dB; −12.96 dB at 9 kHz in the table) | falls monotonically, no nulls and no sidelobes |
| step response | settles in exactly $`N = 8`$ samples | can only "approach": 14 samples to reach 99% ($`\approx 4.6\tau`$) |
| impulse response | eight 0.125s, then all zeros | $`0.296, 0.208, 0.147, \dots`$ never zero |
| Phase | linear phase: a fixed delay of $`(N-1)/2 = 3.5`$ samples (3.492 measured) | non-linear phase |
| Cost | running sum is O(1), but needs to store $`N`$ samples | O(1), only 1 state |

</div>

**By ear:** the RMS of white noise through the two is −25.79 dB and −24.40 dB. The moving average's spectrum has regular "holes" and sounds slightly hollow and "comb-like"; the one-pole is a smooth darkening. The moving average's strength is in the "time domain": its step response completes in a definite time with no overshoot, so it is commonly used as an RMS detection window (Chapters 04 and 11) rather than for tonal processing.

Output WAVs: `l02_ex6_noise_moving_average.wav`, `l02_ex6_noise_one_pole.wav`.

---

## Exercise 7: why Transposed Direct Form II?

**1. Number and meaning of the states.**
- DF-I: 4 ($`x[n-1], x[n-2], y[n-1], y[n-2]`$), which are the real past inputs and outputs.
- DF-II: 2 ($`w[n-1], w[n-2]`$); $`w`$ is the intermediate signal that has passed through "only the pole (feedback) part".
- TDF-II: 2 ($`s_1, s_2`$), which are "partial sums not yet added into the output", of about the same magnitude as the output.

**2. The problem with DF-II.** For a lowpass with a low cutoff the poles are very close to $`z = 1`$, so the DC gain of the pole part, $`1/(1 + a_1 + a_2)`$, is very large (and is then scaled back down by the tiny $`b`$ coefficients). For the 30 Hz LPF in `l02_direct_forms`: the peak of $`w[n]`$ is 18908, which is 65422 times the output of 0.289. In **fixed-point** this overflows outright (values are saturated or wrap around and the output is ruined); in **floating-point** it does not overflow, but adding a small number to a big one swallows the low bits of the input, and changing the coefficients causes trouble (see 3).

**3. Who clicks when the coefficients change.** In DF-II the output is $`y = b_0 w + b_1 w_1 + b_2 w_2`$, where $`w`$ is a quantity amplified tens of thousands of times, so the moment $`b`$ changes the product jumps a big step -- measured, the jump at the coefficient change is 983 times normal. The state of DF-I is the real input and output, and the state of TDF-II is of the same magnitude as the output, so on a coefficient change both show only 1.6 / 2.3 times normal.

**4. The key to precision is double coefficients + double state.** Measured, with float all three structures have round-off at −70 ~ −74 dB (TDF-II is even slightly worse). Doing only half is not enough: taking DF-I as the example, float coefficients + double state gives −89.93 dB (the poles are already off when the coefficients are rounded to float, so a different filter is computed), and double coefficients + float state gives −83.77 dB; with both coefficients and state in double, the difference from the double TDF-II reference (double output) falls to −242.69 dB -- the two double implementations agree almost perfectly, with round-off far below the audible range. That is why the coefficients and state of `dawlab::dsp::Biquad` are both double, with only the input and output in float; its measured −151.20 dB error is exactly the quantization floor of "rounding the double output to float". The contribution of TDF-II is "2 states + no internal amplification + smooth on coefficient changes", and the contribution of double is "precision"; together they make a good biquad.

**5. Modulating the cutoff on every sample.** Choose the TPT state-variable filter (`StateVariableFilter.h`): a per-sample update costs just one `tan` (no RBJ `sin` / `cos` and no normalization), the states are the integrators' physical quantities so it behaves well under modulation, and it provides LP / BP / HP / notch at the same time. [B2] of `l02_cutoff_modulation` shows that the biquad that "updates once per block" differs from the ideal smooth modulation by −2.97 dB, while the per-sample SVF differs by only −25.69 dB; [C] shows that their static responses are exactly the same.
