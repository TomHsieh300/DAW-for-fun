# Chapter 18 - Solutions
**English** | [繁體中文](README.zh-TW.md)

[Back to the chapter](../README.md) | [Exercises](../exercises/README.md)

Run all solution programs with `.\scripts\build.ps1 -Only 18-midi -TestLabel solution`. Every one ends
with `[PASS] l18_exN (... checks passed)`.

---

## Exercise 1 - Why so careful with notes?

1. **Ticks.** A note in samples stays where it was when the tempo changes: the drums (in ticks, or
   following the grid) move, the notes do not, and the song falls apart. To follow, every note would have to
   be rewritten - and rounded again each time, so that a few tempo changes back and forth leave the notes
   somewhere else. In ticks a tempo change touches nothing but the conversion. That conversion is computed
   from the tick every time because a sum drifts: every addition rounds, and the errors add up. Exercise 2
   (a) measures a `float` sum after an hour: four minutes off.
2. **Offs, controllers, ons.** A note played again right where it ends: if the new note on came first, the
   synth would start the note and the old note's off would end it at once - silence where a note should be.
   Controllers between: a pianist changes the pedal *between* two chords, and in a recording the old chord's
   end, the pedal and the new chord often share a tick. Offs first end the old chord; the pedal comes down;
   the new chord starts under it. With the pedal first, the old chord would be held into the new one - mud.
   The same for a pitch bend reset at the start of a note: after the note on, the note's first samples
   would have the old bend.
3. **Chasing a note.** The note would have to start in the middle - but a synth plays every note from its
   attack, so a note "chased" from bar 3 into bar 5 would sound like a new note at bar 5, at a place where
   none was played. To be right, the instrument would have to render the note from its start silently
   until the position, which is expensive, and a sampled piano or a drum hit should not restart at all.
   Many DAWs make note chasing an option for this reason, for long pads and strings; Step 2 leaves it out.
4. **Stealing.** Cutting a sound at once is a jump in the waveform, and a jump is a click that every ear
   hears, with energy at all frequencies. 2 ms of delay on a new note is far below what anyone hears.
   Dropping the new note would be the worst choice: it is the note the player just played and is listening
   for, while the stolen one is the least missed - released, or the oldest.
5. **Scheduled.** A constant delay can be learned: a player adapts to an instrument that always answers
   10 ms late, as an organist adapts to a pipe organ. Jitter cannot be learned: two notes played exactly
   together come out apart, and a steady rhythm comes out uneven. And the timing between notes stays exactly
   as played. As soon as possible is better when the blocks are small - at 64 or 128 samples, the jitter
   (1.3 to 2.7 ms at 48 kHz) is below what anyone hears and the delay matters more - or for a slow pad
   where nobody hears the start's timing at all.
6. **No input latency.** A note's time is the time its event arrived, about a millisecond after the key
   went down (Exercise 2 (b)): MIDI has no converter and no input buffer. Audio comes in with its input
   latency because the sound waits in the converter and the driver's buffer before the program sees it. What
   is subtracted is what the player *heard*: they played along with the song, which reached their ears the
   output latency and the mix latency after it was rendered. So the note belongs where the song was when
   they heard it, not where the engine was rendering at that moment.
7. **Keep what was played.** Moving the notes loses the original for good: no going back to "off", no
   trying another grid, and a second quantize at strength 0.5 compounds the first (to 0.75 of the way). It
   also loses the feel - a swing or a push that the player meant - which a strength below 1 is supposed to
   keep. Computing the quantized notes from the played ones costs a little time when the clip changes, and
   nothing on the audio thread.

## Exercise 2 - MIDI by the numbers

[`solutions/ex2_midi_numbers.cpp`](ex2_midi_numbers.cpp)

- **(a)** $`\frac{44100 \cdot 60}{97 \cdot 960} = 28.414948`$ samples a tick. Bar 33 starts at tick
  $`32 \cdot 4 \cdot 960 = 122\,880`$, sample $`\mathrm{round}(122880 \cdot 28.414948) = 3\,491\,629`$
  (79.18 s). An hour is $`97 \cdot 960 \cdot 60 = 5\,587\,200`$ ticks, exactly sample 158 760 000. A `float`
  has 24 bits of mantissa: once the sum is above $`2^{24} \approx 16.8`$ million, adding 28.4 rounds to a
  multiple of 2, then of 4, 8, 16 - the solution measures **10.6 million samples (240 s)** off. A `double`
  does far better, but still not exactly; from the tick there is one rounding, always.
