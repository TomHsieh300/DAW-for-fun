# Chapter 00 Exercise Solutions

**English** | [繁體中文](README.zh-TW.md)

---

## Exercise 1: dB and amplitude conversion

1. $`20\log_{10}(0.25) = 20 \times (-0.602) = \mathbf{-12.04\ dBFS}`$. (0.25 = 0.5 × 0.5, and 0.5 is -6.02 dB, so twice that is -12.04.)
2. $`10^{-3/20} = 10^{-0.15} = \mathbf{0.708}`$ (≈ $`1/\sqrt2`$). $`10^{9.54/20} = 10^{0.477} = \mathbf{3.00}`$.
3. $`20\log_{10}(3) = \mathbf{+9.54\ dB}`$. (To remember: ×2 = 6.02, ×3 = 9.54, ×4 = 12.04, ×10 = 20.)
4. 16 bit: $`20\log_{10}(1/32768) = -20 \times 15 \times 0.301 = \mathbf{-90.31\ dBFS}`$. 24 bit: $`20\log_{10}(1/8388608) = -20 \times 23 \times 0.301 = \mathbf{-138.47\ dBFS}`$.
   This is $`6.02 \times (N-1)`$: the smallest step of an N-bit signed integer is $`2^{-(N-1)}`$. The SNR formula $`6.02N + 1.76`$ exceeds it by $`6.02 + 1.76 = 7.78`$ dB because (a) the RMS of the quantization error is $`\Delta/\sqrt{12}`$, which is 10.79 dB below $`\Delta`$, and (b) the RMS of a sine wave is 3.01 dB below its peak; $`10.79 - 3.01 = 7.78`$.
5. (a) Identical: amplitude ×2 → RMS +6.02 dB → RMS $`= -12 + 6.02 = \mathbf{-5.98\ dBFS}`$ (≈ -6). Because every sample is exactly doubled, the peak also rises by 6.02 dB.
   (b) Uncorrelated: power ×2 → RMS +3.01 dB → RMS $`= -12 + 3.01 = \mathbf{-8.99\ dBFS}`$ (≈ -9). Note this holds only for **RMS**: you cannot derive the peak of uncorrelated signals with +3 dB, because the peaks of the two signals may happen to pile up with the same sign at some instant (in the worst case the two peaks add directly), or may stay misaligned forever, depending on the actual waveforms.
   `l00_db_math` measures: identical sines +6.02 dB, uncorrelated white noise +3.04 dB (noise is random, so a 0.03 dB difference from theory is perfectly normal).
