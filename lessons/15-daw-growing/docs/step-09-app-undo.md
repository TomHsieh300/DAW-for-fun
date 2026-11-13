# Step 9 - Undo and redo in the app

**English** | [繁體中文](step-09-app-undo.zh-TW.md)

[Overview](../README.md) | Previous: [Step 8 - Saving and opening in the app](step-08-app-files.md)

## Goal

Edit > Undo (Ctrl+Z) and Redo (Ctrl+Shift+Z or Ctrl+Y) in DawLab 15. The menu names the step:
"Undo Delete track Bass". The window title shows a `*` while there are unsaved changes, and New,
Open, the demo song and Quit ask before they throw changes away. Step 5 designed the mechanism on
a small model; here every change the application can make goes through it.

## Design

### Edits: plain values that return their inverse

Step 5's idea, unchanged: an **edit** is a plain value that says what to change, and one
function carries it out *and* returns the edit that takes it back.

```cpp
std::optional<Edit> Session::applyEdit(const Edit& edit);   // the change done, its inverse returned
```

Undo applies the inverse, and redo applies the inverse of the inverse. There is no separate undo
code for each kind of edit. The app needs ten kinds (`app/Source/Edits.h`):

<div align="center">

| Edit | Changes | Its inverse |
|---|---|---|
| `AddTrack` | puts a track back (or adds a new one) from a `TrackSnapshot` | `RemoveTrack` |
| `RemoveTrack` | deletes a track with its clips and inserts | `AddTrack` with the whole track |
| `SetValue` | gain, pan, mute, solo, a send level | the old value |
| `Rename` | a strip's name | the old name |
| `SetFormat` | mono / stereo | the old format |
| `SetClips` | a track's clip list (move a clip, load a file) | the old list |
| `SetInsert` | add, replace or remove an insert, from an `InsertSnapshot` | the old slot as a snapshot |
| `SetParameter` | one parameter of an insert | the old value |
| `SetBypass` | an insert's bypass | the old state |
| `SetTempo` | the tempo | the old tempo |

</div>

What is undoable follows Step 5's rule: what the project file saves (Step 8) is undoable; what
only plays or shows the project is not (transport, loop, selection, zoom). One exception is
documented in the code: changes made inside a VST3 plugin's own editor go straight to the plugin.
They are saved with its state, but the app never sees them, so they cannot be undone.

### Names, not places