- **(b)** $`10 / 31250 = 320`$ µs a byte, so a note on (3 bytes) takes **0.96 ms**. A chord of 10 notes:
  $`3 + 9 \cdot 2 = 21`$ bytes = **6.72 ms** with running status, 30 bytes = 9.6 ms without. The last note
  starts $`19 \cdot 320`$ µs = 6.08 ms = **291.84 samples** after the first. A "chord" from a 5-pin cable is
  an arpeggio of 6 ms; USB MIDI sends the notes in one packet.
- **(c)** A block is $`512 / 48000`$ = **10.67 ms**: "as soon as possible" puts an event anywhere from 0 to
  10.67 ms late, depending on where in the block period it arrived. Scheduled: $`2 \cdot 512 + 512 = 1536`$
  samples = **32 ms**, always. At 128 samples: 0 to **2.67 ms** of jitter, or **8 ms** scheduled.
- **(d)** At 120 BPM: $`48000 \cdot 60 / (120 \cdot 960) = `$ **25 samples = 0.52 ms** a tick; at 200 BPM
  **15 samples = 0.31 ms**. A 1/16 at 120 BPM is 240 ticks = **6000 samples** (125 ms). A tick is finer than
  anyone plays; Step 6's recording rounds to it without anyone hearing.
- **(e)** C7 = $`440 \cdot 2^{27/12}`$ = **2093.0 Hz**. Harmonic 12 is the first above 24 kHz (25 116 Hz)
  and folds to $`48000 - 25116 = `$ **22 884 Hz**. The first alias below 10 kHz is **harmonic 19**
  (39 767 Hz) at $`48000 - 39767 = `$ **8233 Hz**, not a harmonic of C7 at all. A sawtooth's harmonic $`k`$
  has amplitude $`1/k`$: **-25.6 dB** under the fundamental, clearly audible - that is what PolyBLEP is for
  (Step 3).
- **(f)** Swing 0.2 on 480 ticks: $`\mathrm{round}(0.2 \cdot 480) = `$ **96 ticks = 2400 samples = 50 ms**
  at 120 BPM. Tick 990 is nearest to line 2 (tick 960, an even line, not swung); strength 0.5 moves it half
  the way: $`990 + \mathrm{round}(0.5 \cdot (960 - 990)) = `$ **975**. Tick 1430 is nearest to line 3 (1440,
  odd), swung to $`1440 + 96 = `$ **1536**.

## Exercise 3 - Standard MIDI Files

[`solutions/ex3_smf.cpp`](ex3_smf.cpp)

The writer builds each track's data first and writes its chunk afterwards, because the chunk's length
comes before the data. Running status is a single variable: the last status written; a meta event cancels
it, but the writer writes none between the events. The reader follows the file event by event:

- **Every length is checked against what is left** before anything is read - and as `length > end - p`,
  never `p + length > end`, which overflows for a length near $`2^{32}`$ that a damaged file may carry.
- **Meta events and SysEx cancel running status**, so a data byte right after one is an error, not the
  continuation of the last note.
- **The tick is converted from the absolute tick** in the file's division:
  $`\mathrm{round}(tick \cdot 960 / division)`$, in integers. Converting each delta rounds every one: at
  division 7 the seven events a tick apart end on 959 instead of 960.
- **A track without channel messages** is the conductor of format 1 (or an empty one): it may hold the
  tempo and the time signature, but it is not a track of the song.

```text
Exercise 3 - Standard MIDI Files
  10 of 10 numbers of the standard's table written and read back
  a song of one track, 3/4 at 100 BPM: 82 bytes, exactly the expected ones (running status, note offs first)
  a format-0 file at 96 a quarter with SysEx, an unknown chunk and running status: read
  division 7, a tick apart: ticks 137, 274, ... 960 (summed deltas would end on 959)
  23 of 23 broken files refused, each with a reason
  2000 of 2000 random songs (every channel message, names, empty tracks) came back the same
  82 files cut short: 82 refused; 20000 damaged files: no crash (1416 still readable)
[PASS] l18_ex3 (52 checks passed)
```

What it does not do: tempo changes after the first (counted in `tempoChanges`, so the app can warn), format 2,
SMPTE time, and key signatures, lyrics and markers. Exercise 9 brings it into DawLab 18, where the events
become notes.

