# Chapter 06 Solutions: Saturation and Aliasing

**English** | [繁體中文](README.zh-TW.md)

Every solution program verifies its results numerically with `DAWLAB_CHECK` / `DAWLAB_CHECK_NEAR`; when everything passes it prints `[PASS]` and returns 0 (ctest label: `solution`).

```powershell
.\scripts\build.ps1 -Only 06-saturation -TestLabel solution
```

---

## Exercise 1: Symmetry, even harmonics, DC, and latency alignment

**1. Why does tanh produce only odd harmonics and no DC?**

A sine wave has half-wave symmetry: $`x(t + T/2) = -x(t)`$. tanh is an odd function: $`\tanh(-u) = -\tanh(u)`$. Therefore

```math
y(t + T/2) = \tanh\big(x(t+T/2)\big) = \tanh\big(-x(t)\big) = -\tanh\big(x(t)\big) = -y(t)
```

The output also has half-wave symmetry. Write $`y`$ as a Fourier series $`\sum_k c_k e^{jk\omega_0 t}`$; shifting by half a period multiplies the $`k`$-th term by $`e^{jk\pi} = (-1)^k`$. For the whole signal to change sign, only terms with $`(-1)^k = -1`$ may be nonzero, so $`k`$ must be odd. The coefficients for $`k = 0`$ (DC) and all even $`k`$ are zero. In the table of the `l06_harmonic_analysis` example, H2, H4, H6, and H8 of Tanh / Cubic / Arctan are all below −99 dB and DC is 0, which is exactly this result.

**2. Why does Asymmetric always have DC? What are the practical problems?**

Split the curve into odd and even parts: $`f(x) = f_{\text{odd}}(x) + f_{\text{even}}(x)`$. The odd part produces only odd harmonics, so DC can only come from the even part. The even part of Asymmetric is (with $`t = \tanh|x|`$ and $`\tanh 2u = 2\tanh u/(1+\tanh^2 u)`$)

```math
f_{\text{even}}(x) = \frac{f(x) + f(-x)}{2} = \frac{1}{2}\left(t - \frac{t}{1+t^2}\right) = \frac{t^3}{2(1+t^2)} \ge 0
```

It is non-negative everywhere and zero only at $`x = 0`$, so as long as the input is not all zeros, the mean (DC) is necessarily positive. For small signals $`f_{\text{even}} \approx |x|^3/2`$ and DC $`\approx 2A^3/(3\pi)`$ (example `l06_harmonic_analysis`: for $`A = 0.1`$ the approximation is 0.00021 and the measurement is 0.00021). More intuitively: the positive half-wave can reach +1 while the negative half-wave only reaches −0.5, so the average is of course greater than 0 (+0.0748 for $`A = 1`$).

Note that here the even part starts at $`|x|^3`$, not $`x^2`$: both sides have slope 1 and curvature 0 at the origin. As long as the even part is not the zero function and does not change sign, DC is not 0; $`x^2`$ is merely the simplest example.

Practical problems (any two will do):
- It eats headroom: the positive half-wave hits 0 dBFS early.
- The detectors of downstream compressors / limiters / gates treat the DC as signal, changing their gain behavior.
- When a track starts, ends, or is muted / unmuted, DC appears or disappears instantly, causing a click.
- When several nonlinear stages are chained, the bias accumulates and the next stage becomes even more asymmetric.
- Some speaker / headphone amplifiers engage protection or are damaged by DC (extreme cases).

**3. Why a cutoff of 5–20 Hz?**

- **Too high (50 Hz)**: it cuts into genuine low-frequency musical content (kick fundamentals at 40–60 Hz, bass E1 = 41 Hz), and a first-order highpass has a significant phase shift near its cutoff, which alters the waveform of the kick.
- **Too low (1 Hz)**: the time constant is $`\tau = 1/(2\pi f_c) = 159`$ ms, so when the DC changes (for example, the amount of asymmetric distortion varies with level) it takes a long time to be removed; decaying to 1% takes about 0.73 s, during which the bias remains, and it also easily leaves a slow "drift" at the start of a track.
- 10 Hz: $`\tau`$ = 16 ms, and decaying to 1% takes about $`\ln 0.01 / \ln R \approx 3516`$ samples = 73 ms (for the DC blocker implementation and step-response measurement, see Chapter 02, Exercise 2). At 20 Hz it attenuates only 0.97 dB, which is practically inaudible, making this a common compromise.

**4. Dry/wet not aligned?**

