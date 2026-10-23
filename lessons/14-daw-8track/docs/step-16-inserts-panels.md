# Step 16 - Insert slots and auto-generated parameter panels
**English** | [繁體中文](step-16-inserts-panels.zh-TW.md)

[Overview](../README.md) | Previous: [Step 15](step-15-mixer.md) | Next: [Step 17](step-17-vst3-inserts.md) | Back to the [roadmap](../README.md#4-the-twenty-one-steps)

## Goal

Four insert slots per strip. A slot's drop-down offers every built-in effect (EQ, Filter,
Compressor, Gate, Expander, De-esser, Transient, Saturation, Distortion, Amp, Delay, Chorus,
Flanger, Phaser, Tremolo, Reverb, Convolution, Limiter, Gain), "VST3...", Bypass and "(none)". Clicking an occupied
slot opens a parameter panel that nobody designed by hand: it is generated from the effect's
`ParameterInfo` table (stage 1, Step 5).

![Compressor panel](images/dawlab8_panel.png)

## Design

### Session: GUI slots on top of a compact chain

The engine's `InsertChain` has no holes: removing the effect in position 1 moves position 2 to 1.
A mixer, however, has *fixed* slots - an EQ in slot 3 stays in slot 3 when slot 1 is cleared.
`Session` keeps the GUI view, `std::array<InsertSlotState, 4>` per strip
(kind Empty / Builtin / Vst3, effect type, observer pointer, display name), and translates:

```math
\text{chainIndex}(s) = \#\{\, k < s : \text{slot } k \text{ occupied} \,\}
```

```
GUI slots:   [ EQ ][    ][Comp][    ]
chain:       [ EQ ][Comp]                 GUI slot 2 -> chain index 1
```

| user action | Session call | engine command (Step 7) |
|---|---|---|
| choose an effect in an empty slot | `setBuiltinInsert(ref, slot, type)` | `addInsert(ref, chainIndex, createEffect(type))` |
| choose another effect | `setBuiltinInsert` | `replaceInsert(ref, chainIndex, ...)` - swapped at one block boundary |
| (none) | `clearInsert(ref, slot)` | `removeInsert(ref, chainIndex)` |
| Bypass | `setInsertBypassed` | none: `InsertEffect::setBypassed()` is an atomic |

Commands are applied in FIFO order, so even several clicks in a row leave the GUI and the chain
in agreement once the audio thread has caught up. `Engine::addInsert()` prepares the effect *on
the message thread* (a convolution reverb computes its FFT partitions there), then hands over a
ready object; the audio thread only links a pointer.

Removing an effect has one subtle rule: the observer pointer stays valid until the audio thread
has unlinked the effect *and* the message thread has run `collectGarbage()`. Any window that shows
the effect must close before that. `Session::onInsertWillBeRemoved(effect)` fires right before a
removal or replacement; `MainComponent` closes the effect's parameter window (and a VST3 editor,
Step 17) there.

### The menu

`EffectMenu::build()` creates the `juce::PopupMenu` from a data table (`builtinEntries()`: label +
`EffectType`), ticks the current effect, and adds the slot actions. The 19 built-ins are in an
order musicians expect (EQ and dynamics first, time-based effects later), not in enum order.
Clicking the slot body of an occupied slot opens the editor; the arrow at the right, a right-click
or a click on an empty slot opens the menu.

### The generated panel

`ParameterPanel(InsertEffect&)` walks `getNumParameters()` and builds one control per parameter:

| `ParameterKind` | control | value written with |
|---|---|---|
| Float, Int | rotary `juce::Slider` | `setParameter(i, sliderValue)` |
| Bool | `juce::ToggleButton` "On" / "Off" | `setParameter(i, 0 or 1)` |
| Choice | `juce::ComboBox` with `info.choices` | `setParameter(i, min + selectedIndex)` |

The slider's value is the **real** parameter value (-20 dB, 120 ms, 4:1), but its travel uses the
engine's normalized mapping through a custom `juce::NormalisableRange`:

```cpp
juce::NormalisableRange<double> range(info.min, info.max,
    [info](double, double, double n) { return info.fromNormalized((float) n); },   // knob -> value
    [info](double, double, double v) { return info.toNormalized((float) v); },     // value -> knob
    [info](double, double, double v) { return info.clamp((float) v); });           // snap (Int)
```

