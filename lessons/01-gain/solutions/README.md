# Chapter 01 Exercise Solutions

**English** | [繁體中文](README.zh-TW.md)

The problems are in [`../exercises/README.md`](../exercises/README.md). The complete programs for the implementation problems are in this folder; each one is verified with numbers through `DAWLAB_CHECK` / `DAWLAB_CHECK_NEAR` and prints `[PASS] l01_exN (...)` when run.

---

## Exercise 1: dB mental math

1. $`20\log_{10} 0.25 = 20 \times (-2 \times 0.301) = -12.04`$ dB. An amplitude factor of 4 is $`+12.04`$ dB (the opposite sign).
2. Amplitude factor $`10^{10/20} = 10^{0.5} = \sqrt{10} \approx 3.162`$; power factor $`10^{10/10} = 10`$. (+10 dB is the rule of thumb for "sounds twice as loud"; it is neither an amplitude doubling, +6 dB, nor a power doubling, +3 dB.)
3. dB values simply add: $`-3 + 8 - 11 = -6`$ dB. The amplitude factor is $`10^{-6/20} = 10^{-0.3} \approx 0.5`$ (exact value 0.501). This is the benefit of using dB: when several stages are cascaded, multiplication becomes addition.
4. Peak: $`20\log_{10} 0.5 = -6.02`$ dBFS. The RMS of a sine is $`1/\sqrt{2}`$ of its peak: $`0.5 / 1.414 = 0.354`$, and $`20\log_{10} 0.354 = -6.02 - 3.01 = -9.03`$ dBFS. The peak of a sine is always 3.01 dB above its RMS (crest factor $`\sqrt{2}`$).
5. Identical signals: the amplitudes add → $`2 \times 0.5 = 1.0`$ → **0 dBFS** (+6.02 dB). Uncorrelated noise: the powers add → the RMS is multiplied by $`\sqrt{2}`$ → $-6 + 3.01 = $ **−2.99 dBFS** (≈ −3 dBFS, +3.01 dB).
6. RMS: $-20 + 10\log_{10} 16 = -20 + 12.04 = $ **−7.96 dBFS**. In the worst case (the peaks of all 16 tracks occurring at the same time) the peak can be higher by $20\log_{10} 16 = $ **+24.08 dB**. The actual peak usually lies between +12.04 and +24.08. Compare example 3 (B): for 8 tracks the theoretical range is +9.03 to +18.06 dB, and the measured value is +17.5 dB.
7. $20\log_{10}(1/32768) = -20 \times 15 \times 0.301 = $ **−90.3 dBFS** ($`32768 = 2^{15}`$). The commonly heard "the dynamic range of 16-bit is about 96 dB" is $`20\log_{10}(2^{16}) = 6.02 \times 16 = 96.3`$ dB: it counts all 65536 quantization levels from −32768..32767, corresponding to a peak-to-peak range of 2; whereas $`1/32768`$ is the step size with peak = 1.0 taken as 0 dBFS, and uses only $`2^{15}`$. The two differ by exactly one bit, i.e. $`20\log_{10} 2 = 6.02`$ dB (96.3 − 90.3). If you instead compute "the RMS of a full-scale sine ÷ the RMS of the quantization noise ($`\Delta/\sqrt{12}`$)", you get a different formula, SQNR $`= 6.02N + 1.76 \approx 98.1`$ dB (chapter 00); that +1.76 dB is what comes from the RMS and the crest factor of the sine.

## Exercise 2: Parameter smoothing and pan law

