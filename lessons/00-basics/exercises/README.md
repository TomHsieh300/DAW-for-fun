# Chapter 00 Exercises

**English** | [繁體中文](README.zh-TW.md)

Difficulty: ★☆☆ basic / ★★☆ intermediate / ★★★ advanced. The implementation problems (4 to 7) have compilable starter code in `starter/` with `// TODO` markers; running it before you finish prints `[FAIL]`, which is expected. When you are done, every `DAWLAB_CHECK` must pass. The solutions are in [`../solutions/README.md`](../solutions/README.md).

---

## Exercise 1 (★☆☆, calculation): dB and amplitude conversion

Using nothing but a calculator, write out the working and the result (to two decimal places).

1. What is the level, in dBFS, of a sine wave with amplitude 0.25?
2. What linear ratio does -3 dB correspond to? What about +9.54 dB?
3. How many dB is amplifying a signal by a factor of 3?
4. What is the smallest non-zero sample of 16-bit PCM (1/32768) in dBFS? And of 24-bit (1/8388608)? How do these two numbers relate to the SNR formula in section 2.3?
5. Take two signals that both have an RMS of -12 dBFS. What is the **RMS** after adding them, in dBFS, for (a) the same sine wave duplicated, and (b) two uncorrelated noise signals (or two sine waves of different frequencies)?
6. The chapter 14 DAW has 8 tracks. Suppose every track has an RMS of -18 dBFS: (a) if the 8 tracks are mutually uncorrelated, what is the RMS of the mix in dBFS? (b) If the 8 tracks are actually copies of one signal, what is the mix in dBFS? What happens?

Hints: $`20\log_{10}`$, $`10^{dB/20}`$; identical signals add in amplitude, uncorrelated signals add in power.

## Exercise 2 (★☆☆, calculation + concepts): latency and block size

1. What is the one-way latency of block size 256 @ 44.1 kHz, in ms? And of 128 @ 96 kHz?
2. To get one-way latency ≤ 3 ms @ 48 kHz, what is the largest block size (powers of 2 only)? What latency does it give, in ms?
3. For 512 @ 48 kHz, what is the minimum round-trip latency of one input block plus one output block, in ms? Why is the number shown by a real DAW usually larger?
4. A limiter needs 5 ms of lookahead (chapter 05). How many samples is that at 48 kHz? And at 44.1 kHz (rounded to an integer)?
5. Concept: in your own words, explain why the audio callback must not (a) `new` a `std::vector` or (b) take a `std::mutex`. Give one concrete scenario of "what would happen" for each.
6. Concept: why does every example in this course process in blocks of 512 instead of processing the whole signal at once? What bug does this catch in a *stateful* DSP component (a filter, say)?

## Exercise 3 (★★☆, calculation + concepts): aliasing

1. What does 30 kHz @ 48 kHz alias to, in Hz? (The README already worked it out; derive it again yourself.)
2. What does 25 kHz @ 44.1 kHz alias to, in Hz?
3. What about 50 kHz @ 48 kHz? (Hint: $`k`$ can be any integer.)
4. A 5 kHz tone passes through a distortion that generates the 7th harmonic (35 kHz). What does it become at 48 kHz? If you first oversample 4× to 192 kHz and then apply the distortion, does this harmonic still alias? What must you do next to get safely back down to 48 kHz?
5. Concept: why must there always be an anti-aliasing lowpass filter in front of an ADC? Why did the CD choose 44.1 kHz rather than exactly 40 kHz?
6. Proof: sine waves at 30 kHz and 18 kHz, sampled at 48 kHz, produce samples that differ only by a minus sign. (Hint: $`\sin(2\pi n - \theta) = -\sin\theta`$.)

## Exercise 4 (★☆☆, implementation): normalize and clipping count

Files: `starter/ex4_normalize.cpp` (target `l00_ex4_starter`), solution `../solutions/ex4_normalize.cpp` (`l00_ex4_solution`).

Implement two functions:

- `int countClippedSamples(const AudioBuffer&, float threshold = 1.0f)`: the number of samples with $`|x| \ge`$ threshold across all channels.
- `float normalizeToPeakDb(AudioBuffer&, float targetPeakDb)`: scale the whole buffer so its peak lands on `targetPeakDb`, with **every channel multiplied by the same gain** (so the left/right balance does not change), and return the gain applied (in dB). A completely silent buffer is left alone and 0 is returned (it must not produce NaN).

The self-check verifies: amplitude 0.25 → -1 dBFS needs +11.04 dB; a stereo signal with unequal left/right levels keeps its left/right ratio after normalizing; a silent buffer produces no NaN; a 1 kHz sine of amplitude 1.5 at 48 kHz has **26000** samples beyond ±1 in one second (think about why it is 26000); and the drum loop is normalized to -1 dBFS and written to `l00_ex4_normalized.wav`.

