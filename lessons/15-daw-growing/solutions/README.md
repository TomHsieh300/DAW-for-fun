# Chapter 15 - Solutions
**English** | [繁體中文](README.zh-TW.md)

[Back to the chapter](../README.md) | [Exercises](../exercises/README.md)

Run all solution programs with `.\scripts\build.ps1 -Only 15-daw-growing -TestLabel solution` (add
`-Juce` for Exercises 7-9). Every one ends with `[PASS] l15_exN (... checks passed)`.

---

## Exercise 1 - Who owns a deleted track?

1. **Indices.** Tracks Drums, Bass, Keys, Vox are 0-3. The user drags Vox's fader from -6 to 0 dB:
   the undo entry says "track 3 back to -6 dB". Then they delete Bass: Vox is now track 2. They add
   a Pad, which becomes track 3. Undo sets the *Pad* to -6 dB, and Vox keeps 0 dB. An index says
   where a track is *now*; an entry is used *later*. A `TrackId` is handed out once and never again,
   so the entry finds Vox wherever it is - or, if Vox was deleted, nothing (Step 1: a lookup finds
   the right track or none, never a different one).
2. **Who frees what.**
   - The UI thread frees its own entry in the slot table at once, in `removeTrack()`: nobody else
     reads that table.
   - The slot on the audio thread is never freed: it is one of the `kMaxTracks` slots allocated in
     `prepare()`. When the audio thread processes `RemoveTrack` (`Mixer::deactivateTrack()`), it
     resets the slot's own state - above all its latency-compensation delay line, so that the next
     track in the slot does not start by playing the end of Bass - and marks it free.
   - The clip list and the inserts may still be in use: the audio thread could be playing them right
     now, and the command may still be in the queue. Only the audio thread knows when it has stopped
     using them, so it hands them back through the garbage queue (up to `kMaxInserts + 1` objects for
     one command), and the UI thread deletes them in `collectGarbage()`. The audio thread never
     frees anything.
3. **Reusing a slot at once.** There is one command queue and it keeps the order. The audio thread
   therefore always sees `RemoveTrack(Bass, slot 1)` before `AddTrack(Vox, slot 1)`, so by the time
   Vox arrives the slot is free on its side too. With two queues - say structural commands in one
   and values in another - a fader value for Vox could arrive before Vox's `AddTrack`, while Bass
   is still in slot 1. The id in the command catches that (the slot holds a different id, so the
   command does nothing), but it is lost, not delayed. One queue makes the order a guarantee.
4. **The id counter.**
   - If undoing "add Pad (id 7)" put the counter back to 7, the next new track would get id 7. The
     redo entry of the undone add still says "add id 7", and any other entry or curve that named
     the old Pad would now find the new track.
   - If the file did not save `nextTrackId`, opening it would start at the highest id in the file
     plus one. If the user had deleted the track with the highest id before saving, that id is
     handed out again. Chapter 16 keeps automation lanes whose track is gone, so the new track
     would inherit the deleted track's automation.
