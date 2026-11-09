# Chapter 16 - Solutions
**English** | [繁體中文](README.zh-TW.md)

[Back to the chapter](../README.md) | [Exercises](../exercises/README.md)

Run all solution programs with `.\scripts\build.ps1 -Only 16-automation -TestLabel solution` (add
`-Juce` for Exercises 7-9). Every one ends with `[PASS] l16_exN (... checks passed)`.

---

## Exercise 1 - Who may touch a curve?

1. **A mutex.** The audio thread reads the curve in every block. If the UI held a lock while editing
   (and inserting a point may allocate), the audio thread would wait for a lower-priority thread:
   priority inversion, and a dropout when the wait is longer than what is left of the block. With an
   immutable curve the audio thread never waits: it reads whatever curve the pointer gives it, old or
   new, and both are complete. It does not delete the old one because freeing memory may take a lock
   inside the allocator and takes unbounded time; it hands the pointer back, and the UI deletes it
   (Step 5 sends it through the garbage queue).
2. **Who stamps.** The UI only knows roughly where the transport is: it reads a position that was
   true at some block, while the audio it hears left the speakers later, and blocks can be 85 ms long.
   A loop wrap or a seek inside a block makes "the position" two positions. The audio thread knows
   exactly which timeline samples it is rendering, segment by segment (Step 2's `TimelineSegment`s), so
   it stamps the value with the segment it applies to. A wrap then shows up as two events, and the
   recorder ends the pass there.
3. **A short click.** The audio thread reads `touching` once per block. A click that begins and ends
   within one 4096-sample block (85 ms) is never seen as `true`. The counter only goes up, so the next
   block sees a different count and knows a touch happened. Touch does not need it: in Touch the user
   wins only *while* touching, and a click that is already over has nothing left to win. In Latch the
   click should have taken over until the transport stops.
4. **Gone or unknown.** A deleted track's lanes refer to something that no longer exists in *this*
   project, and the user did it: the edit takes them along, its inverse brings them back, and no lane
   can ever point at nothing. An unknown parameter is different: the target exists, but this version
   cannot interpret it - the file probably came from a newer version of the effect. Deleting the lane
   would destroy data the program does not understand, a recording someone made. So it stays, silent,
   and is saved again unchanged.
5. **Latency.** The audio at the fader is 100 samples older than the transport's position: it went
   through the limiter's lookahead. Read at the transport's position, the curve rises 100 samples
   (2 ms) before the audio it belongs to - a drum hit that should start loud starts with its attack at
   the old, lower gain. Reading at $`x_{\text{transport}} - L`$ moves the curve with its audio.
6. **One value per block.** The price: an effect's automation depends on the block size (a change
   takes effect at the next block boundary, up to 512 samples late at 512), and a fast sweep becomes a
   staircase if the effect does not smooth. A choice wants it because a value in between is not a
   ramp: a filter type going from "low pass" to "band pass" through "high pass" for 30 samples would be
   an audible glitch. A switch must change in one step, once.

## Exercise 2 - Automation by the numbers

- **(a)** $`48000 / 512 = 93.75`$ values a second. A 2 s fade moves $`1 / (2 \cdot 93.75) = 0.00533`$ per
  block, which is $`0.00533 \cdot 60 = 0.32`$ dB: a staircase of 0.32 dB steps, 94 times a second, is
  zipper noise. One value per sample moves $`60 / 96000 = 0.000625`$ dB.
- **(b)** The limit is $`1/64`$ per sample. $`0.6 / (1/64) = 38.4`$, so the ramp needs **39 samples**
  (0.8125 ms): 38 full steps reach 0.79375, the 39th lands on 0.8. A jump from 0 to 1 takes exactly
  **64 samples** (1.333 ms).
- **(c)** The line from the first to the last point is $`v = x/6`$ (with $`x`$ in units of 4800). The
  distances are 0.067, 0.113, 0.200, **0.247**, 0.067: point 4 is the farthest and above 0.05, so it
  stays. Left of it, the line from (0, 0) to (4, 0.42) has slope 0.105: distances 0.005, 0.010, 0.015 -
  all below 0.05, dropped. Right of it, the line from (4, 0.42) to (6, 1.00) is at 0.71 at $`x = 5`$:
  the distance 0.19 keeps point 5. **Kept: 0, 4, 5, 6.** Only the two ends stay from a tolerance of
  0.247 on (the program uses 0.25). 0.002 on a 60 dB gain is **0.12 dB**.
- **(d)** $`30 \cdot 93.75 = 2812.5`$, so **2813** points, **45 008 bytes**. A straight fade thins to
  **its two ends**. A slow wobble (0.5 Hz, ±0.2) keeps a few hundred points (365 at 0.002): as many as
  the line needs to stay within the tolerance, wherever it bends.

