# Chapter 12 Solutions: Reverb

**English** | [繁體中文](README.zh-TW.md)

For the problems, see [`../exercises/README.md`](../exercises/README.md). Complete programs for the implementation problems are in this folder. Each has a self-check, and its last output line should be `[PASS]`:

```powershell
.\scripts\build.ps1 -Only 12-reverb -TestLabel solution
```

<div align="center">

| # | Solution file | target |
|---|---|---|
| 3 | `ex3_early_reflections.cpp` | `l12_ex3_solution` |
| 4 | `ex4_fdn_modulation.cpp` | `l12_ex4_solution` |
| 5 | `ex5_gated_reverb.cpp` | `l12_ex5_solution` |
| 6 | `ex6_rt60_estimator.cpp` | `l12_ex6_solution` |

</div>

---

## Exercise 1: Comb feedback and RT60

**1.** D = 1557, fs = 48000:

```math
g = 10^{-3\cdot1557/(2.0\cdot48000)} = 10^{-0.048656} = 0.89401
```

For RT60 = 0.5 s: $`g = 10^{-3\cdot1557/24000} = 10^{-0.194625} = 0.63881`$.

Both values agree with the g printed by Example 1 (`l12_comb_rt60`). Shortening RT60 to 1/4 multiplies the exponent by 4: $`0.63881 = 0.89401^4`$.

**2.** 44.1 kHz, g = 0.84, $`\log_{10}0.84 = -0.075721`$:

```math
\mathrm{RT60}_{1116} = \frac{-3\cdot1116}{44100\cdot(-0.075721)} = 1.003\ \text{s},\qquad
\mathrm{RT60}_{1617} = \frac{-3\cdot1617}{44100\cdot(-0.075721)} = 1.453\ \text{s}
```

For the same g, RT60 is proportional to D: the longest comb decays 45% longer than the shortest. So Freeverb's roomsize is merely a "feedback amount", not a strict RT60: the early part of the overall decay is determined by all the combs together, while the later part is dominated by the longest (slowest-decaying) ones, so the decay curve bends slightly "downward" (fast at first, slow later). To give every comb the same RT60 you need a separate $`g_i = 10^{-3D_i/(\mathrm{RT60}f_s)}`$ for each, which is exactly what the FDN (`Reverb.h`) does.

**3.** $`\omega = 2\pi f/f_s`$, $`|\mathrm{LP}| = 0.5/\sqrt{1.25 - \cos\omega}`$, $`\mathrm{RT60} = -3\cdot1557 / (48000\log_{10}(0.89401\,|\mathrm{LP}|))`$:

<div align="center">

| f | ω | $`\lvert\mathrm{LP}\rvert`$ | $`g\lvert\mathrm{LP}\rvert`$ | RT60 by hand | Measured in the example |
|---|---|---|---|---|---|
| 986.5 Hz | 0.1291 | 0.9838 | 0.8795 | 1.745 s | 1.745 s |
| 4007.7 Hz | 0.5246 | 0.8063 | 0.7209 | 0.685 s | 0.685 s |
| 8015.4 Hz | 1.0492 | 0.5767 | 0.5156 | 0.338 s | 0.339 s |

</div>

(The example uses the comb harmonic $`k\cdot f_s/D`$ closest to 1 k / 4 k / 8 kHz, to avoid landing on a notch.) At 8 kHz the d = 0.5 one-pole is down to 0.58×, and this factor is applied on every trip around the loop, so the RT60 at high frequencies drops to 1/6 of that at low frequencies.

---

## Exercise 2: Why delay lengths should be mutually prime

**1.** For the 1500-sample comb, the modes are at integer multiples of $`48000/1500 = 32`$ Hz; for the 2000-sample comb, at integer multiples of 24 Hz. The frequencies common to both are the integer multiples of lcm(32, 24) = 96 Hz, which is $`f_s/\gcd(1500, 2000) = 48000/500`$. At 96, 192, 288 Hz, ... both combs resonate at the same time.

