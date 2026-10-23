# Chapter 08 Exercises

**English** | [繁體中文](README.zh-TW.md)

Difficulty: ★☆☆ basic, ★★☆ requires thought, ★★★ challenging. Solutions are in [`../solutions/README.md`](../solutions/README.md).

The implementation problems (Exercises 4-8) all come with starter code in `exercises/starter/exN_*.cpp`: it compiles as is and contains `// TODO` markers, and the self-check is already written, so **running it before you finish prints FAIL (this is expected)**; once you have filled it in, it should print PASS. The complete solutions are in `solutions/exN_*.cpp`.

```powershell
.\scripts\build.ps1 -Only 08-gate-expander          # build + run the tests for the examples and solutions
cd build
.\bin\Debug\l08_ex4_starter.exe                       # run your starter code
```

| No. | Type | Starter target | Solution target |
|---|---|---|---|
| 1 | Calculation | — | — |
| 2 | Calculation | — | — |
| 3 | Conceptual | — | — |
| 4 | Implementation | `l08_ex4_starter` (`starter/ex4_sidechain_gate.cpp`) | `l08_ex4_solution` (`solutions/ex4_sidechain_gate.cpp`) |
| 5 | Implementation | `l08_ex5_starter` (`starter/ex5_upward_expander.cpp`) | `l08_ex5_solution` (`solutions/ex5_upward_expander.cpp`) |
| 6 | Implementation | `l08_ex6_starter` (`starter/ex6_lookahead_gate.cpp`) | `l08_ex6_solution` (`solutions/ex6_lookahead_gate.cpp`) |
| 7 | Implementation | `l08_ex7_starter` (`starter/ex7_splitband_deesser.cpp`) | `l08_ex7_solution` (`solutions/ex7_splitband_deesser.cpp`) |
| 8 | Implementation | `l08_ex8_starter` (`starter/ex8_transient_shaper.cpp`) | `l08_ex8_solution` (`solutions/ex8_transient_shaper.cpp`) |

---

## Exercise 1 (calculation, ★☆☆): The expander's output dB

A downward expander: threshold $`T = -40`$ dB, ratio 1:2 (`setRatio(2)`), range $`-30`$ dB. All "inputs" below are steady-state detector levels (dB).

1. Hard knee: for inputs of $`-30`$, $`-45`$, $`-60`$, and $`-80`$ dB, what is the output in dB? Which one is limited by the range?
2. Now with knee $`W = 10`$ dB: what are the outputs for inputs of $`-35`$, $`-40`$, $`-43`$, and $`-45`$ dB? (Use the three-piece formula from README section 2.2; note that the knee spans $`[T - W/2,\ T + W/2]`$.)
3. A mixer wants bleed at $`-50`$ dB to be pushed down to $`-65`$ dB, with the threshold unchanged and a hard knee. What ratio is needed?
4. A gate has range $`-80`$ dB, attack 2 ms, and $`f_s = 48`$ kHz. The `Gate` in this chapter climbs at a constant rate in the dB domain; how many dB per sample is that? What is the gain in dB 0.5 ms after the gate starts opening?

## Exercise 2 (calculation, ★★☆): Setting the hysteresis thresholds

You put a gate on a snare microphone. Measurements show:

- The envelope peak of a snare hit is about $`-6`$ dBFS, and afterwards it decays at about 60 dB/s;
- The envelope peak of the hi-hat bleed is about $`-30`$ dBFS;
- The detector's envelope has a ripple of about ±1.5 dB (3 dB peak-to-peak) on the tail, coming mainly from the full-wave-rectified peaks of the snare body at 185 Hz (this detector's release is set very short, about 3 ms; check it with the estimation formula in README section 2.4.1).

1. How high can the opening threshold (threshold) be at most, and how low must it be at least? If a safety margin of 4 dB above the hi-hat bleed is required, where do you set the threshold?
2. What is the minimum hysteresis needed to avoid chattering? With 6 dB, what is the closing threshold?
3. Continuing from the above (threshold and hysteresis taken from parts 1 and 2), with hold 50 ms and release 100 ms: starting from the snare's peak, roughly how long until the gate begins to release? How long until it is fully closed?
4. With 6 dB of hysteresis, the closing threshold is below the peak of the hi-hat bleed. What phenomenon does this cause? Is it a bug? If you do not want it, what is the maximum hysteresis you can use?
5. If hysteresis = 0 and hold = 0, as the average envelope of the snare tail passes through the threshold, for roughly how long does it stay in the region where "the ripple crosses the threshold"? How many times at most can the gate open and close during that time?

