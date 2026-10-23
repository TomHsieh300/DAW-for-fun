# Chapter 03 Solutions and Explanations: Parametric EQ
**English** | [繁體中文](README.zh-TW.md)

The complete code for the programming exercises is in this folder. Each program verifies its results numerically with `DAWLAB_CHECK` / `DAWLAB_CHECK_NEAR` and finally prints `[PASS] l03_exN (...)`. Run all the solutions with ctest:

```powershell
.\scripts\build.ps1 -Only 03-eq -TestLabel solution
```

All the numbers below are the values the solution programs actually print (48 kHz).

---

## Exercise 1: Work out A, Q, bandwidth, and the RBJ coefficients by hand

**1. The A values.**

```math
A = 10^{g/40}:\quad A(+6) = 10^{0.15} = 1.4125,\qquad A(-12) = 10^{-0.3} = 0.5012
```

The linear gain at the center of a +6 dB peak is $`A^2 = 10^{6/20} = 1.9953`$ (about 2x). $`A`$ is "the square root of the center gain", and it is also the gain at the bandwidth edges (README Section 2.5).

**2. Q ↔ bandwidth.**

```math
Q(N = 2/3) = \frac{2^{1/3}}{2^{2/3} - 1} = \frac{1.2599}{0.5874} = 2.1449
```

```math
N(Q = 2) = \frac{2}{\ln 2}\mathrm{asinh}\left(\frac{1}{4}\right) = 2.8854 \times 0.24747 = 0.7140\ \text{octave}
```

**3. Bandwidth edges.** $`\frac{1}{2Q} = \frac{1}{2\sqrt2} = 0.35355`$ and $`\sqrt{1 + \frac{1}{4Q^2}} = \sqrt{1.125} = 1.06066`$:

```math
f_1 = 1000 \times (1.06066 - 0.35355) = 707.1\ \text{Hz},\qquad f_2 = 1000 \times (1.06066 + 0.35355) = 1414.2\ \text{Hz}
```

These are exactly $`f_0 / \sqrt2`$ and $`f_0 \cdot \sqrt2`$ (1 octave wide, with $`f_0`$ as the geometric center). With +12 dB, the gain at the edges is the midpoint gain, +6 dB (the digital version measures +5.989 / +5.978 dB; the difference comes from bilinear warping).

**4. RBJ coefficients.** $`\omega_0 = 2\pi \cdot 12000/48000 = \pi/2`$, so $`\cos\omega_0 = 0`$, $`\sin\omega_0 = 1`$, $`\alpha = \frac{1}{2Q} = 0.5`$, and $`A = 1.41254`$:

```math
\begin{aligned}
b_0 &= 1 + \alpha A = 1.70627, & b_1 &= 0, & b_2 &= 1 - \alpha A = 0.29373\\
a_0 &= 1 + \alpha/A = 1.35397, & a_1 &= 0, & a_2 &= 1 - \alpha/A = 0.64603
\end{aligned}
```

Dividing by $`a_0`$: $`b_0 = 1.26019`$, $`b_2 = 0.21694`$, $`a_2 = 0.47713`$, and $`b_1 = a_1 = 0`$.

- DC ($`z = 1`$): $`\frac{b_0 + b_2}{1 + a_2} = \frac{1.47713}{1.47713} = 1`$ → 0 dB.
- $`f_0`$ ($`z = j`$, $`z^{-2} = -1`$): $`\frac{b_0 - b_2}{1 - a_2} = \frac{1.04325}{0.52287} = 1.9952`$ → +6.00 dB ($`= A^2`$).

The final "Exercise 1 cross-check" section of `l03_ex2_solution` verifies all of the numbers above in code.

**5. A +6 dB and a −6 dB peak in cascade.** The overall curve is **perfectly flat at 0 dB**, and the phase is 0 as well. The analog prototype of the RBJ peak is $`H_{+g}(s) = \frac{s^2 + sA/Q + 1}{s^2 + s/(AQ) + 1}`$; replacing $`A`$ with $`1/A`$ simply swaps numerator and denominator, so $`H_{-g} = 1/H_{+g}`$. The bilinear transform is the same substitution, so this also holds for the digital version. `l03_ex3_solution` verifies it: over 20 Hz–20 kHz the maximum deviation is $`1.07 \times 10^{-13}`$ dB (only floating-point error is left).

---

## Exercise 2: Q ↔ bandwidth conversion and verification

Solution: [`ex2_q_bandwidth.cpp`](ex2_q_bandwidth.cpp) (target `l03_ex2_solution`)

```cpp
double qToBandwidthOctaves(double q) { return 2.0 / std::log(2.0) * std::asinh(1.0 / (2.0 * q)); }
double bandwidthOctavesToQ(double bw) { const double p = std::pow(2.0, bw); return std::sqrt(p) / (p - 1.0); }
```

