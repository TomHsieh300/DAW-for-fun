# Step 14 - The arrangement view
**English** | [繁體中文](step-14-arrangement.zh-TW.md)

[Overview](../README.md) | Previous: [Step 13](step-13-transport-bar.md) | Next: [Step 15](step-15-mixer.md) | Back to the [roadmap](../README.md#4-the-twenty-one-steps)

## Goal

Eight lanes, one per track, showing every clip with a waveform thumbnail; a ruler with bar
numbers and the loop range; a playhead that follows playback; click the ruler to seek; drag a clip
to move it - snapped to the beat grid - and hear the result immediately.

## Design

### One coordinate system

The view shows the timeline from `viewStart` (in samples) with `samplesPerPixel` samples per
pixel, to the right of a 132-pixel lane header $`H`$:

```math
x(s) = H + \frac{s - s_{\text{view}}}{\text{spp}}, \qquad
s(x) = s_{\text{view}} + (x - H)\cdot \text{spp}
```

Zooming is changing spp; zooming *around the mouse* keeps the sample under the mouse in place:

```math
s_m = s(x_m), \qquad \text{spp}' = \text{spp} / \text{factor}, \qquad s'_{\text{view}} = s_m - (x_m - H)\,\text{spp}'
```

`zoomToFit()` chooses spp so that the song (at least 4 bars) plus one bar fills the width. For
the 8-bar demo song at 120 BPM / 48 kHz in a 1400-pixel window: $`9 \cdot 96000 / 1268 \approx 681`$
samples per pixel, one beat = 35 pixels.

Snapping rounds to the nearest beat, $`\hat s = \mathrm{round}(s / N_{\text{beat}})\,N_{\text{beat}}`$
(holding **Alt** disables it). Exercise 8 reuses `sampleToX`, `xToSample` and `snapToBeat` for an
automation lane drawn under the arrangement.

### Waveform thumbnails: a peak cache

A 12-second clip has 576 000 samples per channel; at 681 samples per pixel, a naive `paint()` would
read every sample on every repaint. Every DAW keeps a reduced copy ("peak file"). `WaveformCache`
computes, once per source buffer, the minimum and maximum over each run of
`kSamplesPerBucket = 128` samples (over all channels). Painting one pixel column then combines
$`\lceil \text{spp} / 128 \rceil`$ buckets - about 6 at the default zoom - and draws one vertical line
from the minimum to the maximum, scaled by the clip gain. Because clips share buffers (the drum
loop is one buffer used by four clips; the 96 shaker hits use two buffers), the cache is keyed by
the buffer pointer: the demo song needs 28 cache entries for 140 clips. Entries whose buffer is
no longer used by any clip are dropped when the session changes (`prune`).

### Playhead without repainting everything

`update()` runs at 30 Hz. If the playhead's x moved, it repaints two 13-pixel-wide strips (old
and new position) instead of the whole view. While playing, the view *pages*: when the playhead
leaves the visible range it jumps one page, like most DAWs, instead of scrolling every frame.

### The playhead shows what you hear

The playhead is drawn at `getPlayheadPosition()`, which is `AudioHost::getAudiblePosition()`
(Step 13), not at `Transport::getPosition()`. The transport position is the sample the engine is
*rendering*; while playing, the listener hears it only after the mix latency (PDC, Step 8: 752
samples = 15.7 ms in the demo song) plus the device's output latency:

```math
x_{\text{playhead}} = \text{sampleToX}\big(\max(0,\ \text{position} - L_{\text{mix}} - L_{\text{device}})\big)
```

Without that correction the line runs ahead of the sound - with a 20 ms device and the demo song,
by almost 36 ms, more than one frame of the 30 Hz display, and very visible when you watch a
drum hit cross the line. The page-scroll uses the same position, so the view turns the page when
the *heard* position leaves it. `MainComponent` passes its `AudioHost` to the constructor;
without one (the test benches of the exercises) the view shows the transport position.

### Editing clips: the GUI copy and the engine copy

The arrangement never touches the engine's clip lists. It draws from `Session::getClips(t)` - the
GUI copy - and a drag calls `Session::moveClip(track, index, newStart)`, which

1. moves the clip in the GUI copy and keeps the list sorted (`std::upper_bound`),
2. sends a *copy* of the new list to the engine with `Engine::setTrackClips()` - the lock-free
   pointer swap of Step 7 (the old list comes back as garbage and is freed by the 30 Hz timer),
3. broadcasts a change message so that the views repaint.

During a drag that happens on every mouse move that changes the snapped position - a few dozen
commands per second, far below the queue's 1024 slots. You hear the clip at its new place within
one audio block.

### Mouse map

<div align="center">

| where | action | effect |
|---|---|---|
| ruler | click / drag | `Transport::setPosition()` (snapped) - a pending seek |
| clip | drag | `Session::moveClip()` (snapped; Alt = free) |
| lane | click | select the track (`Load audio...`, M / S keys act on it) |
| empty lane | double-click | seek there |
| anywhere | wheel / Ctrl + wheel | scroll / zoom around the mouse |

</div>

## Code

`app/Source/ArrangementView.h/.cpp`: `WaveformCache::get` / `prune`, `sampleToX`, `xToSample`,
`zoom`, `snapToBeat`, `paintRuler`, `paintLane`, `paintClip`, the mouse handlers.
`app/Source/Session.cpp`: `moveClip`.

The heart of the waveform drawing:

```cpp
for (int px = x0; px < x1; ++px) {
    const double t0 = xToSample(px) - clip.start + clip.sourceOffset;   // source sample at this pixel
    int b0 = floor(t0 / kSamplesPerBucket), b1 = ceil((t0 + samplesPerPixel_) / kSamplesPerBucket);
    float lo = 0, hi = 0;
    for (int b = b0; b < b1; ++b) { lo = min(lo, peaks.minimum[b]); hi = max(hi, peaks.maximum[b]); }
    g.drawVerticalLine(px, midY - hi * gain * halfH, midY - lo * gain * halfH);
}
```

## Verify

- The snapshot (Step 12) shows all eight lanes of the demo song with their waveforms, the playhead
  at bar 2 beat 3, and the arp entering at bar 3.
- Exercise 8's self-check uses the same mapping: with `setView(0, 192)` one beat is exactly
  125 pixels, and a click 3 pixels next to beat 2 snaps to sample 48 000.
- In the app: drag a drum clip by half a bar while the loop plays - the change is audible on the
  next pass, and no click or dropout occurs.

## Your turn

<div align="center">

| | |
|---|---|
| You write | `ArrangementView::sampleToX()`, `xToSample()` and `snapToBeat()` in `app/Source/ArrangementView.cpp` (the formulas of "One coordinate system"; snapping rounds to the nearest beat and never returns a negative position), and `Session::moveClip()` in `app/Source/Session.cpp` (move, keep the list sorted by start, push a new clip list to the engine) |
| Build + check | `.\scripts\build.ps1 -Only 14-daw-8track -Juce -TestLabel solution`: `l14_ex8_solution` draws its automation lane on *your* time axis |
| Expected | `[PASS] l14_ex8_solution`: with `setView(0, 192)` one beat is 125 pixels and a click 3 pixels next to beat 2 snaps to sample 48 000; the snapshot (`l14_app_snapshot`) shows the clips where they were; in the app a dragged clip lands on a beat and is heard there on the next loop pass |

</div>

## What you learned

- One pair of mapping functions (and their inverse) is the whole geometry of a timeline.
- Peak caches make waveform drawing independent of the clip length.
- The GUI edits its own copy of the data and sends immutable snapshots to the audio thread.
