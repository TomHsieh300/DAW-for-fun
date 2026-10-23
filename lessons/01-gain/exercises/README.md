# Chapter 01 Exercises: Gain, Parameter Smoothing and Pan

**English** | [繁體中文](README.zh-TW.md)

Difficulty: ★☆☆ basic / ★★☆ intermediate / ★★★ advanced.
The implementation exercises (3–7) all have starter code `exercises/starter/exN_xxx.cpp` (it compiles, leaves `// TODO`s, and prints FAIL when run -- that is expected) and a complete solution `solutions/exN_xxx.cpp`.
The solutions and explanations (including the conceptual and calculation problems) are in [`../solutions/README.md`](../solutions/README.md); we recommend finishing the exercises yourself before looking.

Build and run (Windows, from the project root):

```powershell
.\scripts\build.ps1 -Only 01-gain -NoTest
cd build
.\bin\Debug\l01_ex3_starter.exe      # your version (edit exercises/starter/ex3_exp_smoother.cpp)
.\bin\Debug\l01_ex3_solution.exe     # the reference solution
```

After editing a starter, go back to the project root and run `build.ps1` again (or, in the build directory, run `cmake --build . --config Debug --target l01_ex3_starter`).
An exercise is done when all the self-checks of its starter pass and it prints `[PASS]`.

---

## Exercise 1 ★☆☆ (calculation) dB mental math

Do not use a calculator (at most these numbers: $`\log_{10} 2 \approx 0.301`$, $`\log_{10} 3 \approx 0.477`$, $`\sqrt{2} \approx 1.414`$, $`\sqrt{10} \approx 3.162`$) and answer:

1. How many dB is an amplitude factor of 0.25? And an amplitude factor of 4?
2. What amplitude factor does +10 dB correspond to? And what power factor?
3. A gain plugin has three stages in series: −3 dB, +8 dB, −11 dB. What is the total gain in dB? Roughly what is the amplitude factor?
4. A sine wave with a peak amplitude of 0.5: what is its peak in dBFS? What is its RMS in dBFS?
5. Two **identical** tracks, each with a peak of −6 dBFS: what is the peak in dBFS after summing? If they are two **mutually uncorrelated** noises, each with an RMS of −6 dBFS, what is the RMS in dBFS after summing?
6. 16 mutually uncorrelated noise tracks, each with an RMS of −20 dBFS, mixed together: what is the RMS in dBFS? In the worst case, how many dB higher than a single track's peak can the peak get?
7. The smallest quantization step of 16-bit integer PCM is $`1/32768`$. How many dBFS is this step? (This is roughly the order of magnitude of the 16-bit noise floor.)

## Exercise 2 ★★☆ (concepts + calculation) Parameter smoothing and pan law

1. Using "multiplication in the time domain = convolution in the frequency domain" (first review the intuition of "amplitude modulation and sidebands" at the beginning of README section 2.4), explain why an instant jump of the gain from 1 to 0.1 produces a click, and why the click is broadband.
2. At 44.1 kHz, a 50 ms linear ramp from gain 1.0 to 0.1: how many samples in total? How much does it advance per sample?
3. A one-pole exponential smoother: $`y[n] = y[n-1] + a\,(t - y[n-1])`$. At 44.1 kHz with τ = 20 ms, what is $`a`$? How many samples, and how many ms, does the output need to reach 99% of the target?
4. Write down $`g_L`$ and $`g_R`$ (linear and in dB) of the linear and the constant power pan laws at the **center**, as well as the values of $`g_L^2 + g_R^2`$ and $`g_L + g_R`$. Then answer: why does the broadcast / film industry prefer linear or −4.5 dB over pure constant power?
5. Explain in one sentence the difference between **mono pan** and **stereo balance**; which of the two does `dawlab::dsp::Panner::process()` do to a stereo block?
6. Why doesn't `Gain`'s polarity invert simply multiply by −1, and instead sets the SmoothedValue's target to −g? Which value does the gain pass through at the moment of toggling?

## Exercise 3 ★★☆ (implementation) The one-pole exponential smoother and measuring the time constant

Starter: `starter/ex3_exp_smoother.cpp` Solution: `../solutions/ex3_exp_smoother.cpp` Targets: `l01_ex3_starter` / `l01_ex3_solution`

Implement `ExpSmoother`:

- `reset(sampleRate, timeConstantSeconds)`: compute $`a = 1 - e^{-1/(\tau f_s)}`$ ($`a = 1`$ when τ = 0), and snap current to target.
- `getNextValue()`: apply the difference equation; when $`|t - y| <`$ `kSnapThreshold` (1e-6), set current directly to target (otherwise it is never reached, `isSmoothing()` stays true forever, and denormals appear).
- `samplesToReach(s, fraction, maxSamples)`: on a 0 → 1 step, measure the index (counting from 1) of the first output sample ≥ fraction.

The self-check verifies: the value of $`a`$; 63.2% at ≈ τ; 99% at ≈ 4.605 τ; 99.9% at ≈ 6.908 τ; the error against the closed form $`1 - (1-a)^n`$; monotonic increase; never exceeding the target; and that smoothing really stops after long enough.

Hint: the state in the starter code is deliberately a `double`. After finishing, try changing `current_` to `float`, see what happens to the last two checks (`!s.isSmoothing()`, `getCurrentValue() == 1.0f`), and think about why (the IIR filters of chapter 02 run into the same thing again).

## Exercise 4 ★★☆ (implementation) Fader taper: fader position ↔ dB

Starter: `starter/ex4_fader_taper.cpp` Solution: `../solutions/ex4_fader_taper.cpp` Targets: `l01_ex4_starter` / `l01_ex4_solution`

Breakpoints (position → dB): `1.00 → +6`, `0.75 → 0`, `0.50 → −12`, `0.25 → −30`, `0.05 → −60`, `0.00 → −∞`.
Every segment from 0.05 upward is "dB linear in position"; the bottom segment [0, 0.05] is "linear factor linear in position", going from −60 dB to exactly 0.

Implement:

- `faderToGain(position)`: position → linear factor (`faderToDb` is already wrapped for you).
- `gainToFader(gain)`: the inverse function, used when the UI needs to draw the current gain back as a fader position.

The self-check verifies: every breakpoint is exact; the midpoints within segments (0.875 → +3 dB, 0.625 → −6 dB); the bottom segment is linear in factor (0.025 → half the factor of −60 dB); `faderToGain(0) == 0`; the whole curve is monotonically increasing; the inverse round-trip error is < 1e-4; and out-of-range values (−200 dB, +20 dB) are clamped.

## Exercise 5 ★★☆ (implementation) Multiplicative ramp: fader automation that is a straight line in dB

Starter: `starter/ex5_multiplicative_ramp.cpp` Solution: `../solutions/ex5_multiplicative_ramp.cpp` Targets: `l01_ex5_starter` / `l01_ex5_solution`

This chapter's `dsp::SmoothedValue` is a linear ramp: the factor is a straight line in time. Using it for a long fade-out (for example automation pulling a fader from 0 dB to −60 dB) sounds "loud the whole time and then suddenly gone", because the ear hears dB. This exercise implements another kind of smoother, `MultiplicativeRamp`: every sample **multiplies** by the same factor

$$
r = \left(\frac{\text{target}}{\text{current}}\right)^{1/N},\qquad g[n] = g[0]\,r^n,\qquad \text{dB}[n] = \text{dB}[0] + n \cdot 20\log_{10} r
$$

