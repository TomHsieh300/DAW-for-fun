# Chapter 04 Exercises

**English** | [繁體中文](README.zh-TW.md)

Difficulty: ★☆☆ basic / ★★☆ intermediate / ★★★ advanced. The starter code for the implementation exercises is in `starter/` and compiles as-is; when run, the self-check prints FAIL, and once every `// TODO` is complete and everything prints PASS you are done. The solutions are in [`../solutions/README.md`](../solutions/README.md).

Build and run (using exercise 3 as an example):

```powershell
.\scripts\build.ps1 -Only 04-compressor -NoTest
cd build; .\bin\Debug\l04_ex3_starter.exe      # your version
.\bin\Debug\l04_ex3_solution.exe               # the reference solution
```

---

## Exercise 1 (conceptual, ★☆☆): block diagram and design choices

Answer the following questions, 2–4 sentences each:

1. What does each of the compressor's four blocks (detector, gain computer, attack/release smoothing, makeup gain) do? Which block is a "pure function with no state"?
2. Why does `Compressor` put the attack / release smoothing **after** the gain computer (log domain), rather than smoothing the envelope first and then feeding it into the static curve (linear domain)? How does the shape of the release differ?
3. Branching smoothing "underestimates" the peak of a periodic signal such as a sine wave. Why? How does decoupled solve this?
4. Why is stereo link on by default? When would you want to turn it off?
5. Why can a compressor whose attack is labeled 1 ms not catch the peaks of a drum? What mechanism is needed to catch them?

---

## Exercise 2 (calculation, ★★☆): static curves and time constants

Do not write code; calculate by hand (you can check your answers against the output of `l04_transfer_curve`; `l04_ex2_solution` verifies every number of the solution in code):

1. $T = -24$ dB, $R = 3$, hard knee. What are the output dB and gain reduction for inputs of -30, -24, -12 and 0 dB?
2. With the same $T$ and $R$, change to a soft knee with $W = 12$ dB. What range does the knee cover? What are the outputs for inputs of -24 dB (the threshold itself) and -20 dB?
3. Using the formula from 2., verify that at the upper edge of the knee, $x = T + W/2 = -18$ dB, the value of the quadratic equals the value of the hard-knee line.
4. $f_s = 44.1$ kHz, attack 5 ms, release 200 ms. Find $\alpha_A$ and $\alpha_R$ (to 6 decimal places).
5. As above, how many ms does the attack take to reach 90% and 99%? How many ms does the release take to reach 10% (that is, a decay of 90%)?
6. The manual of an analog compressor says "attack time is defined as the time to reach 90% of the final value", and the panel reads 10 ms. If you want to imitate it with this chapter's one-pole (63.2% definition), what should `setAttackMs()` be set to?

---

## Exercise 3 (implementation, ★★☆): Parallel (New York) compression

Starter code: `starter/ex3_parallel.cpp` (target `l04_ex3_starter`)

1. Complete `compressBlocks()`: call `Compressor::process()` block by block (512 samples), and record the largest `getGainReductionDb()` over the whole run.
2. Compute the makeup gain so that the peak of the wet signal equals the peak of the dry signal.
3. Complete `parallelMix()`: $y = (1 - \text{mix})\,\text{dry} + \text{mix}\,\text{wet}$, with mix clamped to 0..1.
4. Observe the crest factor at different mix values.

Hint: use a "very fast attack" (0.02 ms) for the wet signal; see the attack sweep in example 3 of the README for the reason. The self-check requires that mix = 0 is exactly dry, mix = 1 is exactly wet, and the crest factor at mix = 0.5 lies between the two.

---

## Exercise 4 (implementation, ★★☆): Sidechain high-pass filter

Starter code: `starter/ex4_sidechain_hpf.cpp` (target `l04_ex4_starter`)

1. Complete `OnePoleHighpass`: $lp[n] = lp[n-1] + b\,(x[n] - lp[n-1])$, $b = 1 - e^{-2\pi f_c / f_s}$, $hp[n] = x[n] - lp[n]$. The self-check uses `measureGainDb` to verify roughly -3 dB at 400 Hz and below -15 dB at 50 Hz.
2. Complete `compressWithSidechainHpf()`: block by block, copy the main signal into a preallocated sidechain buffer, run it through the HPF, and then call `Compressor::process(block, sidechain)`.
3. Using a signal of 50 Hz @ -6 dBFS + 3 kHz @ -24 dBFS, compare: without the HPF, by how many dB is the 3 kHz dragged down by the bass? And with a 400 Hz HPF?

Hint: `hpf == nullptr` means "no HPF", so the sidechain is a copy of the original signal.

---

## Exercise 5 (implementation, ★☆☆): Auto makeup gain

Starter code: `starter/ex5_auto_makeup.cpp` (target `l04_ex5_starter`)

1. Complete `autoMakeupDb()`: the makeup (dB) needed for a 0 dBFS input to return to 0 dBFS after passing through the static curve.
2. Complete `steadyOutputPeakDb()`: create a `Compressor` (attack 1 ms, release 100 ms, `setMakeupGainDb()`), run a 1-second sine block by block, and return the peak of the last 200 ms.
3. Verify: with $T = -20$, $R = 4$ the makeup = 15 dB; a 0 dBFS sine in → about 0 dBFS out; -6 dBFS in → -1.5 dBFS out.

Think about it: with $T = -6$, $R = 4$, $W = 20$, 0 dBFS falls inside the knee. What is the makeup? (Calculate by hand first, then run it.)

---

## Exercise 6 (implementation, ★★★): Feedback-topology compressor

Starter code: `starter/ex6_feedback.cpp` (target `l04_ex6_starter`)

1. Complete `FeedbackCompressor::processSample()`: the detector looks at the **previous output sample** (not the input), and the gain computer is parameterized by a slope $s$: $\text{GR}_{\text{target}} = \max(0,\; s\,(L_{\text{dB}} - T))$, which is smoothed by `AttackReleaseFilter` and then applied.
2. Derive and fill in the theoretical steady-state GR: $\text{GR} = \frac{s}{1 + s}(X - T)$, with effective ratio $R_{\text{eff}} = 1 + s$.
3. Using tone bursts, compare the steady-state GR for $s = 0.75, 3, 9$, and confirm that the attack t63 for $s = 3$ ($R_{\text{eff}} = 4$) is faster than feedforward 4:1.

Hint: a loop with $s > 1$ is stable only thanks to attack smoothing, so the attack is fixed at 10 ms; an attack of 0 will oscillate.

---

## Exercise 7 (implementation, ★★☆): RMS window length vs pumping

Starter code: `starter/ex7_rms_window.cpp` (target `l04_ex7_starter`)

1. Complete `run()`: call `setDetectorMode(DetectorMode::Rms)` and `setRmsWindowMs(w)`, compress the drum loop block by block, skip the first 0.5 seconds of warm-up, record the maximum and minimum of `getGainReductionDb()` for each block, and compute the "GR swing" = max − min.
2. Sweep the window over 1, 5, 10, 30, 100, 300 and 1000 ms, and observe the swing and the output crest factor.
3. Self-check: the swing decreases monotonically as the window gets longer; the behavior at a 1 ms window is close to the peak detector (the swings differ by < 3 dB); the swing at 1 ms is at least 3 times that at 1000 ms.

Listen to `l04_ex7_rms_1ms.wav` and `l04_ex7_rms_100ms.wav`: with a short window every drum hit makes the whole thing "breathe", while a long window feels like the whole is simply flattened.
