# Chapter 04 Solutions and Explanations

**English** | [繁體中文](README.zh-TW.md)

The questions are in [`../exercises/README.md`](../exercises/README.md). Every solution program has a self-check; `.\scripts\build.ps1 -Only 04-compressor` runs them through ctest (label `solution`); to run one by hand, `cd` to the build directory first, e.g. `.\bin\Debug\l04_ex3_solution.exe`. The numbers quoted below are the results actually printed by the solution programs (48 kHz, block size 512).

| Exercise | Solution file | target |
|---|---|---|
| Exercise 1 conceptual | (this text) | — |
| Exercise 2 calculation | `ex2_calculations.cpp` (checks the hand-calculated answers) | `l04_ex2_solution` |
| Exercise 3 parallel compression | `ex3_parallel.cpp` | `l04_ex3_solution` |
| Exercise 4 sidechain HPF | `ex4_sidechain_hpf.cpp` | `l04_ex4_solution` |
| Exercise 5 auto makeup | `ex5_auto_makeup.cpp` | `l04_ex5_solution` |
| Exercise 6 feedback topology | `ex6_feedback.cpp` | `l04_ex6_solution` |
| Exercise 7 RMS window vs pumping | `ex7_rms_window.cpp` | `l04_ex7_solution` |

---

## Exercise 1: block diagram and design choices

**1. What does each of the four blocks do?**

- **Detector**: turns the signal into a "how loud is it right now" level. Peak mode takes $`\lvert x \rvert`$ directly; RMS mode takes the moving average of $`x^2`$ and then the square root.
- **Gain computer**: uses the static curve (threshold, ratio, knee) to convert the level (dB) into a GR target of "how many dB should be squashed".
- **Attack / release smoothing**: decides how fast the GR approaches its target: the attack time constant when it rises (clamping down) and the release time constant when it falls (letting go).
- **Makeup gain**: after compression the whole signal is quieter, and a fixed gain brings it back.

**The pure function with no state is the gain computer**: the same input dB always gives the same output dB, which is why `Compressor::computeOutputDb()` is `static` and a UI can use it directly to draw the transfer curve. (The Peak-mode detector itself is also stateless; the RMS-mode detector has state.)

**2. Why smooth in the log domain?**

- In the log domain, what is smoothed is the GR itself (dB), so the time curve of attack / release is independent of the threshold, the ratio and how far the signal exceeds the threshold: an attack of 10 ms means the GR covers 63.2% of its travel in 10 ms. If you first smooth the envelope in the linear domain and then pass it through the nonlinear static curve, the shape of the GR's time curve changes with the signal level.
- Shape of the release: the linear-domain envelope decays exponentially, which in dB is a **constant-rate** fall (8.686 dB per $`\tau`$), so the GR falls in a straight line and stops abruptly when it reaches the threshold (B4 in example 2: after 150 ms of release the GR is already 0). The log-domain GR itself decays exponentially, falling more slowly the closer it gets to 0, with a long, smoother, more natural tail (B1: 1.86 dB left at 150 ms, 0.15 dB at 400 ms).

**3. Why does branching underestimate the peak of a sine wave? How does decoupled fix it?**

The peak detector delivers the instantaneous $`\lvert x[n] \rvert`$. A sine wave passes through 0 every half cycle, the instantaneous level drops to -100 dB, and the GR target drops to 0 with it. Branching smoothing switches to the release coefficient at those moments and pulls the state down, so within one cycle it is "pulled up a bit, pulled down a bit", and the steady-state balance point lands below the static value (B2 in example 2: 6.47 dB against the static value of 7.50 dB).

Decoupled first passes through a peak-hold that "rises instantly and decays with the release": $`y_1 = \max(x, \alpha_R y_1 + (1-\alpha_R)x)`$. Between two crests (0.5 ms) the peak-hold decays only slightly with the 100 ms release, which fills in the ripple, and then hands the result to the attack filter, so the steady-state GR equals the static value almost exactly (B1: 7.49 dB).

**4. Why is stereo link on by default?**

Without link the two channels are compressed separately; the louder side is squashed more, the level difference between left and right shrinks, and the image drifts toward the quieter side, wandering back and forth with the dynamics of the music. Example 4: L -6 dBFS, R -30 dBFS; unlinked, the left-right difference shrinks from 24 dB to 13.52 dB; linked, both sides are squashed by the same 10.48 dB and the left-right difference stays at 24.00 dB.