The wet signal passes through the 4x oversampler and is delayed by 36 samples; the dry signal is not delayed. Their sum is a feedforward comb filter: $`y = (1-m)\,x[n] + m\,x[n - D]`$. At frequency $`f`$ the phase difference between the two paths is $`2\pi f D / f_s`$, and they cancel when the phase difference is an odd multiple of $`\pi`$:

```math
f_{\text{notch}} = \frac{(2k+1) f_s}{2D} = \frac{(2k+1) \cdot 48000}{72} = 667,\ 2000,\ 3333,\ \ldots \text{ Hz}
```

The first notch is at about 667 Hz. The fix: delay the dry signal by `getLatencySamples()` samples (this is exactly what `pushAndReadDry()` inside `Saturation` does).

---

## Exercise 2: $`x^2`$, $`x^3`$, and the cubic soft clip by hand

Solution program: `solutions/ex2_cubic_harmonics.cpp` (target `l06_ex2_solution`)

**1.** $`x^2 = A^2\sin^2\theta = \dfrac{A^2}{2} - \dfrac{A^2}{2}\cos 2\theta`$: DC $`= A^2/2`$, second harmonic amplitude $`A^2/2`$, and **no fundamental**. The program uses $`A = 0.8`$ and measures DC 0.3200, H2 0.3200, H1 < 0.001.

**2.** $`\sin^3\theta = \dfrac{3\sin\theta - \sin 3\theta}{4}`$, so $`x^3 = \dfrac{3A^3}{4}\sin\theta - \dfrac{A^3}{4}\sin 3\theta`$. $`H_3/H_1 = 1/3 \Rightarrow 20\log_{10}(1/3) = -9.54`$ dB, **independent of $`A`$** (because both terms are proportional to $`A^3`$). The program measures −9.54 dB at both $`A = 0.5`$ and $`A = 1`$.

**3.** $`y = x - x^3/3 = \left(A - \dfrac{A^3}{4}\right)\sin\theta + \dfrac{A^3}{12}\sin 3\theta`$.

| $`A`$ | $`H_1`$ | $`H_3`$ | $`H_3/H_1`$ | THD |
|---|---|---|---|---|
| 0.5 | $`0.5 - 0.03125 = 0.46875`$ | $`0.125/12 = 0.0104167`$ | $`20\log_{10}(0.022222) = -33.06`$ dB | 2.222 % |
| 1.0 | $`1 - 0.25 = 0.75`$ | $`1/12 = 0.08333`$ | $`20\log_{10}(0.11111) = -19.08`$ dB | 11.111 % |

For $`|x| \le 1`$ only the third harmonic exists, so THD $`= H_3/H_1`$. The values measured by the program agree with theory (within ±0.05 dB).

**4.** $`y = A\sin\theta + kA^2\sin^2\theta = \dfrac{kA^2}{2} + A\sin\theta - \dfrac{kA^2}{2}\cos 2\theta`$. DC $`= kA^2/2`$, $`H_2/H_1 = kA/2`$. For $`k = 0.5`$, $`A = 0.5`$: $`H_2/H_1 = 0.125 \Rightarrow -18.06`$ dB, DC = 0.0625. Note that $`H_2/H_1`$ is proportional to $`A`$: **even-order distortion increases linearly with level**, which is why a tube preamp "only gets dirty when you play hard".

---

## Exercise 3: Alias frequencies

Solution program: `solutions/ex3_alias_frequencies.cpp` (target `l06_ex3_solution`)

**1.** $`f_s = 44.1`$ kHz, Nyquist 22.05 kHz:

| $`k`$ | $`k f_0`$ | Folds to at 1x |
|---|---|---|
| 3 | 15 kHz | 15 kHz (a true harmonic) |
| 5 | 25 kHz | 44.1 − 25 = **19.1 kHz** |
| 7 | 35 kHz | 44.1 − 35 = **9.1 kHz** |
| 9 | 45 kHz | 45 − 44.1 = **0.9 kHz** |
| 11 | 55 kHz | 55 − 44.1 = **10.9 kHz** |
| 13 | 65 kHz | 65 − 44.1 = **20.9 kHz** |
| 15 | 75 kHz | 88.2 − 75 = **13.2 kHz** |