Hints: `dawlab::peakAbs`, `dawlab::dbToGain`, `dawlab::gainToDb`, `AudioBuffer::applyGain`.

## Exercise 5 (★★☆, implementation): stereo ↔ mono downmix / upmix (mind the gain)

Files: `starter/ex5_downmix.cpp` (`l00_ex5_starter`), solution `../solutions/ex5_downmix.cpp` (`l00_ex5_solution`).

- `AudioBuffer downmixToMono(const AudioBuffer& in, float channelGain)`: $`\text{mono}[i] = g \sum_{ch} \text{in}[ch][i]`$.
- `AudioBuffer upmixToStereo(const AudioBuffer& mono, int numChannels = 2)`: copy channel 0 into every channel.

The self-check verifies what the two common choices of `channelGain` do on different material:

<div align="center">

| Material | gain 0.5 (-6 dB) | gain 0.707 (-3 dB) |
|---|---|---|
| Left and right identical (sine) | 0 dB (unchanged) | +3.01 dB |
| Left and right uncorrelated (white noise) | -3.01 dB | 0 dB (unchanged) |
| Left and right in anti-phase (L = -R) | vanishes completely | vanishes completely |
| Left and right identical and full-scale | no clipping (exactly 1.0) | exceeds 1.0 (1.414) |

</div>

Note: gain 0.5 is guaranteed never to clip for **any** material ($`|0.5(L+R)| \le 0.5(|L|+|R|) \le 1`$); gain 0.707 only exceeds 1.0 when left and right are sufficiently correlated (same sign) and large enough; full-scale material that is anti-phase or uncorrelated does not necessarily exceed it.

In addition, stereo → mono → stereo must be lossless for material whose left and right are identical. The outputs are `l00_ex5_mono.wav` and `l00_ex5_stereo_again.wav`.

Think about it: why do most DAWs use -6 dB rather than -3 dB for the mono button? When would you choose -3 dB?

## Exercise 6 (★★☆, implementation): linear vs equal-power fade and crossfade

Files: `starter/ex6_fade.cpp` (`l00_ex6_starter`), solution `../solutions/ex6_fade.cpp` (`l00_ex6_solution`).

Implement three functions:

- `float fadeGain(float t, FadeShape shape)`: the fade-in gain for $`t \in [0,1]`$. Linear: $`g = t`$; EqualPower: $`g = \sin(t\pi/2)`$.
- `void applyFadeIn(AudioBuffer&, int numSamples, FadeShape)`: multiply the first `numSamples` samples by $`g(i/(n-1))`$.
- `void applyFadeOut(AudioBuffer&, int numSamples, FadeShape)`: multiply the last `numSamples` samples by $`g(1 - i/(n-1))`$.

The starter code already provides `crossfade()` (a fade-out plus b fade-in, added together) and the self-check. What is verified:

- Equal-power satisfies $`g_{in}^2 + g_{out}^2 = 1`$ at every position (power conserved); linear gives only 0.5 at the midpoint.
- Crossfading two **uncorrelated** noise signals: linear dips by **-3.01 dB** in the middle (an audible "hole"), while equal-power stays at **0 dB** in the middle.
- Crossfading two **identical** signals: linear is exactly 0 dB (0.5 + 0.5 = 1), while equal-power actually gains **+3.01 dB**.

The outputs are `l00_ex6_crossfade_linear.wav` and `l00_ex6_crossfade_equal_power.wav`; look at the difference in waveform envelope in Audacity.

Think about it: a DAW's default crossfade is usually equal-power, but crossfades at an *edit point within the same recording* often use linear. Why?

## Exercise 7 (★☆☆, implementation): interleave / deinterleave

Files: `starter/ex7_interleave.cpp` (`l00_ex7_starter`), solution `../solutions/ex7_interleave.cpp` (`l00_ex7_solution`).

- `std::vector<float> interleave(const AudioBuffer&)`: `out[frame * numChannels + ch] = buffer[ch][frame]`.
- `AudioBuffer deinterleave(const float* data, int numChannels, int numFrames)`: the reverse.

The self-check uses numbers whose position is easy to read (L = 0 1 2 3, R = 100 101 102 103 → 0 100 1 101 2 102 3 103), mono, 6 channels, and a stereo drum loop that must survive the round trip bit-exactly.

Mind `/W4`: compute indices in `size_t` to avoid signed/unsigned comparison warnings.

Think about it: why does DSP use non-interleaved while drivers / files use interleaved? In the chapter 14 DAW, at which layer does this conversion happen?
