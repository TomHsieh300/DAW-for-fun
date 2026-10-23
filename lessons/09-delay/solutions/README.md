# Chapter 09 Solutions: Delay and Echo

**English** | [繁體中文](README.zh-TW.md)

Every exercise has a complete solution. The programs for the implementation exercises are in this folder (`exN_xxx.cpp`, ctest label: solution), and the calculation exercises 1 and 7 also have programs that actually run the numbers once:

```powershell
.\scripts\build.ps1 -Only 09-delay -TestLabel solution
cd build
.\bin\Debug\l09_ex1_solution.exe      # ex1, ex3, ex4, ex5, ex6, ex7
```

| No. | Type | Solution program | Output WAVs (`build\dawlab_output\`) |
|---|---|---|---|
| 1 | Calculation | `l09_ex1_solution` (`ex1_tempo_math.cpp`) | — |
| 2 | Concept | The numbers for part 2 are verified in part 5 of `l09_ex7_solution` | — |
| 3 | Implementation | `l09_ex3_solution` (`ex3_allpass_interpolation.cpp`) | `l09_ex3_allpass_noise.wav` |
| 4 | Implementation | `l09_ex4_solution` (`ex4_tape_delay.cpp`) | `l09_ex4_tape_dry.wav`, `l09_ex4_tape_wet.wav`, `l09_ex4_dub.wav` |
| 5 | Implementation | `l09_ex5_solution` (`ex5_multitap.cpp`) | `l09_ex5_multitap_dry.wav`, `l09_ex5_multitap_wet.wav` |
| 6 | Implementation | `l09_ex6_solution` (`ex6_crossfade_delay.cpp`) | `l09_ex6_crossfade.wav`, `l09_ex6_abrupt.wav`, `l09_ex6_ramp.wav` |
| 7 | Calculation + concept | `l09_ex7_solution` (`ex7_comb_math.cpp`) | — |

---

## Exercise 1: Tempo sync and echo decay

Formula: $`t_\text{ms} = \text{beats} \times 60000 / \text{BPM}`$; dotted × 1.5, triplet × 2/3.

1. At 120 BPM one beat = 500 ms:
   - Quarter note = **500 ms**
   - Eighth note = 0.5 × 500 = **250 ms**
   - Dotted eighth = 0.75 × 500 = **375 ms**
   - Eighth triplet = (1/3) × 500 = **166.67 ms**
   - Sixteenth note = 0.25 × 500 = **125 ms**
2. A dotted eighth at 95 BPM = $`0.75 \times 60000 / 95 = 473.684`$ ms, which at 48 kHz is $473.684 \times 48 = $ **22 736.84 samples**. It is not an integer: to align exactly with the tempo you need to read "between two samples". (An error of 0.16 sample is inaudible for a single echo, but the same delay line also has to support modulation and smoothly changing the delay time, see exercise 2.)
3. - Quarter triplet at 140 BPM = $(2/3) \times 60000/140 = $ **285.71 ms**
   - Dotted quarter at 90 BPM = $1.5 \times 60000/90 = $ **1000 ms** (exactly 1 second)
   - Sixteenth note at 174 BPM = $0.25 \times 60000/174 = $ **86.21 ms** (so short it is close to slapback)
4. The amplitude of the $`k`$-th echo is $`g^{k-1}`$: 1, 0.5, 0.25, 0.125, i.e. **0, −6.02, −12.04, −18.06 dB**. (The first echo is the original signal delayed directly and has not yet gone through the feedback.)
5. Each trip attenuates by $`-20\log_{10} g`$ dB, so decaying by 60 dB takes $`60/(-20\log_{10}g) = 3/(-\log_{10} g)`$ trips, each lasting $`D`$ seconds:

   ```math
   T_{60} = \frac{3D}{-\log_{10} g}
   ```

   - $`D = 0.375`$, $`g = 0.5`$: $1.125 / 0.30103 = $ **3.737 s** (about 10 trips)
   - $`D = 0.375`$, $`g = 0.7`$: $1.125 / 0.15490 = $ **7.263 s**
   - $`D = 0.25`$, $`g = 0.9`$: $0.75 / 0.045757 = $ **16.39 s** -- echoes with 90% feedback drag on for a very long time, which is why the feedback knob of a delay is especially "sensitive" over its last 10%.
6. $g = 10^{-3D/T_{60}} = 10^{-3 \times 0.5/2} = 10^{-0.75} = $ **0.178**.

`l09_ex1_solution` verifies: 9 examples of `noteToMs()`; the amplitudes of the first 8 echoes of an impulse through `Delay` (375 ms, fb 0.5, mix 1, filters off) (1, 0.5, 0.25... with error < 10⁻⁴, and positions exactly at $`kD`$); the three $`T_{60}`$ values and the inverse-computed $`g`$; and that `setFeedback(1.5)` is clamped to 0.99.

## Exercise 2: Why feedback ≥ 1 blows up, and why interpolation is needed

1. **The impulse-response view**: with an impulse going in, the output is 1 at $`D`$, $`g`$ at $`2D`$, $`g^2`$ at $`3D`$... and the $`k`$-th echo is $`g^{k-1}`$. For $`|g| < 1`$ the geometric series converges and the echoes get smaller and smaller; for $`|g| > 1`$ every trip makes it larger, and any input (including tiny noise) eventually grows to infinity -- in floating point that means Inf, and then NaN.
   **The pole view**: $`1 - g z^{-D} = 0 \Rightarrow z^D = g`$, so the $`D`$ poles are evenly spread on a circle of radius $`|g|^{1/D}`$. Stable (BIBO) ⇔ all poles inside the unit circle ⇔ $`|g| < 1`$.
   **$`g = 1`$**: the poles lie exactly on the unit circle, which is "marginally stable" -- the impulse response never decays (one echo of amplitude 1 every $`D`$ samples) but does not grow either. But it is not BIBO stable: a sine input at exactly a frequency $`k f_s/D`$ makes the output grow linearly without bound (resonance). In practice that is the same as unstable, which is why `Delay` limits the feedback to 0.99.
2. A one-pole lowpass has $`|H(f)| \le 1`$ (= 1 at DC, < 1 elsewhere), and the loop gain $`|g H(f)| \le 0.99 < 1`$, so it is **still stable**. Replace it with a Q = 4 biquad lowpass: the gain near the cutoff is about $`Q \approx 4`$ (+12 dB), the loop gain is $`0.5 \times 4 = 2 > 1`$, and components near the cutoff that happen to lie at $`k f_s/D`$ are amplified on every trip -- **unstable**. `l09_ex7_solution` measures this for real: the RBJ lowpass (fc = 1 kHz, Q = 4) has +12.04 dB at fc ($`= 20\log_{10}Q`$), a maximum of +12.11 dB @ 984 Hz, and with $`g = 0.5`$ a maximum loop gain of 2.02; connect it into a loop with $`D = 240`$ (the comb's peaks are at $`k \times 200`$ Hz, and 1000 Hz lies right on the resonance), fire in an impulse, and it overflows to inf within 3 seconds. In the control, a one-pole LP with $`g = 0.99`$ has a peak of only $`3.07\times10^{-5}`$ at 2.5-3.0 s. Conclusion: the condition for a stable loop is that the loop gain be < 1 at "all frequencies"; $`|g| < 1`$ is sufficient only if everything else in the loop has gain ≤ 1.
3. $`\tanh`$ is bounded ($`|\tanh u| < 1`$), and its "slope" gets smaller with amplitude: for small signals $`\tanh u \approx u`$, the loop gain is 1.1 and the echoes grow; the larger the echo, the more the tanh squashes it and the smaller the effective gain. The amplitude settles at the point where "the effective (fundamental) loop gain is exactly 1". The output amplitude is determined by the feedback, the gain of the loop filters at the oscillation frequency, and the drive (exercise 4 computes it with a describing function). The output does not diverge, but it does not die away by itself either -- that is self-oscillation.
4. At least three situations:
   - **Modulation (chorus, flanger, vibrato)**: the delay time changes every sample. An integer delay makes the read position jump one slot at a time, and every jump is a break in the waveform → a stream of fine clicks / zipper noise instead of a smooth pitch variation.
   - **Smoothly changing the delay time**: when the user turns the knob, the smoothed delay is almost never an integer; you hear zipper noise here too.
   - **The pitch of a short delay (comb, Karplus-Strong, physical modeling)**: the pitch is $`f_s/D`$, an integer $`D`$ can only give discrete pitches, and for high notes the error is large (at 44.1 kHz A4 can only be made as 441 or 436.6 Hz, see exercise 7).
   - **Tempo sync**: the delay is 22 736.84 samples rather than an integer; the error is small, but in a feedback loop it is shifted in the same direction on every trip.
5. **Right when**: the delay time is fixed, there is no modulation, and the delay is long enough (> a few tens of ms). Half a sample is 0.01 ms, which the ear cannot resolve at all, and an integer read has no interpolation high-frequency loss, so it actually has the best sound quality.
   **Wrong when**: whenever the delay time **changes** -- automation, the user turning a knob, a tempo change, LFO modulation -- an integer delay produces stair-stepped jumps and clicks; and when a short delay needs an exact pitch. A general-purpose delay effect cannot know in advance whether the user will turn the knob, so it always uses fractional delay (this chapter's `Delay` uses cubic); when the delay sits on an integer, the cubic coefficients happen to be (0, 1, 0, 0) and nothing is lost.

## Exercise 3: Allpass interpolation

The key code (`ex3_allpass_interpolation.cpp`):

```cpp
void setDelay(float delaySamples) noexcept {
    const float delay = std::max(0.5f, delaySamples);
    d_ = static_cast<int>(std::floor(delay - 0.5f));
    const float frac = delay - static_cast<float>(d_); // in [0.5, 1.5)
    eta_ = (1.0f - frac) / (1.0f + frac);
}

float processSample(float x) noexcept {
    line_.push(0, x);
    const float v0 = line_.readInteger(0, d_);     // v[n]   = x[n − d]
    const float v1 = line_.readInteger(0, d_ + 1); // v[n−1] = x[n − d − 1]
    float y = eta_ * (v0 - y1_) + v1;              // y[n] = η(v[n] − y[n−1]) + v[n−1]
    if (std::abs(y) < 1.0e-20f) y = 0.0f;          // flush denormal
    y1_ = y;
    return y;
}
```

The reasoning:

- Split the delay into an "integer part $`d`$ (read directly from the circular buffer)" and a "fractional part $`\Delta`$ (handed to the allpass)". A first-order allpass has a delay of $`(1-\eta)/(1+\eta)`$ at low frequencies; setting this equal to $`\Delta`$ gives $`\eta = (1 - \Delta)/(1 + \Delta)`$.
- Why $`\Delta \in [0.5, 1.5)`$: as $`\Delta \to 0`$, $`\eta \to 1`$, the pole ($`z = -\eta`$) hugs the unit circle and the impulse response drags on for a long time; the transient when the delay changes is long too. Shifting $`\Delta`$ up by 0.5 puts $`\eta`$ in $`(-0.2, 1/3]`$.
- The recursive $`y[n-1]`$ must have denormals flushed.

Measured (delay 10.5, $`d = 10`$, $`\eta = 1/3`$):

| freq (Hz) | linear (dB) | cubic (dB) | allpass (dB) |
|---|---|---|---|
| 1000 | −0.02 | −0.00 | −0.00 |
| 8000 | −1.25 | −0.23 | +0.00 |
| 12000 | −3.01 | −1.07 | −0.00 |
| 20000 | −11.74 | −8.41 | +0.00 |

Phase delay (samples): linear is 10.500 at all frequencies (at $`\Delta = 0.5`$ it is a symmetric FIR with linear phase); the allpass gives 10.500 at 100 Hz, 10.509 at 4 kHz and 10.537 at 8 kHz, in complete agreement with theory. For fractional parts from delay 10.0 to 11.0, the delay measured at 100 Hz is accurate to 0.001.

What each is suited for:

- **linear**: the cheapest. Suited to places where the delay time does not change much or where the high end is not critical; not suited to modulation (the amount of high-frequency loss rises and falls with $`\Delta`$).
- **cubic**: 2 more reads and a dozen or so multiply-adds. Nearly flat below 8 kHz, with no state, so the delay can change arbitrarily fast → chorus, flanger, vibrato, and the default of this chapter's `Delay`.
- **allpass**: perfectly flat magnitude and the lowest cost (one multiplication), but it has state and its delay varies with frequency. Suited to a fixed or extremely slowly varying fractional delay: tuning for Karplus-Strong / waveguides, and delays inside a reverb.

## Exercise 4: Tape-style delay

The key code:

```cpp
float feedbackPath(int ch, float wet) noexcept {
    float v = highpass_.processSample(ch, wet);
    v = lowpass_.processSample(ch, v);
    if (saturate_)
        v = std::tanh(drive_ * v) / drive_;   // ≈ v for small signals; squashed to ±1/drive when |v| is large
    return feedback_ * v;
}
```

(`Saturation::shape(SaturationCurve::Tanh, drive_ * v) / drive_` is an equivalent way to write it.)

**(a) Darker on every trip.** The first echo comes straight out of the delay line without passing through the filters in the loop (measured peak 1.000, at n = 9600). The second echo is the first echo after HP → LP → × g, so the ratio of the two is the frequency response of the loop, $`g\,|H_{HP}(f)|\,|H_{LP}(f)|`$. How it is measured: use `goertzelAmplitude` on a window of length $`D`$ centred on the echo to measure the 500 Hz and 8 kHz components. Measured at 500 Hz: 0.665 (theory 0.666); at 8 kHz: 0.177 (theory 0.178). The theoretical values are computed by `getLoopGainAt()` from the actual frequency response of the digital `OnePole`; if you approximate with the analog one-pole formula $`1/\sqrt{1+(f/f_c)^2}`$, 8 kHz gives 0.170, a high-frequency difference of about 4%. Only 18% of the highs remain per trip while 66% of the lows do -- each echo is darker than the last.

**(b) Feedback 1.1.** The linear gain of the loop (at 200 Hz) is $`1.1 \times |H_{HP}(200)| \times |H_{LP}(200)| \approx 1.1 \times 0.995 \times 0.995 \approx 1.089 > 1`$ (the 20 Hz HP is still in the loop; its effect at 200 Hz is small but not 0). The linear version's peak at 2.5-3.0 seconds has already grown to 15.36 and is still growing; the tanh version has an overall peak of 1.02 and the tail settles at 0.305.

**Predicting the amplitude: the describing function.** The signal going around the loop is approximated by a sine. If the tanh input is $`X\sin\theta`$, the amplitude of the fundamental of the output is

```math
F_1(X) = \frac{2}{\pi}\int_0^\pi \tanh(X\sin\theta)\sin\theta\,d\theta
```

For small signals $`F_1(X) \approx X`$, and for large signals it approaches $`4/\pi`$. The fundamental gain of one trip around the loop $`= G \cdot F_1(X)/X`$ ($`G`$ = the linear loop gain). Steady oscillation ⇔ this gain = 1 ⇔ $`F_1(X)/X = 1/G`$. $`F_1(X)/X`$ decreases monotonically from 1, so $`X`$ is found by bisection, and the peak of the loop signal (= wet) is $`\text{fb}\cdot\tanh(X)/\text{drive}`$. The program computes 0.298 and the measurement is 0.305 (the difference comes from the 3rd harmonic also going around the loop, so it is not a pure sine).

**Question: why multiply by drive and then divide by drive?** For small signals $`\tanh(\text{drive}\cdot v)/\text{drive}`$ ≈ $`v`$, so the small-signal gain of the loop is **independent of drive**, and the decay time of the echoes is determined by the feedback alone; drive only determines "from what amplitude squashing begins" (about $`1/\text{drive}`$). Without dividing back, adding drive would be the same as multiplying the feedback by drive at the same time, and the echoes would suddenly become longer or even self-oscillate.
**The larger the drive, the smaller the self-oscillation amplitude**: the $`X`$ obtained by solving $`F_1(X)/X = 1/G`$ depends only on $`G`$, and the peak $`= \text{fb}\tanh(X)/\text{drive} \propto 1/\text{drive}`$. Measured (`l09_ex4_solution`): drive 2 → 0.305 (predicted 0.298), drive 1 → 0.608 (predicted 0.595), with a ratio of 2.00.

**(c) Musical material**: 375 ms, fb 0.6, LP 2.5 kHz, HP 120 Hz, drive 1.5, mix 0.35. Peak: dry 0.551, tape 0.402. What you should hear: the echoes get darker and more "distant", with more analog thickness than the ping-pong of example 2; `l09_ex4_dub.wav` is a 200 Hz tone sounding for only 0.3 seconds, after which the loop keeps singing on its own at a fixed amplitude.

## Exercise 5: Multi-tap delay

The key code:

```cpp
for (int t = 0; t < numTaps_; ++t) {
    const TapState& s = state_[t];
    const float v = line_.readCubic(0, s.delaySamples - 1.0f); // read first, push afterwards → −1
    wetL += s.gainL * v;
    wetR += s.gainR * v;
    last = v;                    // feedback takes the "raw" delayed signal of the last tap
}
line_.push(0, mono + feedback_ * last);
```

```cpp
void updateTap(int i) noexcept {
    s.delaySamples = std::clamp(t.delayMs * 0.001f * sampleRate, 2.0f, maxDelaySamples);
    panGains(t.pan, gl, gr);     // θ = (pan + 1)·π/4, gL = cos θ, gR = sin θ
    s.gainL = t.gain * gl;
    s.gainR = t.gain * gr;
}
```

The reasoning: there is only one delay line, and each tap is just a read position. Pan and gain are combined into two coefficients that are precomputed in the setter, so `process()` contains only multiply-adds.

Measured (impulse, L = R = 1 → mono 1):

| tap | delay | sample | gain | pan | L (expected / measured) | R (expected / measured) |
|---|---|---|---|---|---|---|
| 0 | 375 ms | 18000 | 0.60 | −0.6 | 0.5706 / 0.5706 | 0.1854 / 0.1854 |
| 1 | 500 ms | 24000 | 0.45 | +0.6 | 0.1391 / 0.1391 | 0.4280 / 0.4280 |
| 2 | 750 ms | 36000 | 0.30 | 0.0 | 0.2121 / 0.2121 | 0.2121 / 0.2121 |

Total energy 0.6525 = 0.6² + 0.45² + 0.3² (constant-power: $`g_L^2 + g_R^2 = 1`$). With feedback 0.5, tap0 reappears at 36000 + 18000 = 54000, with amplitude 0.2853 = 0.5 × 0.5706.

**Question: multi-tap vs three delays in series.**

- **Time positions of the echoes**: the echoes of a multi-tap are right at the $`D_0, D_1, D_2`$ you specify; in a series connection the echoes of the second unit are a "further delay" of the first unit's output, and you get $`D_1`$, $`D_2`$, $`D_1 + D_2`$, ... all sorts of combinations (plus each unit's own feedback), so the rhythm quickly becomes chaotic.
- **CPU / memory**: a multi-tap is one delay line + N reads; the series connection is N delay lines (N times the memory).
- **High-frequency loss from interpolation**: in a multi-tap each echo passes through interpolation only once; in series the signal is interpolated as many times as the number of units it passes through, and the high-frequency loss accumulates (`l09_ex5_solution` measures: one cubic at $`\Delta = 0.5`$ gives −1.07 dB at 12 kHz, and three in series give −3.22 dB).

## Exercise 6: Switching the delay time with a crossfade

The key ideas (see `ex6_crossfade_delay.cpp` for the full code):

- State: `headDelay_[2]`, `active_` (the head currently in use), `fadePos_` (−1 = not fading), `pending_`.
- `setDelayMs()`: if fading → `pending_ = true`; otherwise set `headDelay_[1 − active_]` to the new value and `fadePos_ = 0`.
- Each sample in `process()`: $`t = \text{fadePos}/\text{fadeLength}`$, $`g_B = \tfrac12(1 - \cos\pi t)`$, $`g_A = 1 - g_B`$, `wet = gA·readCubic(headA) + gB·readCubic(headB)`; when the fade finishes, `active_ = 1 − active_`, `fadePos_ = −1`, and if `pending_` is set and the target differs from the current value, the next fade starts immediately.
- The delay of each read head is **fixed**, so neither has Doppler; the switch is a crossfade of two signals, and the raised-cosine weights are smooth, so there is no click.
- `reset()` must align both heads straight to the current target (the buffer has been cleared, so no fade is needed); otherwise the common order `prepare() → setDelayMs() → reset()` would throw away a fade that has not started and leave us stuck at the old delay.
- A practical detail: the test splits the block at the switch point (48000, not a multiple of 512) to emulate sample-accurate automation.

Measured (440 Hz, 10 → 250 ms at t = 1 s, crossfade 20 ms):

```
Largest adjacent-sample difference within 100 ms after the switch: crossfade 0.0288, direct jump 0.2701
Frequency measured over ±50 ms around the switch: crossfade 444.2 Hz (expected 440)
Delay smooth transition (20 ms ramp), measured during the ramp: 4840.8 Hz (d(tau)/dt = 12 -> theoretical |f_out| = 4840 Hz, a negative sign means reverse playback)
Largest error between y[n] and x[n - 12000] after the fade ends: 0.00e+00
Changing the delay again mid-fade (100 -> 300 ms): it finally settles at 300 ms, error 1.72e-13
```

The reason for 444 Hz rather than 440 Hz is in the phase analysis in the next paragraph; compared with the 4840 Hz (reverse playback) of the smooth transition, there is effectively no Doppler at all.

**Question: the amplitude in the middle of the fade.** The two heads are 240 ms = 105.6 periods apart, a phase difference of 0.6 period = 216° (equivalent to −144°). The combined signal is $`g_A e^{j0} + g_B e^{j\varphi}`$, with amplitude $`\sqrt{1 - 2g_Ag_B(1 - \cos\varphi)}`$, which is smallest at $`g_A = g_B = 0.5`$: $|\cos(\varphi/2)| = |\cos 108°| = $ **0.309**, i.e. 31% of the original amplitude (−10.2 dB). `l09_ex6_solution` measures a peak of 0.1547 at the fade midpoint (original amplitude 0.5), a ratio of 0.309, in agreement with theory; the phase of the combined signal also slips from A to B within these 20 ms (about 0.4 period), which spread over a 100 ms measurement window appears as a +4 Hz deviation.
In real musical material, the two heads read **uncorrelated** content from before and after 240 ms; on average the power is $`g_A^2 + g_B^2`$, which is 0.5 (−3 dB) at the midpoint and lasts only 20 ms, so it is usually inaudible. For a smoother result you can use an equal-power crossfade instead ($`g_A = \cos(\pi t/2)`$, $`g_B = \sin(\pi t/2)`$), but when the two heads are **correlated** (for example for a very small change of delay) it bulges by +3 dB in the middle -- the trade-off is the same as the pan law of chapter 01. The solution program feeds white noise (white noise is uncorrelated with itself at different delays) into the real `CrossfadeDelay`, switching from 10 ms to 250 ms; to keep $`g_A, g_B`$ almost fixed at 0.5 inside the measurement window, the crossfade is stretched to 1 second and only the RMS within ±20 ms of the midpoint is measured, averaged over 8 seeds. The equal-power line is only a formula comparison (this exercise does not implement equal-power, so it is not a check):

```
Level at the crossfade midpoint (relative to the input, measured on CrossfadeDelay):
  raised cosine (0.5 + 0.5), uncorrelated material: -2.98 dB (theory 10·log10(0.5² + 0.5²) = -3.01)
  (Formula comparison) equal-power midpoint gA = gB = cos45°: uncorrelated +0.00 dB, correlated +3.01 dB
```

## Exercise 7: Comb filters

1. Feedback comb, $`D = 240`$, $`g = 0.8`$, $`f_s/D = 200`$ Hz:
   - The peaks are at $`k \times 200`$ Hz (0, 200, 400...), with $`|H| = 1/(1 - 0.8) = 5`$, **+13.98 dB**.
   - The notches are at $`(k + \tfrac12) \times 200`$ Hz (100, 300, 500...), with $`|H| = 1/1.8 = 0.556`$, **−5.11 dB**.
   - `l09_ex7_solution` measures +13.98 dB at 200, 400, 1000 and 5000 Hz, and −5.11 dB at 300, 500, 1100 and 5100 Hz.
2. Feedforward comb: $`|H|^2 = 1 + g^2 + 2g\cos(\omega D)`$. The peaks are at the same $`k \times 200`$ Hz, with $`1 + 0.8 = 1.8`$, **+5.11 dB**; the notches are at $`(k+\tfrac12) \times 200`$ Hz, with $`1 - 0.8 = 0.2`$, **−13.98 dB**. This is exactly "upside down" relative to the feedback comb: feedforward has deep, narrow notches, feedback has tall, narrow peaks.
3. $`g = -0.8`$: the sign of $`-2g\cos(\omega D)`$ flips and the peaks move to where $`\cos(\omega D) = -1`$, i.e. **100, 300, 500 Hz...** (odd multiples of $`f_s/2D`$), and the notches move to 200, 400 Hz. Measured: +13.98 dB at 100/300/500 Hz and −5.11 dB at 200/400 Hz. Audibly: the fundamental drops from 200 Hz to 100 Hz (an octave lower), and only odd harmonics remain, so the timbre is like a tube closed at one end (a hollow quality like a clarinet).
4. $D = 44100/440 = $ **100.227 samples**. The integer choices:
   - $`D = 100`$ → $`441.0`$ Hz, $1200\log_2(441/440) = $ **+3.9 cents**
   - $`D = 101`$ → $`436.63`$ Hz, $1200\log_2(436.63/440) = $ **−13.3 cents** (clearly out of tune)
   - Measured: the integer 100 has its peak at 441.0 Hz, 101 at 436.6 Hz, and 100.227 with cubic at 440.0 Hz. (The program's peak-finding sweep has a resolution of 0.1 Hz; computing cents from the rounded 436.6 Hz would give −13.4, so the cents in `l09_ex7_solution` are computed from the theoretical frequency $`f_s/D`$.) The higher the pitch, the smaller $`D`$ is and the larger the cents error of an integer -- this is why Karplus-Strong needs a fractional delay (usually an allpass, exercise 3).
5. The peaks of a feedback comb line up at $`f_0, 2f_0, 3f_0, \ldots`$ ($`f_0 = f_s/D`$), which is exactly the harmonic structure of a periodic signal. White noise through it has its energy concentrated near these harmonics, and the ear hears the pitch of the fundamental $`f_0`$. In the time domain: output = input + echoes repeating every $`D`$ samples, which approaches a signal of period $`D`$ more and more closely. `makePluck()` is a feedback comb: the ring buffer length = $`D`$ = the period, and inside the loop "the average of two adjacent points × 0.996" acts as lowpass + decay, so the highs decay faster than the lows and it sounds like a plucked string (Karplus-Strong).
6. A single comb has fixed echo spacing, which in the time domain is a periodic "tap, tap, tap" echo (flutter echo) and in the frequency domain is equally spaced peaks (one definite pitch) -- very metallic. With several combs in parallel the echo density becomes several times higher; choosing **mutually prime** delay times means the echoes of the different combs almost never coincide in time and the peaks of the frequency response almost never fall on the same frequency, so the peaks are distributed more densely and irregularly, and the ear no longer hears a single pitch but something like a room's reverberation. Following them with a few allpasses to scatter every echo (raising the echo density without changing the overall spectrum) gives the Schroeder reverb. The $`g`$ of each comb is determined with the $`T_{60}`$ formula from exercise 1, and chapter 12 follows this approach.
