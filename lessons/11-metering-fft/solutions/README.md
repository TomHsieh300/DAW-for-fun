# Chapter 11 Solutions

**English** | [繁體中文](README.zh-TW.md)

The complete programs for the coding exercises are in this folder. Each has `DAWLAB_CHECK` self-checks, and `ctest -L solution` runs them automatically:

| Exercise | File | target |
|---|---|---|
| 3 | `ex3_true_peak.cpp` | `l11_ex3_solution` |
| 4 | `ex4_vu_meter.cpp` | `l11_ex4_solution` |
| 5 | `ex5_loudness_range.cpp` | `l11_ex5_solution` |
| 6 | `ex6_fft_convolution.cpp` | `l11_ex6_solution` |
| 7 | `ex7_correlation_meter.cpp` | `l11_ex7_solution` |

---

## Exercise 1: Bin resolution and the N you need

1. $\Delta f = 48000 / 2048 = 23.4375$ Hz; the frame length is $2048 / 48000 = 42.67$ ms; bin 100 = $100 \times 23.4375 = 2343.75$ Hz.
2. $1000 / 23.4375 = 42.67$ → it falls between bin 42 and bin 43 (the second line at the start of `l11_window_leakage` prints this number).
   For $k \cdot f_s / N = 1000$ we need $N = 48 k$, and the smallest is $N = 48$ (bin 1).
   If N must be a power of two: for $N = 2^m$, $k = 2^m / 48 = 2^{m-4}/3$, which is never an integer → **at 48 kHz, no power-of-two FFT
   can put 1 kHz at a bin center**. This is why real measurements almost always have leakage, and why a window is a necessity.
3. The two notes differ by $43.65 - 41.20 = 2.45$ Hz, and they need to be 2 bins apart → $\Delta f \le 1.225$ Hz →
   $N \ge 48000 / 1.225 \approx 39184$ → take $N = 65536$ ($2^{16}$), giving a bin width of 0.732 Hz and a frame length of **1.365 s**.
   The problem: one frame covers 1.4 seconds, which may contain several drum hits, and the spectrum is the "average" over those 1.4 seconds, so you cannot tell when a transient
   happened. This is the **time–frequency resolution trade-off** (the uncertainty principle): the finer the frequency, the blurrier the time.
   Professional tools use long frames at low frequencies and short frames at high frequencies (multi-resolution / constant-Q).
4. $44100 / 4096 = 10.77$ Hz; $4096 / 44100 = 92.9$ ms.
5. hop $= 48000 / 30 = 1600$ samples; overlap $= 1 - 1600/4096 = 60.9\%$.
   (`SpectrumAnalyzer::setOverlap(0.61f)` gives about the same hop.)

## Exercise 2: Why a window? Where does leakage come from?

1. **The DFT sees only N samples and implicitly assumes these N samples repeat forever.** When the end of the frame is joined back to its start,
   if the sine does not have a whole number of periods, the seam has a jump (a discontinuity). This artificial discontinuity needs many frequencies to build in the frequency domain,
   so energy "leaks" into every bin. Mathematically: taking N samples = multiplying by a rectangular window; multiplication in time = convolution in frequency,
   so the true spectrum (a single line) is convolved with the rectangular window's spectrum (a sinc shape whose side lobes are only -13 dB and decay slowly),
   and the line becomes "a mountain with long skirts on both sides".
2. With a whole number of periods the seam is continuous, with no discontinuity. In the frequency domain: the sine's frequency is exactly at a bin center, and the sinc's zeros fall exactly on
   every other bin, so the FFT samples only at the zeros and there appears to be "no leakage at all" (in the second table, the Rectangular
   bin 44 is -200 dB). But the skirts are still there, just not sampled; shift the frequency slightly and they all appear (with 1000 Hz 1/3 bin off,
   the rectangular window still has -30 dB at bin ±10).
3. A window tapers the two ends of the frame to 0 (removing the jump at the seam), at the cost of shrinking the signal overall: a bin-centered sine reads multiplied by
   $\text{CG} = \frac{1}{N}\sum w[n]$ (coherent gain: Hann 0.5 = -6.02 dB, Hamming 0.54, Blackman-Harris 0.359).
   To compensate: amplitude $A = \dfrac{2\,|X[k]|}{N \cdot \text{CG}}$ (DC and Nyquist are not multiplied by 2). This is how `SpectrumAnalyzer` makes a
   full-scale sine read 0 dBFS.