1. $`y[n] = x[n]\,g[n]`$, and multiplication in the time domain equals convolution in the frequency domain, $`Y(f) = X(f) * G(f)`$. $`g[n]`$ is a step function whose spectrum $`|G(f)| \propto 1/f`$ extends from DC all the way to Nyquist (falling by only 6 dB per octave). The pure tone $`X(f)`$ is a single line, and after convolution the whole shape of $`G(f)`$ is "pasted" onto the position of that line -- a signal that originally had only 1 kHz suddenly has energy at all frequencies, and the ear hears a broadband "click". After smoothing, $`g[n]`$ is continuous (piecewise linear), the spectrum is $`\propto 1/f^2`$ (12 dB per octave), and it is much smaller overall. The sideband table of example 1 (B): stepped −34.7 / −40.8 / −46.8 dB (6 dB per octave), smoothed −74.4 / −86.4 / −98.5 dB (12 dB per octave).
2. $`N = 0.05 \times 44100 = 2205`$ samples. Each step is $`(0.1 - 1.0) / 2205 = -4.08 \times 10^{-4}`$.
3. $`\tau f_s = 0.02 \times 44100 = 882`$; $`a = 1 - e^{-1/882} = 1 - 0.998867 = 1.133 \times 10^{-3}`$ (approximately $`1/882 = 1.134 \times 10^{-3}`$). To 99%: $`n = \ln(100) \times 882 = 4.605 \times 882 \approx 4062`$ samples ≈ **92.1 ms**. Note that a smoother with "τ = 20 ms" actually needs 92 ms to reach 99%, much slower than a linear ramp labeled with the same 20 ms -- when choosing τ remember to multiply by 4.6.
4. At the center $`\theta = 0.5`$:

   | | $`g_L = g_R`$ | dB | $`g_L^2 + g_R^2`$ | $`g_L + g_R`$ |
   |---|---:|---:|---:|---:|
   | Linear | $`1 - 0.5 = 0.5`$ | −6.02 | $`0.25 + 0.25 = 0.5`$ (−3.01 dB) | 1 (0 dB) |
   | Constant power | $`\cos(\pi/4) = 0.7071`$ | −3.01 | $`0.5 + 0.5 = 1`$ (0 dB) | 1.414 (+3.01 dB) |

   Broadcast / film content is often downmixed to mono (TV speakers, telephones, public address). With constant power, when summed to mono the center material (dialogue, lead vocal) is 3 dB louder than at the edges and the overall balance goes off; the amplitude sum of linear is always 1, so nothing changes in mono; −4.5 dB is the compromise in which each side gives up 1.5 dB. Music production monitors on speakers and rarely downmixes, so it prefers constant power.
5. A mono pan "distributes one signal to two outputs, left and right" ($`L = g_L x`$, $`R = g_R x`$; all the content is kept and only its position moves); a stereo balance "multiplies two existing signals, left and right, by a gain each" ($`L' = g_L L`$, $`R' = g_R R`$; when turned to one side the content of the other side disappears and is not moved over). What `Panner::process()` does to a stereo block is a **balance** (channel 0 times $`g_L`$, channel 1 times $`g_R`$); it equals a mono pan only when the two channels hold the same content. For a true mono pan use `processMonoToStereo()`.
6. Multiplying directly by −1 would make the gain jump instantly from $`+g`$ to $`-g`$, a jump of $`2g`$ -- a step even larger than a mute, so it would definitely click. After setting the target to $`-g`$, the SmoothedValue walks linearly from $`+g`$ to $`-g`$ and **passes through 0** on the way (right in the middle of the ramp); the whole process is continuous, so toggling Ø makes no sound. The price is that during the 50 ms of the toggle the level first drops to 0 and then comes back ($`|g|`$ forms a V shape, smaller than $`g`$ everywhere except the two end points), which sounds like a brief dip.

## Exercise 3: The one-pole exponential smoother (`ex3_exp_smoother.cpp`)

The key idea:

```cpp
coeff_ = tauSamples > 0.0 ? 1.0 - std::exp(-1.0 / tauSamples) : 1.0;   // a = 1 − e^{−1/τfs}

float getNextValue() noexcept {
    current_ += coeff_ * (target_ - current_);          // each sample covers a fraction a of the remaining distance
    if (std::abs(target_ - current_) < kSnapThreshold)  // 1e-6: a gap below −120 dB counts as arrived
        current_ = target_;
    return static_cast<float>(current_);
}
```