so a frequency knob (log taper: $`n = \ln(v/v_{\min}) / \ln(v_{\max}/v_{\min})`$) puts 1 kHz near the
middle of 20 Hz .. 20 kHz, exactly like the engine's `getParameterNormalized()`. Text comes from
`formatParameterValue()` (stage 1), a double-click returns to `defaultValue`, knobs whose range
crosses zero draw their arc from the centre. For the 30-parameter EQ the panel uses 5 columns -
one band per row, like a hardware EQ. Effects with a meter (`hasMeter()`: compressor, limiter,
gate, expander, de-esser) get a gain-reduction bar in the header; the header also shows the
effect's latency - read with `getReportedLatencySamples()`, the atomic copy, because
`getLatencySamples()` belongs to the audio thread (Step 5).

Threading: `setParameter()` may be called from any thread (an atomic store plus a dirty bit,
applied by the audio thread at its next block). A 15 Hz timer reads `getParameter()` back, so the
panel follows changes made elsewhere - a second panel on the same effect, an automation lane
(Exercise 8), a project load.

Panels live in top-level `ParameterWindow`s (`juce::DocumentWindow`, always on top), one per
effect, owned by `MainComponent` in a `std::map<InsertEffect*, ...>`. Closing a window deletes it
*asynchronously* (`MessageManager::callAsync`): a window must not delete itself inside its own
`closeButtonPressed()`.

## Code

- `app/Source/Session.h/.cpp`: `InsertSlotState`, `chainIndexOf`, `setBuiltinInsert`,
  `setVstInsert`, `clearInsert`, `setInsertBypassed`, `onInsertWillBeRemoved`.
- `app/Source/EffectMenu.h/.cpp`: the table, `build()`, item IDs.
- `app/Source/ParameterPanel.h/.cpp`: `ParameterPanel`, `ParameterWindow`.
- `app/Source/MainComponent.cpp`: `showSlotMenu`, `handleSlotMenuResult`, `openInsertEditor`.

## Verify

`l14_app_smoke_test`:

```
2. demo song: 140 clips on 28 distinct buffers, 19 inserts in GUI slots, 0 unknown parameter names,
   mix latency 752 samples
```

- every GUI slot's observer pointer equals `engine.getInsert(ref, chainIndexOf(ref, slot))` for
  all 11 strips after the commands were applied;
- the panel of the drum compressor has one control per parameter; its Threshold knob shows
  -20.0 dB (the demo song's value), moving it to -30 sets `getParameter(0)` to -30, and the knob's
  text equals `formatParameterValue()`; "Stereo Link" became a toggle.

The picture at the top is `l14_app_snapshot_panel.png`, written by `--snapshot`: the drum
compressor while the song plays. The snapshot prints the value the header bar shows
(`InsertEffect::getMeterDb()` when bar 2 beat 3 is heard, i.e. 752 samples after the beat-3 hit
reached the compressor - the moment of its largest gain reduction):

```
panel snapshot: ...\dawlab_output\l14_app_snapshot_panel.png (drum compressor, gain reduction 12.1 dB)
```

## Your turn

| | |
|---|---|
| You write | `Session::chainIndexOf()` in `app/Source/Session.cpp`: the engine's chain index of GUI slot `uiSlot` = the number of non-empty slots before it |
| Build + check | `.\scripts\build.ps1 -Only 14-daw-8track -Juce -TestLabel test` (`l14_app_smoke_test`) |
| Expected | section 2 still finds 19 inserts in GUI slots, and the check "every GUI slot's observer pointer equals `engine.getInsert(ref, chainIndexOf(ref, slot))`" passes for all 11 strips; `[PASS] l14_app_smoke_test`. In the app, put an effect into slot 3 of an empty strip, then one into slot 1: the panel of each opens the right effect |

A wrong mapping is the classic bug of a GUI with gaps over a compact engine chain: the panel
edits the neighbour.

## What you learned

- A self-describing parameter API turns "a GUI for every effect" into "one GUI generator".
- Keep fixed GUI slots and a compact processing chain apart, with one translation function.
- Close every view of an object before the object can be freed; a callback before removal makes
  that systematic.