5. **Into the history:** renaming a track, soloing a track, moving a clip, a fader drag (as one
   entry). **Not:** the playhead position, zooming, the selected track, the CPU warning.
   - **Soloing** is the least obvious one. It looks like a way of *listening*, but DawLab saves solo
     in the project file, so it changes the project: a user who saves after soloing and reopens
     finds the solo, and undo must be able to take it back.
   - **A fader drag** sends 50 changes, but it is one gesture: they are merged into one entry
     (Step 5's merge key), or Ctrl+Z would have to be pressed 50 times.

## Exercise 2 - The CPU budget by hand

The block time is $`t = o + N(f + nc)`$, the budget $`T = N / f_s`$, the load $`t / T`$.

- **(a)** $`T = 64/48000 = 1.3333`$ ms, $`256/48000 = 5.3333`$ ms, $`1024/48000 = 21.333`$ ms.
- **(b)** With $`n = 20`$, $`f + nc = 3.4\ \mu`$s per sample:

  <div align="center">

  | Block | Time per block | Load |
  |---|---|---|
  | 64 | $`50 + 64 \cdot 3.4 = 267.6\ \mu`$s | 20.07 % |
  | 256 | $`50 + 256 \cdot 3.4 = 920.4\ \mu`$s | 17.26 % |
  | 1024 | $`50 + 1024 \cdot 3.4 = 3531.6\ \mu`$s | 16.55 % |

  </div>

  The work per sample is the same; the 50 µs per callback is not. It is 3.75 % of a 64-sample
  budget and 0.23 % of a 1024-sample budget: small buffers cost CPU as well as giving low latency.
- **(c)** Solve $`o + N(f + nc) \le 0.75\,T`$ for $`n`$:

  ```math
  n \le \frac{(0.75\,T - o)/N - f}{c}
  ```

  64: $`(1000 - 50)/64 = 14.844`$, $`(14.844 - 0.4)/0.15 = 96.3`$ -> **96** tracks;
  256: $`(4000 - 50)/256 = 15.430`$ -> $`100.2`$ -> **100**; 1024: $`(16000 - 50)/1024 = 15.576`$ ->
  $`101.2`$ -> **101**.
- **(d)** 100 tracks at 256: $`50 + 256 \cdot 15.4 = 3992.4\ \mu`$s, a load of **74.86 %** - just
  below High. The slow block takes $`2.2 \times 74.86 = 164.7`$ %: an overrun. There are
  $`48000 \cdot 60 / 256 = 11250`$ blocks a minute, so **56.25 overruns a minute**, one every
  1.07 s. `CpuMonitor` holds Overload for 2 s after each one, so it **never leaves Overload** -
  although the average is below the High mark. That is the point of counting overruns separately:
  the average looks fine, the user hears a click every second. No overrun means
  $`2.2 \cdot \text{load} \le 1`$, a load of at most 45.45 %: **59 tracks**. The real limit is
  the worst block, not the average.
- **(e)** $`\bar\ell \leftarrow \ell + a(\bar\ell - \ell)`$ with $`a = e^{-T/\tau}`$:
  at 64, $`a = 0.995565`$, the meter shows $`1.3 + 0.995565 \cdot (0.4 - 1.3) = 40.40`$ %;
  at 256, $`a = 0.982379`$: **41.59 %**. Either way the meter hardly moves - one block is 1.3 ms
  or 5.3 ms of a 300 ms time constant. The meter tells the user how busy the machine is over time;
  only the overrun count (1) tells them that something was heard.

## Exercise 3 - Duplicate a track

[`solutions/ex3_duplicate_track.cpp`](ex3_duplicate_track.cpp)

- `copyName()` looks for a trailing `" ("`, digits and `")"`, and takes the part before it as the
  base; then it tries `base (2)`, `base (3)`, ... until no track has that name.
- `duplicateTrack()` builds one `AddTrack` at `index + 1`, with the id `nextTrackId` and the clips
  of the original at positions `clips.size()`, `clips.size() + 1`, ...

**Why no new edit and no new undo code:** a duplicate *is* an added track that happens to have
contents. `AddTrack` already carries a track with its clips (it is how undo brings a deleted track
back), and `applyEdit()` already returns its inverse, `RemoveTrack`. The command is a function that
*describes* a change; Step 5 does the rest.

```text
Exercise 3 - duplicate a track
[PASS] l15_ex3 (41 checks passed)
```

## Exercise 4 - Format 3, and lines from a newer version

[`solutions/ex4_format3.cpp`](ex4_format3.cpp) - Step 4's solution with three changes, each marked
"Exercise 4":

- `parseVersion()` splits the header's version at the dot. Both parts must be whole numbers, so
  `3.`, `.2`, `3.x` and `3.1.1` are refused.
- The track line has 6 tokens in format 3, and the mute flag must be 0 or 1.
- An unknown keyword is kept only when `major == 3` and `minor > 0`; then the raw line (without its
  `\r`) goes into `keptLines`, and the minor version into `keptMinor`. The writer appends the kept
  lines and writes `3.<keptMinor>` when there are any.

```text
Exercise 4 - format 3, and lines from a newer version
  line 3: "colour" is from DawLab format 3.2, kept as it is
  line 5: "marker" is from DawLab format 3.2, kept as it is
[PASS] l15_ex4 (40 checks passed)
```

**The colour of a deleted track.** The newer version opens the file and finds `colour 2 "#ff8800"`
for a track 2 that does not exist. It must not refuse the file - the user did nothing wrong - but
treat the line as stale: drop it with a warning, as Step 8 does with a clip whose audio is gone.
The lesson for the format: a minor version may only add lines that (1) can be skipped without
changing what the known lines mean, and (2) the newer version can check again when it reads them,
because an older version may have changed what they refer to. A line that changes the meaning of
another one ("the gains are now in a different unit") needs a new major version.

## Exercise 5 - An undo history limited by memory

[`solutions/ex5_byte_budget.cpp`](ex5_byte_budget.cpp)

Each entry stores its size, so dropping one never recounts. Every place that adds or removes an
entry changes `bytes_` by that size: clearing the redo stack in `perform()`, the new undo entry,
and both entries in `undo()` and `redo()`. `trim()` runs after every addition:

```cpp
while (bytes_ > byteLimit_ && undo_.size() + redo_.size() > 1)
    drop undo_.front() if there is one, else redo_.front()
```

The case people miss is `undo()` making the history **larger**. Undoing an add turns the small entry
"remove track 3" into the redo entry "add track 3 with its 4 MB clip". So `undo()` must trim too,
and it can drop the oldest undo entry to make room for a redo.

```text
Exercise 5 - an undo history limited by memory
  3 deleted tracks of 4 MB, a budget of 10 MB: 2 entries, 8000376 bytes
  undoing an add of 4 MB next to 8 MB: 1 undo, 1 redo, 8000370 bytes
  4000 random edits, undos and redos: 0 broken (expected 0)
[PASS] l15_ex5 (1090 checks passed)
```

(The byte counts depend on `sizeof(Edit)`, so they differ a little between compilers.)

**Shared audio.** In the app, clips hold their audio through `std::shared_ptr`. If the deleted clip's
audio is also used by a clip still in the project (a duplicate, a clip cut in two), freeing the undo
entry frees nothing, and the budget counts that audio once too often. The cost of an entry is only
the audio that *nobody else* holds - and that changes with every edit to the project. A precise
budget would have to recount whenever the project changes. Counting too much is the safe side: the
history drops entries a little early, but never keeps more memory alive than the budget says.

## Exercise 6 - Saving without losing anything

One design; others are possible.

1. **Autosave.**
   - Write to a separate file next to the project (`Song.dawlab.autosave`), never over the user's
     file. The user decides when the project file changes: they may be trying something out and
     plan to close without saving.
   - About once a minute, and only when the history says the project is modified since the last
     autosave.
   - The writing happens on a background thread. It must work on a *copy*: the message thread takes
     a snapshot of the session (the text of `writeProject()`, or the model values it needs) and
     hands it over. The background thread then only writes bytes. It must not read the live session
     while the user edits it.
2. **After a crash.** Compare the autosave's modification time with the project file's; better, put
   the history's serial (or a counter) into both files when they are written. If the autosave is
   newer, ask: "DawLab did not close properly. Open the autosaved version from 14:32?" Keep both
   files until the user answers. After a normal save or a normal exit, delete the autosave.
3. **The audio.** Every audio file the project names must be complete on disk *before* the project
   file that names it is renamed into place. A new recording or an imported file is copied into the
   media folder first, flushed, then the project file is written and renamed. A crash in between
   leaves an unused audio file, never a project that names a missing one.
4. **A missing file at opening.** Keep the clip as a placeholder: position, length and the file name,
   shown in grey and silent, with the warning. Saving writes it back unchanged. Then the user can
   find the file later (Exercise 9 lets them choose it again). Leaving such clips out, as Step 8
   does, turns a missing disk into data loss at the next save.

## Exercise 7 - Reorder tracks by dragging

[`solutions/ex7_reorder_tracks.cpp`](ex7_reorder_tracks.cpp)

`moveTrack()` takes the snapshot first, then performs `{RemoveTrack{id}, AddTrack{snapshot, to}}` as
one change. The history stores the inverses, so undo needs nothing new. The track keeps its id, so
the selection, which the arrangement keeps by id, stays on it.

The dragger remembers the track under the press only in the header column, ignores movement below
4 pixels, and maps the mouse to a lane with `laneAt()`, or to the first or last index outside the
lanes. The move itself happens on release.

```text
Exercise 7 - reorder tracks by dragging
  dragged and moved: 2 undo entries, the order is back: yes
[PASS] l15_ex7 (36 checks passed)
```

**The cost.** Removing and adding a track again is not free: the engine deactivates its slot, sends
its inserts and clip list to the garbage queue, and builds new insert instances from the snapshot (a
VST3 plugin is loaded again from its file and state, which can take a second). Effects lose their
state - a reverb tail stops. A `MoveTrack` edit would only change the display order in the session:
the engine does not care about display order at all. Add one as soon as moving is common, or as soon
as tracks hold heavy plugins.

## Exercise 8 - Recent projects and autosave

[`solutions/ex8_recent_and_autosave.cpp`](ex8_recent_and_autosave.cpp)

- `noteProject()` removes the file wherever it is (comparing `juce::File`s, which compare as the
  file system does), puts it in front, cuts the list at 8 and writes the settings with
  `saveIfNeeded()` at once.
- `getFiles()` filters on `existsAsFile()` when reading, not when storing: a project on a disk that
  is not connected right now comes back when the disk does.
- The menu items carry their action (`PopupMenu::Item::setAction`), so the menu needs no command ids.
- `poll()` keeps the text it wrote last. A project that is modified but whose text is unchanged since
  the last autosave is not written again. When the project file changes (Open, Save As, New), the
  remembered text and time are reset.

```text
Exercise 8 - recent projects and autosave
  autosaves next to Song.dawlab and in the untitled folder: yes
[PASS] l15_ex8 (64 checks passed)
```

What this solution does not do yet, compared with Exercise 6: it writes on the message thread (a
large project with VST3 states takes noticeable time; a real app hands the captured state to a
background thread), and the autosave has its own copy of the audio in `Song.autosave_media`.

## Exercise 9 - Find missing audio before opening

[`solutions/ex9_relink_audio.cpp`](ex9_relink_audio.cpp)

- `findMissingAudio()` keeps a map from path to its entry, so each file is checked on disk once and
  every clip still adds to its `framesNeeded`.
- `checkCandidate()` converts the frames needed to the candidate's rate, because the opened clip is
  resampled to the project.
- `relink()` keeps a list of folders where something was found. Each missing file is looked for there
  first; only then is the user asked. A second pass at the end catches files that came before the
  folder was known.
- The project is rewritten only if something was found, through a `juce::TemporaryFile`, after the
  old one was copied to `.bak`.

```text
Exercise 9 - find missing audio before opening
  a renamed media folder of 23 files: asked 3 times, 22 found nearby, 1 refused
[PASS] l15_ex9 (33 checks passed)
```

**The wrong file with the right name.** Step 8 names each media file after a 64-bit hash of its
content (`01235f213fcc259d.wav`). So for DawLab's own media the relinker can do better than compare
lengths: read the candidate, compute the same hash, and refuse it if the hash is not its name. A
file the user imported from elsewhere keeps its own name; for those, the project file would have to
store the hash next to the path when it saves.

---

[Back to the chapter](../README.md) | [Exercises](../exercises/README.md)
