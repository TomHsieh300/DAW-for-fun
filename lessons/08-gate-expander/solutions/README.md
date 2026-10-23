# Chapter 08 Solutions

**English** | [繁體中文](README.zh-TW.md)

For the problems see [`../exercises/README.md`](../exercises/README.md). The complete programs for the implementation problems are in the `exN_*.cpp` files in this folder; each has a self-check, and you can verify with `ctest -L solution` or by running `l08_exN_solution` directly.

---

## Exercise 1: The expander's output dB

### 1. Hard knee ($`T = -40`$, $`R = 2`$, range $`-30`$)

Below the threshold $`g = (R - 1)(x - T)`$, then take $`\max(\text{range}, g)`$:

| Input | Static gain $`(R-1)(x-T)`$ | After applying range | Output |
|---:|---:|---:|---:|
| -30 | 0 (above the threshold) | 0 | **-30** |
| -45 | $`1 \times (-5) = -5`$ | -5 | **-50** |
| -60 | $`1 \times (-20) = -20`$ | -20 | **-80** |
| -80 | $`1 \times (-40) = -40`$ | **-30** (limited by range) | **-110** |

Only -80 dB is limited by the range: without the range the output would be -120 dB.

### 2. Soft knee $`W = 10`$ dB

The knee spans $`[T - W/2,\ T + W/2] = [-45,\ -35]`$, and inside the knee $`y = x + \frac{(1 - R)(x - T - W/2)^2}{2W} = x - \frac{(x + 35)^2}{20}`$:

| Input | Calculation | Output |
|---:|---|---:|
| -35 | upper edge, $`(x + 35)^2 = 0`$ | **-35.00** |
| -40 | $`-40 - 25/20`$ | **-41.25** |
| -43 | $`-43 - 64/20`$ | **-46.20** |
| -45 | lower edge, $`-45 - 100/20 = -50`$; the line $`-40 + 2(-5) = -50`$ gives the same | **-50.00** |

The result at -40 dB can also be obtained with the README shortcut $`y(T) = T + (1 - R)W/8 = -40 - 1.25`$. All gains are within the range.

### 3. Finding the ratio

$`-65 = -40 + R(-50 + 40) \Rightarrow -25 = -10R \Rightarrow R = 2.5`$ (1:2.5). The gain is -15 dB, within the range of -30.

### 4. The gate's attack slope

```math
\Delta_a = \frac{|\text{range}|}{t_a f_s} = \frac{80}{0.002 \times 48000} = \frac{80}{96} \approx 0.833\ \text{dB/sample}
```

0.5 ms = 24 samples → it has climbed $`24 \times 0.833 = 20`$ dB → gain = $`-80 + 20 = -60`$ dB. Note that at 0.5 ms the linear gain is only 0.001: the first half of the attack is still practically silent, which is where "the head gets eaten" comes from (Exercise 6).

---

## Exercise 2: Setting the hysteresis thresholds

### 1. Threshold

- It must be **above** the peak of the hi-hat bleed (-30 dB), otherwise the hi-hat opens the gate.
- It must be **below** the snare's peak (-6 dB), and preferably much lower: the lower the threshold, the earlier the snare attack triggers it and the more intact the head.
- With a 4 dB safety margin: **threshold = -26 dB**.

### 2. Hysteresis

