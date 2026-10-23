# Chapter 14 - Solutions
**English** | [繁體中文](README.zh-TW.md)

[Back to the chapter](../README.md) | [Exercises](../exercises/README.md)

Run all solution programs with `.\scripts\build.ps1 -Only 14-daw-8track -TestLabel solution`
(add `-Juce` for exercises 8-13).
Every one ends with `[PASS] l14_exN (... checks passed)`.

---

## Exercise 1 - Who may touch what?

1. **The mutex.** While the UI thread holds the lock - and adding an effect means allocating, which
   may itself take the allocator's lock or page memory in from the OS - the audio thread blocks in
   `processBlock()`. The UI thread runs at normal priority; the OS may preempt it at any moment, for
   any length of time, while it still holds the lock. The high-priority audio thread is then waiting
   for a low-priority thread: **priority inversion**. If the wait exceeds the remaining block budget
   (10.67 ms for 512 samples at 48 kHz, minus the actual processing) the device underruns and you
   hear a click or a dropout. It "works on his machine" because the race is rare - which makes it
   worse, not better: it fails on a busy system, during a live show.

2. **Why a garbage queue.** The audio thread unlinks the effect between two blocks, but it must not
   call `delete` itself: the destructor frees memory (allocator, possible lock, possibly unbounded
   time - a convolution reverb frees megabytes). The UI thread cannot delete it right after sending
   `RemoveInsert` either, because it does not know *when* the audio thread has stopped using it: the
   command may still be waiting in the queue, or the audio thread may be in the middle of
   `process()` on that very effect. The garbage queue is the audio thread's way of saying "I am done
   with it" - the only moment when deleting is safe.

3. **Leaking when the garbage queue is full.** The alternatives are: free it on the audio thread
   (forbidden, see 2.), or wait until there is space (forbidden: waiting). Leaking one object is a
   small, bounded, *counted* error (`getGarbageLeaked()`); a dropout is an audible one. It cannot
   happen in practice because every command produces at most one garbage ticket and both queues
   have the same capacity (1024): the garbage queue could only fill up if more than 1024 structural
   commands were applied without a single `collectGarbage()` call - with a 30 Hz UI timer that is
   more than 30 000 edits per second.

4. **Values vs structure.** A fader or parameter value is one number: writing it into an
   `std::atomic` is complete and consistent in one store, and if the UI writes ten values between two
   blocks only the newest matters. Adding an effect needs memory (allocated on the UI thread), a
   `prepare()` call (allocates, may compute FFTs), and several pointer moves in the chain that must be
   seen together by the audio thread - so it must be a message that the audio thread applies at a
   block boundary.

5. **Muted track with a pre-fader send.** You hear nothing from it - neither dry nor through the
   reverb. In this engine mute/solo is applied *before* the send taps (see Step 6). Users expect
   "mute" to silence a track completely; a reverb tail of a muted track would be a surprise, and
   soloing another track would otherwise still let the muted track's reverb through. (Some consoles
   tap pre-fader sends before the mute for monitor mixes; a DAW could offer that as an option.)

## Exercise 2 - Timing and latency by hand

**(a)** $`N_{\text{beat}} = 60 f_s / B = 60 \cdot 44100 / 96 = 27\,562.5`$ samples;
$`N_{\text{bar}} = 3 \cdot 27\,562.5 = 82\,687.5`$ samples.
Bar 5, beat 2: $`(5-1)\cdot 3 + (2-1) = 13`$ beats $`= 13 \cdot 27\,562.5 = 358\,312.5`$ samples, which
rounds to **358 313** (round half away from zero, as `std::llround`), i.e. $`13 \cdot 0.625 = 8.125`$ s.
Sample 1 000 000: $`1\,000\,000 / 27\,562.5 = 36.2812`$ beats; 36 whole beats = 12 full bars, so bar
**13**, beat $36 \bmod 3 + 1 = $ **1**, tick $\lfloor 0.2812 \cdot 960 \rfloor = $ **269**.

**(b)** A quarter note lasts $`60\,000 / 96 = 625`$ ms; a dotted eighth is $`0.75`$ of it:
**468.75 ms** = $0.46875 \cdot 44100 = $ **20 671.875 samples** (a fractional delay - chapter 09's
`FractionalDelayLine` handles the .875). One cycle per quarter note: $96 / 60 = $ **1.6 Hz**.

