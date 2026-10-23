# Chapter 11 Exercises

**English** | [繁體中文](README.zh-TW.md)

Difficulty: ★☆☆ basic, ★★☆ intermediate, ★★★ advanced. The starter code for the coding exercises is in `exercises/starter/` and compiles as is;
until you finish the `// TODO`s it prints `[FAIL]` when run, which is expected. Once all the self-checks pass it prints `[PASS]`.

```powershell
.\scripts\build.ps1 -Only 11-metering-fft -NoTest
cd build
.\bin\Debug\l11_ex3_starter.exe          # your version
.\bin\Debug\l11_ex3_solution.exe         # reference solution
```

Solutions and explanations: [`../solutions/README.md`](../solutions/README.md).

---

## Exercise 1 (calculation) ★☆☆　Bin resolution and the N you need

1. With fs = 48 kHz and N = 2048, how wide is a bin in Hz? How many ms does a frame cover? What frequency does bin 100 correspond to?
2. At which bin (fractional) does a 1 kHz sine fall? What is the smallest N (not restricted to powers of two) that puts 1 kHz exactly at the center of some bin?
   What if N must be a power of two?
3. You want the spectrum to show the bass notes E1 (41.20 Hz) and F1 (43.65 Hz) as two separate peaks. With a Hann window, two sines must be at least about 2 bins apart
   to show two peaks. At fs = 48 kHz, how large must N be (a power of two)? How long is this frame?
   What problem would you have using it to look at a drum kit's transients?
4. At 44.1 kHz and N = 4096: what are the bin width and the frame length?
5. A spectrum analyzer should update 30 times per second (30 fps), with fs = 48 kHz and N = 4096. How many samples is the hop? What is the overlap in %?

Hint: bin width $\Delta f = f_s / N$; frame length $N / f_s$; overlap $= 1 - \text{hop}/N$.

## Exercise 2 (concept) ★☆☆　Why a window? Where does leakage come from?

1. In your own words, explain spectral leakage: why does a "pure" 1 kHz sine have energy in bins other than 1 kHz after the FFT?
   Hint: the DFT implicitly assumes the frame "repeats periodically"; imagine joining the end of the frame to its start.
2. Why is there no visible leakage when a whole number of periods exactly fills the frame? (the second table of `l11_window_leakage`)
3. After applying a window, why does the peak reading get smaller? How do you compensate? (coherent gain)
4. You want to find a −90 dB 1.2 kHz noise component (for example a harmonic of hum) next to a 0 dBFS 1 kHz tone. Which window should you use?
   What if you want to resolve two equal-sized sines at 1000 Hz and 1010 Hz?
5. Can zero-padding (appending zeros to the frame and doing a larger FFT) improve "frequency resolution"? What does it actually do?

## Exercise 3 (coding) ★★☆　Build a true-peak meter reusing Oversampler, and compare it with Chapter 05's windowed-sinc version

Starter code: `exercises/starter/ex3_true_peak.cpp` (target `l11_ex3_starter`)

Chapter 05 Exercise 4 already built a 4x true-peak meter from scratch (64-tap Hann windowed-sinc, 4 phases). This exercise does not redo it,
but approaches it from two new angles:

1. **Reuse**: Chapter 06's `dsp::Oversampler` (two 2x halfband stages) is already a ready-made, tested 4x upsampler.
   Complete `TruePeakMeter::process()` (TODO 1): call `upsample()` on each block and find the maximum |x| over the 4x-long view.
   It only measures and does not output, so no `downsample()` is needed.
2. **Compare**: the starter code already includes the Chapter 05 Exercise 4 windowed-sinc version (`WindowedSincTruePeak`, already complete) and the FFT "ideal interpolation",
   and feeds the same signal to both meters. You complete `multipliesPerInputSample()` (TODO 2): compute the number of multiplications per input sample from each Oversampler stage's
   `getNumOddTaps()`. Then read the printed table and answer the questions below.

The self-check verifies:
- 1 kHz at 0 dBFS → 0 dBTP; a sine at fs/4 with 45° phase: sample peak = -3.01 dBFS, and the Oversampler reads 0 dBTP
- Phase sweep (fs/4 sine, phase 0°–90°): both meters underestimate by about 0.17 dB at worst, but at different worst phases
- Frequency response near Nyquist: the Oversampler is within 0.02 dB all the way to 22 kHz, while the windowed-sinc clearly underestimates at 22 kHz
- Hard-clipped drum kit and plucked-string material: the gap between each meter and the FFT ideal interpolation; readings are correct after normalizing to -1 dBTP
- Multiplication counts: Oversampler 56, windowed-sinc 64; actual timings are also printed (printed only, not checked, since the numbers depend on the machine and build settings)