First check the ripple given in the problem: the estimate in README section 2.4.1, $`8.686\,(1 - 2/\pi)/(2f\tau_r) = 8.686 \times 0.363 / (2 \times 185 \times 0.003) \approx 2.8`$ dB, agrees with the 3 dB in the problem (if the detector release were the 20 ms default of this chapter's `Gate`, it would be only about 0.43 dB; part [4] of Example 1 measures 0.41 dB).

The ripple is 3 dB peak-to-peak, so the hysteresis must be **> 3 dB** for the peaks and troughs of the envelope not to cross both thresholds. Take 6 dB (a margin of one times the ripple) → **closing threshold = -26 − 6 = -32 dB**.

### 3. Time until release starts and until fully closed

The envelope falls from -6 dB to -32 dB: $`\frac{26\ \text{dB}}{60\ \text{dB/s}} \approx 0.433`$ s → enters Hold; hold of 50 ms → **release begins at about 0.483 s**; the release time of this chapter's `Gate` is the time "from 0 dB down to range", so another 100 ms → **fully closed at about 0.583 s**.

### 4. A closing threshold below the hi-hat bleed

While the gate is open during the snare tail, as long as the envelope of the hi-hat bleed is still above -32 dB, it will **not** trigger closing (the Open state is left only when the envelope < -32 dB; during Hold, the timer is reset only by ≥ -26 dB, so -30 dB bleed does not reset Hold but does postpone the end of Open). The result is that the hi-hat bleed "hitchhikes": it cannot open the gate by itself, but it can make an already-open gate close later, so a bit of hi-hat remains after the snare tail.

This is not a bug but the nature of hysteresis: **opening is decided by $`T_\text{open}`$, and staying open is decided by $`T_\text{close}`$**. If you do not want it:

- Make $`T_\text{close} > -30`$: hysteresis < $`-26 - (-30) = 4`$ dB. It must also be > 3 dB (the ripple), so only a narrow window of 3-4 dB is left (for example 3.5 dB), with almost no margin.
- A better approach: keep the hysteresis at 3-4 dB and leave the remaining chattering risk to **hold** (time); or add a lowpass / bandpass to the sidechain (listening only to the snare body at 150-250 Hz) so the hi-hat bleed becomes smaller in the detection signal.

### 5. Chattering without hysteresis

The average of the envelope falls at 60 dB/s, and while the average lies within $`[T - 1.5,\ T + 1.5]`$ the ripple can cross the threshold:

```math
\frac{3\ \text{dB}}{60\ \text{dB/s}} = 50\ \text{ms}
```

The ripple comes from the peaks of the 185 Hz full-wave-rectified waveform, 370 per second, so in 50 ms there are about **18** peaks and the gate can open and close up to about 18 times: it sounds like a 370 Hz "buzz" or crackle at the end of the tail. Measured on the drum loop of Example 1: hysteresis 0 + hold 0 gives 70 state transitions, while hysteresis 6 dB + hold 20 ms gives only 43.

---

## Exercise 3: Causes of chattering and three fixes

### 1. Cause

Chattering is the gate opening and closing repeatedly and quickly around the threshold. The detector's envelope is not a smooth curve: the peak detector is pulled up by the attack at each peak and falls along the release between two peaks, forming a ripple. When the **average** level of the signal (for example the tail of a drum) slowly passes through the threshold, the ripple's peaks are above the threshold and its troughs below it, and the gate opens and closes at the ripple's frequency. Each opening and closing is an 80 dB gain change, so it sounds like crackling, popping, or buzzing instead of a smooth decay.

A kick is more prone to chattering than a hi-hat: the ripple size depends on "the time between two peaks / the detector release". A kick is 45-120 Hz, with 4-11 ms between peaks; a hi-hat is several kHz, with under 0.2 ms between peaks. With the same detector release, the kick's envelope ripple is much larger: ripple ≈ $`8.686\,(1 - 2/\pi)/(2f\tau_r)`$ dB is inversely proportional to frequency (README section 2.4.1; part [4] of Example 1 measures 1.56 dB at 45 Hz and only 0.08 dB at 1 kHz with release 20 ms); also, a kick's tail is long and mostly low-frequency, so it spends a long time passing through the threshold.

### 2. Three fixes with different mechanisms

| Fix | Mechanism | Principle | Cost |
|---|---|---|---|
| **Hysteresis** | Decision rule | Opening threshold $`T`$, closing threshold $`T - H`$ (Schmitt trigger). As long as $`H`$ > the peak-to-peak ripple, the ripple cannot cross both lines | The closing threshold is lower, so the gate stays open longer; bleed can "make an already-open gate close later" (Exercise 2, part 4) |
| **Hold** | Time | After falling below the closing threshold, wait hold milliseconds before releasing, and cancel if the signal returns in the meantime. The "brief dips below the threshold" caused by ripple last only a few milliseconds and are absorbed by the hold | After the signal has truly ended, the noise stays on for another hold milliseconds; too long a hold in fast passages makes the gate "unable to close" |
| **Detector smoothing** | Detector | Lengthen the detector release, switch to RMS detection, or lowpass the sidechain first, so the envelope's own ripple becomes smaller | Detection becomes slower: the gate opens late (the head is eaten, which lookahead can compensate) and closes late; RMS reacts less to transients than peak |

(In addition, lengthening the gate's attack / release ramps can make chattering "less harsh", but it does not remove the open/close decision itself and only makes the gain change of each switch smaller, so it treats the symptom rather than the cause.)

### 3. Why an expander rarely chatters

The expander's gain **varies continuously with level**: if the ripple makes the level wobble by 3 dB, then with ratio 1:2 the gain wobbles by only $`(R - 1) \times 3 = 3`$ dB, a small amplitude modulation (and one further smoothed by the detector's attack / release). A gate is **discontinuous**: the gain differs by 80 dB on the two sides of the threshold, so the same 3 dB ripple is amplified into an 80 dB jump. The larger the ratio, the closer the expander is to a gate, and the higher the risk of chattering, which is why some expanders also offer a hysteresis parameter.

### 4. Lookahead cannot solve chattering

Lookahead only delays the main signal by $`L`$ relative to the detector: the envelope the detector sees and the sequence of open/close decisions it makes are exactly the same, only the time of application is shifted later by $`L`$. The number of switches is unchanged, so chattering is still there. What lookahead solves is the problem of the head of a transient being cut by the attack (Exercise 6).

---

## Exercise 4: Sidechain, a kick controlling a pad

Solution: [`ex4_sidechain_gate.cpp`](ex4_sidechain_gate.cpp) (target `l08_ex4_solution`).

**The key of (a), the Ducker**:

```cpp
float nextGain(float sidechainSample) noexcept {
    const float envDb = gainToDb(detector_.processSample(sidechainSample));
    const float targetGr = envDb >= thresholdDb_ ? duckDb_ : 0.0f;   // the "gate-style inf:1" static curve
    const float gr = smoother_.process(targetGr);                      // smoothing in the dB domain (AttackReleaseFilter)
    return dbToGain(-gr);
}
```

- The detector takes the sidechain (the kick), and the gain is multiplied onto main (the pad): this is the whole point of a sidechain, where detection and application are two different signals.
- Smoothing is done in the dB domain (on the gain reduction) rather than on the envelope: the 5 ms attack decides "how fast it ducks when the kick arrives" and the 50 ms release decides "how fast the pad comes back", the same concept as the log-domain smoothing of the Chapter 04 compressor. You could also replace the static curve with a proportional one (for example the curve of a 4:1 compressor), which is proper sidechain compression.

**(b) Trance gate**: simply `gate.process(padBlock, kickBlock)`. The sidechain version of `Gate` allows different channel counts (a mono kick controlling a stereo pad).

**Measured results**:

```
[a] ducking: pad originally -13.94 dBFS; 15-60 ms after the kick -25.85 dBFS (should be 12 dB lower); 350-490 ms after the kick -14.10 dBFS (should be back to the original)
[b] trance gate: 20-100 ms after the kick -14.06 dBFS (should be ~= -14.06); 350-490 ms after the kick -94.01 dBFS (should be < -60)
[PASS] l08_ex4_sidechain_gate (5 checks passed)
```

The ducking amount = 25.85 − 13.94 = 11.9 dB (target 12 ± 1). While the trance gate is closed, -94 dBFS = the pad's -14 dB plus the range of -80 dB.

Listen to `l08_ex4_ducked.wav`: the pad "sucks" down at every kick and comes back (the pumping of EDM); `l08_ex4_trance_gate.wav`: the pad is chopped into short notes following the kick's rhythm.

---

## Exercise 5: Upward expander

Solution: [`ex5_upward_expander.cpp`](ex5_upward_expander.cpp) (target `l08_ex5_solution`).

The **static curve** is a direct translation of the three-piece formula in README section 2.7; the anchor sits at the **lower edge** of the knee ($`x - T + W/2`$), so the lower edge joins $`y = x`$ and the slope at the upper edge is $`1 + 2\frac{R - 1}{2W} W = R`$.

**nextGain**: `boost = min(range, y − x)`, with boost ≥ 0.

**Measured** (excerpt):

```
[1] Static curve T=-30 R=1.5 knee=10: knee lower edge -35 dB -> -35.000; upper edge -25 dB -> -22.500
    Slope at knee lower edge 1.000 (should be ~= 1), at upper edge 1.500 (should be ~= 1.5)

[2] Tone bursts (T = -30, R = 1.5, maximum boost 12 dB)
    seg   input   output(meas.)  theory
       1   -30.0         -30.00   -30.0
       2   -20.0         -15.01   -15.0
       3   -10.0          -0.01     0.0
       4   -40.0         -40.00   -40.0

[3] Comparison: detector switched to Branching (all other settings the same)
    seg   input   Decoupled  Branching
       1   -30.0     -30.00     -30.00
       2   -20.0     -15.01     -15.58
       3   -10.0      -0.01      -0.58
       4   -40.0     -40.00     -40.00
    Branching is 0.58 dB below theory in the -20 dB segment
[PASS] l08_ex5_upward_expander (13 checks passed)
```

The value at the upper edge: $`T + R \cdot W/2 = -30 + 1.5 \times 5 = -22.5`$ ✓. The -10 dB segment: $`-30 + 1.5 \times 20 = 0`$ dB, a boost of 10 dB < the 12 dB cap, so the output is 0 dB (it just touches 0 dBFS, which is why a range is essential: 1 dB more input would exceed 0 dBFS, and the static curve itself has no upper limit).

**Further thinking: Decoupled vs Branching**. If the detector uses Branching mode, the envelope of a steady 1 kHz sine falls along the release between peaks and is slowly pulled back up by the 5 ms attack, so on average it sits about 1.2 dB below the true peak. The output is "the true input $`x`$ + boost", and boost $`= (R - 1)(L - T)`$ is affected only through the detected level $`L`$, so the error of the output is $`(R - 1)\delta = 0.5 \times 1.16 \approx 0.58`$ dB. Part [3] of the solution measures this by switching to Branching with the same settings: -15.58 dB in the -20 dB segment and -0.58 dB in the -10 dB segment, outside the ±0.5 dB of the self-check. Decoupled first does a peak-hold (instant rise) and then smooths with the attack, so the steady-state level equals the peak. This chapter's `Expander` also uses Decoupled (README section 2.3).

---

## Exercise 6: Lookahead gate

Solution: [`ex6_lookahead_gate.cpp`](ex6_lookahead_gate.cpp) (target `l08_ex6_solution`).

**Key code**:

```cpp
const float g = gate_.nextGain(det);                       // the detector sees the "present"
const int readPos = (writePos_ - delaySamples_ + maxDelay_) % maxDelay_;
for (int ch = 0; ch < nch; ++ch) {
    ring[writePos_] = d[i];                                // write first
    d[i] = (delaySamples_ > 0 ? ring[readPos] : d[i]) * g; // then read the value from L samples ago
}
writePos_ = (writePos_ + 1) % maxDelay_;
```

The ring buffer's length is "maximum lookahead + 1": since it writes first and reads afterwards, if the delay $`L`$ equals the buffer length, the read position coincides with the write position and reads the value just written (no delay at all, yet a latency of $`L`$ is reported). This is the most common off-by-one with ring buffers.

**Measured** (excerpt):

```
lookahead 10 ms = 480 samples (getLatencySamples)
RMS of the first 2 ms of the burst: input -9.00 dB; no lookahead -79.16 dB (head eaten by 70.2 dB); lookahead -9.00 dB
First 2 ms of the burst, per sample: max |output[burstStart + L + k] - input[burstStart + k]| = 1.24e-03; output RMS of [burstStart, burstStart + L) = -120.0 dB
Maximum error between output[i + L] and input[i] in the steady-state region: 0.00e+00
[PASS] l08_ex6_lookahead_gate (9 checks passed)
```

(The RMS of a -6 dBFS sine wave is about -9.0 dB.) Without lookahead, the ramp of the 10 ms attack climbs only from -80 dB to -64 dB in the first 2 ms, so the head is almost completely cut off.

**Why the self-check compares sample by sample, and uses 997 Hz**: comparing only "the RMS of the first 2 ms" cannot tell whether there is a delay. A gate with a 10 ms attack finishes opening exactly 480 samples after the burst starts, so a **wrong implementation with no delay line at all**, doing only `d[i] *= g`, also measures an RMS of -9.0 dB after `burstStart + L`. If the test signal were 1 kHz (period 48 samples, which divides $`L = 480`$ exactly), then `input[i + L] == input[i]`, and even a sample-by-sample comparison could not tell the difference. 997 Hz runs through 9.97 periods in 480 samples, off by 0.03 period, so the implementation with no delay has a per-sample error of about 0.094; a delay off by 1 sample (for example the ring-buffer off-by-one) also gives an error of about 0.065. The head error of the correct implementation is 1.24e-3: the detector crosses the threshold only at the 2nd sample of the burst, the attack starts climbing a few samples late, so the gain of the first 2 output samples is still 0.33 dB and 0.17 dB short of fully open (from the 3rd sample on it is 0 dB), and those two samples themselves have a tiny amplitude (0 and 0.065). So the tolerance is set to 0.01 (about -34 dB relative to the peak). Further, the output over $`[\text{burstStart}, \text{burstStart} + L)`$ is checked to still be silent (the transient is still in the delay line); an implementation with no delay would measure -21.9 dB there.

**Further thinking**:

- **Why lookahead = attack is exactly right**: the transient arrives at the detector at $`t_0`$, the detector exceeds the threshold within 0.1 ms and starts the attack, and the gain reaches 0 dB at $`t_0 + t_a`$. The main signal is delayed by $`L = t_a`$, so the transient reaches the multiplier only at $`t_0 + t_a`$, exactly when the gate is fully open. With $`L < t_a`$ the head is still partly cut; with $`L > t_a`$ the gate opens "early", and a short stretch of noise is added before the transient (usually inaudible).
- **Why 997 Hz**: the period of 1 kHz at 48 kHz is 48 samples, and $`L = 480 = 10 \times 48`$, so after a delay of $`L`$ the waveform is identical to the original, and no matter how much it is delayed, you cannot tell the difference. When testing delays, the signal's period must not divide the delay under test (or just use white noise).
- **Why latency must be reported**: the output of this gate is $`L`$ samples later than the input. The DAW uses the latency each plugin reports for plugin delay compensation (PDC): it delays the other tracks by the same amount so that all tracks line up at the mix bus. If it is not reported, this track is 10 ms later than the others, producing comb filtering with the other drum microphones or a dragging rhythm.

---

## Exercise 7: A split-band de-esser with an LR4 crossover

Solution: [`ex7_splitband_deesser.cpp`](ex7_splitband_deesser.cpp) (target `l08_ex7_solution`).

**The crossover**: two identical Butterworth stages each for LP and HP (RBJ cookbook, $`Q = 1/\sqrt2`$). **The de-esser**: for each channel, split sample by sample into `low_` and `high_` (`AudioBuffer`s allocated in `prepare()`), wrap `high_` into an `AudioBlock` and hand it to the Chapter 04 `Compressor::process()`, and finally sum sample by sample. Detection uses the high band itself (the compressor's internal detector), so no separate bandpass is needed.

**Measured** (excerpt):

```
[1] LR4 @ 5 kHz: |LP + HP|, |LP|, |HP| (dB)
    freq(Hz)   LP+HP      LP       HP
         100   -0.000    -0.00  -100.00
        1000    0.000    -0.01   -57.15
        3500    0.000    -1.75   -14.79
        5000    0.000    -6.02    -6.02
        7000   -0.000   -14.73    -1.76
       15000   -0.000   -51.57    -0.02
[2] de-esser: maximum GR 14.21 dB
    220 Hz vowel: vowel-only section input -18.08 / output -18.08; during sibilance input -18.08 / output -18.08 dB
    7 kHz sibilance: input -42.31 / output -50.85 dB (reduced by 8.54 dB)
[PASS] l08_ex7_splitband_deesser (15 checks passed)
```

- $`|LP + HP| = 0.000`$ dB holds across the whole band, confirming the derivation in README section 2.5.3: the LP + HP of an LR4 is an allpass.
- At the crossover, LP and HP are each -6.02 dB ($`20\log_{10}0.5`$), and the two are in phase, so they sum to 0 dB.
- 7 kHz is reduced by only 8.54 dB, while the compressor's maximum GR is 14.21 dB: 7 kHz is not far above the 5 kHz crossover, and the LP band still has a -14.73 dB component at 7 kHz that is not compressed (the built-in `DeEsser` in Example 3 shows the same phenomenon).

**Further thinking**: with only a single 2nd-order Butterworth stage, $`H_{LP} + H_{HP} = \frac{1 + s^2}{s^2 + \sqrt2 s + 1}`$, and at $`s = j`$ (the crossover) the numerator is 0: a **fully cancelling notch** (LP phase -90°, HP +90°, a 180° difference). Inverting the HP: $`H_{LP} - H_{HP} = \frac{1 - s^2}{s^2 + \sqrt2 s + 1}`$, which at $`s = j`$ is $`\frac{2}{j\sqrt2}`$, magnitude $`\sqrt2`$ → a **+3 dB bump**. Neither is flat, which is the value of the LR4 (squared) design.

---

## Exercise 8: Transient shaper and level independence

Solution: [`ex8_transient_shaper.cpp`](ex8_transient_shaper.cpp) (target `l08_ex8_solution`).

**The mapping** is a direct translation of README section 2.6.2:

```cpp
static float mapToGainDb(float d, float attackDb, float sustainDb) noexcept {
    if (d > 0.0f)
        return attackDb * std::min(d / kFullScaleDb, 1.0f);   // attack: d > 0
    return sustainDb * std::min(-d / kFullScaleDb, 1.0f);     // sustain: d <= 0
}
```

**`nextGain()`**: the two followers take the same detection value ($`\max|x|`$ over the channels); only when slow is above −80 dBFS is $`d = \text{gainToDb}(e_\text{fast}) - \text{gainToDb}(e_\text{slow})`$ computed and mapped, otherwise the target is 0 dB; finally $`g_\text{dB} \leftarrow t + \alpha(g_\text{dB} - t)`$ with $`\alpha = e^{-1/(0.5\,\text{ms}\cdot f_s)}`$. It corresponds line by line to `dsp::TransientShaper::nextGain()`, so the sample-by-sample comparison error is 0.

**Measured**:

```
[3] Level independence (the same drums, with peaks at -6 dBFS and -26 dBFS)
                         loud(-6)   quiet(-26)
    attack +6: peak increase    +5.83      +5.83 dB
    sustain -12: tail RMS       -8.66      -8.66 dB
[4] Control: peak change of a compressor (threshold -20, 4:1)
    loud  -5.13 dB, quiet  +0.00 dB -> difference 5.13 dB: the compressor only affects the loud version
[PASS] l08_ex8_transient_shaper (14 checks passed)
```

**Why the results at the two levels are exactly the same**: the peak detection of `EnvelopeFollower` (Branching) is $`e[n] = |x[n]| + a\,(e[n-1] - |x[n]|)`$, and whether $`a`$ is the attack or the release coefficient depends only on the **magnitude relationship** between $`|x[n]|`$ and $`e[n-1]`$; when the whole input is multiplied by $`k > 0`$, the magnitude relationship is unchanged and every step is a linear operation, so $`e_\text{fast}`$ and $`e_\text{slow}`$ both become $`k`$ times as large (apart from floating-point rounding), the ratio $`e_\text{fast}/e_\text{slow}`$ is unchanged, and $`d`$ and the gain are therefore the same sample by sample. The peak increase is +5.83 dB rather than +6 dB because the drum's peak occurs within a very short time after the attack, when $`d`$ has already exceeded 6 dB (saturated) but the gain's 0.5 ms one-pole smoothing has not fully caught up with +6 dB.

**The control group**: the compressor's gain computer compares the level with a **fixed** threshold (−20 dB). The −6 dBFS drum exceeds the threshold by 14 dB and is compressed by 5 dB; the −26 dBFS drum stays below the threshold from beginning to end and is not compressed at all. This is what README section 2.6 means by "a compressor looks at absolute loudness, a transient shaper only at how fast the level changes".

**Further thinking**: level independence holds only when $`e_\text{slow} > -80`$ dBFS. If you push the drums below −70 dBFS, the slow envelope of the tail falls below the floor first and processing stops (target 0 dB), so the effect of sustain −12 is weaker than when loud; the floor is the price paid for keeping random fluctuations of the noise floor from being amplified as transients.
