# Chapter 05 Exercise Solutions: Limiter

**English** | [繁體中文](README.zh-TW.md)

Run the solutions (from the project root):

```powershell
.\scripts\build.ps1 -Only 05-limiter -TestLabel solution     # run the self-checks of all solutions
cd build
.\bin\Debug\l05_ex4_solution.exe                              # or run a single exercise on its own
```

---

## Exercise 1 (conceptual) How a limiter differs from a compressor and a clipper

**1. A limiter is a compressor with ratio → ∞ and attack → 0.**
ratio → ∞ means that no matter how large the input above the threshold, the output stays at the threshold (the static curve becomes a horizontal line); attack → 0 means the gain must come down "immediately". Even with the ratio set to 100:1, the Chapter 04 compressor has an attack that is a one-pole smoother: the gain reduction starts at 0 and approaches the target along the curve $`1 - e^{-t/\tau}`$. At the very instant the signal gets louder the gain reduction is still 0, and the entire peak passes through untouched. Measured by `l05_compressor_vs_limiter`: input -30 → -6 dBFS, threshold -20 dB, ratio 100:1, attack 1 ms — the output peak in the first millisecond after the jump is **-7.99 dBFS** (12 dB over the threshold), -15.54 in the second millisecond, -18.27 in the third, and it only gets down to around -19.8 dB after 5 ms. That is overshoot. And what if attack is set to 0? The detector and the gain of this feedforward compressor act on the same sample, so there is no more overshoot, and the output is only $`(x-T)/R`$ above the threshold (here $`14/100 = 0.14`$ dB; it equals the threshold exactly only as ratio → ∞), but the gain jumps within one sample (a click), and with a slightly shorter release it follows the waveform up and down every cycle, which turns it into a clipper and produces harmonic distortion. "Smooth gain" and "never exceeds" must hold at the same time, so the gain must start falling before the peak arrives — and that requires seeing the future, which is lookahead.

**2. Brickwall needs "knowing the future in advance".**
For the sample $`x[n]`$ multiplied by the gain to be ≤ ceiling, the gain must be computed and already down to $`\le \text{ceiling}/|x[n]|`$ *before* $`x[n]`$ reaches the output. A system with no delay sees only $`x[0..n]`$ at time $`n`$, so a smooth gain (one that does not jump at every sample) is necessarily too late; the only zero-delay solution is "each sample is multiplied by the gain it needs on its own" $`y[n] = x[n]\cdot\min(1, c/|x[n]|)`$ — which is exactly a hard clipper. So brickwall + smooth gain ⇒ the audio must be delayed by L samples (lookahead), giving the gain L samples of time to come down.

**3. The price.**
Clipper: zero delay, stateless and the simplest to implement, but it changes the "waveform" → harmonic distortion (Exercise 5 measures a THD of about 21 %); it sounds ugly on sustained signals (bass, pads). Limiter: it preserves the waveform and changes only the gain, so a steady-state sine goes in and a sine comes out (THD ≈ 0), but it costs latency, more CPU, and pumping / dulled transients caused by the release. When to choose a clipper: when you need zero delay (live monitoring, hardware DSP), when you only need to shave off extremely short transient peaks (inter-sample peaks of drums — many mastering engineers put a soft clipper in front of the limiter to shave 1–2 dB), or when you want that "broken-up" tone in the first place (Chapters 06 and 07).

## Exercise 2 (calculation) Lookahead, latency and gain reduction

**1.** `round(ms × fs / 1000)`:

| lookahead | 44.1 kHz | 48 kHz | 96 kHz |
|---|---|---|---|
| 1.5 ms | 66.15 → **66** | **72** | **144** |
| 5 ms | 220.5 → **221** | **240** | **480** |
| 10 ms | **441** | **480** | **960** |

`dawlab::msToSamples` uses `std::lround`, which rounds halves away from zero, so 220.5 → 221. The `getLatencySamples` values printed in the table of `l05_latency_impulse`, and the positions where the impulse actually appears, are these numbers.