In the time domain, the echoes of the first comb arrive at multiples of 1500 and those of the second at multiples of 2000, so they coincide at multiples of lcm(1500, 2000) = 6000 samples (125 ms): every 125 ms there is an echo of double size, which gives a regular flutter.

**2.** 1427 and 1783 are coprime: the only common frequencies are multiples of $`f_s/\gcd = 48000`$ Hz, that is DC (and its periodic copies at fs), so no modes overlap within the audio band. The echoes coincide with a period of 1427 × 1783 = 2,544,341 samples ≈ 53 seconds, far longer than any RT60, so effectively it never happens.

**3.** Overlapping modes mean that two (or more) paths resonate at the same time: the peak at that frequency is higher than at other modes, and they decay together, so a "pitched tone" emerges in the long tail. The ear is very sensitive to such narrow-band, slowly decaying resonances, which sound like striking a metal pipe (metallic ringing). Other ways to reduce it:
- Increase the number of delay lines or their total length → higher mode density, so individual modes stand out less;
- Use a feedback matrix that mixes more thoroughly (FDN) and input diffusion;
- Delay modulation (Exercise 4), which keeps moving the mode frequencies;
- Damping: ringing at high frequencies decays faster;
- Do not choose lengths in simple integer ratios, and do not bunch them together (for example, this chapter spreads them over 30–73 ms).

**4.** Sufficient but not necessary. It is enough that every pair has gcd = 1; they need not all be prime. For example, 1024 (= 2¹⁰), 1125 (= 3²·5³), 1331 (= 11³), 1729 (= 7·13·19): no two share a prime factor, so they are pairwise coprime. Many classic designs choose lengths as powers of distinct primes. Using `nextPrime()` is just the most convenient way to guarantee this.

---

## Exercise 3: Early reflections (`ex3_early_reflections.cpp`)

**Key ideas**

1. **Image source**: mirroring about the wall x = 0 gives $`x \to -x`$, and about x = Lx gives $`x \to 2L_x - x`$; the other axes are analogous. The first-order reflections are 6 images.
2. **Relative to the direct sound**: a tap's delay is "extra path length / c", and its gain is $`\sqrt{1-\alpha}`$ (energy absorption α → amplitude reflection coefficient) times the distance ratio (1/r attenuation). The benefit of working relative to the direct sound is that the ER module does not need to know the absolute delay of the direct sound.
3. **Stereo**: compute a separate set of taps for each ear (16 cm apart). The time differences between the two sets are the interaural time differences, which give the ER a sense of direction.
4. **ER + late**: set the pre-delay of the late reverb to the latest ER across both ears (`er.getMaxDelaySamples()`: the x = 0 wall for the right ear, 538 samples ≈ 11.2 ms; for a single listener position it is 536), so that the tail starts right after the ER.

**Floor reflection by hand** (listener (4.3, 2.6, 1.2), source (2, 1.5, 1.6)):

- Direct: $`\sqrt{2.3^2 + 1.1^2 + 0.4^2} = \sqrt{6.66} = 2.5807`$ m
- Floor image (2, 1.5, −1.6): $`\sqrt{2.3^2 + 1.1^2 + 2.8^2} = \sqrt{14.34} = 3.7868`$ m
- delay = (3.7868 − 2.5807) / 343 = 3.516 ms → × 48 = 168.8 → **169 samples**
- gain = $`\sqrt{0.7}\times 2.5807/3.7868 = 0.8367\times0.6815 =`$ **0.570**

All 6 (sorted by delay; consistent with the program output):

<div align="center">