## Exercise 3 (conceptual, ★☆☆): Causes of chattering and three fixes

1. In your own words, explain what gate chattering is, what it sounds like, and why "envelope ripple around the threshold" causes it. Why is a kick more prone to chattering than a hi-hat?
2. List three fixes with **different mechanisms**, and explain the principle and cost of each (hint: decision rule, time, detector).
3. Why does a downward expander (ratio 1:2) almost never produce audible chattering from the same ripple?
4. Can lookahead solve chattering? Why?

## Exercise 4 (implementation, ★★☆): Sidechain, a kick controlling a pad

Starter code: `exercises/starter/ex4_sidechain_gate.cpp` (target `l08_ex4_starter`). The test material is a sustained pad (chord) and a kick on every beat.

- (a) **Ducking**: complete `Ducker::nextGain()`. Detect the envelope of the sidechain (the kick), and when it is above the threshold (-20 dB) reduce main (the pad) by `duckDb` (12 dB); the gain reduction is smoothed **in the dB domain** with the Chapter 04 `AttackReleaseFilter` (attack 5 ms, release 50 ms).
- (b) **Trance gate**: in `main()`, use `Gate::process(main, sidechain)` so that the pad passes only when the kick is present (threshold -20, hysteresis 6, range -80, attack 1 ms, hold 50 ms, release 50 ms).

Self-check: during ducking (15-60 ms after the kick) the pad is 12 ± 1 dB lower than the original; between kicks (350-490 ms after the kick) it returns to the original level ± 1 dB; the trance gate barely changes anything 20-100 ms after the kick, and at 350-490 ms it is below -60 dBFS.

Output: `dawlab_output/l08_ex4_pad.wav`, `l08_ex4_kick.wav`, `l08_ex4_ducked.wav`, `l08_ex4_trance_gate.wav`.

## Exercise 5 (implementation, ★★☆): Upward expander

Starter code: `exercises/starter/ex5_upward_expander.cpp` (target `l08_ex5_starter`).

Push signals **above** the threshold louder. The static curve ($`R \ge 1`$, knee width $`W`$):

$$
y(x) = \begin{cases}
x, & x \le T - W/2 \\
x + \dfrac{(R - 1)(x - T + W/2)^2}{2W}, & T - W/2 < x < T + W/2 \\
T + R\,(x - T), & x \ge T + W/2
\end{cases}
$$

1. Complete `UpwardExpander::computeOutputDb()`.
2. Complete `nextGain()`: envelope → dB → static curve → boost = $`\min(\text{range},\ y - x)`$ → linear gain (compute `dbToGain` only when the boost changes).

Self-check: the value and slope are continuous at both ends of the knee (numerical differentiation, slopes 1 and $`R`$); for tone bursts {-30, -20, -10, -40} dB with $`T = -30`$, $`R = 1.5`$, and range +12 dB, the output should be {-30, -15, 0, -40} dB (± 0.5 dB).

Further thinking: why must an upward expander have a range (a maximum boost)? The starter code sets the detector to `SmoothingMode::Decoupled`; what happens if you change it to Branching?

## Exercise 6 (implementation, ★★★): Lookahead gate

Starter code: `exercises/starter/ex6_lookahead_gate.cpp` (target `l08_ex6_starter`).

Complete `LookaheadGate::process()`: the detector looks at the "present" sample (`gate_.nextGain(det)`), the main signal is written into a ring buffer of length `maxDelay_`, and the value from `delaySamples_` ago is read out and multiplied by the gain. `setLookaheadMs()` and `getLatencySamples()` are already written.

Test signal: 0.5 seconds of silence followed by a sudden 997 Hz, -6 dBFS burst. The gate's attack is set to 10 ms (deliberately long). Self-check:

- Without lookahead, the RMS of the first 2 ms of the burst is more than 10 dB below the input (the head is cut by the attack);
- With 10 ms of lookahead, after aligning the output for the delay, the RMS of the first 2 ms of the burst differs from the input by < 0.5 dB, and a sample-by-sample comparison of `output[burstStart + L + k]` with `input[burstStart + k]` ($`0 \le k <`$ 2 ms) has an error < 0.01;
- For the first $`L`$ samples after the burst arrives at the input (the transient is still in the delay line), the output RMS is < -60 dB;
- In the steady-state region, `output[i + L] == input[i]` (the delay amount is correct).