## Exercise 4 - A tempo map

[`solutions/ex4_tempo_map.cpp`](ex4_tempo_map.cpp)

`prepare()` adds the segments up in `double`, unrounded: $`start_i = start_{i-1} + (tick_i - tick_{i-1})
\cdot spt_{i-1}`$. `sampleOfTick()` finds the last change at or before the tick with `std::upper_bound` and
rounds once: $`\mathrm{round}(start_i + (t - tick_i) \cdot spt_i)`$. A tick before 0 uses the first change.

`firstTickAtOrAfter()` finds the segment by its start in samples, then does what Step 1 does inside it:
`ceil()`, and back while the tick before still rounds up to the sample. At a change, the tick computed from
the segment before and the one computed from the segment after agree, because the start of a segment is
exactly the same expression as the end of the one before it.

```text
Exercise 4 - a tempo map
  one tempo: the same as Step 1 (bar 33 at 97 BPM, 44.1 kHz: sample 3491629)
  120, 60 and 240 BPM a bar each: bars at samples 0, 96000, 288000, 336000
  300 random maps (1-41 changes, 20-300 BPM): 0 ticks wrong, 0 samples whose first tick is wrong, 0 steps backwards
  a ritardando into every beat, 2048 events in random blocks: 2048 found, 0 outside their block
  2000 changes later: sample 49504480, exactly 49504480; rounding every change would give 49504000
[PASS] l18_ex4 (19 checks passed)
```

The rounded starts are 480 samples early after 2000 changes - not because a rounding is large, but because
it is the same every time: a segment of 1001 ticks is 28 443.36 samples at 97 BPM and 21 061.12 at 131 BPM,
and both are rounded down - by 0.48 a pair, 480 after 1000 pairs. Errors that do not cancel add up.

To bring the map into the engine, `Transport` would keep it instead of one BPM, and everything that calls
`sampleOfTick()` - the clip player, the recorder, the grid - would ask the map. The notes would not change
at all: that is why they are in ticks.

## Exercise 5 - An arpeggiator

[`solutions/ex5_arpeggiator.cpp`](ex5_arpeggiator.cpp)

`process()` walks the block step by step. The steps are the multiples of `rate` in
$`[firstTickAtOrAfter(s_0), firstTickAtOrAfter(s_1))`$ - Step 1's half-open range, so every step belongs to
exactly one block. At each step, in this order:

1. our note ends if its off tick has come (`offTick <= step`: at gate 1 it ends right at this step, before the
   next note);
2. the input up to and including the step's offset is taken: keys go down and up, everything else passes;
3. the step plays: with no key held the pattern starts over, otherwise the next note of the pattern.

After the last step, the note off if it falls in the block, and the rest of the input. The output is kept in
order by inserting from the back - a note off moves before everything else at its offset - which costs a few
swaps and no allocation; `std::stable_sort` would need a buffer.

```text
Exercise 5 - an arpeggiator
  Up, C E G held, 1/16 at half gate:                   yes
  Down:                                                yes
  UpDown:                                              yes
  UpDown, one key:                                     yes
  UpDown, two keys:                                    yes
  AsPlayed, G C E:                                     yes
  AsPlayed, C pressed twice, then E:                   yes
  Up, two octaves of C E:                              yes
  UpDown, two octaves of C E:                          yes
  Down, three octaves of 110 (134 is no note):         yes
  Up, two octaves of 115 (127 is the top note):        yes
  17 keys: the oldest is pushed out:                   yes
  keys at a step and a sample after it:                yes
  all keys up, then a new chord at a step:             yes
  gate 1: the note off first at the next step:         yes
  a tiny gate is still one tick long:                  yes
  1/8 at 97 BPM, 44.1 kHz, blocks of 333: 44 events, every one on its exact sample: yes
  a controller and a bend pass through:                yes
  a controller after a note off, in one block:         yes
  a controller before a note off, in one block:        yes
  a jump: the note ends, the pattern starts again:     yes
  400 random songs (keys at and between steps, every mode, rate and gate) in blocks of 1 to 700 samples:
    0 differ from one block, 0 leave a note hanging, 0 play two notes at once, 0 made `out` grow,
    0 put an event out of its block or out of order
[PASS] l18_ex5 (26 checks passed)
```

The block test is the one that finds most mistakes: the first step computed as `from / rate` (rounded down)
instead of up, or the note off forgotten when no step follows it in the block. Each of these works in some
blocks and fails in others.