4. To find a -90 dB small signal you need a window with low enough side lobes → **Blackman-Harris** (highest side lobe -92 dB;
   in the example it is already down at -99 dB at bin ±10, versus only -70 dB for Hann and -30 dB for rectangular).
   To resolve 1000 / 1010 Hz you need a narrow main lobe → rectangular or Hann, and a large enough N: with a 10 Hz difference,
   rectangular needs a separation of more than 1 bin → $N \ge 4800$ → 8192 (bin 5.86 Hz, 1.7 bins apart);
   with Hann, 2 bins or more is recommended → 16384. The two requirements conflict (low side lobes ⇔ wide main lobe), so there is no universal window.
5. **No.** Zero-padding adds no new information (the observation time is still N samples); it just samples the same DTFT curve more densely,
   so the position and height of peaks can be read more accurately (interpolation), but two peaks that are so close that their main lobes have merged will not separate because of it.
   The last table of `l11_window_leakage` uses 16x zero-padding to draw the window's DTFT in fine detail and measure the main lobe, side lobes and
   scalloping loss (the loss at 0.5 bin off: Rectangular 3.92, Hann 1.42, Hamming 1.75, Blackman-Harris 0.82 dB).
   Zero-padding makes these numbers "measurable", but the main lobe's width (in units of the original bins) does not change at all.

## Exercise 3: A true-peak meter reusing Oversampler, compared with Chapter 05's windowed-sinc version

**TODO 1: reuse.** `Oversampler::upsample()` returns an `AudioBlock` 4 times as long; find `peakAbs` directly on it. No
`downsample` is needed, since the meter only measures and does not output (the upsampled signal is discarded after use, and the next block overwrites the same internal buffer).

```cpp
const AudioBlock up = oversampler_.upsample(block);
for (int ch = 0; ch < nch; ++ch) {
    const float p = peakAbs(up.getChannelPointer(ch), up.getNumSamples());
    if (p > truePeak_[ch].load(std::memory_order_relaxed))
        truePeak_[ch].store(p, std::memory_order_relaxed);
}
```

**TODO 2: multiplication count.** The input rate of stage $i$ is $f_s \cdot 2^{i-1}$, and for each input sample only the odd-tap FIR is computed
(the other output is a pure delay, 0 multiplications):

```cpp
for (int i = 0; i < os.getNumStages(); ++i)
    total += os.getStage(i).getNumOddTaps() << i;   // 32·1 + 12·2 = 56
```

The windowed-sinc version is 4 phases × 16 taps = 64.

Actual output (`l11_ex3_solution`, Debug build):

| Signal | sample peak | Oversampler | windowed-sinc | FFT ideal interpolation |
|---|---|---|---|---|
| 1 kHz 0 dBFS | 0.000 dBFS | 0.000 dBTP | -0.003 dBTP | (0) |
| 12 kHz (fs/4) phase 45° | -3.010 dBFS | 0.000 dBTP | -0.157 dBTP | (0) |
| Drum kit, +12 dB then hard clipped | 0.000 dBFS | **+5.201 dBTP** | +4.589 dBTP | +5.295 dBTP |
| Same, normalized to -1 dBTP using the Oversampler's reading | -6.201 dBFS | -1.000 dBTP | | |
| pluck sequence | -5.289 dBFS | -2.923 dBTP | -3.388 dBTP | -2.172 dBTP |

Phase sweep (fs/4 sine, amplitude 1, error = reading − 0 dBTP; one row excerpted every 11.25°):

| Phase | Peak position between samples | Oversampler | windowed-sinc |
|---|---|---|---|
| 0° | 0 | +0.000 dB | -0.169 dB |
| 11.25° | 7/8 | -0.168 dB | +0.000 dB |
| 22.5° | 3/4 | +0.001 dB | -0.159 dB |
| 33.75° | 5/8 | -0.168 dB | +0.010 dB |
| 45° | 1/2 | +0.000 dB | -0.157 dB |

Frequency response (a sine of amplitude 0.5 with the frequency slightly off an integer ratio so the peak sweeps through all positions; error = reading − (-6.02)):

| Frequency | 1 k | 10 k | 16 k | 18 k | 20 k | 21 k | 22 k |
|---|---|---|---|---|---|---|---|
| Oversampler | +0.001 | +0.000 | +0.000 | +0.001 | +0.000 | +0.000 | +0.000 |
| windowed-sinc | -0.000 | -0.002 | -0.002 | +0.092 | -0.081 | -0.229 | -0.430 |