6. (a) 8 uncorrelated tracks: power ×8 → $`10\log_{10}8 = +9.03`$ dB → $`-18 + 9.03 = \mathbf{-8.97\ dBFS}`$.
   (b) 8 identical tracks: amplitude ×8 → $`20\log_{10}8 = +18.06`$ dB → $`-18 + 18.06 = \mathbf{+0.06\ dBFS}`$. The RMS is already above 0 dBFS and the peak is higher still (a sine wave's peak is 3 dB above its RMS, drums more) -- nothing breaks inside float, but as soon as it is written to an integer WAV or sent to a DAC it clips. This is why the mix bus needs headroom, and why chapter 05 has a limiter.

## Exercise 2: latency and block size

1. $`256 / 44100 = \mathbf{5.80\ ms}`$; $`128 / 96000 = \mathbf{1.33\ ms}`$.
2. $`0.003 \times 48000 = 144`$ samples; the largest power of 2 not exceeding 144 is **128**, with latency $`128/48000 = \mathbf{2.67\ ms}`$. (256 would be 5.33 ms, which is too much.)
3. One input block plus one output block $`= 2 \times 10.67 = \mathbf{21.33\ ms}`$. A real DAW shows more because: the driver usually places another one or two layers of buffering between the hardware and the callback (double / triple buffering); the ADC / DAC filters themselves have a group delay of tens to hundreds of samples; USB interfaces add packet transfer delay; and plugins may report their own latency (for example a limiter's lookahead), which the DAW compensates for by aligning all the tracks (latency compensation).
4. $`0.005 \times 48000 = \mathbf{240}`$ samples; $`0.005 \times 44100 = 220.5 \to \mathbf{221}`$ samples (`msToSamples` uses `std::lround`).
5. (a) **Allocation**: `new` enters the memory allocator, which has a lock inside (the heap is shared among threads), and when the heap has no large enough free space it asks the OS for new pages (a system call that may trigger page faults, or even wait for swap). Scenario: the UI thread happens to be allocating a large image, and the audio thread's `new` waits for it to release the heap lock; after waiting 15 ms, the 10.67 ms deadline of 512 @ 48 kHz has passed, and the user hears a click.
   (b) **Lock**: if the mutex the audio thread wants is held by the UI thread, and the UI thread is swapped out by the OS scheduler (it has low priority), the audio thread can only wait for the UI thread to be scheduled again and release it -- this is priority inversion, and the wait is entirely up to the OS. Scenario: the user is dragging a window and Windows suspends the UI thread for 30 ms; during that time the audio thread is stuck in `mutex.lock()` and playback is interrupted. The fix is `std::atomic` and lock-free queues: whatever the other thread is doing, the audio thread never has to wait.
6. Because this is how real audio systems work: the callback only gets a small block each time, and DSP components must remember "the state at the end of the previous block" (a filter's previous inputs / outputs, the current value of an envelope, the contents of a delay line). When the whole signal is processed at once, this state is naturally continuous, and you would not notice even if every `process()` call reset the state to zero; with block-wise processing, forgetting to save state creates a discontinuity at every block boundary -- a click every 512 samples (about every 10.67 ms at 48 kHz, which sounds like a 94 Hz buzz). The "max difference between block-wise and whole-signal processing" in `l00_block_processing` is how you catch this kind of bug.

## Exercise 3: aliasing

1. $`|30000 - 48000| = \mathbf{18000\ Hz}`$.
2. $`|25000 - 44100| = \mathbf{19100\ Hz}`$.
3. $`k = 1`$: $`|50000 - 48000| = \mathbf{2000\ Hz}`$ (it falls within $`[0, 24000]`$, so this is the one). 50000 for $`k=0`$ and 46000 for $`k=2`$ both exceed Nyquist and are not the answer. Note: 50 kHz is "one whole lap" plus 2 kHz, so it aliases to a very low 2 kHz -- this is nastier than 30 kHz → 18 kHz because it lands in the region where hearing is most sensitive.
4. $`|35000 - 48000| = \mathbf{13000\ Hz}`$ -- completely unrelated to the harmonic series of 5 kHz (5, 10, 15, 20 kHz), so it sounds like "an inharmonic metallic ring".
   After 4× oversampling to 192 kHz, Nyquist is 96 kHz and 35 kHz is below it, so it **does not alias**. But before going back down to 48 kHz you must first use a lowpass filter to remove everything above 24 kHz (this 35 kHz harmonic and all higher ones) and only then keep 1 sample in every 4; otherwise decimation is itself a "sampling" step, and aliases just the same. This is what `downsample()` of the chapter 06 `Oversampler` does.
5. Real-world signals (what a microphone picks up) contain ultrasonic components and high-frequency noise; if they are not filtered out first, they all alias into the audible range when the ADC samples them, becoming noise that can never be removed afterward (once aliased, it is digitally indistinguishable from a genuine 18 kHz signal).
   Why 44.1 kHz and not 40 kHz: an anti-aliasing filter cannot pass 20 kHz completely and block 20.001 kHz completely; it needs a transition band. The 2 kHz from 20 kHz to 22.05 kHz is the transition band left for the filter. (The specific number 44100 comes from the early history of storing digital audio on video recorders: $`3 \times 490 \times 30 = 44100`$.)
6. Let $`f_s = 48000`$ and $`n`$ an integer:
   $$\sin\!\left(2\pi\frac{30000}{48000}n\right) = \sin\!\left(2\pi n \cdot \frac{48000 - 18000}{48000}\right) = \sin\!\left(2\pi n - 2\pi\frac{18000}{48000}n\right)$$
   Because $`\sin(2\pi n - \theta) = \sin(-\theta) = -\sin\theta`$ ($`2\pi n`$ is a whole number of turns),
   $$= -\sin\!\left(2\pi\frac{18000}{48000}n\right)$$
   The two sets of samples differ only in sign, with equal amplitude. The minus sign is just a 180° phase difference, which is inaudible for a single sine wave. The amplitude of 0.5000 that `l00_db_math` measures at 18 kHz is exactly this result.

## Exercise 4: normalize and clipping count

Solution: [`ex4_normalize.cpp`](ex4_normalize.cpp), run `l00_ex4_solution`.

**Key ideas**:

- `countClippedSamples`: two nested loops; for each channel take the pointer with `getReadPointer(ch)`, walk over `getNumSamples()` samples, and `++count` whenever `std::abs(x[i]) >= threshold`.
- `normalizeToPeakDb`: we want $`\text{peak} \times g = 10^{target/20}`$, so $`g = \text{dbToGain(target)} / \text{peak}`$. Use `peakAbs(buffer)` (the maximum over all channels) rather than computing per channel, so that "every channel is multiplied by the same gain". When `peak <= 0`, return 0 immediately -- otherwise you divide by 0 to get Inf, and multiplying back turns it into NaN.

**Why 26000?** 1 kHz at 48 kHz has 48 samples per period, 7.5° of phase apart. $`1.5|\sin\theta| \ge 1 \Leftrightarrow |\sin\theta| \ge 2/3 \Leftrightarrow \theta \in [41.8°, 138.2°]`$ (and the corresponding range in the negative half-cycle). The phases that fall in this interval are $`45°, 52.5°, \ldots, 135°`$, 13 in all; the negative half-cycle has 13 too, so 26 per period; 1000 periods in one second → 26000. The fraction 26/48 = 54.2% is close to the continuous theoretical value $`1 - \frac{2}{\pi}\arcsin(2/3) = 53.5\%`$.

Output:

```
Sine 0.25 -> -1 dBFS: applied +11.04 dB, resulting peak = 0.8913 (-1.00 dBFS)
Stereo: left peak 1.000, right peak 0.500 (ratio 0.500, before normalizing 0.500)
Sine of amplitude 1.5: 26000 / 48000 samples exceed ±1.0 (54.2%)
Drum loop: peak -2.92 dBFS -> applied +1.92 dB -> -1.00 dBFS
[PASS] l00_ex4 (12 checks passed)
```

## Exercise 5: stereo ↔ mono downmix / upmix

Solution: [`ex5_downmix.cpp`](ex5_downmix.cpp), run `l00_ex5_solution`.

**Key ideas**: downmix is "for every channel, `out[i] += gain * x[i]`" -- `AudioBuffer` is zeroed on construction, so you can accumulate straight into it. Upmix copies channel by channel with `AudioBuffer::copyFrom(ch, 0, mono, 0, 0, n)`.

**Choosing the gain**: this is a direct application of the "identical vs uncorrelated" discussion in section 2.5.

- With identical left and right, $`L + R = 2L`$: multiplying by 0.5 restores it exactly (0 dB), and multiplying by 0.707 gives $`1.414L`$ (+3.01 dB).
- With uncorrelated left and right the powers add: $`\text{RMS}(L+R) = \sqrt2 \cdot \text{RMS}(L)`$, so multiplying by 0.5 gives $`0.707`$ (-3.01 dB) and multiplying by 0.707 gives exactly 1 (0 dB).
- With anti-phase left and right: $`L + (-L) = 0`$, which is 0 no matter what you multiply by. This is the "mono compatibility" problem -- stereo wideners and certain microphone placements can make the low frequencies disappear when played back in mono.
- Multiplying by 0.5: $`|0.5(L+R)| \le 0.5(|L|+|R|) \le 1`$, so it **never clips**. Multiplying by 0.707 exceeds 1 when left and right are identical and near full scale.

Output:

```
Identical L/R:     gain 0.5 -> +0.00 dB, gain 0.707 -> +3.01 dB
Uncorrelated L/R:  gain 0.5 -> -3.03 dB, gain 0.707 -> -0.02 dB
Anti-phase L/R:    peak after downmix = 0 (complete cancellation)
stereo -> mono -> stereo: max difference 0
[PASS] l00_ex5 (12 checks passed)
```

**Thinking question**: a DAW's mono button uses -6 dB because it is "absolutely safe" -- no material can clip, and most of the main elements of a mix (lead vocal, kick, bass) sit dead center anyway (left and right identical), so -6 dB leaves their level unchanged. -3 dB suits material whose left and right are "almost completely uncorrelated" (for example two room mics placed far apart), where -6 dB would make it sound 3 dB quieter. Some DAWs let the user choose (pan laws have the same -3 / -4.5 / -6 dB options too; see `Panner` in chapter 01).

## Exercise 6: linear vs equal-power fade

Solution: [`ex6_fade.cpp`](ex6_fade.cpp), run `l00_ex6_solution`.

**Key ideas**:

- `fadeGain`: `switch (shape)`; Linear returns `t`, EqualPower returns `std::sin(t * float(kPi) * 0.5f)`. `kPi` is a `double`, so it needs a `static_cast<float>` to avoid the `/W4` conversion warning.
- `applyFadeIn`: sample $`i`$ has $`t = i/(n-1)`$, so sample 0 gets 0 and sample $`n-1`$ gets exactly 1. For `n <= 1` it is undefined (division by 0), so just return.
- `applyFadeOut`: start from `total - n` and substitute $`1 - t`$.

**Why does equal-power use sin / cos?** Substituting $`1-t`$ into $`\sin(t\pi/2)`$ for the fade-out gives $`\sin((1-t)\pi/2) = \cos(t\pi/2)`$, and $`\sin^2 + \cos^2 = 1`$: at every position the **power** of the two segments sums to 1. For uncorrelated material the powers add, so the total power is conserved and the level sounds unchanged. Linear gives $`0.5^2 + 0.5^2 = 0.5`$ → -3.01 dB at the midpoint, an audible dip.

Conversely, for **identical** material the amplitudes add: linear $`0.5 + 0.5 = 1`$ is exactly right; equal-power $`0.707 + 0.707 = 1.414`$ → +3.01 dB.

Output:

```
fade-in curve  t=0 / 0.5 / 1: linear 0.000 / 0.500 / 1.000, equal-power 0.000 / 0.707 / 1.000
Uncorrelated material, crossfade mid RMS change: linear -3.02 dB, equal-power -0.03 dB
Identical material, crossfade mid RMS change: linear +0.00 dB, equal-power +3.00 dB
[PASS] l00_ex6 (20 checks passed)
```

**Thinking question**: two **different** pieces of material (two songs, two takes) are uncorrelated from each other, so you need equal-power to avoid a dip. But at an *edit point within the same recording*, the waveforms on both sides are nearly identical (correlated) signals near the join -- there equal-power bulges by +3 dB in the middle and only linear stays flat. So a good DAW lets you choose the shape of the crossfade.

## Exercise 7: interleave / deinterleave

Solution: [`ex7_interleave.cpp`](ex7_interleave.cpp), run `l00_ex7_solution`.

**Key ideas**: it is just two index formulas. The interleaved position = `frame * numChannels + ch`:

```cpp
for (int ch = 0; ch < numChannels; ++ch) {
    const float* src = buffer.getReadPointer(ch);
    for (int i = 0; i < numFrames; ++i)
        out[size_t(i) * size_t(numChannels) + size_t(ch)] = src[i];
}
```

Deinterleave is the same formula with the two sides swapped. Compute every index in `size_t` so `/W4` does not complain about signed/unsigned. Since it only moves data and does no arithmetic, the round trip must be bit-exact (max difference = 0).

Output:

```
non-interleaved: L = [0 1 2 3], R = [100 101 102 103]
interleaved:     [ 0 100 1 101 2 102 3 103 ]
Drum loop (2 ch × 96000 frames), max difference after a round trip = 0
[PASS] l00_ex7 (504 checks passed)
```

**Thinking question**: non-interleaved makes "processing one channel" a pointer plus one contiguous loop, which is cache-friendly and easy to SIMD (4 or 8 adjacent samples at a time), and the DSP code for mono and stereo can be exactly the same (only the number of channel-loop iterations differs). Drivers and files use interleaved because a hardware DAC consumes one frame (left and right together) at a time, and when streaming, one frame after another is the most natural. In the chapter 14 DAW, this conversion happens at the **audio device layer**: JUCE's `AudioIODeviceCallback` already converts the driver's interleaved data into `float**` (non-interleaved) before handing it to you, so the engine and all DSP components only ever see non-interleaved; and when reading or writing WAV files, `WavFile.h` (or JUCE's `AudioFormatReader`) does the conversion.