**(c)** $`\max_t L_t = 1024`$, $`\max_b L_b = 512`$:

| path | latency $`L`$ | compensation |
|---|---|---|
| track 1 | 0 | 1024 - 0 = **1024** |
| track 2 | 64 | **960** |
| track 3 | 240 | **784** |
| track 4 | 1024 | **0** |
| tracks 5-8 | 0 | **1024** each |
| bus A | 0 | 512 - 0 = **512** |
| bus B | 512 | **0** |
| track sum | - | $`\max_b L_b`$ = **512** |

Total latency $= 1024 + 512 + 240 = $ **1776 samples** $= 1776 / 48 = $ **37.0 ms**.

**(d)** Constant power: $`\theta = \frac{\pi}{4}(p+1) = \frac{3\pi}{8} = 67.5°`$;
$`\cos 67.5° = 0.38268`$, $`\sin 67.5° = 0.92388`$; fader $`10^{-6/20} = 0.50119`$.
$g_L = 0.50119 \cdot 0.38268 = $ **0.19180** $= -6 + 20\log_{10} 0.38268 = -6 - 8.34 = $ **-14.34 dB**;
$g_R = 0.50119 \cdot 0.92388 = $ **0.46304** $= -6 - 0.69 = $ **-6.69 dB**.
Balance mode: $g_L = 0.50119 \cdot \min(1, 1 - 0.5) = $ **0.25059 (-12.02 dB)**,
$g_R = 0.50119 \cdot 1 = $ **0.50119 (-6.00 dB)**.

**(e)** Per delay line $(16384 + 1) \cdot 2 \text{ channels} \cdot 4 \text{ bytes} = $
**131 080 bytes**; 12 lines (8 tracks + 2 buses + master + track sum): **1 572 960 bytes = 1.50 MiB**.
Small enough that a fixed, generous maximum is the right trade-off against resizing at run time.

`solutions/ex2_timing_and_pdc.cpp` [`l14_ex2_solution`] checks all of these against `Transport`,
`DelayEffect::noteToMs`, a real `Engine` with `PureDelayEffect`s and `ChannelStrip::computeStripGains`.
Verified output:

```
(a) samples per beat 27562.5, per bar 82687.5
    bar 5 beat 2 = sample 358313 = 8.1250 s
    sample 1000000 = 13:1:269
(b) dotted eighth at 96 BPM = 468.750 ms = 20671.875 samples at 44.1 kHz; quarter-note LFO = 1.600 Hz
(c) track compensation: 1024 960 784 0 | bus: 512 0 | track sum: 512 | total 1776 samples = 37.00 ms
(d) pan law : L 0.19180 (-14.34 dB)  R 0.46304 (-6.69 dB)
    balance : L 0.25059 (-12.02 dB)  R 0.50119 (-6.00 dB)
(e) PDC memory: 131080 bytes per delay line, 1572960 bytes in total (1.50 MiB)
[PASS] l14_ex2 (25 checks passed)
```

## Exercise 3 - Volume and pan automation

Solution: `solutions/ex3_automation.cpp` [`l14_ex3_solution`].

- **`valueAt(t)`**: handle the empty lane and the two ends, then
  `std::upper_bound` finds the first point with time $`> t`$; with its predecessor $`(t_k, v_k)`$:
  $`v = v_k + (v_{k+1} - v_k)\,\frac{t - t_k}{t_{k+1} - t_k}`$ (computed in `double` - timeline
  positions are large numbers).
- **`render(start, out, n)`**: one `upper_bound` for `start`, then for each sample advance the
  iterator while its time $`\le t`$. Cost $`O(\log P + n + P_{\text{block}})`$ instead of $`O(n \log P)`$,
  and no allocation - safe on the audio thread. The self-check compares it with `valueAt` for 5000
  samples across all segments.
- **Sample accuracy**: the effect does not count samples itself. The Mixer passes the block's
  timeline segments (`PlayHead`), so the gain is a pure function of the timeline: block size 512 and
  37 give identical output, and the second pass of a loop equals the first (both differences are
  exactly 0). When the transport stops, the last value is held.
- **Why dB**: a linear-gain ramp from 1 to 0.063 is still at $`0.53`$ (-5.5 dB) halfway: the fade seems
  to do nothing and then drops at the end. A ramp in dB is perceptually even: -12 dB halfway. The
  self-check prints:

```
Exercise 3: volume automation 0 dB @0.5 s -> -24 dB @1.5 s
  t = 0.25 s  level    0.00 dB
  t = 0.50 s  level    0.00 dB
  t = 0.75 s  level   -6.00 dB
  t = 1.00 s  level  -12.00 dB
  t = 1.25 s  level  -18.00 dB
  t = 1.50 s  level  -24.00 dB
  t = 2.00 s  level  -24.00 dB
  block 512 vs block 37: max |difference| = 0
  loop pass 1 vs pass 2: max |difference| = 0
  pan sweep: max deviation of L^2 + R^2 from 0.25 = 2.98e-08 (constant power)
```

- **Pan**: `PanAutomationEffect` uses `dsp::Panner::computeGains` per sample; $`L^2 + R^2`$ stays at
  $`0.5^2`$ throughout the sweep (constant power).
- **Editing a lane while playing**: the lane is immutable inside the engine. Build a new lane, a new
  effect, and swap it in with `Engine::replaceInsert()` (one block boundary, no lock). A production
  design would keep automation in the track (not as an insert) and send new lanes through the
  command queue exactly like clip lists.
- **Control-rate alternative**: setting the strip fader from outside before every block also
  "works", but the fader's 50 ms smoother then behaves like a lag. Each block the ramp restarts and
  covers $`\Delta/T`$ of the remaining distance ($`\Delta`$ = block length, $`T`$ = 50 ms), while the
  envelope moves $`r\Delta`$ ($`r`$ = 24 dB/s); in equilibrium the distance $`d`$ satisfies
  $`d\,\Delta/T = r\Delta`$, so $`d = rT = 24 \cdot 0.05 = 1.2`$ dB - independent of the block size. The
  solution measures it with the real engine (fader set once per 512-sample block):

  ```
    control-rate fader at 1.00 s: -10.73 dB, i.e. 1.27 dB behind the sample-accurate envelope
  ```

  (the small rest comes from the staircase of block-start values and from the smoother ramping
  linearly in gain, not in dB; the self-check accepts 1.2 +- 0.2 dB). From a UI
  timer the result would also depend on timer jitter. Sample-accurate automation inside the audio
  thread is both exact and deterministic.

## Exercise 4 - Save and load a project as JSON

Solution: `solutions/ex4_project_json.cpp` [`l14_ex4_solution`], output
`dawlab_output/l14_ex4_project.json`.

Key points:

- **What to store**: everything a user set, nothing derived. Latencies, compensator delays and
  meters are recomputed; audio lives in files referenced by id (here a `MediaPool` map; in a real DAW
  paths relative to the project folder). The clip's media id is found by reverse lookup of its
  `shared_ptr` in the pool.
- **By name, not by index**: effects by `getName()`, parameters by `ParameterInfo::name`. A newer
  adapter with an extra parameter still loads old projects (missing parameters keep their default,
  unknown names are skipped). A `"version"` field lets a future loader convert old files.
- **Exact floats**: `%.9g` prints enough significant digits for any IEEE-754 `float` to read back as
  the same value (`float` needs at most 9). That is why the self-check can demand both identical
  JSON text after save -> load -> save, and a bit-identical re-render (`max |difference| = 0`).
- **Loading uses the public API only**: `createEffect(name)`, `setParameterByName()`,
  `setBypassed()`, `Engine::addInsert()`, `Engine::setTrackClips()`, strip setters. So loading works
  the same into an engine that is already playing (the commands go through the queue).
- **Errors are messages, not crashes**: unknown effect type -> `unknown effect type 'Compresor'`,
  missing media -> `missing media 'arp'`, syntax errors from the parser with the position.

Verified output (excerpt):

```
Exercise 4: project saved (5717 bytes) to dawlab_output/l14_ex4_project.json
{
  "format": "dawlab-project", "version": 1,
  "sampleRate": 48000,
  "transport": {"tempo": 124, "numerator": 4, "denominator": 4, "loopEnabled": false, "loopStart": 0, "loopEnd": 96000},
  "pdc": true,
  "strips": [
    {
      "kind": "track", "index": 0, "name": "Drums \"live\"",
      ...
  re-render of the loaded project: max |difference| = 0 (peak -2.94 dBFS)
  misspelled effect -> error: unknown effect type 'Compresor'
  missing media     -> error: missing media 'arp'
[PASS] l14_ex4 (18 checks passed)
```