```text
Exercise 2 - automation by the numbers
(a) 93.75 values a second; a 2 s fade moves 0.005333 a block = 0.320 dB; per sample 0.000625 dB
(b) 0.2 -> 0.8: 39 samples = 0.8125 ms; 0 -> 1: 64 samples = 1.3333 ms
(c) kept: 0 4 5 6
    tolerance 0.002 on a 60 dB fader = 0.120 dB
(d) 30 s: 2813 points = 45008 bytes before thinning
    after thinning (0.002): a straight fade 2 points, a 0.5 Hz wobble 365 points
[PASS] l16_ex2 (13 checks passed)
```

## Exercise 3 - A curved segment with a tension

[`solutions/ex3_curve_shape.cpp`](ex3_curve_shape.cpp)

`curveFraction()` clamps the tension with comparisons that NaN fails (as Step 1's `clamp01()` does),
then computes `pow(t, e)` or `1 - pow(1 - t, e)`. For $`k = 0`$, $`e = 4^0 = 1`$ and `pow(t, 1.0)` is $`t`$
exactly, which is why tension 0 is Linear bit for bit - with the formula $`v_i + (v_{i+1} - v_i)\,f`$
unchanged. `between()` is Step 1's with `f` in place of `t` for `Shape::Curve`.

```text
Exercise 3 - a curved segment with a tension
  a fade with tension 0.5 is at 0.2500 halfway (a line: 0.5000), with -1 at 0.9375
[PASS] l16_ex3 (45 checks passed)
```

**In the file.** The obvious line is `point 1 48000 0.25 curve 0.3`. A DawLab 16 that only knows
`linear` and `hold` fails in `parseShape()` and refuses the **whole project** - the user cannot open a
song because one point has a shape. Better: write the point as before (`point 1 48000 0.25 linear`)
and the tension in a line of its own (`tension 1 48000 0.3`), under a new minor version. An older
version skips the unknown line (chapter 15, Exercise 4), keeps it, and plays the segment as a straight
line: not the same, but close, and nothing is lost.

## Exercise 4 - Trim

[`solutions/ex4_trim.cpp`](ex4_trim.cpp)

1. The break positions: every pass point, and every old point strictly between the pass's ends,
   sorted, each once.
2. The new curve: the old points before `first`, a point at `first` where the old line ends, then for
   each break the sum - twice where the old curve jumps or a hold ends (`valueBefore() != valueAt()`),
   so the jump stays a jump. Then the old value again at `last`, and the old points after it.

`AutomationCurve::add()` clamps every value and keeps points at one position in the order they were
added: that is all the jump handling needs.

```text
Exercise 4 - trim
  a trim over a hold and a jump: off by 6.0e-08 inside, 6.0e-08 outside
[PASS] l16_ex4 (20 checks passed)
```

**Clamping between points.** The old curve goes from 0.5 to 0.9 in one segment and the trim adds 0.3:
the ends become 0.8 and $`\min(1.2, 1) = 1.0`$, and the new line goes from 0.8 to 1.0. But the true
$`\text{clamp}(\text{old} + \text{offset}) = \min(0.8 + 0.4t, 1)`$ reaches 1.0 halfway and stays there.
The line is too low in between. To make it exact, add a point wherever the sum crosses 0 or 1 inside a
segment - where $`v_a + (v_b - v_a)\,t = 1`$ - and the corner is where it belongs.

## Exercise 5 - Copy and paste automation

[`solutions/ex5_clipboard.cpp`](ex5_clipboard.cpp)

- `copyRange()` needs two values the points alone do not give: the curve's value at `start` (which may
  lie in the middle of a segment) and the value the curve reaches at `end` from the left
  (`valueBefore()`: before a jump there, not after it). The point at 0 takes the shape of the segment it
  lies in, so a copy that starts inside a hold holds.
- `pasteChange()` builds the new curve in order: the old points before `at`; the old line's end at
  `at`; the clip; the old curve's value (and shape) at $`at + length`$; the old points after that.
  Points added at one position keep their order, so both edges become jumps where the values differ.
  A target without a lane gets one in Read mode, with the clip's default value.

```text
Exercise 5 - copy and paste automation
  pasted after the end: off by 0.0e+00 outside, 6.0e-08 inside
  pasted over the middle: off by 0.0e+00 outside, 6.0e-08 inside
  pasted at 0: off by 0.0e+00 outside, 6.0e-08 inside
  pasted onto the compressor's threshold: off by 0.0e+00 outside, 6.0e-08 inside
[PASS] l16_ex5 (52 checks passed)
```

The small difference inside comes from interpolating from a new point in the middle of a segment
instead of from the old one: the same line, rounded differently.

## Exercise 6 - Automating a VST3 plugin

One design; others are possible.