**2.** The 9th harmonic lands at 900 Hz with an amplitude of 1/9 of the fundamental: $`20\log_{10}(1/9) = -19.08`$ dB. The program builds an approximate square wave as "sine × 100, then clamp to ±1" and measures −19.10 dB (other very high harmonics, such as the 873rd and 891st, also fold to 900 Hz, but their amplitudes are only about 1% of 1/9). It is particularly unpleasant because: (a) it is more than two octaves below 5 kHz; masking extends mainly upward in frequency, so components far below the fundamental are barely masked and stand out; (b) it has no integer relationship to 5 kHz, so it is dissonant; (c) when the input pitch changes, its direction and speed of movement do not match the pitch: this alias $`= 9f_0 - f_s`$ slides at **9 times** the speed in the **same** direction (at $`f_0`$ = 5010 Hz it becomes $`9 \times 5010 - 44100 = 990`$ Hz; the fundamental moved only 10 Hz while it moved 90 Hz), whereas the 5th- and 7th-harmonic aliases ($`f_s - kf_0`$, 19.1 kHz and 9.1 kHz) slide in the **opposite** direction. The program also measures 9.1 kHz (the alias of the 7th harmonic), −16.91 dB versus the theoretical −16.90 dB, and a control at 800 Hz, which is below the measurement floor.

**3.** At 2x, first fold at 88.2 kHz:

| $`k`$ | $`k f_0`$ | At 88.2 kHz | After downsampling |
|---|---|---|---|
| 5 | 25 kHz | 25 kHz | > 22.05 kHz → **filtered out** (strictly speaking it is still at the tail end of the transition band, but it is already attenuated by about 45 dB, and when it folds back to 19.1 kHz only about −59 dB remains, so it can be considered blocked) |
| 7 | 35 kHz | 35 kHz | Filtered out |
| 9 | 45 kHz | 88.2 − 45 = 43.2 kHz | Filtered out |
| 11 | 55 kHz | 33.2 kHz | Filtered out |
| 13 | 65 kHz | 23.2 kHz | In the transition band (stage 1 at 44.1 kHz covers about 18.7–25.7 kHz), **attenuated by only about 15 dB**, and after downsampling folds back to 44.1 − 23.2 = **20.9 kHz** (about −22.3 − 14.6 ≈ −37 dB relative to the fundamental) |
| 15 | 75 kHz | 13.2 kHz | **Still aliases** to 13.2 kHz |

Conclusion: 2x blocks most of them, but higher harmonics still fold back (with an ideal brickwall filter the safe upper limit is $`2f_s - f_s/2 = 66.15`$ kHz; a real halfband has a transition band, so a harmonic close to the limit such as 65 kHz is blocked only partially). The program's table uses the coefficients of this chapter's `Oversampler` stage 1 to compute the actual attenuation inside the transition band. The harmonics of a square wave decay only as $`1/k`$, so a hard clip needs a much higher factor than tanh, or ADAA (Exercise 6).

---

## Exercise 4: Chebyshev harmonic exciter

Solution program: `solutions/ex4_chebyshev_exciter.cpp` (target `l06_ex4_solution`)

**Key ideas**
- `chebyshev(n, x)` uses the recurrence $`T_{n+1} = 2xT_n - T_{n-1}`$; the program first verifies $`|T_n(\cos\theta) - \cos n\theta| < 10^{-5}`$.
- `processSample()` does not need to call `chebyshev()` four times: the same recurrence runs from $`T_2`$ up to $`T_5`$, and each term is multiplied by $`g_n`$ and accumulated as it is computed.
- You must `clamp(x, -1, 1)` first: for $`|x| > 1`$, $`T_n`$ grows exponentially ($`T_5(2) = 362`$).

**Measurements** (amplitude 1, 1 kHz): H2 −20.00 dB, H3 −26.02 dB, H5 −33.98 dB, exactly matching $`20\log_{10} g_n`$; H4 and H6 are −100 dB (absent). A 5th-degree polynomial produces at most the 5th harmonic, so 6th and above must be 0.

**Hand calculation for amplitude 0.5**: only $`T_2`$ produces an even-order component ($`g_4 = 0`$).

```math
T_2(A\cos\theta) = 2A^2\cos^2\theta - 1 = A^2\cos 2\theta + (A^2 - 1)
```

So DC $`= g_2(A^2 - 1) = 0.1 \times (0.25 - 1) = -0.075`$ and the H2 amplitude $`= g_2 A^2 = 0.025`$. The program measures −0.0750 and 0.0250. This illustrates the limitation of a Chebyshev exciter: it is exact only at the design amplitude; when the amplitude changes the harmonic ratios change (H2/H1 goes from 0.1 to 0.05), and the even-order term brings DC, so a DC blocker must follow.

---

## Exercise 5: Latency-aligned dry/wet mix and the comb notches

Solution program: `solutions/ex5_aligned_dry_wet.cpp` (target `l06_ex5_solution`)

(The DC blocker implementation and measurements are in Chapter 02, Exercise 2 and are not repeated here.)