Output: `dawlab_output/l08_ex6_input.wav`, `l08_ex6_no_lookahead.wav`, `l08_ex6_lookahead.wav`.

Further thinking: when the lookahead is set equal to the attack, why does the gate finish opening exactly when the transient arrives? Why must a plugin report `getLatencySamples()`? Why does the test signal use 997 Hz instead of 1 kHz (hint: how many samples is the period of 1 kHz? How does it relate to $`L = 480`$?)

## Exercise 7 (implementation, ★★★): A split-band de-esser with an LR4 crossover

Starter code: `exercises/starter/ex7_splitband_deesser.cpp` (target `l08_ex7_starter`).

1. Complete `LinkwitzRileyCrossover`: `setCrossoverHz()` uses `BiquadCoefficients::makeLowpass / makeHighpass(sampleRate, fc, 1/√2)` to set two stages each for LP and HP; `processSample()` outputs `low = lp2(lp1(x))` and `high = hp2(hp1(x))`.
2. Complete `SplitBandDeEsser::process()`: split (crossover at 5 kHz) → hand only the high band to the Chapter 04 `Compressor` (threshold -30, 8:1, attack 1 ms, release 40 ms) → low band + compressed high band.

Self-check:

- Between 100 Hz and 15 kHz, $`|LP + HP|`$ is within 0 dB ± 0.1 dB (the sum is flat);
- At the 5 kHz crossover, LP and HP are each -6.02 dB ± 0.2 dB;
- During the sibilance, the level of the 220 Hz vowel changes by < 0.3 dB, and the 7 kHz sibilance is reduced by ≥ 8 dB.

Output: `dawlab_output/l08_ex7_input.wav`, `l08_ex7_output.wav`.

Further thinking: if you use only a single 2nd-order Butterworth stage for LP / HP (instead of two cascaded stages), what is $`|LP + HP|`$ at the crossover frequency? What if you invert the HP?

## Exercise 8 (implementation, ★★☆): Transient shaper and level independence

Starter code: `exercises/starter/ex8_transient_shaper.cpp` (target `l08_ex8_starter`). The `MyTransientShaper` in the starter code already has the two `EnvelopeFollower`s, fast (0.5 / 30 ms) and slow (10 / 150 ms), the smoothing coefficient, and `process()` prepared.

1. Complete `mapToGainDb(d, attackDb, sustainDb)`: the mapping from README section 2.6.2, using attackDb for $`d > 0`$ and sustainDb for $`d \le 0`$, saturating when $`|d|`$ reaches 6 dB.
2. Complete `nextGain()`: the two envelopes → $`d = 20\log_{10}(e_\text{fast}/e_\text{slow})`$ → mapping → 0.5 ms one-pole smoothing (in the dB domain) → linear gain. When the slow envelope is below −80 dBFS, the target gain is 0 dB.

Self-check:

- Mapping: with $`d = +3`$, attack +12 → +6 dB; $`d = +20`$ → +12 dB (saturated); with $`d = -3`$, sustain -12 → -6 dB; $`d = 0`$ → 0 dB.
- Compare sample by sample against `dawlab::dsp::TransientShaper`, with an error < $`10^{-5}`$.
- **Level-independence test**: the same `makeDrumLoop()` is normalized to a peak of −6 dBFS and to −26 dBFS. With attack +6 dB, the peak increase of both must be greater than 3 dB and equal to each other (± 0.3 dB); with sustain −12 dB, the tail RMS of both (120-240 ms of each eighth-note cell) must drop by more than 3 dB, and the drops must be equal (± 0.3 dB).
- Control group: the same two levels are sent into a compressor (threshold −20 dB, 4:1), and the peak changes differ by 3 dB or more: a compressor looks at absolute level, a transient shaper does not.

Output: `dawlab_output/l08_ex8_{loud,quiet}_{dry,attack6,sustain-12}.wav`.

Further thinking: why does $`d`$ depend only on the "ratio", so that multiplying the whole signal by a constant $`k`$ leaves $`d`$ completely unchanged (hint: is `EnvelopeFollower` linear)? Under what circumstances is level independence broken (hint: the −80 dBFS floor)?