An edit names its strip with a `StripTarget`: a track **by its id**, a bus by its index, or the
master. It never names an engine slot or a display position. Deleting a track and undoing the
delete brings it back with the same id (Step 1's promise), but possibly in another engine slot
and after other tracks moved. Every older edit still finds it, because it looks the track up by
id when it is applied.

To bring a deleted track back with its id, `Session` has one more way to add a track: with an id
it handed out before (`addTrackWithId`). The id counter still only moves up, so Step 7's rule
holds: an id is never given to a *different* track.

### Snapshots: what a deleted thing was

`RemoveTrack`'s inverse must rebuild the whole track. So before deleting, `Session` takes a
`TrackSnapshot`: name, format, fader, pan, mute, solo, sends, four `InsertSnapshot`s and the clip
list. The clips share their audio buffers, so a snapshot is cheap.

An `InsertSnapshot` is enough to build the same effect again. For a built-in effect that is its
type and every parameter value. For a VST3 plugin it is the plugin file and the state the plugin
returns, as in Step 8. Building the plugin again needs chapter 13's `PluginHost`, which `Session`
does not know, so `MainComponent` hands it a factory: `Session::createVst3`. Without that factory,
such an edit does not apply.

### The history

`History` (`app/Source/History.h/.cpp`) is Step 5's `History`. It applies edits through the
`EditTarget` interface, which `Session` implements, and gives each entry a name for the menu:

- **All or nothing.** A change of several edits (loading a file: format, clip, name) is one
  entry. If one of its edits does not apply, the ones already applied are taken back.
- **Merge keys.** A fader drag performs an edit on every mouse move. All moves with the same key
  ("the gain of track 17") go into one entry. Every control closes the merge when a drag
  **starts** and when it **ends**, so two drags are two undo steps.
- **Serials.** Every entry has a serial that is never reused. Saving remembers the serial of the
  newest entry, and the project is modified when the newest entry is a different one. So undo
  can make a project clean again, and redo makes it modified again.
- **New, open, the demo song, a device change** start a new history. A device change converts
  every clip to the new rate, and old entries hold clip lists at the old rate. It forgets the
  entries (`forgetEntries()`), not whether there are unsaved changes: edited before, the project
  is still modified after; saved before, the conversion is no change of its own.
- **A change without an entry** - a take recovered after a crash (chapter 17) - calls
  `markModified()`: unsaved until the next save, whatever undo and redo do.

### Every change goes through `perform()`

The functions that change the session directly (`setClips`, `removeTrack`, ...) still exist.
`applyEdit()` is built from them, and so are File > New, the demo song and opening a file. But
every change the **user** makes now goes through `Session::perform(edits, name, mergeKey)`:

<div align="center">

| Where | What |
|---|---|
| mixer strip | fader, pan, send knobs, mute, solo, name, mono/stereo, the master's ceiling |
| arrangement | dragging a clip (`Session::moveClip`) |
| effect panel | every knob, toggle and choice, and bypass (`ParameterPanel::onParameterChange`) |
| transport bar | tempo (`TransportBar::onTempoChange`) |
| commands | add / delete track, mute / solo of the selected track, inserts from the slot menu |
| loading a file | format, clip and name as one entry |

</div>

The panel and the transport bar only report a change through a callback; `MainComponent` turns it
into an edit. Without the callback they still write the effect or the transport directly, so they
stay usable on their own.

### Asking before throwing changes away

New, Open, the demo song and Quit go through `MainComponent::askToSaveChanges(then)`. When the
history says the project is modified, a box asks **Save / Don't save / Cancel**. Save first asks
for a file if the project has none, and continues only after the save worked. Cancel does nothing.
The app's `systemRequestedQuit()` asks the same way, so closing the window cannot lose changes
either.

## Testing

The smoke test's sections 6e and 6f (180 checks in total):

- **Delete and undo.** Delete the bass with the command. The title gets its `*`, and the Undo item
  reads "Undo Delete track Bass". Undo: the bass is back at lane 2 with id 10, 16 clips and 2
  inserts, the whole project equals the text it had before (Step 8's format), and the title is
  clean again. Redo deletes it again.
- **Gestures.** Two fader drags, with three and two moves, give two entries. The first undo
  returns to -14 dB (the end of the first drag), and the second to -3 dB.
- **Effect panel.** The compressor's threshold knob goes from -20 to -35, and undo returns it to -20.
- **Saved or not.** After a save the title is clean. A mute adds the `*`, undo makes it clean,
  redo adds it again.
- **VST3.** Delete a track with a VST3 insert (the test plugin stands in for a file), then undo:
  the plugin is rebuilt through `createVst3`.
- **Random walk.** 150 random edits of all 14 kinds the test uses (add, delete, gain with and
  without a drag, pan, mute, solo, send, rename, format, move a clip, insert, parameter, bypass,
  tempo); 129 apply. Undoing all 129 gives the same project text as before. Redoing all 129 gives
  the end state. Nothing leaks.

### One float step: the order of a sum

After undoing the random walk, the project was equal as text, but the sound differed by
`1.79e-07`, about one rounding step of a float. The test counted which tracks came back in
another **engine slot**: 3 of 8. The engine sums the tracks in slot order, and floats added in
another order round differently. That is the whole difference, so the test now demands an exact
match when no track moved, and less than `1e-6` (-120 dB) when one did. Redoing everything gives
exactly the same sound again, because then the slots match.

### A stack overflow that printed nothing

The first run of the new sections ended with exit code `0xC00000FD` and printed nothing, not
even the test's first line:

- **Nothing printed.** stdout into a pipe is buffered, and the crash took the buffer with it. The
  smoke test now writes stdout unbuffered, so a crash can no longer hide what it printed.
- **The crash itself.** A `MainComponent` holds an engine with 128 track slots, several
  megabytes. A Debug build reserves the stack for *every* block of a function at once, and the
  two new sections pushed `runSmokeTest()` past the 1 MB stack. The windows of the new sections
  now live on the heap.

### Broken on purpose

<div align="center">

| Broken on purpose | Caught by |
|---|---|
| the track snapshot leaves out the inserts | 6e: the bass comes back with 0 inserts; 6f: the project differs |
| undo puts a deleted track at the end, not at its place | 6e and 6f: the project differs |
| the fader never ends its merge | 6e: two drags become one entry |
| the effect panel writes the effect directly | 6e: undo does not return the threshold |
| saving does not mark the history | 6e: the title keeps its `*` after a save |

</div>

Two controls first slipped through, and both were weak tests, not code that worked by luck:

- **The fader control crashed.** The test called the fader's `onDragStart` without checking that
  it was set. It now does what a real drag does: it calls the callback only if it is there.
- **The save control passed.** At the moment of the save, the test had undone every change, so
  the history was empty and a clean title proved nothing. The test now renames a track first, so
  the save has something to save.

## Try it

```powershell
.\scripts\build.ps1 -Only 15-daw-growing -Juce
& ".\build\bin\Debug\DawLab 15.exe"
```

Delete a few tracks, move clips, turn knobs in an effect panel, then press Ctrl+Z until the menu
says there is nothing left, and Ctrl+Shift+Z all the way back. Save, change something, and watch
the `*` come and go with undo and redo. Close the window with unsaved changes.

## Limits of this step

- Changes inside a VST3 plugin's own editor are not undoable (see above).
- The history keeps 200 entries and is not saved with the project.
- Undoing a delete restores the track's plugin from its saved state. A plugin that is no longer
  installed cannot come back, and that undo step does not apply.

## What you learned

- Undo is "apply the inverse": one function per kind of edit both changes and describes the way
  back, and the history only keeps those values.
- Name things in an edit by what never changes (ids), and look up where they are only when the
  edit is applied.
- A deleted thing is kept as a snapshot of values. Anything only another layer can build (a
  plugin) comes from a factory that the layer passes in.
- A gesture is one step: close the merge where the gesture starts and where it ends.
- "Modified" is a property of the history (the serial at the save), not a flag that every change
  must remember to set.
- When the result is almost equal, find the reason before you widen a tolerance. Here it was the
  order of a float sum, and the test now says exactly when it may differ.