Improvements worth trying: store choice parameters by label ("Peak") rather than by number; write
the file to a temporary name and rename it (an interrupted save never destroys the old project);
keep the convolution IR file name in the effect state.

## Exercise 5 - Moving clips and crossfades

Solution: `solutions/ex5_clip_crossfade.cpp` [`l14_ex5_solution`].

- **Snap**: $`\text{round}(x / G) \cdot G`$ with $`G = N_{\text{beat}} \cdot \text{gridBeats}`$
  (1/16 at 120 BPM: $`G = 6000`$): 25 000 -> 24 000, 27 001 -> 30 000. Snap in `double`, convert once.
- **Move**: copy the clip, erase it, clamp/snap the start, insert at `std::upper_bound` by start
  (stable for equal starts). The engine does not need sorted clips, but an arranger view and a
  project file do. Send the new list with `setTrackClips()`.
- **Crossfade**: `b.start = a.end() - F`, `a.fadeOutSamples = b.fadeInSamples = F`, same shape on
  both. Thanks to Clip.h's fade definition the two fade arguments at overlap index $`k`$ are
  $`\frac{F-k}{F+1}`$ and $`\frac{k+1}{F+1}`$ - they sum to 1 at every sample.

Predictions and measurements (verified output):

```
  Linear     same audio  : level at the middle +0.00 dB, max deviation in the overlap 0.00 dB
  Linear     noise A->B : steady -16.77 dBFS, middle of the overlap -19.79 dBFS (-3.03 dB)
  EqualPower same audio  : level at the middle +3.01 dB, max deviation in the overlap 3.01 dB
  EqualPower noise A->B : steady -16.77 dBFS, middle of the overlap -16.84 dBFS (-0.07 dB)
```

| | same (correlated) audio | independent audio |
|---|---|---|
| Linear ($`g_a + g_b = 1`$) | amplitudes add: $`0.5 + 0.5 = 1`$ -> **0 dB**, perfect | powers add: $`0.5^2 + 0.5^2 = 0.5`$ -> **-3 dB** hole |
| EqualPower ($`g_a^2 + g_b^2 = 1`$) | $`2 \sin(\pi/4) = 1.414`$ -> **+3 dB** bump | power constant -> **0 dB**, perfect |

So: Linear for crossfading a clip with (a part of) itself or with a time-aligned take of the same
source (comping), EqualPower for unrelated material. Many DAWs offer both - now you know why.

## Exercise 6 - Designing per-track input recording

A design that fits this engine:

1. **Input into the engine.** `processBlock()` gets an input block too (the device callback has
   one). Each record-armed track copies its input channel(s) into a **pre-allocated lock-free FIFO**
   (an `SpscQueue<float>` of, say, 10 s of audio, or a ring of fixed-size chunks) - never into a
   growing `std::vector`. The audio thread's job is just a `memcpy`-sized push per block.
2. **The disk thread.** A separate "disk writer" thread (normal priority) pops from the FIFO and
   appends to the WAV file and to an in-memory buffer that grows on *its* thread. When recording
   stops, that thread builds an immutable `AudioBuffer`, wraps it in a `shared_ptr`, and the UI
   creates the clip and sends a new clip list with `setTrackClips()`.
3. **Placement.** What the musician heard at timeline position $`T`$ left the speakers
   $`L_{\text{pdc}} + L_{\text{out}}`$ samples after the engine rendered $`T`$; their playing reaches the
   engine $`L_{\text{in}}`$ later. So the input sample that arrives while the engine renders position
   $`P`$ belongs to timeline position
   $$T = P - (L_{\text{in}} + L_{\text{out}} + L_{\text{pdc}})$$
   i.e. the recorded clip must be placed $`L_{\text{in}} + L_{\text{out}} + L_{\text{pdc}}`$ samples
   *earlier* than the playhead position at which recording started (JUCE reports the device latencies
   via `AudioIODevice::getInputLatencyInSamples()` / `getOutputLatencyInSamples()`). Verify it with a
   loop-back cable: record the metronome and check that the clicks land on the grid.