| Wall | image | Distance (m) | delay (ms) | delay (samples) | gain |
|---|---|---|---|---|---|
| Floor z = 0 | (2, 1.5, −1.6) | 3.7868 | 3.516 | 169 | 0.5702 |
| Ceiling z = 3 | (2, 1.5, 4.4) | 4.0915 | 4.405 | 211 | 0.5277 |
| Wall y = 4 | (2, 6.5, 1.6) | 4.5453 | 5.728 | 275 | 0.4750 |
| Wall y = 0 | (2, −1.5, 1.6) | 4.7181 | 6.231 | 299 | 0.4576 |
| Wall x = 6 | (10, 1.5, 1.6) | 5.8189 | 9.441 | 453 | 0.3711 |
| Wall x = 0 | (−2, 1.5, 1.6) | 6.4078 | 11.158 | 536 | 0.3370 |

</div>

(The ms values printed by the program are converted back from the rounded sample counts, so they differ slightly; for example, 169 samples = 3.521 ms.)

**Actual output**

```
=== Exercise 3: early reflections (image source -> tapped delay) ===
Direct-sound distance 2.5807 m; the 6 first-order reflections (relative to the direct sound):
delay(sp)  delay(ms)     gain
     169      3.521   0.5702
     211      4.396   0.5277
     275      5.729   0.4750
     299      6.229   0.4576
     453      9.438   0.3711
     536     11.167   0.3370
The left-ear impulse response has 6 non-zero samples (= the number of taps); positions and gains all match
RMS: dry 0.1034, dry+ER 0.1538, dry+ER+late 0.1615 (peak 0.991)
[PASS] l12_ex3 (28 checks passed)
```

**Verification**: the impulse response of the TappedDelay has exactly 6 non-zero samples, with positions and gains all matching (error < 10⁻⁶); sample 0 is 0 (the direct sound is not included). After processing the pluck sequence the RMS is: dry 0.1034 → dry + ER 0.1538 → dry + ER + late 0.1615, with a peak of 0.991 (< 1).

**Listening**: `l12_ex3_er_only.wav` has just 6 very early reflections added, so the sound is "in a room, at some distance from you", but has no tail; `l12_ex3_er_late.wav` has the complete sense of space. Extensions: adding second-order reflections (mirroring the images again, 18 in total) is more realistic; adding a distance-dependent lowpass to each tap (air absorption) also helps.

---

## Exercise 4: FDN modulation and metallic ringing (`ex4_fdn_modulation.cpp`)

**Key ideas**

1. **The −1 for read-before-write**: the convention of `FractionalDelayLine` is that, after a push, `read(0)` is the value just written. In `processSample()` we read first and push last, so reading `L − 1` gives the value written L samples ago; the loop length is then exactly L, and only then is the $`g_i`$ formula exact.
2. **Modulation**: the LFO phases of the lines are 90° apart (`i·π/2`), which avoids all 4 lines "stretching together" and wobbling the overall pitch; using `readCubic` loses less high-frequency content at non-integer delays than linear interpolation.
3. **Damping must be truly switchable off**: even with cutoff = 20 kHz, the one-pole is still −1.3 dB at Nyquist, applied on every trip around the loop, which noticeably shortens the high-frequency RT60. So `setDampingHz(≥ 20000)` bypasses it directly. Without the bypass, this 4-line FDN measures an RT60 of only about 1.66 s instead of 2 s.
4. **Roughness metric**: a spectrum with distinct modes looks like a row of spikes (large peak-to-valley differences) and has a large dB standard deviation; once the modes are smoothed out the spectrum looks like noise, and the dB standard deviation approaches the theoretical dB value for the magnitude of white noise (Rayleigh distribution), about 5.6 dB.

**Actual output**