`bandEdges` is the two roots of the quadratic from README Section 2.4, multiplied by $`f_0`$. Output:

```
    Q =  0.707  ->  BW = 1.900 oct
    Q =  1.414  ->  BW = 1.000 oct
    Q =  4.319  ->  BW = 0.333 oct
    BW = 1.000 oct  ->  Q = 1.4142
    BW = 0.333 oct  ->  Q = 4.3185
  f0 = 1000 Hz, Q = sqrt(2) (1 octave): edges = 707.1 Hz / 1414.2 Hz
  peak +12 dB: |H| @ 707.1 = +5.989 dB, @ 1000 = +12.000 dB, @ 1414.2 = +5.978 dB
  bandpass     : |H| @ 707.1 = -3.020 dB, @ 1000 = +0.000 dB, @ 1414.2 = -3.029 dB
  measured peak @ 1414.2 Hz = +5.978 dB
```

**Key points**:

- At the same pair of frequencies, the peak is at the midpoint gain and the bandpass is at −3 dB. This is no coincidence: README Section 2.5 proves that both are the same condition, $`|1 - \Omega^2| = \Omega/Q`$.
- A commonly misremembered fact: Q = 0.707 is not "1 octave"; it is about 1.9 octaves. 1 octave is Q = √2.
- The digital edge gain is 0.01–0.02 dB less than +6 dB. This is the frequency warping of the bilinear transform; the closer to Nyquist, the larger the difference.

---

## Exercise 3: Verify that cascaded peaks add in dB

Solution: [`ex3_cascade_sum.cpp`](ex3_cascade_sum.cpp) (target `l03_ex3_solution`)

`Cascade2::processSample` is simply `b_.processSample(ch, a_.processSample(ch, x))`. Output (excerpt):

```
  freq(Hz)   H1(dB)   H2(dB)   H1+H2   |H1·H2|   measured p1->p2   measured p2->p1
      500    +6.00    -0.07    +5.93    +5.93      +5.93         +5.93
     1414    +0.88    -1.33    -0.45    -0.45      -0.45         -0.45
     2000    +0.42    -4.00    -3.58    -3.58      -3.58         -3.58
```

**Why do the dB values add?** A cascade = convolution = multiplication in the frequency domain: $`H = H_1 H_2`$, so $`|H| = |H_1||H_2|`$, and after taking $`20\log_{10}`$ the multiplication becomes addition. The phase $`\angle H = \angle H_1 + \angle H_2`$ also adds. Multiplication is commutative, so the measured results are identical for both orders.

**Stacking two identical +6 dB peaks**: the dB value at every frequency doubles, giving +12.00 at the center and +5.98 at 1414 Hz (a single one gives +2.99). The curve is stretched to twice its height on the dB axis, and the positions of the "midpoint gain" edges do not move. It is **close to, but not exactly the same as,** a single +12 dB, Q = √2 peak: the center (+12 dB) and the midpoint-gain edges (about +6 dB at both 707 and 1414 Hz) agree, but the skirts are narrower. For example, at 500 / 2000 / 4000 Hz, the two cascaded +6 dB peaks give +2.26 / +2.24 / +0.42 dB, while a single +12 dB peak gives +2.51 / +2.49 / +0.51 dB (48 kHz, RBJ digital formulas). The reason: for the RBJ peak (analog prototype, README Section 2.5), $`|H|^2 = \dfrac{x + A^2}{x + A^{-2}}`$ (with $`x = Q^2(1-\Omega^2)^2/\Omega^2`$), so the cascade of two gives $`\left(\dfrac{x + A^2}{x + A^{-2}}\right)^2`$, whereas a single +12 dB peak replaces $`A`$ with $`A^2`$: $`\dfrac{x + A^4}{x + A^{-4}}`$. The two are equal only at $`x = 0`$ (the center) and $`x = 1`$ (the midpoint-gain edges), and differ elsewhere. If you want a "wider" or "narrower" shape, you have to adjust Q.

---

## Exercise 4: 10-band graphic EQ

Solution: [`ex4_graphic_eq.cpp`](ex4_graphic_eq.cpp) (target `l03_ex4_solution`, writes `l03_ex4_graphic_smile.wav`)

`kQ = 2^(1/2) / (2^1 − 1) = √2`. Output:

```
  only the 1 kHz band at +6 dB:
      250.0 Hz: theory  +0.22 dB, measured  +0.22 dB
      500.0 Hz: theory  +1.13 dB, measured  +1.13 dB
      707.1 Hz: theory  +2.99 dB, measured  +2.99 dB
     1000.0 Hz: theory  +6.00 dB, measured  +6.00 dB
     1414.2 Hz: theory  +2.99 dB, measured  +2.99 dB
     2000.0 Hz: theory  +1.12 dB, measured  +1.12 dB
     4000.0 Hz: theory  +0.21 dB, measured  +0.21 dB
  all +6 dB:
    theory at 1 kHz +8.80 dB; between 125 Hz and 4 kHz the curve lies in +6.89 .. +8.83 dB (ripple 1.94 dB)
```

