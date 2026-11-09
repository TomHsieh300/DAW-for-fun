# Step 3 - One voice

**English** | [繁體中文](step-03-one-voice.zh-TW.md)

[Overview](../README.md) | Previous: [Step 2 - Playing a MIDI clip](step-02-midi-clips.md) | Next: Step 4 - Polyphony (coming)

## Goal

Make a note sound: a sawtooth oscillator that does not alias, a filter, and an envelope that never clicks -
rendered on the audio thread, without allocating, the same whatever the block size.

## Design

### Subtractive synthesis

The classic analog synthesizer, and still the first one anyone builds, is three parts in a row:

```text
oscillator  ->  filter  ->  amplifier
(all the harmonics)  (takes the top off)  (an envelope: how loud, over time)
```

A sawtooth has every harmonic, harmonic `k` at `1/k` of the fundamental: bright, buzzy, and a good start
for a filter to shape. The filter here is chapter 02's `StateVariableFilter` as a lowpass; the new parts are
the oscillator and the envelope. Each has its own way to fail audibly.

### Note number to frequency

Equal temperament: 12 notes to the octave, each a factor of 2^(1/12), anchored at A4 = MIDI note 69 = 440 Hz:

```text
f = 440 x 2^((note - 69 + bend) / 12)
```

`bend` is in semitones, fractions too. Step 4 will feed it from the pitch bend wheel of Step 1.

### Aliasing, and PolyBLEP

The obvious sawtooth is `2 x phase - 1`, with a phase that rises by `f / sampleRate` every sample and wraps
at 1. Its jump from +1 to -1 is infinitely steep, and an infinitely steep jump has harmonics that never stop.
A digital signal cannot hold anything above half the sample rate: harmonic 10 of a 2489 Hz note would be at
24.9 kHz, and it comes back *folded* at 48 - 24.9 = 23.1 kHz. Harmonic 30, at 74.7 kHz, comes back at
21.3 kHz, and so on - and since folding goes back and forth, some land low, at a few kHz. These *aliases*
belong to no harmonic series. They sound like a second, wrong note, and they move the wrong way when the
pitch bends.

A band-limited sawtooth would have no harmonics above Nyquist. Building it from sines is exact but costs one
sine per harmonic. *PolyBLEP* (polynomial band-limited step) is the cheap approximation every soft-synth uses:
the difference between the naive jump and a smooth one is a small residual that matters only in the samples
right next to the jump, so it subtracts a polynomial approximation of it there - two samples per cycle,
nothing elsewhere:

```text
t < dt:       x = t / dt          residual = 2x - x^2 - 1     (just after the jump)
t > 1 - dt:   x = (t - 1) / dt    residual = x^2 + 2x + 1     (just before it)
saw = 2 x phase - 1 - residual
```

The check measures what that buys. It analyses each alias's level against the fundamental:

<div align="center">

| | naive | PolyBLEP |
|---|---|---|
| A4 (440 Hz), worst alias | 40.6 dB down | 71.3 dB down |
| D#7 (2489 Hz), aliases below 10 kHz | 24.1 dB down | 52.3 dB down |
| D#7 (2489 Hz), aliases just below 24 kHz | 20.0 dB down | 28.4 dB down |

</div>

PolyBLEP is very good where it counts - the aliases that fold far down, into the range where they are heard
as wrong notes - and only fair just below Nyquist, where the aliases are above 20 kHz and the lowpass filter
behind the oscillator takes them out anyway. Higher-order BLEPs, or oversampling (chapter 06), do better near
the top, for more CPU.

### An envelope that never jumps

The amplifier follows an ADSR envelope: up in the *attack*, down to the *sustain* level in the *decay*, held
while the key is down, down to silence in the *release* after the note off. A level that jumps clicks - a
jump is a broadband pulse - so the envelope has three rules:

- **No stage shorter than 1 ms.** A 0 ms attack is a jump. 1 ms is 48 samples at 48 kHz, short enough to
  sound instant and long enough not to click.
- **A note starts from where the level is.** A note played again during its release does not drop to 0 first:
  the attack goes on up from the level it finds.
- **A release starts from where the level is, and takes the release time.** A note let go half-way up its
  attack is released from there. The slope is `level / releaseSamples`, so every release lasts the same
  time, whatever level it starts from.

The stages are linear, which makes the check exact: the attack is at 0.5 half-way, at 1.0 after 480 samples,
a release from 0.5 takes 9600 samples. Real synths often use exponential curves, which sound more natural for
decays; the rules stay the same.

The check also fires note ons and offs at random with every time set to 0, and requires that no step between
two samples is bigger than 1/48 - one 1 ms ramp's worth.

### The voice

`Voice` puts them together:

```text
out[i] += filter(saw.next()) x envelope.next() x velocityGain(velocity)
```

- It *adds* to the buffer, because Step 4 mixes many voices into one.
- An idle voice adds nothing and costs nothing: the loop stops when the envelope is idle.
- `velocityGain` is a square law, `(v / 127)^2`: velocity 64 is about a quarter (-12 dB) of 127, the feel most
  synths have.
- A voice that starts while silent resets its saw to phase 0 and its filter to rest, so every note starts the
  same. A voice that is played again *while it still sounds* keeps both: resetting the phase would jump the
  waveform in the middle of the sound - a click.

All of it is plain arithmetic per sample, with no state that depends on the block, so the check renders the
same note in blocks of 1, 7, 64, 512 and 1000 samples and requires identical output.

## Your turn

Write the nine functions in `steps/step03-one-voice/starter/step03.cpp`:

1. `noteFrequency()`: the formula, with the bend.
2. `polyBlep()`: the two polynomials, 0 elsewhere.
3. `Saw::next()`: the naive value minus the residual, then the phase.
4. `Adsr::noteOn()`: the attack, from the current level.
5. `Adsr::noteOff()`: the release and its slope, unless idle.
6. `Adsr::next()`: one step of the current stage, clamped at its target.
7. `Voice::start()`: reset only a silent voice, then pitch, gain, frequency, attack.
8. `Voice::release()`: the envelope's release.
9. `Voice::render()`: add filter x envelope x gain while active.

When it works, the output ends like this:

```text
Step 3: one voice
  A4: worst alias 40.6 dB down naive, 71.3 dB with PolyBLEP
  D#7: aliases below 10 kHz 24.1 dB down naive, 52.3 dB with PolyBLEP (just below 24 kHz: 20.0, 28.4)
  A3 at velocity 127 is 3.94 times as loud as at 64 (11.9 dB)
[PASS] l18_step03 (43 checks passed)
```

## What you learned

- A note number is a frequency: 440 x 2^((n - 69) / 12).
- A naive sawtooth aliases; PolyBLEP corrects the two samples next to each jump and removes most of what is
  heard.
- Measure an oscillator's aliases; do not judge it by its waveform.
- An envelope never jumps: no stage under 1 ms, every stage starts from the current level.
- A voice adds to the mix, costs nothing when idle, and keeps its phase when played again.
