# Chapter 12 Exercises: Reverb

**English** | [繁體中文](README.zh-TW.md)

There are 7 problems: 2 conceptual, 1 calculation (problem 7 also involves calculation), and 4 implementation problems. The starter code for the implementation problems is in `exercises/starter/` and compiles as is. When you run it, the self-check prints `[FAIL]` (this is expected); once you finish the `// TODO` items, everything should print `[PASS]`. Complete solutions are in [`../solutions/`](../solutions/README.md).

Build and run (PowerShell, from the project root):

```powershell
.\scripts\build.ps1 -Only 12-reverb -NoTest
cd build
.\bin\Debug\l12_ex3_starter.exe        # your version
.\bin\Debug\l12_ex3_solution.exe       # reference solution
```

| # | Type | Difficulty | Topic | File / target |
|---|---|---|---|---|
| 1 | Calculation | ★☆☆ | Comb feedback and RT60 | (pencil and paper) |
| 2 | Conceptual | ★☆☆ | Why delay lengths should be mutually prime | (pencil and paper) |
| 3 | Implementation | ★★☆ | Early reflections: image-source + tapped delay | `starter/ex3_early_reflections.cpp` → `l12_ex3_starter` |
| 4 | Implementation | ★★★ | Add modulation to an FDN and quantify metallic ringing | `starter/ex4_fdn_modulation.cpp` → `l12_ex4_starter` |
| 5 | Implementation | ★☆☆ | Gated reverb (Chapter 08 Gate) | `starter/ex5_gated_reverb.cpp` → `l12_ex5_starter` |
| 6 | Implementation | ★★☆ | Estimating RT60 with Schroeder backward integration | `starter/ex6_rt60_estimator.cpp` → `l12_ex6_starter` |
| 7 | Conceptual + calculation | ★★☆ | Latency, CPU, and memory of partitioned convolution | (pencil and paper) |

---

## Exercise 1 (Calculation ★☆☆): Comb feedback and RT60

fs = 48 kHz.

1. A feedback comb has D = 1557 samples. What is g if you want RT60 = 2.0 s? What if RT60 is 0.5 s?
2. At 44.1 kHz, the shortest Freeverb comb is 1116 samples and the longest is 1617 samples. With the roomsize setting giving g = 0.84, what is the RT60 of each of these two combs? What does this say about the problem with Freeverb's relationship between roomsize and RT60?
3. In the damping experiment of Example 1, d = 0.5, g = 0.894, D = 1557. Compute the RT60 at 1 kHz and 8 kHz by hand (using the formula in README Section 2.3) and compare with the example output.

Hints: $`g = 10^{-3D/(\mathrm{RT60}\,f_s)}`$, $`\mathrm{RT60} = -3D / (f_s\log_{10} g)`$, $`|\mathrm{LP}(e^{j\omega})| = (1-d)/\sqrt{1 - 2d\cos\omega + d^2}`$.

---

## Exercise 2 (Conceptual ★☆☆): Why delay lengths should be mutually prime

1. Take two parallel combs of length 1500 and 2000 samples (fs = 48 kHz). At which frequencies does each one have its modes? At which frequencies do both combs resonate at the same time? In the time domain, how often do their echoes arrive simultaneously?
2. What about 1427 and 1783 (both prime)?
3. Why are "modes that resonate simultaneously" a perceptual problem? Besides choosing mutually prime lengths, what other methods can reduce metallic ringing?
4. `Reverb.h` uses `nextPrime()` to make every line a prime length. Is "all lengths are prime" a sufficient or a necessary condition for "pairwise coprime"? Give a set of lengths that are not prime but are pairwise coprime.

---

## Exercise 3 (Implementation ★★☆): Early reflections

File: `exercises/starter/ex3_early_reflections.cpp` (target `l12_ex3_starter`).

Room 6 × 4 × 3 m (walls at 0 and Lx/Ly/Lz), wall absorption coefficient α = 0.3, source at (2.0, 1.5, 1.6), listener at (4.3, 2.6, 1.2), c = 343 m/s, fs = 48 kHz.

1. Complete `firstOrderReflections()`: compute one image source for each of the 6 walls (for example, wall x = 0 → (−sx, sy, sz), wall x = Lx → (2Lx − sx, sy, sz)) and convert it to a `TappedDelay::Tap`:
   - delay (samples) = round((|image − listener| − |source − listener|) / c · fs)
   - gain = √(1 − α) · |source − listener| / |image − listener|
   - sort by delay, smallest first.
2. Complete `EarlyReflections::process()`: for each channel, call `TappedDelay::processSample()` sample by sample (output only the reflections, without the direct sound). Each ear gets its own set of taps (the ears are at the listener position y ± 0.08 m).
3. Chain the ER with `Reverb` (late reverb, pre-delay = the time of the latest tap) to process `makePluckSequence()`, and write `dawlab_output/l12_ex3_er_only.wav` (dry + ER) and `l12_ex3_er_late.wav` (dry + ER + late).

Self-check: the earliest reflection (the floor) is at 169 samples with gain 0.570; the latest (the x = 0 wall) is at 536 samples with gain 0.337; the impulse response of the TappedDelay has exactly 6 non-zero samples. Work out the floor reflection by hand first to verify your formulas.

Listen: with only the ER added, the sound becomes "closer" and gains a sense of room, but has no tail; only after adding the late reverb does it sound like a complete reverb.

---

## Exercise 4 (Implementation ★★★): Add modulation to an FDN and compare metallic ringing