**Key ideas**
- `DryDelay` writes first, then reads: `buf_[pos_] = x`, then it reads `buf_[(pos_ - D + size) % size]`. A length of `maxDelay + 1` is needed to hold the $`D + 1`$ samples from $`x[n]`$ to $`x[n - D]`$; writing before reading is what makes the output equal the input when $`D = 0`$.
- For small signals $`\tanh(gx)/g \approx x`$, so the wet signal is just $`x[n - L]`$, and the whole system is a feedforward comb: $`y[n] = (1 - m)\,x[n - D] + m\,x[n - L]`$. Let $`\Delta = L - D`$; the two paths are in antiphase when the phase difference $`2\pi f\Delta/f_s = (2k + 1)\pi`$:

```math
f_k = \frac{(2k + 1)\,f_s}{2\Delta}
```

- Notch depth: the phasor lengths of the two paths are $`1 - m`$ and $`m`$; in antiphase what remains is $`|1 - 2m|`$. At $`m = 0.5`$ the notch is theoretically infinitely deep; at $`m = 0.3`$ it is $`20\log_{10} 0.4 = -7.96`$ dB.
- After alignment $`|H| = |(1 - m) + m| = 1`$, independent of $`m`$.

**Program output**

```
4x oversampler latency L = 36 samples (0.750 ms)

[Misaligned] dry not delayed, mix 50%:
  notch 0: predicted   666.7 Hz, measured   -89.6 dB
  notch 1: predicted  2000.0 Hz, measured   -96.1 dB
  notch 2: predicted  3333.3 Hz, measured  -103.3 dB
  Peak between two notches (1333.3 Hz): +0.000 dB
  Sweep 500-850 Hz: deepest at 666 Hz (-56.1 dB)
  mix 30%: 667 Hz -7.96 dB (theory 20*log10|1 - 2m| = -7.96 dB)

[Aligned] dry delayed by 36 samples, mix 50%:
     666.7 Hz: +0.0003 dB
    2000.0 Hz: +0.0001 dB
    3333.3 Hz: -0.0001 dB
   10000.0 Hz: -0.0001 dB

[Off by 1 sample] dry delayed by 35 samples:
     1000 Hz: -0.018 dB (theory -0.019 dB)
    10000 Hz: -2.011 dB (theory -2.011 dB)
[PASS] l06_ex5 (14 checks passed)
```

- At the predicted notch positions the measurement is below −90 dB; the depth is limited only by the oversampler's passband error and the slight nonlinearity of $`\tanh`$. The deepest point of the sweep is 666 Hz (−56 dB) rather than something deeper because the 2 Hz grid is 0.67 Hz away from the true 666.67 Hz; the notches are very narrow, which is also why a comb filter sounds "hollow" rather than "missing one frequency band": it is a whole row of thin slots.
- After alignment all four frequencies are within ±0.001 dB, proving that the latency of the FIR halfband is a clean integer delay (the linear phase of README section 2.6).
- When off by one sample, $`|H| = |\cos(\pi f/f_s)|`$: at 1 kHz there is almost no difference (−0.02 dB), but at 10 kHz it is already −2 dB, so "roughly aligned" is not enough; latency must be accurate to the sample.

**Answer to "Think about it"**: at mix = 100% the dry signal does not take part in the mix, so delayed or not makes no audible difference (but a plugin must still report its latency to the host so that it can be aligned with other tracks). The phase of an IIR halfband is not linear, so the "delay" of wet relative to dry varies with frequency, and any integer delay can align them only at certain frequencies; this is what the IIR row in the README section 2.6 table means by "mix has comb filtering or phase shift".

---

## Exercise 6: First-order ADAA hard clip

Solution program: `solutions/ex6_adaa.cpp` (target `l06_ex6_solution`)

**1. The antiderivative**

```math
F(x) = \begin{cases} x^2/2 & |x| \le 1 \\ |x| - 1/2 & |x| > 1 \end{cases}
```

For $`|x| \le 1`$, $`F' = x`$; for $`x > 1`$, $`F' = 1`$; for $`x < -1`$, $`F' = -1`$. At $`x = \pm 1`$ both pieces have the value 1/2, so $`F`$ is continuous. The program checks $`F' = f`$ between −3 and 3 using numerical differentiation.

**2. Implementation notes**
- Use `double` throughout: $`F(x[n]) - F(x[n-1])`$ is a subtraction of two nearby numbers.
- When $`|\Delta| \le 10^{-6}`$, use the midpoint $`f\big((x[n] + x[n-1])/2\big)`$; this is the limit of "the average of $`f`$ over the segment" as $`\Delta \to 0`$, so the output is continuous.
- The state `x1_` is kept across blocks.

**3. Measurements** (7 kHz, peak 2, 48 kHz)