CPU: 56 vs 64 multiplications; measured times (10 s of stereo white noise, per channel-sample) are about 130–150 ns vs 410–530 ns in Debug,
and about 20 ns vs 59 ns in Release; the windowed-sinc is about 3 times slower (exact numbers depend on the machine and load).

**Question 1: interpolation point positions.** The Oversampler is two 2x halfband stages, and each stage has one phase that is a pure delay (the output is the input sample),
so the 4x points land at 0, 1/4, 1/2, 3/4 between the original samples; the 64-tap FIR has even length with its center at 31.5, so its points land at
1/8, 3/8, 5/8, 7/8. If the peak is exactly on one of the points the measurement is accurate; the worst case is when it falls midway between two points (1/8 sample from the nearest):
an fs/4 sine turns 90° per sample, so 1/8 sample = 11.25° and the reading is $\cos 11.25° = 0.981$ times, i.e. -0.17 dB.
Both sets of points are spaced 1/4 sample apart, so the **worst-case error is the same size**; it just happens at the phase where the other is most accurate. Looking only at the single example of "fs/4, phase 45°"
might suggest the halfband is more accurate, but that is just because the peak happens to sit on its points (the -6.18 dBTP in the Chapter 05 README section 2.8 table is the same thing).

**Question 2: near Nyquist.** At 22 kHz the first halfband stage has only 0.928x gain in a single pass (-0.65 dB), but the image it fails to filter out is at
$48 - 22 = 26$ kHz with amplitude 0.072x. The halfband symmetry $|H(f)| + |H(f_s - f)| = 1$ makes the two add up to exactly 1:
as the peak sweeps through all positions, there is always a moment when the two add in phase, and the reading returns to the original amplitude. This is a coincidence specific to "measuring peaks";
if what you want is a clean 4x signal (for example for saturation), that 26 kHz image is an impurity and not a help.
The Hann windowed-sinc has no such complementarity, and its transition band is wide (the main lobe of a 64-tap Hann is wide), so from 18 kHz up there is
±0.1 dB ripple, and at 22 kHz it underestimates by 0.43 dB. This also explains the difference on drums and plucks: hard-clip harmonics and the white-noise excitation of plucks both have a lot of
content near Nyquist, where the windowed-sinc reads 0.7–1.2 dB below the ideal and the Oversampler only 0.1–0.75 dB below.
(The +4.54 dBTP measured in Chapter 05 Exercise 4 used a 1-bar drum kit, while this exercise uses 2 bars; this exercise feeds the same signal to both meters at once,
which makes the comparison fair: +4.59 vs +5.20.)

**Question 3: multiplication count vs actual speed.** The multiplication counts differ by only 14%, yet the real speed differs by 3x; the difference is in how the inner loop is written:

- `HalfbandStage` uses a "mirrored ring buffer" (each sample is written twice), so the most recent N inputs are always a contiguous block of memory, and the inner loop is just
  `acc += g[j] * hist[j]`: no branches, all float, which the compiler (Release) can vectorize directly.
- The reference version checks `if (--idx < 0) idx += kTapsPerPhase` for wraparound on every tap, converts float to double before accumulating,
  and scans the history once for each of the 4 phases. The branches and type conversions make it hard to vectorize.

Lesson: when estimating CPU, the multiplication count is only a starting point; memory layout and loop structure often matter more. You can try changing the reference version to a mirrored ring buffer + float accumulation
and see how much the timing differs.

**Question 4: which to choose?** For real-time display use the Oversampler: it is already tested, faster, and its readings near Nyquist are closer to the ideal.
But the latency is the opposite: for upsampling only, the Oversampler's delay is 31 2x-samples for the first stage + 11 4x-samples for the second
= 15.5 + 2.75 = 18.25 original samples (about 0.38 ms); the 64-tap FIR is 31.5 4x-samples = 7.875 original samples
(about 0.16 ms). A meter doesn't care about this delay (nobody can see a display that is 0.4 ms late), but a true-peak limiter's sidechain
delay must be counted into the lookahead, so the shorter filter saves more.

Other key points:
- Hard clipping produces a lot of high-frequency harmonics, and on both sides of the flat top the reconstructed waveform "bounces" out, so the inter-sample overshoot can reach several dB.
  A "0 dBFS master" judged only by sample peak will clip after conversion to AAC/MP3 or after passing through a DAC.
