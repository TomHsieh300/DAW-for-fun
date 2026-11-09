# Chapter 16 - Exercises
**English** | [繁體中文](README.zh-TW.md)

[Back to the chapter](../README.md) | [Solutions](../solutions/README.md)

Difficulty: ★☆☆ easy, ★★☆ medium, ★★★ hard. Implementation exercises have a starter file in
`exercises/starter/` that compiles and runs; until you complete it, its self-check prints `FAIL`
(that is expected). Build target names are given in brackets. None of them needs JUCE:

```powershell
.\scripts\build.ps1 -Only 16-automation
.\build\bin\Debug\l16_ex3_starter.exe
```

Exercise 3 builds on Step 1's curve, Exercises 4 and 5 on Step 4's model (`exercises/model.h`: Step
4's `minidaw.h` with its edits solved). Do the steps first.

---

## Exercise 1 - Who may touch a curve? ★☆☆ (concept)

Answer in a few sentences each.

1. Step 2 never changes a curve the audio thread may be reading: the UI changes a copy and swaps a
   pointer. Why not a `std::mutex` around the points? And why does `exchangeCurve()` return the old
   curve instead of deleting it?
2. Step 3 lets the audio thread stamp the user's value with a timeline position. The UI thread also
   knows the transport position - why can it not stamp the value itself? Think of a loop, a seek
   and a large buffer.
3. In Latch mode, a quick click on the fader must latch. With a buffer of 4096 samples, why could
   the audio thread miss it, and how does the counter in `touch()` fix that? Why does Touch mode not
   need the counter?
4. Deleting a track takes its lanes with it, inside the same edit. A lane whose parameter this
   version does not know is kept, silent. Why are these two cases treated so differently?
5. A track has a limiter that looks ahead by 100 samples. Its fader's curve rises sharply at bar 9.
   What would the listener hear if the lane were read at the transport's position, and why does Step 5
   read it at the transport's position minus 100?
6. The strip's faders get one value per sample; an effect's parameters one value per block. What is
   the price of the second, and why does a switch (a choice parameter) want it anyway?

## Exercise 2 - Automation by the numbers ★★☆ (calculation)

Compute by hand (show the formulas), then run [`l16_ex2_solution`] to check. For (a) and (c), the
gain's normalized value maps to -60..0 dB, linear in dB.

- (a) A fade from 0 to 1 over 2 s at 48 kHz, read once per 512-sample block (chapter 15). How many
  values a second, how far does the value move per block - normalized and in dB? And with one value
  per sample?
- (b) Step 2's slew limit with `declickSamples = 64`. How many samples, and how many milliseconds, does
  a jump from 0.2 to 0.8 take? A jump from 0 to 1?
- (c) A recorded pass, one point every 4800 samples: values 0.00, 0.10, 0.22, 0.30, 0.42, 0.90, 1.00.
  Thin it by hand with Step 3's rule at a tolerance of 0.05: which points stay? At which tolerance
  would only the two ends stay? What is Step 3's default tolerance of 0.002 in dB on this gain?
- (d) A 30 s fader ride, one capture event per block. How many points before thinning, and how many
  bytes at 16 bytes a point? What do you expect after thinning for a straight fade, and for a slow
  wobble?

## Exercise 3 - A curved segment with a tension ★★☆ (implementation)

[`l16_ex3_starter`, file `exercises/starter/ex3_curve_shape.cpp`]

Add a third shape to Step 1's curve: `Shape::Curve`, bent by the point's tension $`k`$ in -1..1.
With $`t`$ the position inside the segment (0..1) and $`e = 4^{|k|}`$:

```math
f(t) = \begin{cases} t^{e} & k \ge 0 \\ 1 - (1-t)^{e} & k < 0 \end{cases}
\qquad v = v_i + (v_{i+1} - v_i)\, f(t)
```

- `curveFraction(t, tension)`: $`f(t)`$, the tension clamped, NaN treated as 0;
- `AutomationCurve::between()`: Step 1's, with the new shape. `valueAt()` and `render()` both call it.

The self-check requires $`f(0) = 0`$, $`f(1) = 1`$, a curve that only rises, the mirror rule
$`f_{-k}(t) = 1 - f_k(1-t)`$, tension 0 equal to Linear **bit for bit**, values that never leave the
range of their segment, a jump followed by a curve, and `render()` equal to `valueAt()` for blocks of
1, 37, 512 and 4096 samples.

Think about it: Step 4's file writes a point as `point 1 48000 0.25 linear`. How would you write a
curve with its tension? What does a DawLab 16 that only knows `linear` and `hold` do with such a file -
and what could it do instead (chapter 15, Exercise 4)?

## Exercise 4 - Trim ★★★ (implementation)

[`l16_ex4_starter`, file `exercises/starter/ex4_trim.cpp`]

Touch, Latch and Write replace the curve with what the hand did. A mix engineer who likes a vocal ride
but wants the second chorus 0.2 higher needs a fifth mode, **Trim**: the fader becomes an offset, and
the recorded pass is *added* to the curve instead of punched into it. Write the merge, on Step 4's
curve:

- `applyTrim(old, pass)`: from the pass's first to its last position, the value is
  $`\text{clamp}_{0..1}(\text{old}(p) + \text{offset}(p))`$; outside, the old curve. Between two
  points, both the old curve and the offset are straight lines, so the new curve needs a point wherever
  either has one, and two where the old curve jumps or a hold ends.
- `trimChange(project, target, pass)`: the same as one undoable `SetLane`.

The self-check trims across a hold and a jump (the hold stays flat where the offset is flat, the jump
stays a jump), starts and ends on points of the old curve and on its jump, clamps a trim that goes too
far, and undoes a trim.

Think about it: the self-check only asks for exact values at the points when a trim goes past 1. Why
is the curve between two clamped points not always $`\text{clamp}(\text{old} + \text{offset})`$? What
would it take to make it so?

## Exercise 5 - Copy and paste automation ★★☆ (implementation)

[`l16_ex5_starter`, file `exercises/starter/ex5_clipboard.cpp`]

Select bars 5-8 of a curve, copy, paste at bar 17 - or onto another parameter.

- `copyRange(curve, start, end)`: the curve on $`[start, end)`$ as a `CurveClip`: a point at 0 with the
  value at `start`, the points inside moved by $`-start`$, a point at the length with the value the
  curve reaches at `end`.
- `pasteChange(project, target, clip, at)`: the `Change` (one `SetLane`) that puts the clip at `at`.
  Before `at` and from $`at + length`$ on, the old curve stays - so the paste may need a jump at both
  ends, and a point in the middle of an old segment. A lane that does not exist yet is created.

The self-check copies a piece with a hold and a jump inside, pastes it after the end, over the middle,
at 0 and onto a compressor's threshold that had no lane, and compares the result with the old curve
outside and the source inside, sample by sample. Every paste is one undo step, and undo gives back
the project exactly.

## Exercise 6 - Automating a VST3 plugin ★★☆ (design)

DawLab 16 does not offer a VST3 insert's parameters as targets yet (Step 6, "What this step leaves
open"), and the built-in effects get one value per block (Step 5). Design, in writing, what proper
plugin automation needs. Cover:

1. **Sample accuracy.** A VST3 plugin receives parameter changes as a queue of points with sample
   offsets inside the block (`IParameterChanges`). JUCE's `AudioProcessor` interface sets a value per
   block. What are the three ways to get closer to sample-accurate automation, and what does each one
   cost?
2. **From a curve to points.** A lane's curve over one block is Linear and Hold segments. Which
   points must go into the plugin's queue so that it hears the same curve, if it interpolates linearly
   between them?
3. **Recording from the plugin's own window.** The user turns a knob in the plugin's editor, not in
   DawLab. How does the host learn that a touch began and ended, so that Touch and Latch work?
4. **Latency.** Step 5 shifts a fader's lane by the latency before it, but leaves effects' lanes
   alone. What goes wrong for a plugin late in a chain with 2000 samples of lookahead in front of it?
5. **Identity.** Step 4 names parameters by name. What do VST3 plugins offer instead, and what
   should the project file store?

---

[Back to the chapter](../README.md) | [Solutions](../solutions/README.md)