Measuring the time constant means calling it sample by sample on a 0 → 1 step and counting at which sample it first reaches ≥ 0.632 / 0.99 / 0.999. The output of a run:

```
tau = 10.0 ms = 480 samples, a = 0.002081 (theory 1 - e^(-1/480) = 0.002081)
Reaches 63.2%: 480 samples (1.00 tau, expected 1.00 tau)
Reaches 99%  : 2211 samples (4.61 tau, expected ln(100) = 4.605 tau)
Reaches 99.9%: 3316 samples (6.91 tau, expected ln(1000) = 6.908 tau)
Max error vs. the closed form 1 - (1-a)^n: 4.29e-08; monotonically increasing: yes; never exceeds the target: yes
After 50 ms: linear ramp = 1.000, exponential (tau = 50 ms) = 0.632
[PASS] l01_ex3 (11 checks passed)
```

The last line is worth remembering: for the same "50 ms", the linear ramp reaches exactly 1.000 at 50 ms, while the exponential (τ = 50 ms) has only reached 0.632.

**Why must the state be a double?** This is the trap the exercise hint makes you step on. If `current_` is a float, once the remaining distance $`d = t - y`$ becomes so small that $`a \cdot d`$ is below half an ulp of $`y`$, `current_ += a·d` adds nothing -- the state gets stuck. When $`y`$ approaches 1 from below (in $`[0.5, 1)`$), the ulp is $`2^{-24} \approx 6 \times 10^{-8}`$ and half an ulp is $`\approx 3 \times 10^{-8}`$, so it gets stuck at $`d \approx 3 \times 10^{-8} / a = \varepsilon/(4a) \approx 3 \times 10^{-8} / 0.0021 \approx 1.4 \times 10^{-5}`$ ($`\varepsilon = 2^{-23} \approx 1.19 \times 10^{-7}`$ is the ulp of 1.0; the float version measured got stuck at $`d = 1.43 \times 10^{-5}`$, matching the derivation), never reaching the snap threshold of 1e-6. As a result `isSmoothing()` is true forever (the last two checks FAIL), and every block does pointless multiplications. With a double (ε ≈ 2.2 × 10⁻¹⁶) the place where it gets stuck is on the order of $`10^{-14}`$, far below the threshold. Another approach is to set the snap threshold larger than the stuck position (for example 1e-4 ≈ −80 dB), but then the threshold would have to change with τ, which is less clean than simply using a double.

## Exercise 4: Fader taper (`ex4_fader_taper.cpp`)

The key idea: in the bottom segment $`p \le 0.05`$ the factor is linear, `dbToGain(-60) * (p / 0.05)` (which gives exactly 0 at $`p = 0`$); for the other segments, find which two breakpoints $`p`$ falls between, `lerp` in **dB**, and finally apply `dbToGain`. The inverse function does exactly the same: for a factor at or below the −60 dB factor it inverts in factor, otherwise it first applies `gainToDb` and then inverts the position in dB.

```cpp
for (int i = 1; i < kNumPoints; ++i)
    if (p <= kPoints[i].position) {
        const float t = (p - kPoints[i-1].position) / (kPoints[i].position - kPoints[i-1].position);
        return dbToGain(lerp(kPoints[i-1].db, kPoints[i].db, t));
    }
```

The output of a run (excerpt):

```
position         dB       gain
    0.00    -100.00    0.00000      <- a true 0 (printed as this course's −∞ = −100 dB)
    0.05     -60.00    0.00100
    0.25     -30.00    0.03162
    0.50     -12.00    0.25119
    0.75       0.00    1.00000
    0.90       3.60    1.51356
    1.00       6.00    1.99526
round-trip max error of gainToFader(faderToGain(p)): 5.96e-08
[PASS] l01_ex4 (16 checks passed)
```