## Exercise 6 - Mono, legato and glide

[`solutions/ex6_mono_synth.cpp`](ex6_mono_synth.cpp)

The keys are an array, oldest first: a new key goes at the end, a key that goes up is taken out wherever it
is, and only when it was the last one does the sound change. `sound()` does what every new note does: the
target and the gain, then either a slide (when the note overlaps another) or a jump, then the envelope - an
attack from silence, or with legato off; nothing with legato on.

The slide is a number of samples and a step per sample, $`(target - pitch) / samples`$. Adding the step up
leaves a rounding error, so the last step sets the pitch to the target exactly. A slide in semitones is a
curve in hertz: the saw's frequency is computed from the pitch every sample, $`440 \cdot 2^{(p - 69)/12}`$.

```text
Exercise 6 - mono, legato and glide
  one note: starts at its pitch with an attack, sounds (peak 0.25), releases to silence
  legato: E over a held C keeps the envelope and slides 4 semitones in exactly 2400 samples
  back to the key still down, a new slide from mid-slide, an octave as long as a semitone: done
  last-note priority: C E G, E up changes nothing, G up returns to C, C up releases
  legato off: every new note restarts the attack from the level it is at, and still glides
  the sound: 55 cycles in a quarter second at A3, 110 at A4
[PASS] l18_ex6 (36 checks passed)
```

A glide of constant *time* (here) sounds the same for every interval. Many synths glide at a constant *rate*
instead - a number of semitones a second - so that an octave takes longer than a step. Changing the number of
samples to $`|target - pitch| \cdot samplesPerSemitone`$ is all it takes.

## Exercise 7 - A VST3 instrument on a track

1. **Events.** A JUCE plugin gets a `juce::MidiBuffer`: messages at sample positions inside the call. For
   each chunk the track already has its `MidiBlock` - the clip's events, the chased controllers, the live
   keyboard - so the host copies them into a `MidiBuffer` prepared in `prepare()` (`ensureSize`, then only
   `clear()` and `addEvent()`: no allocation). JUCE's VST3 wrapper turns notes into VST3 note events, and
   controllers and pitch bend into *parameter changes* at the same sample, through the plugin's
   `IMidiMapping`. A plugin that maps no parameter to controller 64 has no sustain pedal; the host cannot fix
   that, only show it.
2. **Hanging notes.** The table is still needed - more than before: the plugin cannot know that the transport
   jumped, only the host can. On a stop, a seek or a wrap the host sends a note off for every note in the
   table, at the chunk's offset, and the notes release as they would. For a hard stop (a panic button) it
   calls the plugin's `reset()`, which cuts its voices and tails. Keys of the live keyboard that are still
   down when the track is disarmed get their note offs too.
3. **Latency.** The plugin's latency $`L`$ is part of the track's latency, and chapter 14's compensation
   delays every other track by it, so the song stays together. A note from the keyboard cannot be sent
   earlier - it has not happened yet - so the player hears it $`L`$ later than with the built-in synth. A
   recorded note is placed by what the player heard, and the mix latency of Step 6 already includes the
   compensation, so it comes out right as long as the anchor uses the engine's total latency.
4. **The tail.** The engine keeps calling the plugin every block, stopped or not, with an empty buffer when
   there are no events - the release, a reverb, a delay go on. A muted track is still processed (its note offs
   must arrive); only its output is not mixed. A bypassed instrument outputs silence (`processBlockBypassed`)
   but still gets its note offs. An offline render goes on after the song's end for
   `getTailLengthSeconds()`, or - when that says "infinite" - until the output has been below -90 dB for a
   second.
5. **State.** The project keeps the plugin's identity (format, file, unique id) and the blob of
   `getStateInformation()`, base64 in a text line. Opening the project creates the instance on the message
   thread, calls `setStateInformation()` there, and only then hands it to the engine through the command
   queue, like any insert (chapter 15). A plugin that is not installed keeps its blob in a silent placeholder,
   so saving the project does not lose it.
6. **Trust.** Nothing can be proved: a plugin may allocate, lock, log to a file or crash. The host measures
   instead - the time of every call against the block's deadline, the dropouts, a CPU meter per plugin - and a
   careful one runs plugins in another process, so that a crash costs the plugin and not the song. DawLab's
   engine tests keep using the built-in synth; a plugin is tested with chapter 13's own, which is known to
   behave.