**2.** Each limiter reports 240 samples; the latencies of a series connection add up: 480 samples = **10 ms**. The host delays every other path that does not go through this chain by 480 samples (see Exercise 7).

**3.** The average has L + 1 = **241** values. A step passed through a box filter of length 241 becomes a linear ramp 241 samples long = 241 / 48000 = **5.02 ms**. In the table of part (3) of `l05_latency_impulse` you can see the gain start to fall 240 samples before the step (-0.12 dB) and reach exactly -6.00 dB at the step.

**4.** After pushing +12 dB the peak = -2.92 + 12 = +9.08 dBFS; limiting it to -1 dBFS requires 9.08 − (−1) = **10.08 dB** of gain reduction. The `max GR` column of the example output is exactly 10.08 dB (because the lookahead gain is in place "exactly" when the peak arrives, no more and no less).

**5.** The linear value of -1 dBTP = $`10^{-1/20} = 0.891`$. An fs/4 sine has only 4 samples per cycle; with a 45° phase offset the samples land at $`\pm A\sin 45° = \pm 0.707A`$, so the sample peak is $20\log_{10}(1/\sqrt2) = $ **3.01 dB** below the true peak. The gap measured by the Exercise 4 meter is 2.85 dB rather than 3.01 dB, not because the filter attenuates at fs/4 (the gain of every phase at fs/4 is within 0.00 to +0.01 dB), but because the interpolated points of an even-length (64-tap) filter fall on 1/8-sample grid points and cannot hit the peak exactly in the middle; see the explanation in Exercise 4.

## Exercise 3 (implementation) Sliding window minimum

Solution: [`ex3_sliding_window_min.cpp`](ex3_sliding_window_min.cpp)

The key idea — the deque keeps only the (value, index) pairs that still have a chance to become the minimum:

1. When a new value $`v`$ arrives, drop every element at the back whose value is **≥** $`v`$. The reason: they expire earlier than $`v`$ and are no smaller, so at no moment while $`v`$ is still in the window can they beat $`v`$. Using `>=` rather than `>` is because an equal older value is equally useless (dropping it also keeps the deque shorter; both are correct).
2. Drop elements at the front with index ≤ n − W (they have left the window). Because the indices in the deque increase, only the very front needs to be checked.
3. Put $`(v, n)`$ at the back. At this point the deque holds at most W − 1 elements (all with index inside (n − W, n)), so a circular array of length W always has room.
4. The front is the answer.

Each element is pushed at most once and popped at most once → total work O(N). The solution shows 0 mismatches on 9 window lengths, on random data with many repeated values, and on a staircase signal; the deque version is dozens of times faster and the gap widens with the window length (measured in a Debug build: about 20–30x for a window of 241 and about 70–100x for 961; the exact ratios and absolute times depend on the machine and load, and the gap is larger in Release). `dawlab::dsp::SlidingWindowMin` (in `Limiter.h`) is this same algorithm.

## Exercise 4 (implementation) A 4x oversampling true-peak meter

Solution: [`ex4_true_peak_meter.cpp`](ex4_true_peak_meter.cpp)

**Filter design.** Upsampling by 4 = zero insertion + lowpass. The prototype has its cutoff at $`f_s/2`$ at the new sample rate $`4f_s`$, i.e. 1/8 of the new sample rate, and is a windowed sinc:

$$h[k] = \mathrm{sinc}\!\Bigl(\frac{k-M}{4}\Bigr)\, w[k],\qquad M = \frac{N-1}{2},\quad N = 64,\quad w = \text{Hann}$$

Zero insertion drops the average level to 1/4, so we normalize to $`\sum h = 4`$ (the DC gain of each phase ≈ 1).