**Discussion**:

- When you push one band, the centers of the neighboring bands (1 octave away) are also lifted by about +1.1 dB: the skirts of a peak are very wide.
- With all bands pushed +6 dB, the level at 1 kHz is **+8.80 dB**, not +6: every frequency falls on the skirts of several bands at once, and their dB values add. Between band centers (for example 707 Hz) the level is a bit lower than at the centers, producing a ripple of about 1.9 dB. So the shape of a graphic EQ's sliders ≠ the actual curve.
- Directions for improvement: (1) increase Q, which makes the ripple larger but reduces the interaction between bands; (2) this exercise is a constant-Q design to begin with (Q is fixed at √2 and does not change with gain); with proportional Q (README Section 2.11), the bands are wider at small gains (more interaction) and narrower at large gains (less interaction); each has its trade-offs, but neither makes the stacking problem go away; (3) a more advanced design solves a small system of linear equations so that the "actual curve's value at each band center" equals the slider value exactly (solving back for the interaction between bands).
- The smiley curve (boosted lows and highs, scooped mids) raises the peak of the drum loop from −2.92 to +0.05 dBFS, which is above 0 dBFS! This is a common graphic EQ pitfall; remember to lower the output gain.

---

## Exercise 5: Tilt EQ

Solution: [`ex5_tilt_eq.cpp`](ex5_tilt_eq.cpp) (target `l03_ex5_solution`, writes `l03_ex5_tilt.wav`)

```cpp
low_.setCoefficients(BiquadCoefficients::makeLowShelf(sampleRate_, pivotHz_, kShelfQ, -0.5 * tiltDb));
high_.setCoefficients(BiquadCoefficients::makeHighShelf(sampleRate_, pivotHz_, kShelfQ, 0.5 * tiltDb));
```

Output (pivot 1 kHz, tilt +6 dB):

```
    freq(Hz)   theory(dB)   measured(dB)
          30     -3.00      -3.00
         500     -2.64      -2.64
        1000     -0.00      +0.00
        2000     +2.65      +2.65
       16000     +3.00      +3.00
```

**Why is the pivot exactly 0 dB?** At $`f_0`$, the gain of an RBJ shelf is the midpoint $`A`$ (half of the gain in dB, README Section 2.6). At the pivot the low shelf contributes $`-t/4`$ and the high shelf contributes $`+t/4`$, which sum to 0.

**Why is it antisymmetric?** Replacing $`s`$ with $`1/s`$ in a low shelf gives a high shelf (mirrored about $`f_0`$ on a log frequency axis), and the gain is opposite, so $`\text{dB}(f_0 \cdot 2^k) = -\text{dB}(f_0 \cdot 2^{-k})`$. There is a little bilinear warping at the high end, so the self-check allows 0.15 dB at 2 octaves.

**Why Q = 0.707?** With S = 1 each shelf is monotonic, so their sum is monotonic too, with no bump. The smaller the Q, the gentler the transition and the more even the "tilt"; try Q = 0.5 yourself to see the curve.

**Smoothing**: `setTiltDb` only sets the target of the `SmoothedValue`; `process()` calls `skip(32)` every 32 samples and recomputes the coefficients of both shelves from the current value, the same trick as in `ParametricEq`. The WAV switches from bright to dark at the 2-second mark with no audible click.

---

## Exercise 6: A high cut switchable between 12 / 24 dB/oct

Solution: [`ex6_switchable_highcut.cpp`](ex6_switchable_highcut.cpp) (target `l03_ex6_solution`, writes `l03_ex6_highcut_switch.wav`)

Output (fc = 1 kHz, excerpt):

```
  12 dB/oct (Butterworth, order 2), fc = 1000 Hz
    freq(Hz)   theory(dB)   measured(dB)   analog Butterworth(dB)
       1000      -3.01      -3.01        -3.01
       2000     -12.37     -12.37       -12.30
       4000     -24.48     -24.48       -24.10
  24 dB/oct (Butterworth, order 4), fc = 1000 Hz
    freq(Hz)   theory(dB)   measured(dB)   analog Butterworth(dB)
       1000      -3.01      -3.01        -3.01
       2000     -24.25     -24.25       -24.10
       4000     -48.92     -48.92       -48.16

  The two sections of the 4th order at fc: Q=0.5412 -> -5.33 dB, Q=1.3066 -> +2.32 dB, sum -3.01 dB

  1 kHz sine (= fc), switching 12 -> 24 dB/oct midway: largest sample jump
    direct switch   0.3505 (the sine itself changes by at most 0.0654)
    20 ms crossfade 0.0461
```