Questions:
1. In the phase sweep table, at which "peak positions" does the Oversampler measure accurately, and at which does the windowed-sinc? Explain using where the two interpolation points
   fall between the original samples (README section 2.5). Why is the worst-case error the same size for both?
2. At 22 kHz, Chapter 06 says the first halfband stage attenuates 0.65 dB in a single pass, so why does the Oversampler's reading show almost no error?
   (Hint: the "Passband edge" paragraph of Chapter 06 section 2.6, $|H(f)| + |H(f_s - f)| = 1$.) Why doesn't the windowed-sinc have this property?
3. The multiplication counts differ only 56 vs 64 (about 14%), yet the measured times differ by several times. Compare the inner loops: is accumulation in float or double?
   How is the history buffer accessed (contiguous memory from a mirrored ring buffer vs checking for wraparound on every tap)? Which style is easier for the compiler to vectorize?
4. If you wanted to display true peak in real time on a DAW's master bus, which would you choose? If you were building the true-peak limiter described in Chapter 05 section 2.8
   (upsampling in the sidechain), which has the smaller latency? (Hint: the halfband latency is in Chapter 06 section 2.6;
   the group delay of the 64-tap FIR is 31.5 4x-samples.)

## Exercise 4 (coding) ★★☆　VU meter ballistics

Starter code: `exercises/starter/ex4_vu_meter.cpp` (target `l11_ex4_starter`)

A VU meter is a needle with mass and damping: when a 0 VU sine suddenly appears, the needle must reach 99% of its final reading in 300 ms
and overshoot by 1–1.5%. Model it with a second-order system:

$$\ddot p + 2\zeta\omega_n \dot p + \omega_n^2 p = \omega_n^2\, u(t),\qquad u = |x| \cdot \text{inputScale}$$

Complete two TODOs: the per-sample semi-implicit Euler integration, and the calibration scale for "a -18 dBFS sine = 0 VU".

The self-check verifies: at 300 ms the needle is at 99% ± 0.5% of the final reading, overshoot is 1–1.5%, -24 dBFS → -6 VU,
and a short 10 ms burst cannot move the needle (< -12 VU).

## Exercise 5 (coding) ★★★　Loudness Range (LRA, EBU Tech 3342)

Starter code: `exercises/starter/ex5_loudness_range.cpp` (target `l11_ex5_starter`)

LRA = the 95th percentile minus the 10th percentile of the short-term loudness distribution (after the -70 LUFS absolute gate and the
-20 LU relative gate). The starter code already uses this chapter's `LoudnessMeter` to take a short-term value every 100 ms and put it in a histogram;
you complete `computeLra()`.

The self-check verifies EBU Tech 3342-style test signals: sines at two levels, the first 20 s and the last 20 s,
-20/-30 dBFS → 10 LU, -20/-15 → 5 LU, -40/-20 → 20 LU (each ±1 LU); a constant level → 0 LU.

## Exercise 6 (coding) ★★★　Fast convolution with FFT overlap-add

Starter code: `exercises/starter/ex6_fft_convolution.cpp` (target `l11_ex6_starter`)

The convolution theorem: convolution in time = multiplication in frequency. Zero-pad each input block to $N \ge B + L - 1$, take the FFT, multiply by the IR's spectrum, take the IFFT,
and add the tail that extends beyond the block onto the next block (overlap-add). Complete `OverlapAddConvolver::process()`.

The self-check verifies: the error against direct convolution is < 1e-5 (it must also be right when block lengths vary), the output for an impulse equals the IR itself,
and it is faster than direct convolution for large blocks. This is the foundation of Chapter 12's convolution reverb.

## Exercise 7 (coding) ★★☆　Stereo correlation meter

Starter code: `exercises/starter/ex7_correlation_meter.cpp` (target `l11_ex7_starter`)

$$r = \frac{E[L R]}{\sqrt{E[L^2]\,E[R^2]}}$$

Estimate the three expectations with an exponential moving average (time constant 300 ms). Complete `process()`.

The self-check verifies: L = R → +1; L = −R → −1; sin / cos → 0; independent noise on left and right → about 0;
common : independent components = 1 : 1 → 0.5; when a mono signal suddenly turns anti-phase, the time to cross 0 = τ ln 2 ≈ 208 ms.
