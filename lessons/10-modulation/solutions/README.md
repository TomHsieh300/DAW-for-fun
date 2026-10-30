# Chapter 10 Exercise Solutions

**English** | [繁體中文](README.zh-TW.md)

The problems are in [`../exercises/README.md`](../exercises/README.md). The complete code for the implementation problems is in this folder. Each program checks its numbers with `DAWLAB_CHECK` / `DAWLAB_CHECK_NEAR` and prints `[PASS] l10_exN (...)` when it runs.

```powershell
.\scripts\build.ps1 -Only 10-modulation -TestLabel solution
```

---

## Exercise 1: The essential differences between chorus, flanger and vibrato

1. **Delay range**: chorus 5–30 ms (this chapter's default is centre 15 ms ± 3 ms), flanger 0.5–10 ms (default 1–3 ms).
   - **Frequency domain**: both are "dry + delayed" = a comb filter, with tooth spacing $`1/d`$. For the flanger with $`d = 1`$ ms the spacing is 1000 Hz and the notches are at 500, 1500, 2500 Hz and so on: sparse, and in the band where the ear is most sensitive. During a sweep the whole row of notches slides together, so it sounds like "the timbre being filtered". For the chorus with $`d = 15`$ ms the spacing is 66.7 Hz, and the teeth are so dense that a single critical band contains several peaks and valleys; the ear cannot resolve individual notches and the colouration is weak.
   - **Pitch**: the amount of detuning $`2\pi r D`$ depends only on depth × rate, but the typical values differ. A chorus has a larger depth and a moderate rate (3 ms × 0.8 Hz → ±26 cents), so the detuning is noticeable and the main impression is "another, slightly out-of-tune person". A flanger has a very low rate (0.1–0.3 Hz), the detuning is only a few cents, and the main impression is the comb sliding.
   - In one sentence: **a chorus makes you hear "two voices", a flanger makes you hear "one filtered voice"**. The dividing line is around 10–15 ms, where the delay becomes short enough that the comb tooth spacing exceeds the frequency resolution of the ear.
2. Feedback creates resonances at $`f = k/d`$. For a flanger with $`d = 1`$ ms the resonances are at 1, 2, 3 kHz and so on, and that "pitched metallic resonance" is exactly the sound a flanger wants. For a chorus with $`d = 20`$ ms the resonances would be at 50, 100, 150 Hz and so on, a 50 Hz humming bass (like singing into a water pipe). Feedback would also make the 20 ms delay repeat several times, which starts to sound like a fast flutter echo and completely destroys the "natural ensemble" feel. So a chorus almost never uses feedback.
3. **mix**. Vibrato = mix 1 (only the delay path). With no dry signal there is no "sum of two paths": $`|H| = |e^{-j\omega d}| = 1`$, with no peaks or valleys at all, leaving only the pitch drift from the changing delay (the self-check of Exercise 3: output RMS differs from the input by < 0.3 dB).
4. No. At the notch $`z^{-D} = -1`$: $`|H| = |(1 - m) - m| = |1 - 2m|`$. For mix 0.2 → $`|0.8 - 0.2| = 0.6`$ → $20\log_{10}0.6 = $ **−4.44 dB**; at the peak $`|0.8 + 0.2| = 1`$ (0 dB). Only mix 0.5 (both paths equal) cancels completely, which is why the default mix of flanger / phaser is 0.5.

## Exercise 2: Work through the formulas

1. $`d = 0.5`$ ms: notches $`f_k = (2k+1)/(2d) = (2k+1) \times 1000`$ Hz → **1000, 3000, 5000 Hz**; peaks $`f_k = k/d`$ → **0, 2000, 4000 Hz**.
2. The first notch $`f_0 = 1/(2d)`$ ⇒ $`d = 1/(2f_0)`$: $`f_0 = 1000`$ Hz → $`d = 0.5`$ ms; $`f_0 = 200`$ Hz → $`d = 2.5`$ ms. So **`setDelayMs(0.5f)`, `setDepthMs(2.0f)`** (the delay sweeps 0.5–2.5 ms). Note that the notch is inversely proportional to $`d`$: a 5× delay means a 5× notch frequency (about 2.3 octaves).
3. $`H_{\text{peak}} = (1-m) + m/(1-g)`$, $`H_{\text{notch}} = (1-m) - m/(1+g)`$, with $`m = 0.5`$:
   - $`g = +0.7`$: peak $`0.5 + 0.5/0.3 = 2.167`$ → **+6.72 dB**; notch $`0.5 - 0.5/1.7 = 0.206`$ → **−13.73 dB**.
   - $`g = -0.7`$: at the former peak position $`0.5 + 0.5/1.7 = 0.794`$ → **−2.00 dB**; at the former notch position $`0.5 - 0.5/0.3 = -1.167`$ → $`|{-1.167}|`$ → **+1.34 dB**. Peaks and valleys swap (the −0.7 column measured in Example 3 holds exactly these numbers).
4. $`2\pi r D = 2\pi \times 1 \times 0.005 = 0.0314`$ → upward $1200\log_2(1.0314) = $ **+53.6 cents**, downward $1200\log_2(0.9686) = $ **−55.3 cents** (more than half a semitone, a very obvious detuning; the log curve is concave, so the downward magnitude is slightly larger).
   For ±10 cents: taking the upward deviation, $`1 + 2\pi r D = 2^{10/1200} = 1.005793`$ ⇒ $r = 0.005793 / (2\pi \times 0.005) = $ **0.184 Hz**; requiring the downward deviation also not to exceed 10 cents, $`1 - 2\pi r D = 2^{-10/1200}`$ ⇒ $`r = 0.183`$ Hz, a very small difference. When the depth is large the rate has to be very slow if the chorus is not to sound "seasick".
5. $`t = \tan(\pi \cdot 1000/48000) = \tan(0.06545) = 0.065543`$, $a = (t-1)/(t+1) = -0.934457/1.065543 = $ **−0.87698**.
   At 500 Hz: $\varphi = -2\arctan\big(\tan(\pi \cdot 500/48000)/t\big) = -2\arctan(0.032737/0.065543) = -2\arctan(0.49946) = $ **−53.08°** (the table of Example 4: −53.08°). Note that 500 Hz is half of $`f_c`$ but the phase is not −45°: arctan is not linear.
6. $`\tan(\pi f_k/f_s) = t \cdot \tan\frac{(2k+1)\pi}{8}`$:
   - $`k = 0`$: $`0.065543 \times \tan(22.5°) = 0.065543 \times 0.414214 = 0.027149`$ → $f = \arctan(0.027149) \times 48000/\pi = $ **414.7 Hz**.
   - $`k = 1`$: $`0.065543 \times \tan(67.5°) = 0.065543 \times 2.414214 = 0.158236`$ → **2397.8 Hz**.
   (Example 4 measures 414.7 / 2397.8 Hz.)
7. $`g(t) = (1 - 0.3) + 0.3\sin(2\pi \cdot 6t)`$:
   - 1000 Hz: $0.5 \times (1 - D/2) = 0.5 \times 0.7 = $ **0.35**;
   - 994 Hz and 1006 Hz: $0.5 \times D/4 = 0.5 \times 0.15 = $ **0.075** (one line each).
   6 Hz itself **does not** appear in the output (the input is 1 kHz, and the multiplication only creates sidebands on either side of 1 kHz).
8. $r = 128 / (60 \times 0.75) = $ **2.844 Hz**; period $= 0.75 \times 60/128 = $ **351.6 ms**.

## Exercise 3: Vibrato

Complete solution: [`ex3_vibrato.cpp`](ex3_vibrato.cpp) (target `l10_ex3_solution`).

The key parts:

```cpp
// prepare: the only place that allocates
delay_.prepare(dawlab::msToSamples(kMaxCentreMs + kMaxDepthMs, sampleRate) + 8, numChannels_);

// advanceModulation: once per time step (channel 0), shared by all channels
const float ms = std::max(centreMs_ + depth * lfo_.getNextValue(), 0.05f);
currentDelaySamples_ = std::clamp(ms * fs / 1000, 2.0f, maxDelay - 3);

// process: push, then read; the output is ONLY the delayed signal
delay_.push(ch, d[i]);
d[i] = delay_.readCubic(ch, currentDelaySamples_);
```

- **centre must be ≥ depth**, otherwise $`d(t)`$ goes negative (reading the "future"). The centre itself is just a fixed latency (5 ms) that the ear cannot hear.
- Use `readCubic`: the delay keeps changing, and linear interpolation would make the highs fluctuate with the LFO (README Section 2.8).
- Measured: 1 kHz, 5 Hz, 1 ms → 968.6 .. 1031.4 Hz (same as theory, +53.6 / −55.3 cents), mean 1000.51 Hz, RMS −0.00 dB; with depth 0 it stays at exactly 1000.00 Hz.
- A singer's natural vibrato is about 5–6 Hz, ±20–50 cents. The solution's output uses 5.5 Hz and 0.6 ms → $1200\log_2(1 \pm 2\pi \cdot 5.5 \cdot 0.0006) = $ +35.5 / −36.3 cents.

## Exercise 4: Ring modulator

Complete solution: [`ex4_ringmod.cpp`](ex4_ringmod.cpp) (target `l10_ex4_solution`).

```cpp
void advance() noexcept {
    mix_ = mixSm_.next();
    carrierValue_ = carrier_.getNextValue();   // sine Lfo, rate = carrier frequency
}
float processSample(int channel, float x) noexcept {
    if (channel == 0) advance();
    return (1.0f - mix_) * x + mix_ * x * carrierValue_;
}
```

- The carrier is simply a sine `Lfo` with a high rate. The `Lfo` phase is a double, so a carrier of 150 Hz or 500 Hz is very accurate.
- The maths: $`A\sin(a)\sin(b) = \frac{A}{2}[\cos(a-b) - \cos(a+b)]`$ → 850 / 1150 Hz each have $`A/2 = 0.25`$, and 1000 Hz and 150 Hz vanish. Measured: 0.2500 / 0.2500, and both 1000 Hz and 150 Hz are at −100 dB (the measurement floor).
- mix 0.5: the dry signal $`0.5 \times 0.5 = 0.25`$ stays at 1000 Hz; the wet signal is halved, so each sideband is $`0.125`$. The measurements match exactly.
- **Why a 30 Hz carrier gives a swell "60 times per second"**: the ring-mod "gain" is $`\sin(2\pi \cdot 30t)`$, whose absolute value crosses zero twice per cycle (once in the positive half-cycle and once in the negative), and the ear cannot hear the sign (polarity), only the swell of $`|{\sin}|`$ → 60 times per second. In the spectrum: every frequency is split into two lines at $`f \pm 30`$ Hz, 60 Hz apart, and the beating of the two is 60 Hz.
- The difference from tremolo: the tremolo gain has a DC offset ($`1 - D/2`$), so the original frequency is kept; ring mod has no DC, so the original frequency is cancelled.

## Exercise 5: Stereo chorus and left/right correlation

Complete solution: [`ex5_stereo_chorus.cpp`](ex5_stereo_chorus.cpp) (target `l10_ex5_solution`).

```cpp
void reset() noexcept {
    delay_.reset();
    lfo_[0].setPhase(0.0f);
    lfo_[1].setPhase(phaseOffset_);           // the two LFOs are always phaseOffset cycles apart
    for (Lfo& l : lfo_) l.reset();
}
// process (every sample, every channel):
delay_.push(ch, x);
const float delayMs = centreMs_ + depthMs_ * lfo_[ch].getNextValue();
const float wet = delay_.readCubic(ch, std::clamp(delayMs * toSamples, 2.0f, maxRead));
d[i] = (1.0f - mix_) * x + mix_ * wet;
```

Measured (white noise, identical left and right input):

<div align="center">

| phase offset | mix | $`\rho`$ |
|---|---|---|
| 0° | 1.0 | 1.0000 |
| 90° | 1.0 | 0.0012 |
| 90° | 0.5 | 0.5502 |
| 180° | 1.0 | 0.0057 |

</div>

- Theoretical $`\rho`$ at mix 0.5: $`L = 0.5x + 0.5w_L`$. If $`x, w_L, w_R`$ are mutually uncorrelated and each has power $`\sigma^2`$: $`E[LR] = 0.25\sigma^2`$, $`E[L^2] = 0.5\sigma^2`$ → $`\rho = 0.5`$. The measured 0.55 is slightly higher because of interpolation: the delay keeps changing, and the cubic (Catmull-Rom) interpolator is a time-varying lowpass that keeps on average only $`\alpha \approx 0.814`$ of the power of white noise (about −0.9 dB), so the wet power is $`\alpha\sigma^2`$ rather than $`\sigma^2`$. Substituting: $`\rho = 0.25/(0.25 + 0.25\alpha) = 0.551`$, matching the measurement (the 0.711 in README Section 2.4 has the same cause).
- **Mono compatibility (where −1.25 dB comes from)**: $`P(L) = 0.25\sigma^2 + 0.25\sigma^2 = 0.5\sigma^2`$; $`(L+R)/2 = 0.5x + 0.25w_L + 0.25w_R`$ has power $`0.25 + 0.0625 + 0.0625 = 0.375\sigma^2`$; the ratio is $`0.375/0.5 = 0.75`$ → $10\log_{10}0.75 = $ **−1.25 dB**. The measured value is −1.11 dB: including the interpolation power factor $`\alpha`$, the ratio is $`(0.25 + 0.125\alpha)/(0.25 + 0.25\alpha)`$, which at $`\alpha = 0.814`$ is −1.10 dB, exactly explaining the gap. The loss is small because the dry signal (identical in left and right) is fully preserved when summed to mono.
- **The phase difference must be verified separately**: in the table above, the $`\rho`$ at 90° and 180° are both close to 0 and differ by less than 0.05. For white noise, any non-zero phase difference makes the wet signal nearly uncorrelated, so $`\rho`$ cannot tell whether the offset is 0.25, 0.5 or 0.05. The solution adds a phase-sensitive self-check: set the rate to 0 (the LFO stays at its starting phase), feed in an impulse, and the position of the output impulse is the delay $`d = \text{centre} + \text{depth}\cdot\sin(2\pi p)`$. Measured: left 15.000 ms, right 18.000 ms ($`p = 0.25`$ → $`\sin 90° = 1`$); if the offset were written as 0.5, the right channel would be at 15 ms, and if written as 0.05 it would be at 15.93 ms, and both would FAIL.
- The $`\rho`$ against `dsp::Chorus` (1 voice, mix 1, spread 1): 0.0012 vs 0.0012 for your own version, in agreement (the spread of 1 in `Chorus` = right-channel phase +0.25, the same as in this exercise).
- **Why is 90° the usual choice?** For broadband material such as white noise, a tiny delay difference makes left and right almost completely uncorrelated, and the $`\rho`$ at 90° and 180° are both close to 0, so numerically there is no difference. The difference is in the dynamics: at 180°, one channel always has the longest delay while the other has the shortest, the detuning goes in exactly opposite directions, and the image is pulled symmetrically between left and right. At 90° left and right take turns leading, and it sounds like a more even, "rotating" width. Many hardware choruses use a 90° (quadrature) LFO; this is a choice of sound and tradition, not a mathematical optimum.

## Exercise 6: Tempo-synced tremolo

Complete solution: [`ex6_tempo_tremolo.cpp`](ex6_tempo_tremolo.cpp) (target `l10_ex6_solution`).

```cpp
float getRateHz() const noexcept { return Lfo::tempoToRateHz(bpm_, beats_); }   // bpm / 60 / beats

void process(dawlab::AudioBlock block, double ppqAtBlockStart) noexcept {
    tremolo_.setRateHz(getRateHz());
    const double cycles = ppqAtBlockStart / beats_;               // LFO cycles elapsed since the start of the song
    const double phase = cycles - std::floor(cycles) + phaseOffset_;
    tremolo_.setLfoPhase(static_cast<float>(phase - std::floor(phase)));
    tremolo_.process(block);
}
```

- **Why realign on every block**: if you only set the rate, the LFO phase counts from "the instant play was pressed". If playback starts in the middle of the song (say at ppq 3.3, between two eighth-note beats), the tremolo peaks fall between the beats and do not line up with the drums at all. Computing the phase from the ppq on every block is immediately correct after jumping to any position or after a tempo change; during normal playback the recomputed phase equals the phase the LFO accumulated on its own (up to rounding error), so there is no jump.
- Measured: gain 1.00000 on the beats, 0.00000 between beats, 8 troughs in 2 seconds; the largest difference between starting playback at ppq 3.3 and playing from the start is $`2.4 \times 10^{-7}`$ (float rounding level); after 120 → 90 bpm the trough spacing is 0.3333 s.
- Measurement tip: with DC 1.0 as input, the output is the gain curve itself. When looking for "troughs", do not use "smaller than both neighbours": near the bottom of a trough, float quantization creates several false local minima. The solution counts each run of consecutive values below 0.05 as one trough.
- In a real host (Chapters 13 and 14) the ppq comes from the transport (JUCE's `AudioPlayHead::PositionInfo::getPpqPosition()`); here it is simulated with `ppq += n / fs × bpm / 60`.

## Exercise 7: Why are a phaser's notches not evenly spaced?

1. With two paths summed, notches appear at the frequencies where the "phase difference = an odd multiple of π".
   - Flanger: the phase difference $`= 2\pi f d`$ is **linear** in $`f`$, adding $`2\pi`$ for every extra $`1/d`$ Hz, so the notches are evenly spaced ($`1/d`$).
   - Phaser: the phase difference $`= N\varphi(f) = -2N\arctan\big(\tan(\pi f/f_s)/\tan(\pi f_c/f_s)\big)`$ has an arctan shape: **measured in log frequency (octaves)**, the phase turns fastest near $`f_c`$ and more slowly the more octaves you move away, so the notches are symmetric about $`f_c`$ on a log axis, and on a linear Hz axis they are dense at low frequencies and sparse at high frequencies (for example 414.7 / 2397.8 Hz for 4 stages). Note this is a "per octave" statement: on a linear Hz axis, the phase change per Hz, $`|d\varphi/d\omega| = (1+u^2)\,t/(t^2+u^2)`$ (with $`u = \tan(\omega/2)`$ and $`t = \tan(\omega_c/2)`$), is largest at DC (about $`1/t`$) and is only about half of that at $`f_c`$. The total phase is only $`N\pi`$, so the number of notches is limited ($`N/2`$), unlike a comb that continues all the way to Nyquist.
2. The notch condition is $`\tan(\pi f_k/f_s) = \tan(\pi f_c/f_s)\tan\theta_k`$ with $`\theta_k = (2k+1)\pi/(2N)`$. For a paired $`k`$ and $`k' = N/2 - 1 - k`$: $`\theta_{k'} = \frac{(N - 2k - 1)\pi}{2N} = \frac{\pi}{2} - \theta_k`$. So
   $`\displaystyle \tan\frac{\pi f_k}{f_s}\cdot\tan\frac{\pi f_{k'}}{f_s} = \tan^2\frac{\pi f_c}{f_s}\cdot\tan\theta_k\tan\!\left(\frac{\pi}{2} - \theta_k\right) = \tan^2\frac{\pi f_c}{f_s}`$
   At low frequencies $`\tan x \approx x`$, and cancelling $`(\pi/f_s)^2`$ from both sides gives $`f_k f_{k'} \approx f_c^2`$. That is, the notches are symmetric about $`f_c`$ on the **log frequency axis**, but the ratio between adjacent notches is not constant (8 stages: 3.36, 2.23, 3.26), so they are neither equally spaced in difference nor in ratio.
3. $`268.3 \times 3665.4 = 983{,}427 \approx 10^6`$ (the middle one, 1000.0 Hz, pairs with itself: $`1000^2 = 10^6`$). The product is not exactly $`10^6`$ because $`\tan x \approx x`$ is only an approximation: 3665 Hz is no longer "low frequency", and the bilinear transform pushes the high frequencies down (frequency warping). Checking with the exact form: $`\tan(\pi \cdot 268.3/48000)\cdot\tan(\pi \cdot 3665.4/48000) = 0.0042959`$ and $`\tan^2(\pi \cdot 1000/48000) = 0.0042959`$, equal to five significant digits (the remaining difference is just 268.3 and 3665.4 being rounded to 0.1 Hz; Example 4 substitutes the unrounded 4-stage notches and they agree to the tenth decimal place).
4. The total phase $`= \sum_{i=1}^{4}\varphi_i(\omega)`$. Each term still falls monotonically from 0 to $`-\pi`$ and the sum still falls from 0 to $`-4\pi`$, so **the number of notches is unchanged (still 2)**. As for position: once the $`f_c`$ are spread out, the total phase starts to change earlier at low frequencies (the 300 Hz stage turns first) and finishes later at high frequencies (the 2400 Hz stage turns last), so the curve is flatter and spans a wider frequency range → the first notch moves toward the low frequencies and the second toward the high frequencies, so the two notches are farther apart and wider. Many analog phasers (for example 4-stage circuits built from JFETs or OTAs) adjust the notch distribution by using "different capacitors in each stage", which is one source of their individual sounds.
5. For evenly spaced notches, the phase difference would have to be linear in frequency = a pure delay → and that is a flanger. You could also approximate a delay with many allpass stages (the group delay of an allpass chain is roughly constant at low frequencies), but it takes a lot of stages (CPU) and the result becomes the metallic sound of a comb filter, losing the soft character of a phaser. The more practical approach is to accept the uneven spacing and use a different distribution of $`f_c`$ (part 4) to adjust the notch positions.

## Exercise 8: Staggered phaser

Complete solution: [`ex8_staggered_phaser.cpp`](ex8_staggered_phaser.cpp) (target `l10_ex8_solution`).

- **TODO 1**: `theoryPhase` simply adds up `Phaser::allpassPhase(f, scale · fc_i, fs)` over the stages. The amplitude of a first-order allpass is 1 everywhere; the response of the cascade = the product of the stage responses, so the phases add.
- **TODO 2**: each $`\varphi_i`$ falls monotonically from 0 to $`-\pi`$ and the sum $`\Phi`$ falls monotonically from 0 to $`-N\pi`$, so $`\Phi(f) = -(2k+1)\pi`$ has exactly one solution and bisection always converges: if $`\Phi(\text{mid})`$ is larger than the target (not negative enough yet), move `lo` to mid; otherwise move `hi`. Bisecting the 24 kHz interval 60 times leaves an error far below 1 Hz.
- **TODO 3**: all stages share the same factor $`s = 2^{\text{depth}\cdot\text{octaves}\cdot m}`$, and stage $`i`$ is set to $`f_c = s \cdot f_{c,i}`$. The whole set of notches then translates together on the log frequency axis, while the distribution between stages (the timbre) is unchanged. Note that `makeFirstOrderAllpass` is called "for each stage separately", unlike `Phaser`, which computes the coefficients once and applies them to all stages.

Measured results (the output of `l10_ex8_solution`):

```
4 x 1000 Hz: predictNotchHz vs Phaser::notchFrequencyHz (closed form)
  k = 0: 414.7042 Hz vs 414.7042 Hz
  k = 1: 2397.7862 Hz vs 2397.7862 Hz
  White noise through StaggeredPhaser vs dawlab::dsp::Phaser: largest difference 0.00e+00 (should be 0)

300 / 600 / 1200 / 2400 Hz: found 2 notches (expected 2)
   k   measured(Hz)  predicted(Hz)   error      depth
   0        289.1         289.1   +0.002%    -92.4 dB
   1       2481.5        2481.5   -0.001%   -100.0 dB

  4 x 1000 Hz:           414.7 /  2397.8 Hz, ratio 5.78x (2.53 octaves)
  300-2400 Hz spread:    289.1 /  2481.5 Hz, ratio 8.58x (3.10 octaves)
     total phase   4 x 1000 Hz    300-2400 Hz
       414.7 Hz     -180.0°        -235.0°
      1000.0 Hz     -360.0°        -389.3°
      2397.8 Hz     -540.0°        -535.0°

LFO = +1 (fc x 2 = 600 / 1200 / 2400 / 4800 Hz): found 2 notches (expected 2)
   k   measured(Hz)  predicted(Hz)   error      depth
   0        580.5         580.5   -0.001%   -97.4 dB
   1       4895.9        4895.8   +0.001%   -94.2 dB
```

Interpretation:

- **The number of notches is unchanged**: the total phase still runs from 0 to $`-4\pi`$, with only two solutions, $`-\pi`$ and $`-3\pi`$, consistent with the conclusion of Exercise 7, part 4.
- **The two notches are farther apart** (2.53 → 3.10 octaves): the 300 Hz stage starts turning very early, so at 414.7 Hz the total phase is already −235° (past −180°), and the first notch appears earlier, at 289.1 Hz; the 2400 Hz stage finishes turning very late, so at 2397.8 Hz it has only reached −535°, and the second notch is delayed to 2481.5 Hz. Note that the shifts of the two notches are not symmetric (the low one moves 0.52 octaves and the high one only 0.05 octaves): the geometric mean of this set of $`f_c`$ is $`300 \cdot 2^{1.5} \approx 848.5`$ Hz, not 1000 Hz, so the overall centre of gravity sits lower to begin with.
- **LFO sweep**: after doubling all the $`f_c`$, the first notch moves from 289.1 to 580.5 Hz (about 2×) and the second from 2481.5 to 4895.9 Hz (1.97×; being short of 2× is the bilinear frequency warping at high frequencies).
- Sound: in `l10_ex8_staggered.wav` the two notches are farther apart, so during the sweep "the low one" and "the high one" are more like two independent dips moving; in `l10_ex8_uniform.wav` the two notches are closer and sound like one lump. The components of an analog phaser's stages are never exactly identical either, which is one reason their sounds differ.