```
=== Exercise 4: FDN modulation and metallic ringing ===
Delay-line lengths (samples): 1123, 1567, 1949, 2251 (all prime)
lossless: RMS @1.0s = 0.0390, @2.5s = 0.0391 (difference +0.01 dB, should be close to 0)
RT60 set to 2.0 s -> measured 2.021 s
Std. dev. of the tail spectrum in dB (from 0.4 s, 16384-point FFT, 100 Hz..6 kHz): static 8.85 dB, modulated 5.89 dB
Measured RT60 of the modulated version: 1.863 s
depth 4 ms: spectrum std. dev. 5.60 dB (flatter, but the maximum pitch deviation is 3.3% = 56 cents -> audible pitch wobble)
[PASS] l12_ex4 (7 checks passed)
```

**Discussion questions**

- The mode density of the 4-line FDN = (1123 + 1567 + 1949 + 2251) / 48000 = 0.14 modes/Hz (on average one mode every 7 Hz); the 8-line `Reverb` has 0.41 modes/Hz (one every 2.5 Hz). The sparser the modes, the easier it is to hear each one individually. By the Schroeder and Logan rule of thumb "mode density ≥ 0.15 × RT60" (see README Section 2.6), RT60 = 2 s needs at least 0.3 modes/Hz, so the 4-line version is clearly insufficient, and modulation compensates for exactly this.
- The lossless test shows that the Householder matrix is orthogonal: with no $`g_i`$ and no damping, the system energy stays exactly constant for 1.5 seconds. If the matrix were written wrongly (for example 1/N instead of 2/N), this test would immediately show the RMS steadily rising or falling.
- Modulating the delay causes a Doppler-like pitch change: the instantaneous frequency ratio is $`f'/f = 1 - dL/dt`$ (L in seconds), and for sinusoidal modulation $`L(t) = L_0 + D\sin(2\pi f_{\mathrm{LFO}}t)`$ the maximum deviation is $`2\pi f_{\mathrm{LFO}}D`$. Depth 1 ms, 1.3 Hz → 0.82% (about 14 cents, nearly inaudible); depth 4 ms → 3.3% (about 56 cents, more than a quarter tone), and the tail sounds "wobbly", like a chorus set too deep. Modulation is a trade-off between "smoothing out the ringing" and "not hearing the pitch wobble"; common values are 0.3–1 ms and 0.3–2 Hz.
- Modulation makes the measured RT60 slightly shorter (1.86 s). One possible reason is that cubic interpolation has a little high-frequency loss at non-integer positions (at integer positions it is completely lossless), accumulating on every trip around the loop; in addition, an RT60 measurement from a single IR itself fluctuates by a few percent. This is also why some reverb designs modulate only a few of the lines, or use allpass interpolation instead.

---

## Exercise 5: Gated reverb (`ex5_gated_reverb.cpp`)

**Key ideas**

- The reverb itself runs as usual (RT60 3 s, mix 1, pure wet); the gate goes **after** it, with the **sidechain taken from the dry snare**. Every snare hit opens the gate instantly (attack 0.5 ms), holds it for 100 ms, and then closes it to −80 dB within 30 ms.
- Why not use the reverb's own output as the detection signal: the reverb tail decays slowly, and the gate would only close once the tail falls below the threshold. The closing time then depends on an interplay of reverb level, RT60, and threshold, which is hard to control, and you would not get the "abruptly chopped off" effect. With dry detection, the opening and closing times of the gate depend only on the drum hit, and hold directly determines the length of the tail.
- Remember to call `reverb.reset()` after setting the parameters: this makes mix = 1 take effect from the very first sample; otherwise the dry signal leaks through in the first 0.1 seconds.

**Actual output**

```
=== Exercise 5: gated reverb ===
window                  ungated(dB)    gated(dB)
open   0.02..0.10 s           -21.9        -21.9
closed 0.40..0.58 s           -31.2       -100.0
Final gate state: closed, gain reduction 80.0 dB
Output: dawlab_output/l12_ex5_dry.wav, l12_ex5_reverb.wav, l12_ex5_gated.wav
[PASS] l12_ex5 (6 checks passed)
```