- 4x is not perfect: the peak of a sine near Nyquist can land exactly between two 4x samples, underestimating by at most
  $20\log_{10}\cos(\pi/8) = -0.69$ dB; the filter's transition band also attenuates content near fs/2.
  That is why the standards recommend a ceiling of -1 dBTP.
- The pure-delay phase of the polyphase halfband is the original sample, so the Oversampler version's true peak is always ≥ the sample peak
  (the self-check verifies this); the windowed-sinc version has no such guarantee (none of its points is on an original sample).

## Exercise 4: VU meter

```cpp
const double u = std::abs(x[i]) * inputScale_;                    // full-wave rectification + calibration
const double acc = wn2_ * (u - s.position) - twoZetaWn_ * s.velocity;
s.velocity += acc * dt_;                                          // semi-implicit Euler: velocity first
s.position += s.velocity * dt_;                                   //                      then position
...
inputScale_ = kPi / (2.0 * dbToGain(referenceDbfs_));             // the rectified average of a sine of amplitude A = 2A/π
```

Derivation:
- Overshoot $M_p = e^{-\pi\zeta/\sqrt{1-\zeta^2}}$; $\zeta = 0.82$ → 1.11% (the standard says 1–1.5%).
- Step response of an underdamped second-order system ($u = \omega_n t$): $y(u) = 1 - \dfrac{e^{-\zeta u}}{\sqrt{1-\zeta^2}}\sin\!\left(\sqrt{1-\zeta^2}\,u + \arccos\zeta\right)$.
  Solving $y(u_{99}) = 0.99$ by bisection gives $u_{99} = 4.128$, so $\omega_n = 4.128 / 0.3\text{ s} = 13.76$ rad/s (2.19 Hz).
- Why semi-implicit Euler? Explicit Euler makes the energy of an undamped oscillation grow without bound; updating the velocity first and then the position with the new velocity is
  symplectic and far more stable. $\omega_n T = 13.76/48000 \approx 3\times10^{-4}$, so the error is negligible.

Actual output: the final reading is 0.9986 (not 1.0: at 48 kHz a 1 kHz cycle has only 48 points, and the average of |sin| after sampling is
$= \cot(\pi/48)/24 = 0.63571$, 0.14% below $2/\pi$); at 300 ms the needle is at 99.00% of the final reading; overshoot 1.11%;
-24 dBFS → -6.01 VU, 0 dBFS → +17.99 VU, and after changing to -20 dBFS = 0 VU, -24 dBFS → -4.01 VU; 300 ms after stopping the needle is down to 0.0100.
A 10 ms 0 VU burst pushes the needle only to 0.0573 (**-24.8 VU**). Note that you must observe for long enough: the needle has inertia and keeps climbing for about 80 ms after the burst ends
before reaching its maximum (the peak time of a second-order impulse response is $t = \arccos\zeta / (\omega_n\sqrt{1-\zeta^2}) \approx 77$ ms).
The drum kit's VU swings between -36.7 and +7.3 VU, and its maximum converts to -10.7 dBFS, about 8 dB below the sample peak (-2.92 dBFS).

## Exercise 5: LRA

```cpp
// 1. relative gate (power average)
for (b) { sumPower += count[b] * lufsToPower(binCentre(b)); total += count[b]; }
gate = powerToLufs(sumPower / total) - 20;
// 2. the first bin with binCentre >= gate; accumulate the total that passes, gated
// 3. percentile: the first time the cumulative count is >= ceil(p · gated)
lra = percentile(0.95) - percentile(0.10);
```

Actual output (`l11_ex5_solution`):

| A / B (dBFS) | LRA | 10% | 95% |
|---|---|---|---|
| -20 / -30 | 10.00 LU | -29.95 | -19.95 |
| -20 / -15 | 5.00 LU | -19.95 | -14.95 |
| -40 / -20 | 20.00 LU | -39.95 | -19.95 |

(Identical stereo channels → LUFS = dBFS + 0.01; the 0.05 offset of the histogram bin centers cancels in the subtraction.)

Key points:
- **Why percentiles instead of min/max**: an explosion or a fade-out takes up very little time and should not inflate "the dynamic range of this song".
- The relative gate is **-20 LU** (integrated uses -10 LU): LRA wants to keep the quieter passages and exclude only the near-silent parts.
  In the self-check's "-20 dBFS for 20 s → -50 dBFS for 10 s", the power average of the short-term values is about
  $10\log_{10}\frac{20P + 10\cdot 0.001P}{30} \approx -20 - 1.76 = -21.8$ LUFS ($P$ is the power of the -20 dBFS segment),
  and the -50 segment (about -50 LUFS) is about 28 LU below the average, far below the -20 LU relative gate, so it is excluded and the LRA is only 1.6 LU.