Observations: the top quarter of the fader travel (0.75 → 1.0) covers only 6 dB, 1.2 dB per 0.05 of travel, which suits fine adjustment; the 0.25 → 0.5 segment has 3.6 dB per 0.05; 0.05 → 0.25 has 7.5 dB per 0.05. This is "dense at the top, sparse at the bottom". `dbToFader(-200)` gives 0 and `dbToFader(20)` gives 1: out-of-range values must be clamped so that the UI never draws a position outside the fader.

## Exercise 5: The multiplicative ramp (`ex5_multiplicative_ramp.cpp`)

The key idea: a linear ramp **adds** the same step every sample, a multiplicative ramp **multiplies** by the same factor every sample, so the latter is a straight line in dB:

```cpp
const double from = std::max(current_, kFloorGain);                    // 0 is replaced by the −100 dB floor
const double to   = std::max(static_cast<double>(value), kFloorGain);
current_    = from;
multiplier_ = std::pow(to / from, 1.0 / static_cast<double>(rampSamples_));
// getNextValue(): the last step snaps to the exact target_, the others do current_ *= multiplier_
```

The output of a run:

```
multiplier r = 0.998562 (-0.0125 dB per sample)
Halfway (2400 samples): multiplicative -30.00 dB, linear -6.01 dB
Max deviation from the straight line in dB: 0.00000 dB
Time spent above -20 dB during the ramp: multiplicative 33.3%, linear 90.1%
Max |delta dB| between adjacent samples: multiplicative 0.0125 dB, linear 1.381 dB (in the last few samples)
mute: halfway -50.00 dB, second-to-last -99.98 dB, last = 0
unmute: first sample 1.002e-05 (-99.9792 dB), last sample 1.000000
Switching back to 0 dB at -15.00 dB: the next sample advances only +0.0031 dB
Wrote dawlab_output/l01_ex5_multiplicative_ramp.wav (first half: linear fade-out, second half: multiplicative fade-out)
[PASS] l01_ex5 (17 checks passed)
```