Cases where you would want to turn it off: when the left and right channels are really two unrelated signals (for example two dual-mono microphones, or one guitar on each side), or for creative uses that deliberately make the stereo image "widen / move".

**5. Why can an attack of 1 ms not catch the peaks of a drum?**

At the $`n`$-th sample a one-pole GR has traveled only $`1 - \alpha^n`$. With attack 1 ms @ 48 kHz, $`\alpha = e^{-1/48}`$, and the first sample reaches only $`1 - e^{-1/48} = 2.06\%`$ of the target. The peaks of a drum kit are the snare noise and hi-hat spikes only a few samples wide, and the spike is gone before the GR has risen (example 3: with attack 1 ms the maximum GR is 12.53 dB, but the peak drops only 5.48 dB).

To "guarantee" catching them you need **lookahead**: delay the main signal by a few ms so the detector sees the upcoming peak first and lowers the gain in advance. The price is latency. This is exactly what the chapter 05 limiter does.

---

## Exercise 2: static curves and time constants

`l04_ex2_solution` checks every answer below against `computeOutputDb()` and `attackReleaseCoefficient()` (all 17 checks PASS).

**1. $`T = -24`$, $`R = 3`$, hard knee**: above the threshold $`y = T + (x - T)/R`$.

| Input | Output | GR |
|---|---|---|
| -30 | -30 (below the threshold, untouched) | 0 |
| -24 | -24 (exactly at the threshold) | 0 |
| -12 | $`-24 + 12/3 = -20`$ | 8 |
| 0 | $`-24 + 24/3 = -16`$ | 16 |

**2. Soft knee $`W = 12`$**: the knee covers $`T \pm W/2 = -30 \ldots -18`$ dB. Inside the knee $`y = x + \frac{(1/R - 1)(x - T + W/2)^2}{2W}`$, where $`1/R - 1 = -2/3`$ and $`2W = 24`$:

- $`x = -24`$: $`x - T + W/2 = 6`$, $`y = -24 + (-2/3)(36/24) = -24 - 1 = -25`$ dB (there is already 1 dB of GR at the threshold; you can also check with $`y(T) = T + (1/R - 1)W/8 = -24 - 1`$).
- $`x = -20`$: $`x - T + W/2 = 10`$, $`y = -20 + (-2/3)(100/24) = -20 - 2.7778 = -22.7778`$ dB (the hard knee gives -22.667; the soft knee compresses a bit more because inside the knee the slope changes gradually from 1 to 1/3, and on average that compresses somewhat more than a "single cut").

**3. Upper edge $`x = -18`$**: $`x - T + W/2 = 12 = W`$, the quadratic gives $`y = -18 + (-2/3)(144/24) = -18 - 4 = -22`$; the hard-knee line gives $`y = -24 + 6/3 = -22`$. They are equal. The general form is in section 2.3 of this chapter's README: the value at the upper edge $`= T + \frac{W}{2R}`$, here $`-24 + 12/6 = -22`$. The solution program also uses a numerical derivative to confirm that the slope just inside the knee ≈ 0.334 ≈ 1/R.

**4. Coefficients** ($`\alpha = e^{-1/(\tau f_s)}`$):

- $`\tau_A f_s = 0.005 \times 44100 = 220.5`$, $`\alpha_A = e^{-1/220.5} = 0.995475`$
- $`\tau_R f_s = 0.2 \times 44100 = 8820`$, $`\alpha_R = e^{-1/8820} = 0.999887`$

**5. Time to reach a fraction** $`t_p = -\tau \ln(1 - p)`$:

- attack to 90%: $`5 \ln 10 = 11.51`$ ms; to 99%: $`5 \ln 100 = 23.03`$ ms
- release decaying to 10% (= 90% of the way): $`200 \ln 10 = 460.5`$ ms

(The solution program simulates the step response and measures 11.52, 23.04 and 460.5 ms; the 0.01 ms discrepancy comes from the quantization of "taking the first sample that exceeds the value".)

**6. Converting "10 ms to 90%"**: we need $`\tau \ln 10 = 10`$ ms, so $`\tau = 10 / \ln 10 = 4.343`$ ms, i.e. `setAttackMs(4.343f)`. Simulation check: the time to 90% is 10.02 ms. General rule: **a number given under the 90% definition, divided by 2.303, is this chapter's τ**.

---

## Exercise 3: Parallel (New York) compression

Solution: `ex3_parallel.cpp`.

Key ideas:

