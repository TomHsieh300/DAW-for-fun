# Chapter 09 Exercises: Delay and Echo

**English** | [繁體中文](README.zh-TW.md)

Difficulty: ★☆☆ basic　★★☆ intermediate　★★★ advanced

The implementation exercises (3, 4, 5, 6) all have starter code `starter/exN_xxx.cpp` that compiles as it is; when run, the self-checks print FAIL, which is expected -- fill in the `// TODO`s until every check turns into PASS and you are done. The solutions are in [`../solutions/README.md`](../solutions/README.md) and `../solutions/exN_xxx.cpp`. The calculation exercises 1 and 7 also have solution programs (`l09_ex1_solution`, `l09_ex7_solution`) that actually run the answers once -- work them out yourself first, then run the program to check.

Build and run (from the project root):

```powershell
.\scripts\build.ps1 -Only 09-delay -NoTest
cd build
.\bin\Debug\l09_ex3_starter.exe      # or l09_ex4_starter, l09_ex5_starter, l09_ex6_starter
```

---

## Exercise 1 ★☆☆ (calculation) Tempo sync and echo decay

1. At 120 BPM, how many ms are a quarter note, an eighth note, a dotted eighth, an eighth triplet and a sixteenth note?
2. How many ms is a dotted eighth at 95 BPM? How many samples is that at 48 kHz? Why does this number show that a delay line needs fractional delay?
3. How many ms are a quarter triplet at 140 BPM, a dotted quarter at 90 BPM, and a sixteenth note at 174 BPM (drum & bass)?
4. Delay 375 ms, feedback 0.5 (no filter in the loop). What are the amplitudes of echoes 1 to 4, and what are they in dB?
5. Derive the time $`T_{60}`$ for the echoes to decay by 60 dB (in terms of $`D`$ and $`g`$). With $`D = 375`$ ms, how many seconds for $`g = 0.5`$ and $`g = 0.7`$? And for $`D = 250`$ ms, $`g = 0.9`$?
6. The other way round: with $`D = 500`$ ms, you want the echoes to decay by 60 dB within 2 seconds. What should the feedback be set to?

## Exercise 2 ★☆☆ (concept) Why feedback ≥ 1 blows up, and why interpolation is needed

1. Use the impulse response to explain the stability condition of a feedback echo. Then explain it again using the pole locations of $`W(z) = z^{-D}/(1 - g z^{-D})`$. What happens when $`g = 1`$ (is it "stable" or "unstable")?
2. With a one-pole lowpass added to the loop, is $`g = 0.99`$ still stable? If the one-pole is replaced by a Q = 4 biquad lowpass (+12 dB at the cutoff), is $`g = 0.5`$ still guaranteed to be stable?
3. Why can a $`\tanh`$ in the loop let you set the feedback to 1.1 without blowing up? What determines the output amplitude in that case?
4. Give at least three situations in which "an integer delay is not enough", and explain what problem you would hear in each if you forced an integer delay.
5. Someone says: "A 400 ms echo doesn't need interpolation at all -- who can hear half a sample of difference?" When is this statement right, and when is it wrong?

## Exercise 3 ★★☆ (implementation) Allpass interpolation

Starter code: `starter/ex3_allpass_interpolation.cpp`

Complete `setDelay()` and `processSample()` of `AllpassInterpolator`:

```math
H(z) = \frac{\eta + z^{-1}}{1 + \eta z^{-1}},\qquad \eta = \frac{1 - \Delta}{1 + \Delta},\qquad y[n] = \eta\,(v[n] - y[n-1]) + v[n-1]
```

Requirements:

- At delay = 10.5, the magnitude response from 100 Hz to 20 kHz is within 0 ± 0.05 dB;
- The phase delay matches the theory $`\tau(\omega) = d + 1 - \frac{2}{\omega}\arctan\frac{\eta\sin\omega}{1 + \eta\cos\omega}`$ (error < 0.02 sample);
- At 100 Hz the phase delays for delays of 10.1, 10.3, 10.5, 10.7 and 10.9 are all accurate (error < 0.01).

Hint: arrange the fractional part in $`[0.5, 1.5)`$ -- `d = floor(delay − 0.5)` -- and $`\eta`$ will not get close to 1. Remember to flush denormals.

Further thought: the solution program prints a table of the magnitude and phase delay of linear / cubic / allpass. Use that table to explain what each is suited for.

## Exercise 4 ★★☆ (implementation) Tape-style delay

Starter code: `starter/ex4_tape_delay.cpp`