that is, it runs in a straight line in dB (JUCE's `SmoothedValue<float, ValueSmoothingTypes::Multiplicative>` does exactly this). 0 cannot be reached by multiplying and nothing can be multiplied up from it, so 0 is replaced by a floor `kFloorGain` (−100 dB), and the last sample snaps to the exact target value.

Implement:

- `setTargetValue(value)` (TODO 1): start from the **current value** (changing the target mid-ramp does not jump), replace 0 with the floor, and compute `multiplier_`.
- `getNextValue()` (TODO 2): `current_ *= multiplier_` (the snap on the last step is already written).

The self-check (100 ms = 4800 samples @ 48 kHz, 0 dB → −60 dB) verifies: each sample advances −0.0125 dB; halfway, the multiplicative ramp is at −30.00 dB while the linear ramp of `dsp::SmoothedValue` is at −6.01 dB; the deviation from the straight line in dB is < 0.001 dB; the time spent above −20 dB during the ramp is 1/3 for the multiplicative ramp and 90.1% for the linear one; the maximum |ΔdB| between adjacent samples is 0.0125 dB for the multiplicative ramp and > 1 dB for the linear one; and the end is exactly the target. For a mute (→ 0), halfway is −50 dB and the last sample is exactly 0; for an unmute (0 →), the first sample is about −100 dB rather than 0; and when the ramp has reached −15 dB and the target is switched back to 0 dB, the next sample advances by < 0.01 dB.

Think about it: since the multiplicative ramp is so good, why does this chapter's `Gain` (the 50 ms fader smoothing) still use a linear ramp? When ramping from the −100 dB floor to 0 dB, the first half of the time is spent below −50 dB -- for an unmute, is that an advantage or a disadvantage? Why must the state be a `double`?

## Exercise 6 ★★☆ (implementation) Mute / solo resolution logic for N tracks

Starter: `starter/ex6_mute_solo.cpp` Solution: `../solutions/ex6_mute_solo.cpp` Targets: `l01_ex6_starter` / `l01_ex6_solution`

The rules (the chapter 14 DAW mixer uses the same rules; the difference is that there a track has no soloSafe flag, and only the FX bus and the master are implicitly solo-safe):

1. As soon as any track is soloed, only the soloed tracks sound.
2. Mute always takes effect (solo + mute → silent).
3. A soloSafe track still sounds in solo mode (but is silent when muted itself).

Implement:

- `resolveAudible(tracks)`: returns, for each track, "should it be audible right now?".
- `audibleToGainDb(audible)`: audible → 0 dB, not audible → `kMinusInfinityDb`.

The self-check has 8 truth-table cases, and finally feeds the result into `dsp::Gain`: mute at about 0.32 s (sample 15372) and unmute at about 0.64 s (sample 30756), verifying that the first difference does not grow (no click), that the peak during the mute is exactly 0, and that the signal returns after the unmute. These two times deliberately fall in the middle of a block, on the peaks of the 1 kHz sine (not at a zero crossing, otherwise you could not hear a click even without a ramp, see README section 2.4); the program also runs a "control case" with `setRampSeconds(0)` to confirm that the check really does catch the click when there is no smoothing.

## Exercise 7 ★★★ (implementation) Mid/Side stereo width and stereo balance

Starter: `starter/ex7_stereo_width.cpp` Solution: `../solutions/ex7_stereo_width.cpp` Targets: `l01_ex7_starter` / `l01_ex7_solution`

$$
M = \tfrac{1}{2}(L + R),\quad S = \tfrac{1}{2}(L - R);\qquad L' = M + w\,S,\quad R' = M - w\,S
$$

Implement:

- `applyStereoWidth(stereo, width)`: in place; $`w = 0`$ gives mono, $`w = 1`$ leaves it unchanged, and $`w = 2`$ doubles the left/right difference.
- `applyBalance(stereo, balance)`: for $`b > 0`$ the left channel is multiplied by $`(1 - b)`$ and the right channel is untouched; for $`b < 0`$ the reverse.

The self-check verifies: for the extreme signal with left = sine and right = 0, the left/right peaks at $`w`$ = 0 / 1 / 2 are 0.25/0.25, 0.5/0 and 0.75/0.25 respectively (at $`w = 2`$ an inverted-polarity signal appears in the right channel); **for any $`w`$, $`(L' + R')/2`$ equals the original $`M`$** (mono compatibility); for uncorrelated noise at $`w = 2`$ the side rises by 6.02 dB and the mid is unchanged; at balance +1 the left channel is exactly 0 and not one sample of the right channel has changed; and at balance +0.5 the left is −6.02 dB and the right 0 dB.

Think about it: when $`w > 1`$, the peak of $`L'`$ can exceed the original peak (in the example above, 0.5 → 0.75); what does this mean for headroom? If the left and right channels are exactly identical (true mono), no $`w`$ has any effect -- why?

## Exercise 8 ★☆☆ (calculation + concepts) The same vocal on a mono track and on a stereo track

Read README section 2.10 first. A mono vocal is, for simplicity, a constant 0.5. Use the constant power pan law of section 2.9 for the mono track and the linear balance law $`g_L = \min(1, 1 - p)`$, $`g_R = \min(1, 1 + p)`$ for the stereo track; the faders are at 0 dB and there are no inserts.

1. Give the L and R output values (and dBFS) in each of these four cases:
   - (a) on a **mono track**, panned to the center;
   - (b) duplicated to a **stereo track** as $`L = R`$, balance at the center;
   - (c) on the mono track, panned **hard right**;
   - (d) on the stereo track, balance at **+0.5**.
2. How many dB louder is each side in (b) than in (a)? Where does this difference come from?
3. Compare (c) and (d): both are "to the right", but what is left on the left speaker in each case?
4. (Concepts) A chorus with stereo spread is inserted on the track. Why does it sound wide in case (b) but not in case (a)? What would you change in (a) to make the chorus wide while keeping the vocal on a mono track?