- $`r = 0.001^{1/4800} = e^{\ln(0.001)/4800} = 0.998562`$, and $`20\log_{10} r = -60/4800 = -0.0125`$ dB.
- Halfway through, the linear ramp's factor is still $`1 - 0.999/2 = 0.5005`$ (−6.01 dB), and it takes until $`(1 - 0.1)/0.999 = 90.1\%`$ of the time to fall below −20 dB; the remaining less than 10% of the time has to cover −20 → −60 dB, and in the last few samples each step drops 1.4 dB. That is the reason for "loud the whole time and then suddenly gone". The multiplicative ramp reaches −20 dB at exactly 1/3 of the way, with every step 0.0125 dB.
- A side effect of the floor: for a mute the dB line goes 0 → −100 dB and is already at −50 dB halfway; for an unmute the first half is spent below −50 dB, which sounds as if "it waited a moment before fading in". So the multiplicative ramp suits long fades and automation of dB parameters, while for short mutes / unmutes (the 50 ms of this chapter's `Gain`, the 10 ms of the chapter 14 `ChannelStrip`) a linear ramp is actually more natural -- this answers the first and second "think about it" questions.
- Why `double`: with 4800 successive multiplications, each float rounding contributes a relative error of about $`6\times10^{-8}`$, and accumulated it reaches the order of $`10^{-4}`$ (about 0.001 dB), and the longer the ramp the worse it gets; a double stays at the order of $`10^{-13}`$. The snap on the last step keeps the end independent of the accumulated error (a mute stops at an exact 0).

## Exercise 6: Mute / solo resolution (`ex6_mute_solo.cpp`)

The key idea: first scan once to find `anySolo`, then apply the following to each track

```cpp
audible[i] = !t.mute && (!anySolo || t.solo || t.soloSafe);
```

The output of a run:

```
all normal                               [0:---->on ] [1:---->on ] [2:---->on ] [3:---->on ]
mute 1                                   [0:---->on ] [1:M--->off] [2:---->on ] [3:---->on ]
solo 2                                   [0:---->off] [1:---->off] [2:-S-->on ] [3:---->off]
solo 0 + solo 2                          [0:-S-->on ] [1:---->off] [2:-S-->on ] [3:---->off]
solo 0 + solo 2 + mute 2 (mute wins)     [0:-S-->on ] [1:---->off] [2:MS-->off] [3:---->off]
solo 1 + 3 is solo-safe                  [0:---->off] [1:-S-->on ] [2:---->off] [3:--F->on ]
solo 1 + 3 solo-safe but muted           [0:---->off] [1:-S-->on ] [2:---->off] [3:M-F->off]
nobody soloed, 3 is solo-safe            [0:---->on ] [1:---->on ] [2:---->on ] [3:--F->on ]
mute/unmute through Gain (50 ms ramp): max first difference 0.0653 (natural value of the sine 0.0654), peak during the mute = -100.0 dBFS
Control case (ramp = 0, instant jump): max first difference 0.5000 (7.6x the natural value) -> proves this check can catch a click
[PASS] l01_ex6 (13 checks passed)
```

Each bracket reads `[track:flags->state]`, where the three flag characters are mute (`M`), solo (`S`) and solo-safe (`F`), with `-` meaning "off".

The last two lines are the point: a mute is not implemented by "setting the samples to 0" but by setting the `Gain` target to `kMinusInfinityDb` and letting the 50 ms ramp walk there, so the first difference is the same as for a pure sine (no click); and −100 dB becomes an exact 0 through `dbToGain`, so the peak during the mute is a true 0 (printed as −100 dBFS).

Why is a "control case" needed? A check that cannot detect an error is no check at all. The mute / unmute are deliberately placed at samples 15372 and 30756 (in the middle of a block, at the positive and negative peaks of the 1 kHz sine respectively): if they were placed on the block boundary 15360 = 320 × 48, that spot is exactly a zero crossing of the sine (the exception in README section 2.4), and even with no ramp at all, jumping instantly to 0 would produce no steep slope, so `maxDiff < naturalDiff * 1.05` would always pass. So the program runs the same scenario again with `setRampSeconds(0)` and demands that the control case's first difference be more than 2 times the natural value (7.6 times measured) -- proving that the check really has discriminating power. When an event falls in the middle of a block, the program splits the block into two sub-blocks, which is how a DAW does sample-accurate automation.

The chapter 14 mixer does the same thing, only split across two places: at the start of each block `Mixer::process()` first computes `anySolo` once, then for each track computes whether it should sound with `!mute && (!anySolo || solo)` (the same expression as `Mixer::isTrackAudible(t)`, which is provided for UI / tests to query) and hands it to `ChannelStrip::beginBlock(audible)`; the `ChannelStrip` then uses its own 10 ms `SmoothedValue` (not the 50 ms of `dsp::Gain`) to ramp the mute gain to 0 or 1. A track there has no soloSafe flag: solo-safe is used only for the FX bus and the master (which only have a mute), so while one track is soloed you can still hear the reverb tail it sends into the reverb bus.

## Exercise 7: M/S stereo width and balance (`ex7_stereo_width.cpp`)

The key idea: compute $`M`$ and $`S`$ sample by sample, multiply $`S`$ by $`w`$, then decode:

```cpp
const float mid  = 0.5f * (l[i] + r[i]);
const float side = 0.5f * (l[i] - r[i]) * w;
l[i] = mid + side;
r[i] = mid - side;
```

Because $`(L' + R')/2 = M`$ is independent of $`w`$, any width processing "disappears" in a mono downmix -- this is the most important property of M/S processing (mono compatibility), and the self-check verifies it for all three values of $`w`$.

The output of a run:

```
width 0/1/2: L peak 0.250 / 0.500 / 0.750, R peak 0.250 / 0.000 / 0.250 (the mono sum is unchanged)
Uncorrelated noise, width 2: mid -19.83 -> -19.83 dB, side -19.83 -> -13.81 dB (+6.02 dB)
balance +0.5: L -6.02 dB, R 0.00 dB; balance +1: L peak = -100.0 dBFS
Wrote dawlab_output/l01_ex7_width.wav (width 0 -> 1 -> 2, 2 seconds each)
[PASS] l01_ex7 (17 checks passed)
```

- With left = sine and right = 0, $`M = S = L/2`$. At $`w = 2`$: $`L' = M + 2S = 1.5L`$ (peak 0.75) and $`R' = M - 2S = -0.5L`$ (an inverted-polarity signal appears on the right, peak 0.25). This answers the first "think about it" question: $`w > 1`$ makes the peak larger (here +3.5 dB), so headroom must be checked again after width processing.
- For true mono ($`L = R`$), $`S = 0`$, and multiplying by any $`w`$ still gives 0, so there is no effect -- width can only amplify a left/right difference that "already exists", it cannot create one from nothing.
- Balance multiplies each channel by its own gain: at +1 the left channel is exactly 0 and not one sample of the right channel has changed, which proves it does not move the left content to the right (consistent with the conclusion of example 2 (C)).

## Exercise 8: The same vocal on a mono track and on a stereo track

1. With $`x = 0.5`$ ($`-6.02`$ dBFS):

   | Case | $`g_L`$ / $`g_R`$ | L | R |
   |---|---|---|---|
   | (a) mono track, center, constant power ($`\theta = 0.5`$) | 0.7071 / 0.7071 | 0.3536 (−9.03 dBFS) | 0.3536 (−9.03 dBFS) |
   | (b) stereo track $`L = R`$, balance center ($`p = 0`$) | 1 / 1 | 0.5 (−6.02 dBFS) | 0.5 (−6.02 dBFS) |
   | (c) mono track, hard right ($`\theta = 1`$) | $`\cos(\pi/2) = 0`$ / $`\sin(\pi/2) = 1`$ | 0 (silent) | 0.5 (−6.02 dBFS) |
   | (d) stereo track, balance +0.5 | $`\min(1, 0.5) = 0.5`$ / $`\min(1, 1.5) = 1`$ | 0.25 (−12.04 dBFS) | 0.5 (−6.02 dBFS) |

2. $`20\log_{10}(0.5 / 0.3536) = 20\log_{10}\sqrt{2} = `$ **+3.01 dB** per side. The mono track applies the −3 dB center gain of the constant power law; the stereo track's balance is at unity at the center. In terms of power, (a) puts $`2 \times 0.3536^2 = 0.125 = 0.5^2`$ into the room, exactly the power of the source, while (b) puts $`2 \times 0.5^2 = 0.25`$, twice as much (+3.01 dB). That is the whole point of the pan law (constant loudness while panning) and the whole point of balance on stereo material (a stereo source at the center must not drop).
3. (c) The left speaker is completely silent and all of the vocal is on the right: a pan moves the source. (d) The left speaker still plays the vocal at 0.25 (6.02 dB below the right): balance only attenuates the left side, so the image sits right of center (a 6 dB level difference) but is not hard right. To get L = 0 on the stereo track you would need balance +1.
4. In (b) the chorus receives two channels; with stereo spread it modulates the left and right delay lines differently (for example with LFO phases 90° or 180° apart), so its L and R outputs differ and are partly decorrelated, and the ear hears width. In (a) the insert sits before the panner and runs on the one channel of the mono track: there is no second side to make different, so whatever spread the effect would produce is lost, and the panner afterwards only sends two scaled copies of the same signal to L and R (correlation 1, a point source). To keep the vocal on a mono track and still get a wide chorus, put the chorus on a stereo FX channel / bus fed by a send (after the panner everything is stereo), or put it on a stereo group the track is routed to; changing the track to stereo also works but costs twice the processing and switches the pan control to balance.