| Method | Fundamental (dBFS) | Alias total |
|---|---|---|
| naive 1x | 1.72 | −24.6 dB |
| ADAA 1x | 1.32 | −42.1 dB |
| naive 2x | 1.72 | −36.8 dB |
| ADAA + 2x | 1.62 | −56.0 dB |
| naive 4x | 1.71 | −49.2 dB |
| naive 8x | 1.71 | −61.7 dB |

ADAA alone at 1x improves by 17.5 dB, which is cleaner than naive 2x; combined with 2x it improves by a further 19.3 dB. The last two rows are the control group: with oversampling alone, a hard clip improves only 12–13 dB per doubling of the factor (the harmonics decay as $`1/k`$), and 8x still has −61.7 dB; "ADAA + 2x" reaches the same order of magnitude with far less computation (the program checks that the gap between the two is < 10 dB).

**4. What is it in the linear region?** For $`|x| \le 1`$, $`y[n] = \dfrac{x[n]^2/2 - x[n-1]^2/2}{x[n] - x[n-1]} = \dfrac{x[n] + x[n-1]}{2}`$: a 2-tap average (moving average). Its frequency response is $`H(e^{j\omega}) = e^{-j\omega/2}\cos(\omega/2)`$: a **delay of half a sample** with magnitude $`\cos(\omega/2)`$. At 7 kHz, $`\cos(\pi \cdot 7/48) = 0.897 \Rightarrow -0.94`$ dB; the program measures a fundamental difference of 0.40 dB between naive and ADAA (the behavior in the clipping region differs from the linear region, so it does not exactly equal −0.94 dB). At 2x the same 7 kHz is attenuated by only $`\cos(\pi \cdot 7/96)`$ ≈ −0.23 dB, and in the table the fundamental loss of ADAA + 2x is only 0.10 dB.

A half-sample delay is usually negligible in a plugin (or is aligned with the dry path using a fractional delay), but it must be taken into account if ADAA is placed inside a feedback loop.

---

## Exercise 7: Oversampling quality and cost

Solution program: `solutions/ex7_oversampling_cost.cpp` (target `l06_ex7_solution`)

**Key ideas**
- `aliasDb()`: the same measurement as the `l06_aliasing_demo` example, skipping the first 4800 samples (latency + parameter ramp).
- `timeMs()`: each factor runs 3 times and the minimum is taken (the minimum is closest to the true cost "undisturbed by the operating system"); copying the buffer and `prepare()` are outside the timing.
- CPU load = processing time / audio duration.

**Measured on the author's machine** (`-Config Release` build; times vary from machine to machine, the alias numbers do not):

| Factor | Latency | Alias total | Time | vs 1x | CPU load |
|---|---|---|---|---|---|
| 1x | 0 | −17.9 dB | 11.9 ms | 1.00 | 0.12 % |
| 2x | 31 | −38.3 dB | 37.3 ms | 3.15 | 0.37 % |
| 4x | 36 | −85.2 dB | 70.3 ms | 5.93 | 0.70 % |
| 8x | 38 | −111.5 dB | 124.3 ms | 10.49 | 1.24 % |

A Debug build is 3–4 times slower, and the relative factors jump around between runs (2x about 3.8–4.7, 4x about 6.6–8.4, 8x about 12–15). So always use Release when comparing cost, and run it several times and take the minimum.

**How to choose?** For a smooth curve like tanh: 2x improves by 20 dB, but an alias level of −38 dB is still audible on high-frequency material; 4x brings it down to −85 dB, close to the 16-bit noise floor (about −96 dB) and below the noise of most material; the −111 dB of 8x makes no audible difference yet costs about 1.8 times more CPU. So **4x is the sweet spot**, and many commercial saturation plugins also default to 2x–4x and let the user switch to a higher factor at mixdown.

**What about a hard clip?** The harmonics of a hard clip decay only as $`1/k`$ to $`1/k^2`$ (sharp corners → slowly converging spectrum), so even 8x still has noticeable aliasing (the analysis of Exercise 3: at every factor a batch of higher harmonics still folds back; the measurement in Exercise 6: naive hard clip at 8x still has −61.7 dB). A more cost-effective approach in that case is ADAA + 2x–4x (Exercise 6), or replacing the hard clip with a soft clip.

Also note that the cost jumps especially much from 1x to 2x: the 1x Oversampler is just a copy, whereas from 2x on it must run the 32-tap halfband FIR (once for upsampling, once for downsampling), plus twice as many `tanh` calls. After that, each additional stage adds "`tanh` at an even higher sample rate" plus a shorter FIR.