File: `exercises/starter/ex4_fdn_modulation.cpp` (target `l12_ex4_starter`).

The starter code already contains the skeleton of a 4-line FDN, `Fdn4` (delays of roughly 23–47 ms, rounded to primes; Householder matrix; one-pole damping; $`g_i = 10^{-3L_i/(\mathrm{RT60}f_s)}`$).

1. Complete `Fdn4::processSample()`:
   - Advance the LFO phase; read the 4 lines $`v_i`$ = `lines_.readCubic(i, L_i − 1 + depth·sin(φ + i·π/2))` (read first, then write, hence the −1);
   - Householder: $`u_i = v_i - \frac{2}{N}\sum_j v_j`$; apply damping where needed;
   - Write back $`g_i u_i + x`$; output $`v_0 - v_1 + v_2 - v_3`$.
2. Complete `spectralRoughnessDb()`: take a 16384-point, Hann-windowed FFT of the tail (starting at 0.4 s), compute the magnitude (in dB) of each bin from 100 Hz to 6 kHz, and return the **standard deviation**. The more distinct the modes (the more metallic the sound), the "spikier" the spectrum and the larger the standard deviation.
3. Compare modulation off vs. depth 1 ms / 1.3 Hz, and depth 4 ms.

Self-check: in lossless mode (g = 1, no damping), the RMS at 1.0 s and at 2.5 s differs by < 1 dB (verifying that the Householder matrix is lossless); the measured RT60 ≈ 2 s; with modulation on, the standard deviation drops by at least 1 dB, and RT60 does not change noticeably.

Think about it: what is the mode density of a 4-line FDN, in modes per Hz? Why is ringing easier to hear than with the 8-line version in `Reverb.h`? What side effect does a depth of 4 ms have?

---

## Exercise 5 (Implementation ★☆☆): Gated reverb

File: `exercises/starter/ex5_gated_reverb.cpp` (target `l12_ex5_starter`).

The classic 1980s drum sound: a very long reverb is chopped off abruptly by a gate, which sounds both big and tight.

```
 snare ──┬──────────────────────────────────────────► dry
         └─► Reverb (RT60 3 s, mix 1) ──► Gate (sidechain = dry snare) ──► wet
```

1. Configure the Chapter 08 `Gate`: threshold −30 dB, hysteresis 6 dB, range −80 dB, attack 0.5 ms, hold 100 ms, release 30 ms.
2. Call `gate.process(main, sidechain)` block by block: main = the reverb's wet signal, sidechain = the dry snare.
3. Think about it: why use the dry snare as the detection signal instead of the reverb's own output?

Self-check: while the gate is open (0.02–0.10 s), gated ≈ ungated (difference < 1 dB); after the gate closes (0.40–0.58 s), gated is more than 40 dB below ungated; the gate must open again for the next hit (0.6 s). The solution writes `l12_ex5_dry.wav`, `l12_ex5_reverb.wav`, and `l12_ex5_gated.wav`; the starter writes only `l12_ex5_gated.wav` (dry + gated wet).

---

## Exercise 6 (Implementation ★★☆): An RT60 estimation tool for IRs (Schroeder backward integration)

File: `exercises/starter/ex6_rt60_estimator.cpp` (target `l12_ex6_starter`).

`measureRt60()` runs a regression on a windowed-RMS envelope, but the envelope of a single impulse response fluctuates a lot. Schroeder's (1965) method integrates the squared IR **backwards, from the tail**:

$$
\mathrm{EDC}[n] = \sum_{k=n}^{N-1} h[k]^2,\qquad
\mathrm{EDC}_{\mathrm{dB}}[n] = 10\log_{10}\frac{\mathrm{EDC}[n]}{\mathrm{EDC}[0]}
$$

1. Complete `schroederDecayDb()`: a single O(N) loop (accumulating from the tail backwards).
2. Complete `estimateRt60()`: over the region where EDC_dB lies between −5 and −25 dB, fit a least-squares line to (t, dB); RT60 = −60 / slope (T20).
3. Verify with three kinds of IR: an ideal exponentially decaying sine (RT60 1 s), `makeSyntheticImpulseResponse()` (0.5 s and 1.5 s), and (added in the solution) the IR of `Reverb`.

Self-check: the EDC of the exponentially decaying sine is −30 dB at t = 0.5 s (±0.5); the RT60 estimation error is within 3%; the synthetic IR error is within 10%.

Think about it: why is EDC_dB a straight line with slope −60/RT60 for an exponentially decaying signal? How does the EDC get distorted when the tail of a real recorded IR contains background noise?

---

## Exercise 7 (Conceptual + calculation ★★☆): Latency and cost of partitioned convolution

fs = 48 kHz, the IR is 3.0 s long (mono), and you use this chapter's `ConvolutionReverb` (uniform partitions, N = 2P, overlap-save).

1. With P = 512, how many partitions K are there? How much memory does the IR spectrum occupy (each bin is a `complex<float>` = 8 bytes)? What about adding the FDL for one channel?
2. On average, how many complex multiply-accumulates are needed per output sample? Roughly how many times cheaper is this than direct convolution?
3. What if P is changed to 64? What do the latency and the number of complex multiply-accumulates per sample become?
4. Where does the "latency of uniform partitioned convolution = P" come from? If the host delivers exactly 512 samples per callback, aligned with the partitions, can the extra latency be made 0? Why does this chapter's implementation not do that?
5. If you want zero latency without paying the CPU cost of P = 64, what can you do?
