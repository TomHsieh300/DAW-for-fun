# Step 6 - Send / return buses

**English** | [繁體中文](step-06-sends-buses.zh-TW.md)

[Overview](../README.md) | Previous: [Step 5](step-05-insert-effects.md) | Next: [Step 7 - Commands](step-07-commands.md)

## Goal

Add two FX buses (for example a reverb and a delay) that every track can feed through its own send
level, before or after its fader, and return them into the master.

## Design

### Why buses instead of eight reverbs?

Putting a reverb insert on every track costs eight reverbs of CPU, and the tracks would sit in
eight different "rooms". A **send** taps a copy of the track at an adjustable level into a **bus**;
the bus holds one reverb at 100 % wet; its output (the **return**) is mixed into the master next
to the dry tracks. One room, one reverb, and the dry/wet balance of each track is just its send
level.

### Pre- or post-fader

```math
\text{bus}_b[n] = \sum_{t=1}^{8} s_{t,b}[n]\cdot
\begin{cases} g_t[n]\,x_t[n] & \text{post-fader (default)} \\ x_t[n] & \text{pre-fader} \end{cases}
```

- **Post-fader** (after fader *and* pan): pulling the fader down pulls the reverb down too; the
  wet/dry ratio stays. That is what you want for effects.
- **Pre-fader**: the send ignores the fader. Used for monitor / headphone mixes, or to fade a dry
  signal out and leave only its reverb.

The send level $`s_{t,b}`$ is ramped (50 ms) like the fader. -100 dB (`kMinusInfinityDb`) means off -
the default for every send - and costs nothing: when the send gain is 0 and not ramping, the
`addFrom` is skipped.

### Where mute and solo act

Mute/solo is applied **before** both send taps. Consequences:

- a muted track sends nothing - also not pre-fader (otherwise "mute" would leave its reverb
  playing, which surprises everybody);
- soloing a track silences the *other* tracks' sends too, while the buses themselves are
  **solo-safe**: they have no solo and are never silenced by a track solo, so the soloed track is
  heard *with* its reverb.

### Bus strips

A bus is a `ChannelStrip` in `PanMode::Balance` (it carries stereo): inserts -> PDC -> mute ->
fader x balance -> meters -> master. Buses could in principle send to each other; we do not allow
that (no feedback loops, no ordering problems) - the mixer graph is fixed:
tracks -> buses -> master.

## The code

`ChannelStrip.h`:

```cpp
void setSendLevelDb(int bus, float db) noexcept;   // any thread; <= -100 dB = off
void setSendPreFader(int bus, bool preFader) noexcept;
void addSend(int bus, AudioBlock dest, const AudioBlock& src) noexcept;   // dest += src * ramped gain
```

`Mixer::process()`:

```cpp
track.applyMuteSolo(tb);
for (int b = 0; b < kNumBuses; ++b)
    if (track.isSendPreFader(b)) track.addSend(b, bus[b], tb);       // pre-fader tap
track.applyFaderAndPan(tb);
for (int b = 0; b < kNumBuses; ++b)
    if (!track.isSendPreFader(b)) track.addSend(b, bus[b], tb);      // post-fader tap
...
for (int b = 0; b < kNumBuses; ++b) {                                // after all tracks
    strip.beginBlock(!strip.isMuted());
    strip.processInserts(bus[b]);   strip.applyMuteSolo(bus[b]);
    strip.applyFaderAndPan(bus[b]); strip.meter(bus[b]);
    master.addFrom(bus[b]);
}
```

The send is called every block even when it is (being turned) off, so its smoother always keeps
moving towards the target - otherwise a send turned off would freeze at its old gain and jump when
turned on again.

## Your turn

`steps/step06-sends-buses/minidaw.h` is your Step 5 engine plus send levels (atomics + one ramp
per bus) and pre/post switches in `ChannelStrip`, and two bus strips in balance mode whose returns
`Mixer::process()` adds to the master after all tracks.

<div align="center">

| | |
|---|---|
| You write | `ChannelStrip::addSend()` (`dest += src * ramped gain`, one `getNextValue()` per sample) and `Mixer::processTrack()` again, now with the pre-fader tap before and the post-fader tap after `applyFaderAndPan()` |
| File | `steps/step06-sends-buses/starter/step06.cpp` |
| Run | `l14_step06_starter`; it prints `[FAIL] l14_step06 (9 of 10 checks failed)` until you are done |
| Solution | `steps/step06-sends-buses/solution/step06.cpp` (ctest `l14_step06_solution`) |

</div>

Expected output (verified) - the worked example above, then mute, both buses, solo and the ramp:

```
Step 6: sends and buses (build-along)
  direct 0.5 g + bus A (0.5 g x 0.5 post + 0.25 pre) : out 0.7803 (expected 0.7803), bus A 0.4268 (expected 0.4268)
  bus A muted: 0.3536 (expected 0.3536); track 2 muted: 0.5303 (expected 0.5303)
  one track, send A 0 dB + send B -6 dB: 0.8839 (expected 0.3536 x 2.5 = 0.8839)
  solo track 1, both tracks send to bus A: 0.7071 (expected 0.5 g + 0.5 g = 0.7071)
  send switched on while playing: max step 0.000147 (expected < 0.001), ends at 0.7071 (expected 0.7071)
[PASS] l14_step06 (10 checks passed)
```

## Verify (reference route)

`l14_test_mixer`, `testSends()` (verified). Track 1: DC 0.5, post-fader send to bus A at -6.02 dB.
Track 2: DC 0.25, fader at $`-\infty`$, pre-fader send to bus A at 0 dB. With $`g = 0.7071`$ (track
centre pan) and buses / master at unity balance:

```math
\text{direct} = 0.5\,g = 0.3536,\qquad
\text{bus A} = \underbrace{0.5\,g \cdot 0.5}_{\text{post}} + \underbrace{0.25 \cdot 1}_{\text{pre}} = 0.4268,\qquad
\text{out} = 0.3536 + 0.4268 = 0.7803
```

The test checks `out`, the bus A meter (`getMaxPeakLinear() = 0.4268`), that muting bus A leaves
only `direct`, that muting track 2 also removes its pre-fader contribution, and that one track
sending to both buses adds both returns. `testSoloKeepsReverbBus()` solos track 1 with both tracks
sending to bus A: the output is track 1 plus *its* reverb return only (0.5 g + 0.5 g).

In `l14_render_song` the arp and lead feed bus B (ping-pong delay, dotted eighth = 375 ms at
120 BPM) and most tracks feed bus A (reverb, 2.2 s). Bus meters in the report (verified):

```
  Reverb   -19.6   -36.9    -33.8    -2.0 dB  0.00   Reverb
  Delay    -20.7   -39.7    -34.3    -4.0 dB  0.00   Delay
```

and `l14_render_song_drums_solo.wav` is the drum solo with its own reverb.

## What you learned

- Shared effects belong on buses; tracks reach them through sends.
- Post-fader sends keep the wet/dry ratio; pre-fader sends ignore the fader.
- Mute/solo before the send taps + solo-safe buses = the behaviour users expect.
- Ramp the send gain even when it is off, so it never jumps.