**Polyphase.** The upsampled $`y[4n+p] = \sum_k h[k]\,u[4n+p-k]`$, and the zero-inserted $`u`$ is non-zero only at multiples of 4, which leaves $`y[4n+p] = \sum_j h[4j+p]\,x[n-j]`$: four dot products of length 16 per input sample, with no need to build a 4x-long buffer and no allocation at all. (Why zero insertion produces images, why we multiply by 4, and why $`h[n] = \mathrm{sinc}(n/4)`$ are covered in README section 2.8, "How do we upsample by 4?")

**Verify the filter first, then run signals.** Self-check (0) runs no signal and only checks the coefficients: $`\sum h = 4.0000`$; the four phase sums are 0.9999, 1.0001, 1.0001, 0.9999 (the DC gain of each phase ≈ 1: for a constant input all 4 interpolated points must equal that constant; it is not exactly 1 because after keeping only 64 taps and applying a window the filter is no longer an ideal lowpass, and the images at $`f_s`$ and $`2f_s`$, which are what DC maps to, are not cancelled perfectly); and $`h[k] = h[63-k]`$ is perfectly symmetric (linear phase). When TODO 1 is wrong (for example you forgot the normalization, or split the phases as `coeffs_[k / 16][k % 16]`), this part FAILs first, so you do not have to wait until the true-peak numbers are off before looking back.

**Measured results (`l05_ex4_solution`):**

| Signal | sample peak | true peak | Difference |
|---|---|---|---|
| 1 kHz sine, amp 0.5 | -6.02 dBFS | -6.02 dBTP | 0.00 |
| 12 kHz (fs/4) sine, phase 45° (after warm-up) | -9.03 dBFS | -6.18 dBTP | +2.85 dB |
| drums +12 dB, hard clipped to 0 dBFS | 0.00 dBFS | +4.54 dBTP | +4.54 dB |
| drums +12 dB → Limiter (ceiling -1) | -1.00 dBFS | +1.02 dBTP | +2.02 dB |

**Why is the fs/4 row -6.18 rather than -6.02?** The prototype length $`N = 64`$ is even, so the center $`M = 31.5`$ is not on an integer. Converted back to the original sample rate, the delay of phase $`p`$ is $`(M - p)/4 = 7.875 - p/4`$ samples, with fractional parts 0.875, 0.625, 0.375, 0.125 — that is, the 4 interpolated points fall at 1/8, 3/8, 5/8 and 7/8 between two original samples, not at 0, 1/4, 1/2, 3/4. The peak of this signal is exactly midway between two samples (1/2), and the nearest interpolated point is 1/8 sample (= 11.25°) away from it, so the reading is $`0.5\cos 11.25° = 0.4904`$, i.e. -6.19 dBTP (the filter has a +0.01 dB ripple near fs/4, hence the printed -6.18). This is an inherent error of a 4x oversampling meter: it underestimates by at most about 0.17 dB. The 48-tap filter of BS.1770-4 is likewise a symmetric design of even length and has the same property; to make one phase line up exactly with the original samples you can use an odd-length prototype instead (for example 63 taps, padded with one 0 at the end).

When measuring this row, `measure()` first feeds `kTapsPerPhase` samples to warm up the filter and only then starts recording the peak (`resetPeaks()`). Without skipping them, the instant at which the signal "starts suddenly from 0" makes the interpolation filter produce an onset transient (Gibbs ringing), and the reading would be -6.07 dBTP — the peak of the opening transient, not the steady-state true peak of the sine.