1. `compressBlocks()`: copy the buffer, call `process()` 512 samples at a time with `AudioBlock::getSubBlock()`, and read `getGainReductionDb()` once after each block to take the maximum, which is exactly what a GR meter does in a UI.
2. The wet signal uses a "heavy compression" setting: $`T = -30`$, 10:1, knee 3 dB, attack **0.02 ms**, release 60 ms. Makeup = dry peak dB − wet peak dB (measured +20.64 dB, maximum GR 24.37 dB). The attack must be very fast, for the reason in the example 3 sweep: with attack ≥ 1 ms the peaks slip through, the wet crest actually grows, and parallel compression loses its point.
3. `parallelMix()`: first `std::clamp(mix, 0, 1)`, then sample by sample $`y = (1 - \text{mix})\,d + \text{mix}\,w`$.

Measured:

```
crest factor: dry 14.51 dB, wet 11.03 dB
mix 0.00: peak  -2.92 dBFS, crest 14.51 dB
mix 0.25: peak  -3.67 dBFS, crest 13.04 dB
mix 0.50: peak  -4.50 dBFS, crest 11.33 dB
mix 0.75: peak  -4.67 dBFS, crest 10.22 dB
mix 1.00: peak  -2.92 dBFS, crest 11.03 dB
```

Observations:

- The crest at mix 0.5 (11.33 dB) lies between dry and wet, and the self-check passes.
- **The crest at mix 0.75 (10.22 dB) is even lower than pure wet**, and the peak is lower than both dry and wet (-4.67 dBFS). The reason is that the peaks of dry and wet are not perfectly aligned: the wet peaks are flattened and appear in other places, so after mixing the peaks of the two sides do not occur at the same time, while the RMS (the body) is present on both sides and adds up, giving "low peak, high energy". This is why parallel compression is so useful: it is not just an interpolation between dry and wet.
- What it sounds like (`l04_ex3_parallel_50.wav` versus `l04_ex3_parallel_wet.wav`): the wet signal is very flat, with the room sound and tails pulled up; the 50% mix keeps the impact of the dry drum heads while also having the thickness of the wet signal.

---

## Exercise 4: Sidechain HPF

Solution: `ex4_sidechain_hpf.cpp`.

**The HPF**: $`lp[n] = lp[n-1] + b\,(x[n] - lp[n-1])`$, $`b = 1 - e^{-2\pi f_c / f_s}`$, $`hp = x - lp`$. This is the "complement" of a first-order lowpass, so it is -3 dB around $`f_c`$ and attenuates 6 dB per octave below $`f_c`$. Measured (with `measureGainDb`): 50 Hz -18.36 dB, 400 Hz -3.24 dB, 3 kHz -0.30 dB. (At 400 Hz it is -3.24 dB rather than -3.01 dB. The lowpass itself is exactly -3.01 dB at $`f_c`$ ($`\lvert H_{lp}\rvert = 0.7074`$); the deviation comes from the complementary structure $`hp = x - lp`$: let $`a = 1 - b`$, then the lowpass is $`H_{lp}(z) = \dfrac{b}{1 - a z^{-1}}`$, so
```math
H_{hp}(z) = 1 - H_{lp}(z) = \frac{a\,(1 - z^{-1})}{1 - a z^{-1}}
```
Its gain at high frequency ($`z = -1`$, Nyquist) reaches only $`2a/(1+a) \approx 0.974`$ (about -0.23 dB) rather than the 1 of an analog first-order HPF, and its phase also differs from the analog version, so the whole HPF curve sits slightly lower: -3.24 dB at $`f_c`$ and -0.30 dB at 3 kHz (an analog first-order HPF would be only -0.08 dB at 3 kHz). This is entirely adequate for a sidechain.)

**Flow**: the sidechain buffer is allocated once outside the loop (mimicking `prepare()`), and for each block: `sc.copyFrom(block)` → `hpf->process(sc)` → `comp.process(block, sc)`. The gain is always applied to the **original** signal; the HPF only affects "what the detector hears".

**Numbers**: the input is 50 Hz @ -6 dBFS + 3 kHz @ -24 dBFS with a peak of about $`20\log_{10}(0.501 + 0.063) = -4.97`$ dBFS. $`T = -20`$, 4:1, hard knee:

- Without the HPF: the static estimate is GR = $`(−4.97 + 20) \times 0.75 = 11.27`$ dB; the measured GR is 11.23 dB, and the 3 kHz component is dragged down by **11.08 dB**. The quiet highs are "hijacked" by the loud lows.
- HPF at 400 Hz: the 50 Hz drops by about 18 dB in the sidechain, leaving around -24 dBFS, and with the 3 kHz only slightly over the threshold the GR drops to just **1.21 dB**, and the 3 kHz is reduced by only 1.19 dB.

What it sounds like: in `l04_ex4_no_hpf.wav` the highs are "modulated" by every crest of the 50 Hz (the GR ripple at 50 Hz also produces distortion); in `l04_ex4_with_hpf.wav` the highs are stable. In practice a sidechain HPF is often set at 60–150 Hz (so that the kick and bass do not dominate a bus compressor); this exercise uses 400 Hz to make the difference obvious.

---

## Exercise 5: Auto makeup gain

Solution: `ex5_auto_makeup.cpp`.

```math
\text{makeup} = -\,y(0) = -\,\text{computeOutputDb}(0, T, R, W)
```

| Setting | Calculation | makeup |
|---|---|---|
| $`T=-20`$, 4:1, hard | $`-(-20 + 20/4)`$ | 15 dB |
| $`T=-20`$, 4:1, $`W=10`$ | 0 dBFS is outside the knee (-25..-15), same as above | 15 dB |
| $`T=-30`$, 2:1, hard | $`-(-30 + 30/2)`$ | 15 dB |
| $`T=-10`$, 10:1, hard | $`-(-10 + 10/10)`$ | 9 dB |
| $`T=-6`$, 4:1, $`W=20`$ ("think about it") | the knee is -16..+4, so 0 dBFS is inside the knee: $`y = 0 + (-0.75)(0 + 6 + 10)^2/40 = -4.8`$ | **4.8 dB** |

The last row is a trap: using the hard-knee formula $`-(−6 + 6/4) = 4.5`$ dB would be off by 0.3 dB, because the soft knee compresses more than the hard knee just above the threshold.

Real run (attack 1 ms, release 100 ms, 1 kHz sine): 0 dBFS in → **0.01 dBFS** out; -6 dBFS in → **-1.49 dBFS** out (expected $`-20 + 14/4 + 15 = -1.5`$); without makeup, 0 dBFS in → -14.99 dBFS. The 0.01 dB difference comes from the tiny decay of decoupled's peak-hold between two crests (the same as 10.48 versus 10.5 in example 4).

Why do many plugins make up only half (7.5 dB)? Auto makeup assumes the signal always sits at 0 dBFS, but the peaks of real material are usually at -6 to -12 dBFS, where the GR is smaller than at 0 dBFS, so a full 15 dB of makeup makes the output noticeably louder than the input, and in an A/B comparison you get fooled by "louder sounds better". A more rigorous approach is to make up according to the average GR of the actual material, or to use loudness matching (the chapter 11 `LoudnessMeter`).

---

## Exercise 6: Feedback topology

Solution: `ex6_feedback.cpp`.

**Implementation key**: `FeedbackCompressor` keeps the previous output sample `lastOutput_`; for each sample:

```cpp
const float levelDb  = gainToDb(std::abs(lastOutput_));                 // look at the output, not the input
const float grTarget = std::max(0.0f, slope_ * (levelDb - thresholdDb_)); // hard knee with slope s
const float gr       = smoother_.process(grTarget);                      // AttackReleaseFilter (decoupled)
lastOutput_ = x * dbToGain(-gr);
```

A one-sample delay is irrelevant at 48 kHz, and it is precisely the nature of "the detector can only see the output that has already been computed".

**Derivation** (README section 2.6): at steady state $`Y = X - \text{GR}`$ and $`\text{GR} = s(Y - T)`$ ⇒ $`\text{GR} = \frac{s}{1+s}(X - T)`$, $`R_{\text{eff}} = 1 + s`$.

**Measured** (tone bursts -30 → -10 → -30 dBFS, $`X - T = 10`$ dB, attack 10 ms, release 100 ms):

```
feedforward R = 4 (s = 0.75): steady-state GR 7.49 dB (theory 7.50), t63 = 10.77 ms
feedback s = 0.75 (R_eff = 1.75): steady-state GR 4.28 dB (theory s/(1+s)·10 = 4.29), t63 = 5.27 ms
feedback s = 3.00 (R_eff = 4.00): steady-state GR 7.50 dB (theory s/(1+s)·10 = 7.50), t63 = 2.79 ms
feedback s = 9.00 (R_eff = 10.00): steady-state GR 9.00 dB (theory s/(1+s)·10 = 9.00), t63 = 1.79 ms
```