- The histogram means the audio thread needs no dynamic allocation; EBU's reference implementations (for example libebur128) use a similar approach.
- Limitation: a single `process()` call must not exceed 100 ms, otherwise short-term updates are missed (explained at the top of the program).

## Exercise 6: overlap-add fast convolution

```cpp
std::copy_n(data, numSamples, timeBuffer_.begin());                  // 1. zero-pad
std::fill(timeBuffer_.begin() + numSamples, timeBuffer_.end(), 0.0f);
fft_.forwardReal(timeBuffer_.data(), spectrum_.data());              // 2. X[k]
for (k) spectrum_[k] *= irSpectrum_[k];                              //    Y[k] = X[k]·H[k]
fft_.inverseReal(spectrum_.data(), timeBuffer_.data());              //    y = IFFT (includes 1/N)
for (i < N) overlap_[i] += timeBuffer_[i];                           // 3. overlap-add
std::copy_n(overlap_.begin(), numSamples, data);                     // 4. output the first B complete samples
std::copy(overlap_.begin() + numSamples, overlap_.end(), overlap_.begin()); // 5. shift the tail forward
std::fill(overlap_.end() - numSamples, overlap_.end(), 0.0f);
```

Why $N \ge B + L - 1$: the linear convolution of length B and length L has length $B + L - 1$. The product of DFTs is a **circular convolution**,
and the part beyond N wraps around and is added onto the beginning (time-domain aliasing); with N large enough, nothing wraps around.

Actual output (Debug, IR of 12000 taps, 96000 samples; timings differ on every run, so the range over two runs is listed):
direct convolution 2700–3020 ms; overlap-add with B = 512 (N = 16384) 2530–4650 ms, which is **about the same as direct convolution or even slower**;
B = 4096 (same N = 16384) 320–650 ms, 4 to 9 times faster. The error for both block lengths is -100 dB (float precision).
The self-check only requires that at B = 4096 it is more than twice as fast as direct convolution, and that B = 4096 is faster than B = 512.
Why is B = 512 such a poor deal? Every 512 samples it has to do a 16384-point forward + inverse FFT,
which works out to about $2 \cdot \frac{N}{2}\log_2 N / B \approx 448$ complex multiplications per sample, the same order of magnitude as direct convolution's 12000 multiply-adds
(and in the Debug configuration `std::complex` is especially costly); at B = 4096 it drops to about 56.
**A single partition pays off only when B and L are of the same order of magnitude**; but the audio callback's B is small, and a larger B means more latency.
Chapter 12's uniform partitioned convolution cuts the IR into many small segments of length B, multiplies each separately and accumulates them in the frequency domain, solving both problems at once.

## Exercise 7: correlation meter

```cpp
lr_ += a_ * (x * y - lr_);
ll_ += a_ * (x * x - ll_);
rr_ += a_ * (y * y - rr_);
const double denom = std::sqrt(ll_ * rr_);
const double corr = denom > 1e-10 ? std::clamp(lr_ / denom, -1.0, 1.0) : 0.0;
```

$a = 1 - e^{-1/(\tau f_s)}$ is the coefficient of a one-pole lowpass (the same formula as the Chapter 04 envelope follower).

Actual output: mono +1.000 (S/M -100 dB = no side at all), anti-phase -1.000, sin/cos -0.001 (S/M 0.0 dB),
independent noise -0.008 (S/M 0.1 dB), common : independent = 1 : 1 → 0.494
(theory $r = \frac{E[s^2]}{E[s^2]+E[n^2]} = 0.5$; S/M energy -4.7 dB, theory $10\log_{10}\frac{1-r}{1+r} = -4.77$ dB).

Response time: switching from mono to anti-phase, $E[LR]$ moves exponentially from $+P$ toward $-P$: $m(t) = -P + 2P e^{-t/\tau}$,
and the time to cross 0 is $t = \tau\ln 2 = 208$ ms; the measured value is 205 ms (block granularity 10.7 ms).

By ear: listening to `l11_ex7_out_of_phase.wav` on headphones, the sound seems to come "from the sides of the head, even from behind the head", which is very unnatural;
summed to mono (L+R) it cancels completely and you hear nothing. When mixing, be careful if the correlation stays below 0 for a long time.