1. **Sample accuracy - three ways.**
   - *Split the block* at every change and call the plugin once per piece with the new value: exact for
     Hold steps, but many small calls cost a lot (every call has fixed overhead), and a ramp still needs
     a split every few samples.
   - *Pass the points* through the plugin's own parameter queue (`IParameterChanges` with sample
     offsets). This is what VST3 is made for, and it costs nothing extra, but JUCE's `AudioProcessor`
     wrapper does not expose it to a host: the host would have to talk to the VST3 interfaces itself.
   - *One value per block* and let the plugin smooth, as DawLab does: cheap and simple, but the result
     depends on the block size and on how each plugin smooths.
2. **From a curve to points.** At the block's first sample, the value there; then the value at every
   point of the curve inside the block (two at a jump: before and after); and at the block's last
   sample, the value there. Between those, the curve is a straight line, and a plugin that interpolates
   linearly between queue points hears exactly that. A Hold segment needs its end value repeated just
   before the step, or the plugin would ramp across it.
3. **The plugin's own window.** VST3 plugins announce a gesture: `beginEdit()`, `performEdit()` values,
   `endEdit()`. JUCE passes these on as `AudioProcessorParameter::Listener::parameterGestureChanged()`
   and `parameterValueChanged()`. The host maps begin to `touch()`, each value to `setKnobValue()`, and
   end to `release()` - exactly what DawLab's own faders do - so Touch, Latch and recording work the
   same.
4. **Latency.** A plugin after a 2000-sample lookahead hears audio that is 2000 samples (42 ms) old.
   An unshifted lane moves the parameter 42 ms early: a filter sweep that should open on a hit opens
   before it. The lane should be read at the transport's position minus the latency of everything in
   front of the plugin in its chain (plus the track's PDC, as for the fader), and the recorded gestures
   need the same shift.
5. **Identity.** A VST3 parameter has a `ParamID`, a number the plugin promises to keep across its
   versions - unlike the name, which may be translated or renamed. Store the plugin's class id with
   the effect, and the `ParamID` and the name with the lane: the id to find the parameter, the name to
   show the user something when the plugin is missing.

## Exercise 7 - A line tool for automation lanes

[`solutions/ex7_line_tool.cpp`](ex7_line_tool.cpp)

`drawLine()` is `engine::punch()` with a pass of two points: punch already keeps the curve outside and
puts a jump at both ends where needed. `drawLineInLane()` copies the lane's content (or starts with
`LaneContent{}`, as `Session::addPoint()` does), changes the curve and performs one `SetLane`. The tool
remembers the press, keeps the end inside the lane's bounds while dragging, and on release turns both
ends into samples (`snapToBeat(xToSample(x), !Alt)`) and values (`yToValue()`).

```text
Exercise 7 - a line tool for automation lanes
  a line from 120000 (0.08) to 384000 (0.92): 4 points, undo "Draw line Gain"
[PASS] l16_ex7 (31 checks passed)
```

**Why on top.** A mouse listener sees the events *as well as* the arrangement: a press on an automation
lane would add a point there (the arrangement's own behaviour) and start a line at the same time. A
component on top with `hitTest()` takes the event *instead of* the arrangement - but only where it says
yes, so the clips, the headers and the ruler behave as before.

## Exercise 8 - Touch a control, see its automation

[`solutions/ex8_follow_touch.cpp`](ex8_follow_touch.cpp)

`attach()` moves the old `onTouch` into the new lambda, calls it first, and on a touch (not a release)
calls `touched()` with the parameter's address. `touched()` returns early unless the following is on
and the address is a track that exists; `showAutomation()` replaces what the track showed, and
`setSelectedTrack()` selects it and scrolls it into view (the first version of this solution called
`scrollToTrack()` as well; a mutant that removed it changed nothing, and that is how it was found to be
redundant).

```text
Exercise 8 - touch a control, see its automation
  touched Threshold on track 7: its lane is shown at y 287..359 of 359
[PASS] l16_ex8 (22 checks passed)
```

## Exercise 9 - A lane menu: thin, clear, remove

[`solutions/ex9_lane_menu.cpp`](ex9_lane_menu.cpp)

`thinCurve()` walks the points once. Each anchor closes a run that started at the previous anchor; the
run goes through `engine::thin()` (whose first and last points are those two anchors) and the kept
points are added after the first one. The point after a Hold point must be an anchor: the hold ends
there with a jump, and if the point happens to lie on the line from the hold to the next point,
thinning would drop it and the hold would go on too long. A jump smaller than the tolerance would be
dropped by thinning as well; the user drew it, so it stays.

The menu computes the thinned curve when the menu opens, so the item can say "154 -> 67" and be
disabled when nothing would go.

```text
Exercise 9 - a lane menu: thin, clear, remove
  thinned 154 points to 67, off by at most 0.00199 (tolerance 0.002)
  menu: "Thin Points (154 -> 67)"
[PASS] l16_ex9 (29 checks passed)
```

---

[Back to the chapter](../README.md) | [Exercises](../exercises/README.md)