Interpretation:

- Moving the feedforward 4:1 slope (0.75) straight to feedback gives only 1.75:1 (4.28 dB). To get 4:1 from feedback you must use $`s = 3`$.
- **With the same 4:1 and the same attack of 10 ms**, the feedback t63 is only 2.79 ms, much faster than the feedforward 10.77 ms. The reason: when the attack has just begun the output has not yet been squashed, the detector sees 10 dB over, and the GR target is $`3 \times 10 = 30`$ dB (far larger than the steady-state 7.5); the smoother rushes toward 30 dB and quickly passes 63% of 7.5 dB; as the output gets squashed, the target drops with it, finally converging at 7.5. So the feedback attack is **program-dependent**: the more it goes over, the faster the early part of the attack.
- The larger $`s`$ is, the faster it gets (1.79 ms) and the closer it gets to limiter behavior; but $`R_{\text{eff}} = 1 + s`$ is always finite, so feedback cannot make ∞:1, nor can it do lookahead.
- Stability: when $`s > 1`$ the loop gain is greater than 1, and if attack = 0 (the GR jumps to the target immediately at every sample), the GR oscillates back and forth between "squashed too much → output gets smaller → let go → output gets larger". Attack smoothing adds a lowpass to the loop, so it is stable.

What it sounds like: at the start of the second segment of `l04_ex6_feedback.wav`, the overshoot lasts clearly shorter than in example 2's `l04_step_out.wav` (the GR squashes 63% within about 3 ms, versus about 11 ms for feedforward), so the "head" is reined in faster.

---

## Exercise 7: RMS window length vs pumping

Solution: `ex7_rms_window.cpp`.

**Implementation key**: `setDetectorMode(DetectorMode::Rms)` + `setRmsWindowMs(w)`; skip the first 0.5 seconds of warm-up (the RMS detector with a long window needs time to "fill up", otherwise the minimum GR gets polluted by the initial 0); after each block read `getGainReductionDb()` to update max / min.

Measured ($`T = -24`$, 4:1, knee 3, attack 1 ms, release 50 ms):

```
Peak detector (reference): GR max 15.46 / min 0.04, swing 15.41 dB, output crest 18.98 dB
RMS window     1 ms: GR max 14.20 / min 0.00, swing 14.19 dB, output crest 19.69 dB
RMS window     5 ms: GR max 12.65 / min 0.00, swing 12.65 dB, output crest 20.21 dB
RMS window    10 ms: GR max 11.75 / min 0.00, swing 11.75 dB, output crest 20.61 dB
RMS window    30 ms: GR max 10.59 / min 0.00, swing 10.59 dB, output crest 20.72 dB
RMS window   100 ms: GR max 8.99 / min 0.00, swing 8.99 dB, output crest 20.29 dB
RMS window   300 ms: GR max 7.54 / min 0.45, swing 7.09 dB, output crest 19.00 dB
RMS window  1000 ms: GR max 5.97 / min 1.96, swing 4.00 dB, output crest 17.80 dB
```

Interpretation:

- **The swing decreases monotonically with the window** (14.19 → 4.00 dB); the 1 ms swing is 3.5 times the 1000 ms one; the 1 ms RMS differs from the peak detector by only 1.22 dB, because once the window is shorter than the drum sound itself, RMS degenerates into peak. These three points are all self-checks.
- Short windows: every kick / snare makes the GR rush to 14 dB and release again, and the whole loop "breathes" with the beat; this is **pumping**. Sometimes it is a desired effect (the sidechain pumping of EDM), but on a bus it is usually a flaw.
- Long windows: the detector sees "the average energy over the last second", the GR stays almost fixed at 2–6 dB, the compression becomes a slow overall volume control, and the dynamics of the drum sounds themselves are preserved.
- The crest factor first rises and then falls: with a medium window (10–30 ms) the GR rises only after the drum head, squashing the drum body, and the crest is actually at its largest (the same phenomenon as the "slow attack" in example 3); with a still longer window the GR no longer follows individual drum hits and the crest falls back. This shows that the RMS window and the attack time stack on top of each other in effect.

What it sounds like: `l04_ex7_rms_1ms.wav` clearly "inhales" between beats; `l04_ex7_rms_100ms.wav` is steadier, as if the whole were pressed down a layer.