4. **Disk too slow.** The FIFO absorbs short stalls (that is what its 10 s are for). If it is
   nevertheless full, the audio thread must not wait: it drops the input block, sets an atomic
   "overrun" flag/counter, and the UI tells the user ("disk too slow - recording damaged at
   1:23"). Playback never glitches because of recording.
5. **Monitoring vs PDC.** Monitoring through the track's inserts adds the full PDC delay to what
   the musician hears of *themselves* - 37 ms in Exercise 2's project is unplayable. Options: monitor
   the dry input directly (zero-latency "direct monitoring"), or put the engine in a low-latency
   mode while recording (bypass or disable latent plugins, `setLatencyCompensationEnabled(false)` for
   the monitored path).

## Exercise 7 - Tempo changes and time-stretching

1. **Musical clips** store their position and length in **beats** (or ticks) and a "source tempo"
   of the audio; **absolute clips** store samples. Drum loops, arpeggios, anything grid-locked must
   be musical; a voice-over, a sound effect synced to video, or a live recording with free timing
   should stay absolute. On a tempo change the engine recomputes the sample positions of musical
   clips (start = beatsToSamples(startBeats)) and the required stretch ratio
   $`r = \frac{\text{sourceTempo}}{\text{projectTempo}}`$ (duration factor).
2. **Resampling** changes duration *and* pitch together ($`r = 1.2`$ also transposes by
   $`12 \log_2 1.2 = 3.2`$ semitones) - fine for a "vari-speed" effect, wrong for music that must stay in
   key. **Time-stretching** changes duration only, by cutting the signal into short overlapping
   grains and re-spacing them: **time-domain** methods (OLA / **WSOLA**: overlap-add, with the
   grain position chosen by cross-correlation so the waveforms match) - good on speech and
   monophonic material, artefacts: doubling / stutter on transients, "warble"; **frequency-domain**
   methods (**phase vocoder**: STFT with the FFT of chapter 11, phases advanced per bin for the new
   hop size) - good on polyphonic material, artefacts: "phasiness" / smearing of transients, a
   reverberant sound. Production stretchers combine both with transient detection.
3. **Where to run it.** A good stretch needs analysis windows of tens of ms (latency) and much CPU.
   Real-time safe design: a **background thread renders stretched audio ahead of the playhead** into
   a cache (per clip, per tempo), and the audio thread only plays cached buffers (immutable, swapped
   in like clip lists). When the tempo changes, the UI sends a "new tempo" message; until the new
   cache is ready, the engine plays the old one (or a cheap real-time stretch as a fallback),
   then switches at a block boundary with a short crossfade. Offline bounces simply wait for the
   high-quality render.
4. **Tempo map.** Store tempo segments sorted by time:
   $`(t_i^{\text{samples}}, \text{beat}_i, \text{bpm}_i)`$ for each change. For a timeline sample $`t`$:
   binary-search the last segment with $`t_i \le t`$, then
   $`\text{beat} = \text{beat}_i + (t - t_i) \cdot \text{bpm}_i / (60 f_s)`$ (constant tempo inside a
   segment; ramps integrate the tempo instead). The inverse is the same search on the `beat` column.
   A sorted `std::vector` + `std::upper_bound` is $`O(\log n)`$, and because the playhead moves forward,
   a cursor cached between blocks makes it $`O(1)`$ - the same trick as Exercise 3's `render()`.

---

# Optional projects (Exercises 8-13): the application (JUCE)

All six solutions are JUCE console programs linked against the app library; they build the real
`MainComponent` in its offline mode (no device, no window) and check everything with numbers.
Outputs below are from the author's Debug build.

## Exercise 8 - An automation lane in the GUI

Solution: [`solutions/ex8_automation_lane.cpp`](ex8_automation_lane.cpp) (`l14_ex8_solution`).

- **Mapping.** $`y(v) = (1 - p)\,h`$ with $`p = (v - v_{\min}) / (v_{\max} - v_{\min})`$, and the
  inverse $`v(y) = v_{\max} - (y/h)(v_{\max} - v_{\min})`$. The x axis is *not* the lane's own: the
  lane sits under the arrangement and uses `ArrangementView::xToSample()` / `sampleToX()` /
  `snapToBeat()`, so a point drawn at a bar line is on that bar line in the audio.
- **Gestures.** A click first hit-tests (distance to each point's centre at most 6 px); a hit only
  selects. Otherwise the new point is inserted at `std::upper_bound` - the list stays sorted, so
  `valueAt()` can binary-search it. Dragging clamps the time into $`[t_{k-1}, t_{k+1}]`$: re-sorting
  while dragging would make the selected index jump to another point under the user's mouse, and
  the "move" would turn into "push other points around". Every edit publishes a new immutable
  `AutomationLaneData`.
- **Audio.** `valueAt()` interpolates linearly *in dB* between breakpoints, so halfway between 0 and
  -24 dB the gain is exactly $`10^{-12/20}`$. The effect evaluates it per sample from
  `segment.timelineStart + i`, so loops and seeks need no special code.
- **Hand-over.** The UI exchanges the new lane into `pending_` and deletes whatever it got back
  (the audio thread never saw it). The audio thread exchanges `pending_` with `nullptr`, retires its
  current lane into an `SpscQueue` and keeps the new one; the UI deletes retired lanes. No lock, no
  `delete` on the audio thread, nothing leaks.

```
lane 0 dB @0, -24 dB @24000, -24 dB @72000: level at 12000 = -12.00 dB, at 60000 = -24.00 dB
block size 512 vs 37: max difference 0
after removing the middle point: level at 36000 = -12.00 dB
lane objects alive after 200 publishes: 1 (1 = only the one in use)
[PASS] l14_ex8 (18 checks passed)
```

It also writes `dawlab_output/l14_ex8_lane.png`. To use it in the app, put an
`AutomationLaneComponent` under the `ArrangementView` (sharing its width) and add an
`AutomatedGainEffect` to a track's first slot.

## Exercise 9 - Recording audio input into a track

Solution: [`solutions/ex9_recording.cpp`](ex9_recording.cpp) (`l14_ex9_solution`).

- `pushInput()`: `AbstractFifo::prepareToWrite(n, start1, size1, start2, size2)` returns up to two
  regions (the second when the ring wraps); copy `size1` samples to `start1` and the next `size2` to
  `start2`, then `finishedWrite(size1 + size2)`. Checking `getFreeSpace() < n` first makes the
  decision all-or-nothing: a partial block would put a gap *inside* a block, a dropped block at
  least keeps the take aligned in whole blocks (and the overrun counter tells the user). An
  `AbstractFifo` of size $`N`$ holds $`N - 1`$ samples, so a 12 000-sample FIFO takes 46 blocks of 256;
  the remaining $`282 - 46 = 236`$ pushes are overruns - the test checks exactly that.
- `drain()`: `prepareToRead(getNumReady(), ...)`, append both regions to the take (a
  `std::vector` that may grow - on the disk thread), `finishedRead()`.
- The **timeline position** of the first recorded sample is captured on the audio thread, from the
  transport position of the block in which recording starts (a pending seek is already accounted
  for by `Transport::getPosition()`). Asking the UI thread "where are we?" would be off by up to a
  block plus scheduling jitter.
- `clipStart = recordStart - (L_in + L_out + L_pdc)` = 96 000 - (64 + 96 + 240) = 95 600 (the
  derivation is in Exercise 6). The PDC of 240 samples is the master limiter's 5 ms lookahead.

```
recorded 72000 of 72000 samples, 0 overruns, max |take - input| = 0
recording started at 96000, latencies in 64 + out 96 + PDC 240 -> clip at 95600
without a disk thread: 282 blocks pushed, 236 overruns (expected 236)
[PASS] l14_ex9 (9 checks passed)
```

A complete recorder would write the take to a WAV file on the disk thread as it grows
(`juce::AudioFormatWriter`), show a growing clip while recording, and get the latencies from
`AudioIODevice::getInputLatencyInSamples()` / `getOutputLatencyInSamples()`.

## Exercise 10 - A MIDI track with a simple synthesizer

Solution: [`solutions/ex10_midi_synth.cpp`](ex10_midi_synth.cpp) (`l14_ex10_solution`).

- `notesFromMidi()`: after `updateMatchedPairs()` every note-on knows its note-off
  (`noteOffObject`). Ticks to samples: $`s = \mathrm{round}(\text{ticks} / 960 \cdot N_{\text{beat}})`$,
  rounded once, at the end (stage 1, Step 2). A note on beat 2 at 120 BPM / 48 kHz starts at
  24 000.
- `noteOn()`: the first voice in stage Off; if none, the voice with the smallest start (the oldest)
  is stolen. A stolen voice restarts at phase 0 and level 0 (a real synth would fade it out over a
  few ms to avoid a click).
- `envelope()`: attack step $`1 / (t_A f_s)`$, decay step $`(1 - S) / (t_D f_s)`$, release step
  $`\ell / (t_R f_s)`$ computed from the level $`\ell`$ at note-off, so a release always lasts $`t_R`$.
- Sample accuracy: `processDsp()` renders up to the next note start inside each PlayHead segment,
  starts the note(s) at exactly that sample and continues. A voice releases when the timeline
  sample reaches its note-off time.
- Jumps: when a segment does not continue the previous one (seek, loop restart), all voices are
  released and the note cursor is re-positioned by binary search. Without it, notes held across the
  loop end would hang forever (their note-off lies "behind" the jump). A *chasing* DAW would also
  start the notes that began before the jump target and are still sounding there; we do not (simple
  and fine for drums, wrong for long pads).

```
MIDI -> 15 notes; first: note 69 at 24000 for 24000 samples
first non-zero output sample: 24001 (note-on at 24000)
A4: 440 Hz -15.0 dB, 466 Hz -72.1 dB
voices during the triad: 3
block size 512 vs 100: max difference 0
loop of bar 1: 440 Hz in pass 1 0.177, pass 2 0.177
[PASS] l14_ex10 (11 checks passed)
```

The first non-zero sample is 24 001, not 24 000: the sine starts at phase 0 and sin(0) = 0. Listen
to `dawlab_output/l14_ex10_synth.wav` (saw wave, 300 ms release). PolyBLEP subtracts a two-sample
polynomial correction at each discontinuity of the naive saw / square, which removes most of the
aliasing a naive saw would fold back below Nyquist (chapter 06 explains aliasing).

## Exercise 11 - Save and open projects from the File menu

Solution: [`solutions/ex11_project_file.cpp`](ex11_project_file.cpp) (`l14_ex11_solution`).

- **Format.** `<DawLabProject version sampleRate tempo ...>` with `<Media id file>` children and one
  `<Strip>` per strip (mixer order) holding `<Send>`, `<Insert>` (with `<Param name value>` or a VST3
  `file` + base64 `state`) and `<Clip>` children. Parameters are stored **by name**: a later version
  of an effect may add or reorder parameters, names survive that; unknown names are skipped on load.
- **Exactness.** Audio: a 32-bit float WAV stores every `float` sample bit-exactly (16 / 24-bit PCM
  would quantize). Numbers: `%.9g` - 9 significant decimal digits are enough to round-trip any IEEE
  float (`FLT_DECIMAL_DIG` = 9), parsed back with `strtof`. Positions are `int64` as text. With
  that, re-rendering after save + open is bit-identical, and saving again gives the same text.
- **Order of loading.** Media first (if a file is missing, the current project is untouched), then
  `newProject()` and clearing every slot, then the strips. A project saved at another sample rate
  is converted with `Session::handleSampleRateChange()` after loading - the synchronous form of the
  conversion a device change runs (both end in `Session::applySampleRateConversion()`; the device
  change resamples on the loader thread first, Step 11).
- **The menu.** `ProjectCommands` is an `ApplicationCommandTarget`; `setExtraCommandTarget()`
  registers its commands (so Ctrl+S / Ctrl+Shift+O become key mappings) and chains it behind
  `MainComponent`; `onExtendMenu` puts the two items at the top of the File menu. The commands open
  a `juce::FileChooser`; the test calls `saveTo()` / `openFrom()` directly.

```
saved ...\dawlab_output\l14_ex11_project.dawlab (32148 bytes of XML, 28 media files)
re-render after save + open: 414508 samples, max difference 0
saving the opened project gives identical XML
[PASS] l14_ex11 (14 checks passed)
```

(28 media files: the demo song's 140 clips use 28 distinct buffers. 414 508 samples = 4 bars at
118 BPM plus 0.5 s of tail.)

## Exercise 12 - A third FX bus

Solution: [`solutions/ex12_third_bus.cpp`](ex12_third_bus.cpp) (`l14_ex12_solution`).

1. **What follows `kNumBuses`:** `ChannelStrip` (send levels, pre/post flags and send smoothers are
   `std::array<..., kNumBuses>`), `Mixer` (bus strips, bus buffers, the per-block array of bus
   blocks), `Engine::allChannels()` (size `kNumTracks + kNumBuses + 1`), `ChannelRef::bus()`
   validity, `DemoSong` (bus names, send table), and in the app `kNumStrips` (`Session.h`), the
   per-strip send knobs and the bus strips in `MixerView`. A compile-time size lets every one of
   these be a fixed array allocated once - nothing to grow, nothing to reallocate while the audio
   thread reads it. A run-time bus count would need all of them to be rebuilt on the UI thread and
   swapped in through a command (like clip lists): a new routing table, new send arrays per strip,
   garbage for the old ones - possible, but it touches every strip at once.
2. **`configureThirdBus()`:** name, `setBuiltinInsert(bus, 0, Chorus)` with Mix 1 / Depth 6 /
   Voices 3, `setBuiltinInsert(bus, 1, ParametricEq)` with band 1 (the low cut) at 200 Hz, then
   `setSendLevelDb(2, -6)` and `setSendPreFader(2, true)` on the Pad and the Lead.

```
mixer strips: 12 (8 tracks + 3 buses + master), send knobs per track: 3
bus C only: master RMS -30.3 dBFS, bus C RMS -32.3 dBFS, Pad strip (fader -inf) peak -100.0 dBFS
sends off: master peak -100.0 dBFS
[PASS] l14_ex12 (13 checks passed)
```

With the Pad and Lead faders at $`-\infty`$ their own strips are silent (-100 dBFS is the meter's
floor), yet bus C carries about -32 dBFS RMS: the sends tap *before* the fader. With the sends off,
the master is exactly 0. `dawlab_output/l14_ex12_third_bus.png` shows the 12-strip mixer.

## Exercise 13 - A keyboard shortcut map

Solution: [`solutions/ex13_shortcut_map.cpp`](ex13_shortcut_map.cpp) (`l14_ex13_solution`).

- `findConflicts()`: for each pair $`(i < j)`$ and each key of command $`i`$, check whether command $`j`$
  has it - $`O(n^2 k)`$ for $`n = 28`$ commands, instant.
- The nudge goes to the previous / next beat *line*: $`\lfloor b \rfloor + 1`$ forward,
  $`\lceil b \rceil - 1`$ back, with $`b`$ the position in beats (a tiny epsilon keeps a position
  exactly on a line from counting as "between"). From 30 000 samples (1.25 beats) forward is 48 000;
  back twice from there is 0.
- `createXml(true)` writes only the differences from the defaults - a `MAPPING` for P and an
  `UNMAPPING` for Space - so a later version of the app can change other defaults without the user's
  file overriding them. `restoreFromXml()` re-applies the defaults first, then the differences.
- **Why no keyboard focus on knobs and buttons:** the key mappings listen on `MainComponent`. A
  focused child gets the key first; a focused `TextButton` would treat Space as a click on itself,
  a focused slider would move with the arrow keys. Refusing the focus guarantees that Space always
  means Play / Stop.

```
28 commands, 0 conflicts in the default map
conflict found: 'L' is on both "Play / Stop" and "Loop"
shortcut editor: 32 rows (4 categories + 28 commands)
saved the user's shortcuts to ...\dawlab_output\l14_ex13_shortcuts.xml:
<KEYMAPPINGS basedOnDefaults="1">
  <MAPPING commandId="2101" description="Start or stop playback" key="P"/>
  <UNMAPPING commandId="2101" description="Start or stop playback" key="spacebar"/>
</KEYMAPPINGS>
[PASS] l14_ex13 (14 checks passed)
```

`dawlab_output/l14_ex13_editor.png` is JUCE's `KeyMappingEditorComponent` for the same mapping set,
with P on Play / Stop. Two details make it render in a console program: the editor fills its tree
only when it gets a parent (`parentHierarchyChanged()`), so it is put into a holder component; and
a `TreeView` lays out its rows asynchronously through the message loop, which does not run here -
`setRootItem(nullptr)` followed by `setRootItem(root)` makes it lay them out at once. The check
`rows == 32` (4 categories + 28 commands) guards against an empty picture.