The flat tops of the hard clip, and the high frequencies held down by the limiter (the hi-hat is noise close to Nyquist), clearly exceed the sample peak once reconstructed. To guarantee -1 dBTP, the ceiling of this simple sample-peak limiter has to go about 2 dB lower (the last line of the solution prints a suggested value of about -3.02 dBFS), or switch to true-peak detection (upsample inside the limiter's sidechain before computing the gain). The official filter in BS.1770-4 has 48 taps (4 phases × 12), so its numbers differ slightly from this simple version.

## Exercise 5 (implementation) Soft clipper vs limiter

Solution: [`ex5_soft_clipper.cpp`](ex5_soft_clipper.cpp)

```cpp
float softClip(float x, float threshold) noexcept {
    const float t = std::clamp(threshold, 1.0e-3f, 0.999f);
    const float a = std::abs(x);
    if (a <= t) return x;
    const float over = (a - t) / (1.0f - t);
    return std::copysign(t + (1.0f - t) * std::tanh(over), x);
}
```

$`\tanh'(0) = 1`$, so the curve is not only continuous at $`t`$ but also has continuous slope (the solution verifies with numerical derivatives that both sides have slope ≈ 1); $`\tanh < 1`$ so $`|y| < t + (1-t) = 1`$ (in float a very large input saturates to exactly 1.0).

**Measured:** for a 1 kHz +6 dBFS sine with $`t = 0.5`$, the clipper output has a peak of -0.02 dBFS and a THD of **20.9 %**, with harmonics H3 at -13.8 dB and H5 at -27.6 dB, while H2 and H4 are both -100 dB (unmeasurable) — because the curve is odd-symmetric, $`f(-x) = -f(x)`$, and an odd function produces only odd harmonics. The limiter (ceiling -0.5 dB) outputs a THD of **0.0000 %**: in steady state the window minimum is the constant $`c/A`$ and the moving average is also constant, so the whole sine is multiplied by one and the same number and the waveform does not change at all.

Drum loop at +12 dB: the clipper (t = 0.6) gives a peak of -0.00 dBFS and an RMS of -7.75 dBFS; the limiter (ceiling -0.1) gives a peak of -0.10 dBFS and an RMS of -9.75 dBFS (dry is -17.43). The clipper's RMS is higher (it flattens the transients completely instead of pulling the whole level down), at the cost of audible gritty harmonics on the kick / snare; the limiter is much cleaner, but its transients are dulled and it pumps slightly.

Listening files (in `dawlab_output/`): `l05_ex5_sine_clipped.wav` / `l05_ex5_sine_limited.wav` (the 1 kHz sine: the clipped one has clearly added harsh high overtones, the limited one just gets quieter), `l05_ex5_drums_clipped.wav` / `l05_ex5_drums_limited.wav`. Exercise 4 additionally writes `l05_ex4_intersample.wav` (an fs/4 sine at 45° phase, which you can use with a DAW's true-peak meter to compare sample peak and true peak).

## Exercise 6 (implementation) Fuzz test

Solution: [`ex6_fuzz_test.cpp`](ex6_fuzz_test.cpp)

Structure: a reproducible `Random` (wrapping `NoiseGenerator` with a fixed seed, so that a failing case can be re-run), `makeRandomSignal()` producing 7 kinds of signal, and `processRandomBlocks()` feeding the limiter blocks of random length 1..maxBlock. Each case checks three things: peak ≤ ceiling (linear value, exact comparison), no NaN / Inf, and `getSafetyClipCount() == 0`. The last one matters: the Limiter ends with a safety clamp that pulls the output back to the ceiling even if the algorithm is wrong, so checking only the peak would miss bugs; the clip count is the evidence that "the algorithm itself stayed under the ceiling".

Measured (`l05_ex6_solution`):

- (1) 400 random cases: 0 failures; the minimum margin (ceiling − output peak) = -0.0000 dB, which means some case did reach the ceiling (and did not exceed it beyond floating-point rounding). `getLatencySamples()` equals `clamp(round(lookaheadMs·fs/1000), 0, round(maxLookaheadMs·fs/1000))` in every case.
- (2) Randomly changing release and input gain between blocks: 50 cases, 0 failures. The proof in section 2.5 uses only "$`gr[n] \le g[n]`$", which holds however release varies; the input gain is multiplied in *before* the delay line and the peak is computed from the multiplied value, so it does not matter either.
- (3) Lowering the ceiling from -1 to -12 dB during playback: the second-half peak is -12.00 dBFS, but the safety clamp fired 471 times. The reason: the delay line still holds L samples whose gain was computed with the "old ceiling"; they exceed the new ceiling when they come out, and only the clamp can catch them (with clipping distortion). That is why the ceiling should be smoothed or changed while stopped — or the setter should only let the ceiling fall as a ramp.
- (4) Injecting NaN / ±Inf: the output peak is -6.02 dBFS with no NaN / Inf. `std::max(peak, NaN)` ignores the NaN; Inf makes $`g = 0`$; a NaN produced by the multiplication is caught by `!(|y| <= ceiling)` on the clamp line and replaced with 0.
- (5) Simulating a plugin host that calls `setInputGainDb(6)` once every block: the output after 25 ms = 0.199526 (= 0.1 × $`10^{6/20}`$), which means the 20 ms ramp really did finish. If the setter recomputed the step from the "remaining distance" every time, the ramp would become an ever-slowing exponential curve and this item would fail — a bug that this chapter really did catch and fix during verification.

## Exercise 7 (conceptual) How a DAW compensates for latency

**1.** The vocal track's sound comes out 240 samples (5 ms) later than the guitar track's — the vocal sounds slightly "behind the beat", and if the two tracks have correlated content (for example two microphones on the same source) there is also comb filtering. PDC finds the largest latency over all paths (240), then inserts 240 − 0 = 240 samples of delay on **the track with the smaller latency (guitar, 0)**, so that all tracks arrive at the mix bus equally late. The total delay becomes 240 samples, but the tracks are aligned with each other.

**2.** The master bus is where all tracks pass, so no track needs to be delayed (everyone is 240 samples late together). But the "playhead" has to be corrected: when the engine's transport is computing position $`t`$, what the master actually outputs is the content of $`t - 240`$ samples; after this block is handed to the audio device it still has to go through the device's own output latency (driver and DA converter buffers) before it comes out of the speakers. So the cursor on screen should be drawn at `transport position − PDC latency − device output latency` (subtracted only during playback; when stopped there is no sound, so the transport position is shown directly), and the alignment of metering and automation must account for this offset too. Chapter 14 implements this in two parts: the engine does the PDC and reports the mix latency through `Engine::getLatencySamples()` ([Step 8](../../14-daw-8track/docs/step-08-pdc.md)); the app's TransportBar counter and ArrangementView playhead display `AudioHost::getAudiblePosition()`, i.e. $`\max(0,\ \text{position} - \text{engine latency} - \text{device output latency})`$ ([Step 13](../../14-daw-8track/docs/step-13-transport-bar.md), [Step 14](../../14-daw-8track/docs/step-14-arrangement.md)).

**3.** The latency of live monitoring is "the delay with which the singer hears their own voice", and lookahead adds straight onto it. 5 ms alone is not large, but together with the audio interface buffer (for example 128 samples × 2 ≈ 5.3 ms), the driver, and the AD/DA it easily exceeds 10 ms, and the singer feels they are "hearing an echo", with pitch and rhythm affected. The monitoring path should use zero-latency processing (or hardware direct monitoring); a limiter with lookahead is added later, at the mixing stage.

**4.** `setLookaheadMs()` changes `getLatencySamples()`, so the plugin must call the host's notification mechanism (JUCE: `AudioProcessor::setLatencySamples(n)`, upon which the host receives a latency-changed event). The host then has to recompute the PDC of the whole session and reallocate its delay buffers — which interrupts playback and may even cause a brief silence — so most plugins design lookahead to "take effect only when playback is stopped", or report a fixed maximum lookahead in `prepareToPlay` (always delaying by the max, so that a shorter lookahead merely brings the gain down earlier). This chapter's `Limiter` does the former: `setLookaheadMs()` resets the window state, and the README of this chapter also recommends changing it while stopped.

**5.** Chained on the same track: 240 + 64 = 304 samples. The other track gets 304 samples of delay inserted.
