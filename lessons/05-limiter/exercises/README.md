# Chapter 05 Exercises: Limiter

**English** | [繁體中文](README.zh-TW.md)

Difficulty: ★☆☆ basic　★★☆ intermediate　★★★ advanced

The implementation exercises (3, 4, 5, 6) all come with starter code `starter/exN_xxx.cpp` that compiles as it is. When you run it, the self-check prints FAIL — that is expected. Fill in the `// TODO` parts until every check turns PASS and you are done. The solutions are in [`../solutions/README.md`](../solutions/README.md) and `../solutions/exN_xxx.cpp`.

Build and run (from the project root):

```powershell
.\scripts\build.ps1 -Only 05-limiter -NoTest
cd build
.\bin\Debug\l05_ex3_starter.exe      # or l05_ex4_starter, l05_ex5_starter, l05_ex6_starter
```

---

## Exercise 1 ★☆☆ (conceptual) How a limiter differs from a compressor and a clipper

1. Using the two parameters "ratio" and "attack", explain which extreme case of a compressor a limiter is. Why can a compressor with ratio 100:1 and attack 1 ms still not be called a brickwall limiter? (Hint: the table from the example `l05_compressor_vs_limiter` — what is the output in the first millisecond after the jump?)
2. What does the "brickwall" guarantee (the output sample peak is always ≤ ceiling) require mathematically? Why can a "zero-delay" limiter not achieve it (unless it turns into a clipper)?
3. A clipper and a limiter can both guarantee that the output does not exceed the limit. What is the "price" each one pays? When would you choose a clipper?

## Exercise 2 ★☆☆ (calculation) Lookahead, latency and gain reduction

1. How many samples are lookaheads of 1.5 ms, 5 ms and 10 ms at 44.1 kHz, 48 kHz and 96 kHz? (Use `round(ms × fs / 1000)`; note how to round 44.1 kHz × 5 ms = 220.5.)
2. A master chain has two limiters in series, each with a 5 ms lookahead (48 kHz). What does the host read from each plugin's `getLatencySamples()`? What is the total delay of the whole chain in ms?
3. The smoothing in this chapter uses a "moving average of length L + 1". With L = 240, how many values are averaged? For a suddenly falling gain, how many samples and how many milliseconds does the linear ramp produced by this moving average last?
4. The drum loop in the example `l05_drum_limiting` has an original peak of -2.92 dBFS. After pushing the input gain +12 dB, how many dB of maximum gain reduction are needed to limit it to a ceiling of -1 dBFS? (Compare with the `max GR` column of the example output.)
5. With the ceiling at -1 dBTP, what is the linear value? For a sine of amplitude A at exactly fs/4 whose sampling phase is offset by 45°, how many dB lower than the true peak is its sample peak?

## Exercise 3 ★★☆ (implementation) An O(1) amortized sliding window minimum

Starter code: `starter/ex3_sliding_window_min.cpp`

Step 4 of the Limiter takes the minimum within the lookahead window (L + 1 values). Brute force scans the window for every sample and costs O(L) — a 10 ms lookahead @ 96 kHz is 961 comparisons per sample. Use a **monotonic deque** to complete `MonotonicWindowMin::push()` so that it:

- returns exactly the same values as the brute-force `BruteForceWindowMin` on every input (including inputs with lots of repeated values — think carefully about `>=` versus `>`);
- lets each element enter and leave the deque at most once → N samples cost O(N) in total;
- implements the deque as a pre-allocated circular array, with no allocation inside `push()`.

The starter code compares the two approaches over 9 window lengths and prints the timings and the speedup ratio.

Hint: the deque keeps only elements that still have a chance to become the minimum — index increasing and value strictly increasing from oldest to newest. When a new value v arrives, every element at the back that is ≥ v can be dropped (why?).

## Exercise 4 ★★★ (implementation) A simple 4x oversampling true-peak meter

Starter code: `starter/ex4_true_peak_meter.cpp`

ITU-R BS.1770 measures true peak by upsampling the signal by 4 and then measuring the sample peak. The full theory of windowed sinc and polyphase is not taught until Chapter 06 section 2.6, so before you start, read "How do we upsample by 4? (the intuitive version)" in README section 2.8: zero insertion → image → lowpass interpolation → polyphase computing only the non-zero terms. After reading you should be able to answer: why multiply by 4? Why is the lowpass $`\mathrm{sinc}(n/4)`$? Why does phase p use `h[4j + p]`? Complete `TruePeakMeter`:

1. `designFilter()`: design a 64-tap windowed-sinc lowpass (cutoff = the original fs/2, Hann window, sum normalized to 4), and do the polyphase decomposition: the j-th tap of phase p is `h[4j + p]`.
2. `process()`: for each input sample compute only 4 dot products `y[4n + p] = Σ_j h[4j + p] · x[n − j]`, and update `truePeak_`.

The TODO comments in the starter code break down "why we do it this way" step by step. I suggest doing only step 1 first: self-check (0) runs no signal and just checks the coefficients (sum = 4, each phase sums to about 1, left-right symmetry); once it PASSes, do step 2.

Self-checks (1)–(4) verify that: the true peak of a 1 kHz sine ≈ its sample peak; for an fs/4 sine at a phase of 45° the sample peak underestimates by 3.01 dB, while the true peak reads around -6.19 dBTP (about 0.17 dB lower than the real -6.02 — why? Hint: where do the interpolated points of an even-length filter fall); the true peak of a hard-clipped drum loop is higher than its sample peak (by about +4.5 dB); and for the output of this chapter's Limiter the sample peak is ≤ -1 dBFS, but the true peak is higher than the sample peak and also clearly above -1 dBTP (about 2 dB higher).

Think: what do the measured gaps tell you about how to set the ceiling of a sample-peak limiter if you want to guarantee -1 dBTP?

(Chapter 11 Exercise 3 switches to building a true-peak meter with Chapter 06's `Oversampler`, and uses this exercise's version as the control, comparing the reading error and CPU cost of the two.)

## Exercise 5 ★★☆ (implementation) A soft clipper, compared with the limiter

Starter code: `starter/ex5_soft_clipper.cpp`

Complete `softClip(x, t)`:

```math
y = \begin{cases} x & |x| \le t \\ \mathrm{sign}(x)\,\bigl[\,t + (1-t)\tanh\!\frac{|x|-t}{1-t}\,\bigr] & |x| > t \end{cases}
```

Requirements: the linear region is left completely untouched, continuous with continuous slope (C¹) at $`t`$, $`|y| \le 1`$, odd symmetry, monotonically increasing. The starter code compares the THD and harmonics (H2–H5) of the clipper and the limiter using a 1 kHz +6 dBFS sine, then compares peak / RMS using the drum loop at +12 dB, and writes WAV files for you to listen to.

Think: why are the clipper's H2 and H4 almost -100 dB? Why is the limiter's THD close to 0?

## Exercise 6 ★★★ (implementation) Fuzz test: verifying the ceiling guarantee

Starter code: `starter/ex6_fuzz_test.cpp`

README section 2.5 proved that the algorithm does not exceed the ceiling, but the code may still have bugs such as off-by-one errors, circular-buffer wrap-around or block boundaries. Write a fuzz test with:

- random signals (noise / sine / bursts / impulses / DC / square wave / a mix, with amplitude up to +60 dB);
- random parameters (sample rate, channel count, `setMaxLookaheadMs`, `setLookaheadMs` (deliberately out of range), ceiling, release, input gain);
- random block lengths (1..1024) fed to `process()`;
- checks: output peak ≤ ceiling (exact comparison on the linear value), no NaN / Inf, `getSafetyClipCount() == 0`, and `getLatencySamples()` agreeing with the theoretical value;
- at least one case must actually "reach" the ceiling (minimum margin < 0.01 dB), otherwise the test is not testing anything.

Advanced: randomly change release / input gain between blocks (the guarantee should still hold); lower the ceiling during playback (what happens? why is `getSafetyClipCount()` > 0?); put NaN / Inf into the input; simulate a plugin host that "re-sets the same input gain every block" and check that the 20 ms ramp still finishes on time.

## Exercise 7 ★★☆ (conceptual) How a DAW compensates for latency

1. A session has two tracks: the vocal track has a limiter with 5 ms lookahead inserted, and the guitar track has nothing inserted. If the DAW does nothing, what happens when the two tracks are mixed together? Specifically, which track does the DAW's plugin delay compensation (PDC) act on, what does it do, and how much delay does it add?
2. Continuing from that, what if the limiter is inserted on the master bus? Does PDC still need to delay anything? How should the "playhead position" during playback be corrected so that the cursor on screen lines up with the sound being heard?
3. Why is it generally not advisable to put a plugin with lookahead in the path of "live software monitoring" while recording?
4. This chapter's Limiter changes `getLatencySamples()` when `setLookaheadMs()` is called. When, and in what way, should the plugin tell the host that the delay has changed? (Hint: JUCE's `AudioProcessor::setLatencySamples()`; think about what the host has to do after being notified and why changing it during playback is discouraged.)
5. Two plugins report latencies of 240 and 64 samples and are chained on the same track; another track has no plugins. Where does PDC insert how many samples of delay?
