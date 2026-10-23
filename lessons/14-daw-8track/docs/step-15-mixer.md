# Step 15 - The mixer
**English** | [繁體中文](step-15-mixer.zh-TW.md)

[Overview](../README.md) | Previous: [Step 14](step-14-arrangement.md) | Next: [Step 16](step-16-inserts-panels.md) | Back to the [roadmap](../README.md#4-the-twenty-one-steps)

## Goal

Eight track strips, the FX bus strips and a master section, each strip with: name, Load button,
four insert slots, send knobs, pan, a vertical fader with a dB scale, mute / solo and a stereo
peak meter. Every control writes straight into the engine's atomics; every display reads them.

## Design

### Strips are generated from the engine's constants

`MixerView` creates one `ChannelStripView` per entry of `stripRefAt(0 .. kNumStrips-1)`
(`Session.h`): tracks 1-8, buses A..(kNumBuses), master. A track strip gets `kNumBuses` send knobs.
Nothing in the GUI says "2 buses": Exercise 12 recompiles the engine and the app with three buses
and the mixer simply has twelve strips.

| strip kind | Load | inserts | sends | pan | fader | M | S | extra |
|---|---|---|---|---|---|---|---|---|
| track | yes | 4 | one per bus (post-fader by default; a pre-fader send shows "A pre" and says so in its tooltip) | pan law (constant power) | yes | yes | yes | - |
| bus | - | 4 | - | balance | yes | yes | - (solo-safe) | - |
| master | - | 4 | - | balance | yes | - | - | limiter ceiling, spectrum, LUFS |

The distinctions come from stage 1: tracks use the constant-power pan law, buses and master use
balance (Step 4, "Everything 3 dB too quiet" in the debugging table); buses are solo-safe, so they
have no solo button (Step 6).

### The fader law

A fader's travel should spend most of its length where mixing happens (-20 .. +6 dB), not on
-60 .. -40. The app maps a position $p \in [0, 1]$ to dB with a square law:

$$
\text{dB}(p) = 12 - 72\,(1 - p)^2, \qquad p(\text{dB}) = 1 - \sqrt{\frac{12 - \text{dB}}{72}}
$$

| dB | +12 | +6 | 0 | -6 | -12 | -30 | -60 (bottom = $-\infty$) |
|---|---|---|---|---|---|---|---|
| $p$ | 1.000 | 0.711 | 0.592 | 0.500 | 0.423 | 0.236 | 0 |

So 0 dB sits at 59 % of the travel and -6 dB exactly in the middle; the bottom position means
silence (`FaderLaw::toEngineDb` maps -60 to $-\infty$ = `kMinusInfinityDb`). The scale painted
next to the fader uses `dbToPosition()` too, so the numbers line up with the cap. The fader writes
`ChannelStrip::setGainDb()` - an atomic; the strip ramps to the new gain over 50 ms (Step 4), so
dragging the fader never produces zipper noise, no matter how coarse the mouse events are.

### Controls that follow the engine

A strip's controls are not the source of truth; the engine is. `ChannelStripView::update()`
(30 Hz) reads gain, pan, sends, mute and solo back from the `ChannelStrip` and moves the controls
- unless the mouse is on them. That is why a project load, a keyboard shortcut (M / S on the
selected track, Step 21) or an exercise program that calls `setGainDb()` directly all show up in
the GUI without any notification code.

### Meters

Each strip's `MeterBar` shows `getPeakDb(ch)` (chapter 11's `LevelMeter`: instant attack, decay
in dB per second) and `getPeakHoldDb(ch)`. Linear in dB from -60 to +6 dBFS, green below -18,
yellow up to -6, red above, a tick at 0 dBFS:

$$
\text{position} = \frac{\text{dB} + 60}{66}
$$

The meter repaints only if a value moved by more than 0.1 dB worth of pixels (1/660 of the height)
- most of the 11 meters are still most of the time.

### What the strip does *not* do

It never calls a DSP function, never touches an insert chain and never allocates on behalf of
the audio thread. Inserts go through `Session` (Step 16). The Load button only calls
`onLoadRequested(track)`; `MainComponent` opens the file chooser and hands the file to the loader
thread (Step 18).

## Code

`app/Source/MixerView.h/.cpp`: `FaderLaw`, `ChannelStripView` (constructor wires every control
to the strip's setters), `update()`, `refreshStructure()` (names and slot labels after a
`Session` change), `MixerView::resized()` (98-pixel strips, a gap before the buses and the
master, the rest of the width for the spectrum and loudness, Step 20).

```cpp
fader_.onValueChange = [this] {
    const float db = FaderLaw::positionToDb(static_cast<float>(fader_.getValue()));
    strip().setGainDb(FaderLaw::toEngineDb(db)); // atomic; the engine ramps over 50 ms
    gainLabel_.setText(FaderLaw::format(db), juce::dontSendNotification);
};
```

## Verify

`l14_app_smoke_test`:

- `FaderLaw::dbToPosition(0) = 0.5918` and `positionToDb(dbToPosition(-12)) = -12`;
- moving track 1's fader to the position of -6 dB sets `ChannelStrip::getGainDb()` to -6.00;
- clicking S on track 1 solos it in the engine;
- 11 strips, each track strip with `kNumBuses` send knobs.

The snapshot shows the demo song's mix: faders at their song values (-2 dB drums, -10 dB wind...),
send knobs (orange) where tracks feed the reverb and delay buses, meters with level.

## Your turn

| | |
|---|---|
| You write | `FaderLaw::positionToDb()` and `FaderLaw::dbToPosition()` in `app/Source/MixerView.cpp` (the square law of "The fader law"; a position at the bottom means $-\infty$) |
| Build + check | `.\scripts\build.ps1 -Only 14-daw-8track -Juce -TestLabel test` (`l14_app_smoke_test`) |
| Expected | the fader checks of this page pass: `dbToPosition(0) = 0.5918`, `positionToDb(dbToPosition(-12)) = -12`, moving track 1's fader to the -6 dB position sets the engine to -6.00 dB; `[PASS] l14_app_smoke_test` |

## What you learned

- Generate GUI structure from the engine's constants, not from literals.
- A good fader law is a formula; use its inverse for the scale.
- Let the GUI re-read the engine instead of keeping a second copy of values in sync.