While the gate is open it has no effect at all (0.0 dB difference); after it closes, the tail without the gate has dropped only about 9 dB (RT60 3 s → 20 dB per second), whereas the gated version is already at −100 dB (below the measurement floor). At the next hit at 0.6 s the gate opens again (the self-check verifies that the two differ by < 1.5 dB over 0.62–0.70 s).

**Listening**: the tail of `l12_ex5_reverb.wav` drags on and joins every hit into one wash; in `l12_ex5_gated.wav` every hit sounds "big and wide" but stops dead after 100 ms, the 80s drum sound of Phil Collins' "In the Air Tonight".

---

## Exercise 6: Schroeder backward integration (`ex6_rt60_estimator.cpp`)

**Key ideas**

```cpp
double acc = 0.0;
for (int i = n - 1; i >= 0; --i) {   // accumulate from the tail backwards: EDC[i] = EDC[i+1] + h[i]²
    acc += double(h[i]) * h[i];
    edc[i] = acc;
}
// then convert to dB: 10·log10(edc[i] / edc[0]) (energy uses 10·log10)
```

Regression: take only the points where EDC_dB is within [−25, −5] dB; the least-squares slope = (nΣty − ΣtΣy) / (nΣt² − (Σt)²), and RT60 = −60 / slope.

**Why the EDC is a straight line**: if $`h(t) = A e^{-kt}\cdot w(t)`$ (w is unit-power noise or a sinusoid), the mean of $`h^2`$ is $`A^2 e^{-2kt}`$, so

```math
\mathrm{EDC}(t) = \int_t^\infty A^2 e^{-2k\tau}d\tau = \frac{A^2}{2k}e^{-2kt}
\;\Longrightarrow\;
\mathrm{EDC}_{\mathrm{dB}}(t) = 10\log_{10}e^{-2kt} = -\frac{20k}{\ln 10}t
```

The definition of RT60, $`e^{-k\cdot\mathrm{RT60}} = 10^{-3}`$, gives $`k = 3\ln10/\mathrm{RT60}`$; substituting yields $`\mathrm{EDC}_{\mathrm{dB}}(t) = -60\,t/\mathrm{RT60}`$: the slope is exactly −60/RT60, the same as the dB slope of the amplitude envelope, but **much smoother**. Schroeder proved that this integral equals the result of "averaging infinitely many interrupted-noise measurements in the same room", so a single IR gives you the ensemble average.

**Actual output**

```
=== Exercise 6: estimating RT60 with Schroeder backward integration ===
EDC of the exponentially decaying sine: t=0.25 s -> -15.00 dB (theory -15), t=0.5 s -> -30.00 dB (theory -30)

IR                             truth(s)       T20(s)       T30(s) windowed-RMS(s)
decaying sine (RT60 1.0)          1.000        1.000        1.000          1.000
synthetic noise IR                0.500        0.502        0.501          0.501
synthetic noise IR                1.500        1.505        1.497          1.499
FDN Reverb (RT60 2.0)             2.000        1.991        1.998          2.004

EDC of the FDN IR (every 0.25 s):
  t = 0.00 s :    0.00 dB
  t = 0.25 s :   -6.24 dB
  t = 0.50 s :  -13.73 dB
  t = 1.00 s :  -28.75 dB
  t = 2.00 s :  -58.77 dB
  ... (excerpt)
[PASS] l12_ex6 (11 checks passed)
```

T20 and T30 are both Schroeder backward-integration results; they differ only in the regression range (−5..−25 dB versus −5..−35 dB). From 0.25 s on, the EDC of the FDN drops about 7.5 dB every 0.25 s (= 60 dB / 2 s × 0.25 s), a nice straight line; the first 0–0.25 s drops only 6.2 dB, because the echoes are still building up and the energy has not yet started its "pure decay".