**Key points**:

- **A 4th-order Butterworth is not two Q = 0.707 sections**: that would give −6 dB at $`f_c`$. The correct Qs are determined by the pole positions (README Section 2.3); at $`f_c`$ the two sections give −5.33 and +2.32 dB respectively, which sum to −3.01.
- **Steeper than the analog filter**: the digital Butterworth replaces $`(f/f_c)^{2N}`$ with $`\tan(\pi f/f_s)/\tan(\pi f_c/f_s)`$, so attenuation increases the closer you get to Nyquist (at Nyquist there is a zero, −∞ dB).
- **Why direct switching clicks**: at $`f_c`$, the phase of a 2nd-order Butterworth is −90° and that of a 4th-order is −180°. The two chains have the same output amplitude (both −3 dB, amplitude 0.354) but differ by 90° in phase, so the difference is $`y_{24} - y_{12} = 0.5\cos(\theta + 45°)`$. Switching at the 48th block (0.512 s) happens at $`\theta = 0°`$, where the difference is about 0.35, so the output instantly jumps by 0.35, more than 5 times the sine wave's natural change.
- **How the crossfade works**: both chains **run all the time** (so their state is always up to date), and only the output is blended with a mix: $`y = y_{12} + m(y_{24} - y_{12})`$, where $`m`$ ramps linearly from 0 to 1 over 20 ms. The cost is that both chains must be computed during the transition. `ParametricEq`'s `setSlope()` uses a different approach (fade out to bypass first, then fade in the new slope), which does not need to maintain two chains at once but briefly passes through a "no EQ" state.

---

## Exercise 7: EQ and phase

**1. Why must an IIR EQ change the phase?** An IIR filter uses feedback (poles) to "accumulate" energy at certain frequencies to create a boost / cut; accumulation takes time, so different frequencies are delayed by different amounts → phase shift. Mathematically, a causal, stable rational filter whose poles and zeros are all inside (or on) the unit circle is called **minimum phase**; for such a system the log-magnitude and the phase are a Hilbert transform pair, so **once the magnitude is fixed, the phase is fixed too**, and it has the least delay among all causal systems with that magnitude. So an IIR EQ that "changes only the magnitude and not the phase" does not exist.

**2. The phase values** (section A of `l03_eq_phase_cramping`): for +6 dB @ 1 kHz, Q = 1, the phase is +18.00° at 500 Hz (leading), −17.97° at 2 kHz (lagging), and 0° at the center. For −6 dB the phase flips sign exactly (500 Hz −18°, 2 kHz +18°; the program prints −18.39° for −6 dB at 707 Hz), because $`H_{-g} = 1/H_{+g}`$ and the phase is negated.

**3. When does phase become a problem?** On a single track the phase shift is only a dozen or so degrees and the group delay a fraction of a ms, which the ear can hardly hear. The problem comes when **two versions of the same sound are summed**:

- Multiple microphones picking up the same source (kick in / kick out, overheads on a drum kit): EQ-ing only one of them changes the phase relationship between them, and some frequencies cancel when summed (comb filtering).
- Parallel processing (dry + processed wet), or recombining the bands of a multiband processor.
- Applying different EQs to the L / R channels of a stereo signal → the imaging shifts.

**4. The costs of linear phase.** (a) Latency: a symmetric FIR delays by $`(N-1)/2`$ samples, so 4097 taps → 2048 samples → $`2048/48000 = 42.7`$ ms; resolving low frequencies requires a long FIR, so the latency grows with it, the DAW must do plugin delay compensation, and it is unsuitable for live monitoring. (b) Pre-ringing (next question). (c) CPU: a long FIR needs FFT-based partitioned convolution (Chapter 12). (d) Changing a parameter requires redesigning the whole FIR, and smoothing is harder.

**5. Pre-ringing.** The impulse response of a linear-phase filter is symmetric: half of the oscillation lies before the main peak and half after it. So you hear the filter's ringing **before** a transient (such as the attack of a drum), like the sound "taking a breath before it hits". In nature, resonance only happens **after** the excitation (post-ringing), so pre-ringing sounds especially unnatural; it is most noticeable with sharp transients such as kick and snare combined with narrow, deep EQ (high Q → long ringing).

**6. The trade-off for a narrow 40 Hz cut.** A narrow (high-Q), low-frequency cut means the linear-phase version needs a very long FIR (low frequency + narrow bandwidth = long impulse response), with large latency and a long stretch of pre-ringing, and it lands right on the fundamental of the kick / bass, where it is easiest to hear. The minimum-phase version only has a little phase shift and post-ringing around 40 Hz, which is usually safer. Unless the cut affects the phase relationship with other parallel signals (for example in multiband processing), choose minimum-phase in general; if in doubt, try both and compare in a blind listening test after **matching the levels**.