Complete `TapeDelay::feedbackPath()`: wet → highpass → lowpass → $`\tanh(\text{drive}\cdot v)/\text{drive}`$ → × feedback. (It can also be written with the chapter 06 `Saturation::shape(SaturationCurve::Tanh, drive * v) / drive`, with the same result -- `shape(Tanh, x)` is itself just `std::tanh(x)`, so you have to do the multiplication and division by drive yourself.)

The self-check verifies:

- (a) delay 200 ms, fb 0.7, LP 2 kHz, HP 100 Hz: the ratio of the second echo to the first is ≈ $`g\,|H_{HP}||H_{LP}|`$ ≈ 0.67 at 500 Hz and ≈ 0.17 at 8 kHz;
- (b) with feedback = 1.1 the output is still bounded (peak < 1.2), and the steady-state amplitude is close to the describing-function prediction (for the intuition behind the describing function see section 2.8 of the main text; the full derivation is in the solution).

Question: why is the tanh "multiplied by drive first, then divided by drive"? As drive increases, does the amplitude of the self-oscillation get larger or smaller?

## Exercise 5 ★★☆ (implementation) Multi-tap delay

Starter code: `starter/ex5_multitap.cpp`

One mono delay line with up to 8 taps, each tap with its own delay, gain and pan (constant-power: $`g_L = \cos\theta`$, $`g_R = \sin\theta`$, $`\theta = (\text{pan} + 1)\pi/4`$). Feedback goes from the last tap back to the input. Complete `process()` and `updateTap()`.

The self-check verifies, with three taps at 120 BPM (dotted eighth panned left, quarter panned right, dotted quarter in the centre):

- With an impulse going in, each tap appears at exactly the right sample, with amplitude = gain × pan coefficient, and the samples immediately before and after are 0;
- The total energy = $`\sum \text{gain}^2`$ (constant-power pan does not change the energy);
- With feedback 0.5 added, tap0 reappears at $`D_\text{last} + D_0`$, with the amplitude multiplied by 0.5.

Question: how does a multi-tap delay differ from "three delays in series"? (Compare in three respects: the time positions of the echoes, CPU, and the high-frequency loss caused by interpolation.)

## Exercise 6 ★★★ (implementation) Switching the delay time with a crossfade

Starter code: `starter/ex6_crossfade_delay.cpp`

Implement the second strategy of section 2.7: two read heads; when a new delay time arrives, put the one "not in use" at the new position, crossfade over 20 ms with a raised cosine ($`g_B = \tfrac12(1 - \cos\pi t)`$, $`g_A = 1 - g_B`$), and swap roles when it finishes; a new value arriving during the fade must be queued.

The self-check uses a 440 Hz sine and switches the delay from 10 ms to 250 ms at t = 1 s, and verifies:

- Within 100 ms after the switch, the largest difference between adjacent samples is < 1.5 times the sine's own maximum slope (no click);
- The frequency measured over ±50 ms around the switch is close to 440 Hz (no Doppler);
- After the fade ends the output is exactly identical to $`x[n - 12000]`$;
- A change during the fade is queued: while a 10 → 100 ms fade is in progress it is changed again to 300 ms; when the fade ends the next one follows, and it finally settles at $`x[n - 14400]`$, with no click anywhere in the process.

Question: during the crossfade, the two read heads read different phases of the same 440 Hz sine. How low does the amplitude of the combined signal drop at the middle of the fade? (Hint: the two heads are 240 ms apart, which is 105.6 periods.) Is this serious for real musical material?

## Exercise 7 ★★☆ (calculation + concept) Comb filters

1. For a feedback comb with $`D = 240`$ samples (48 kHz) and $`g = 0.8`$: at which frequencies are the peaks and how many dB high? At which frequencies are the notches and how many dB low?
2. With the same $`D`$, what about a feedforward comb ($`y = x + 0.8\,x[n - D]`$)?
3. If $`g`$ is changed to −0.8, how do the positions of the peaks and notches change? How does the audible pitch change?
4. At 44.1 kHz you want the pitch of a feedback comb to be A4 = 440 Hz. What should $`D`$ be? If only integer $`D`$ is allowed, by how many cents are the two nearest choices off?
5. Why does white noise through a feedback comb sound pitched? What does this have to do with `makePluck()` (Karplus-Strong) in `Signals.h`?
6. The chapter 12 Schroeder reverb puts several feedback combs in parallel. Why "several", and why should the delay times be chosen as mutually prime numbers?