**Effect of background noise**: the tail of a real recorded IR ends in a constant-level noise floor that no longer decays. Backward integration also integrates the energy of this noise, so the end of the EDC "curls up" and flattens, and if the regression range extends into it the RT60 is overestimated. The practical remedy (the ISO 3382 series and the method of Lundeby et al.): first estimate the noise floor, then truncate the upper limit of the integral at the time when the IR reaches the noise floor (and compensate for the truncated energy), or subtract the noise power from $`h^2`$ before the regression. This is also why T20 / T30 are commonly used instead of measuring all the way to −60 dB.

---

## Exercise 7: Latency and cost of partitioned convolution

fs = 48 kHz, M = 3.0 × 48000 = 144000 samples.

**1.** P = 512: $`K = \lceil 144000/512\rceil = \lceil 281.25\rceil = 282`$. N = 1024, so each spectrum has N/2 + 1 = 513 bins.
IR spectrum: 282 × 513 × 8 = 1,157,328 bytes ≈ **1.10 MiB** (the time-domain IR would need only 576,000 bytes: storing it as spectra takes about twice as much, because of the zero-padding to 2P). The FDL of one channel is also 282 spectra, again 1.10 MiB, for a total of about 2.21 MiB.

**2.** For every P = 512 output samples we do K × 513 complex multiply-accumulates: 282 × 513 / 512 ≈ **282.6 complex MACs per sample** (≈ 1130 real multiplications). In addition, every 512 samples we do two 1024-point FFTs; radix-2 needs about (N/2)·log₂N = 5120 butterflies, which amortizes to about 20 butterflies per sample, relatively small.
Direct convolution needs 144000 multiply-accumulates per sample → 144000 / 1130 ≈ **127×** (counted in real multiplications; this is the P/4 ≈ 128 of the README).

**3.** P = 64: K = 144000 / 64 = 2250, N = 128, 65 bins. Per sample, 2250 × 65 / 64 ≈ **2285 complex MACs**, 8 times that of P = 512; the latency drops from 512 samples (10.67 ms) to **64 samples (1.33 ms)**. Memory is almost unchanged (2250 × 65 × 8 = 1,170,000 bytes ≈ 1.12 MiB). Conclusion: for uniform partitioning, latency and CPU are inversely related, and you cannot have both.

**4.** Where the latency comes from: each overlap-save frame needs **a full P new input samples** before the FFT can be done. If the input arrives sample by sample, the first sample of a block must wait until the P-th sample has arrived before it can be processed, so its output can be produced no earlier than that; to make the output a continuous, evenly spaced stream, the whole thing must be delayed by P samples.

If the host delivers exactly P = 512 samples each time, aligned with the partition boundaries: at the start of the callback all 512 input samples are **already in hand**, so you can immediately FFT, multiply-accumulate, and IFFT, and write the outputs of these 512 samples back within the same callback. The extra latency is 0 (the time of "waiting to fill up" is already included in the buffer latency the host has anyway).

This chapter's `ConvolutionReverb` does not do this, because the host's block length is not guaranteed to be fixed (JUCE and VST3 both allow the callback to deliver fewer samples than maxBlockSize, even a different number each time, and DAWs often split blocks into smaller pieces at automation points or loop boundaries). A FIFO with a fixed delay of P gives correct results for any block length, and the latency is a constant, which makes it easy for the host to compensate for the delay. The cost is P extra samples of latency.

**5.** Hybrid / non-uniform partitioning:
- Use **time-domain direct convolution** (an FIR) for the very beginning of the IR (for example the first 64 or 128 samples): zero latency, and cheap in CPU because it is short;
- Use small partitions (for example P = 128) for the next part; its output is only needed after a delay of at least 128 anyway, so its latency is "hidden" in the time offset of the IR;
- Use ever larger partitions (1024, 8192, ...) further back: a large FFT costs less per sample, and there is enough time (the work can be spread over several callbacks or a background thread) to compute it.

This is the spirit of Gardner's (1995) zero-latency method. The total cost approaches the efficiency of the large partitions, with a latency of 0.
