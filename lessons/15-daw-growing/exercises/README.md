# Chapter 15 - Exercises
**English** | [繁體中文](README.zh-TW.md)

[Back to the chapter](../README.md) | [Solutions](../solutions/README.md)

Difficulty: ★☆☆ easy, ★★☆ medium, ★★★ hard. Implementation exercises have a starter file in
`exercises/starter/` that compiles and runs; until you complete it, its self-check prints `FAIL`
(that is expected). Build target names are given in brackets. None of them needs JUCE:

```powershell
.\scripts\build.ps1 -Only 15-daw-growing
.\build\bin\Debug\l15_ex3_starter.exe
```

**Exercises 1-6** are the chapter's exercise set, about the steps (no JUCE). **Exercises 7-9 are
optional projects** that extend the application DawLab 15. They need a JUCE build
(`.\scripts\build.ps1 -Only 15-daw-growing -Juce`); like chapter 14's JUCE exercises they are console
programs that build the real `MainComponent` in its offline mode, so no audio device and no clicking
is needed to check them, and none of them changes the app.

Exercises 3 and 5 build on the model of Step 5 (`exercises/model.h`: Step 5's `minidaw.h` with its
functions solved), Exercise 4 on Step 4's. Do the steps first.

---

## Exercise 1 - Who owns a deleted track? ★☆☆ (concept)

Answer in a few sentences each.

1. Chapter 14 named tracks by their index (0-7). Give a concrete sequence of user actions after
   which an undo entry that says "set the gain of track 3" changes the wrong track. Why can a
   `TrackId` not go wrong in the same sequence?
2. The user deletes Bass. Which thread frees what: the UI's entry in the slot table, the slot on the
   audio thread, Bass's clip source? When is each one safe to free, and who decides that?
3. `removeTrack()` gives slot 1 to the next `addTrack()` at once, before the audio thread has
   processed the remove. Why is that safe without waiting for an answer? What would break if the
   commands went through two queues instead of one?
4. Undoing "add track" removes the track, but the id counter does not go back; opening a project
   reads `nextTrackId` from the file. Give one example each of what would go wrong if the counter
   did go back after an undo, or were not saved in the file.
5. Step 5 says what is saved in the project file is undoable, what only shows or plays it is not.
   Sort these, and give a reason for the two that are least obvious: renaming a track, the playhead
   position, zooming the arrangement, soloing a track, moving a clip, the selected track, the CPU
   meter's warning, a fader drag.

## Exercise 2 - The CPU budget by hand ★★☆ (calculation)

A machine at 48 kHz needs, for a block of $`N`$ samples with $`n`$ tracks,

```math
t = o + N\,(f + n\,c), \qquad o = 50\ \mu\text{s},\ f = 0.40\ \mu\text{s},\ c = 0.15\ \mu\text{s}
```

($`o`$ the callback itself, $`f`$ master and buses per sample, $`c`$ each track with its inserts per
sample). Compute by hand (show the formulas), then run [`l15_ex2_solution`] to check:

- (a) The budget of a block of 64, 256 and 1024 samples.
- (b) The load of 20 tracks at those three sizes. Why is it different, although the cost per sample
  is the same?
- (c) The most tracks that stay below `CpuMonitor`'s High mark (75 %) at each size.
- (d) One block in 200 takes 2.2 times as long (a page fault, a plug-in cleaning up). What happens
  with 100 tracks at 256 samples: the average load, the slow block's load, the overruns per minute,
  and what `CpuMonitor` shows (it holds Overload for 2 s)? How many tracks never overrun?
- (e) A steady 40 % load, then one block at 130 %. What does `CpuMeter` show right after it, at 64
  and at 256 samples ($`\tau = 0.3`$ s)? What does the user learn from the meter, and what from
  the overrun count?

## Exercise 3 - Duplicate a track ★★☆ (implementation)

[`l15_ex3_starter`, file `exercises/starter/ex3_duplicate_track.cpp`]

Write the "Duplicate Track" command on Step 5's model, as two functions:

- `copyName(project, name)`: "Bass" -> "Bass (2)", or "Bass (3)" if that is taken. A trailing
  " (n)" with digits is a number, not part of the name, so a copy of "Bass (2)" is "Bass (3)", not
  "Bass (2) (2)". The smallest free number wins.
- `duplicateTrack(project, id)`: the `Change` that duplicates the track - or nothing if there is no
  such track. It is one `AddTrack`: the next id, the copy's name, the same gain and pan, right after
  the original, with a copy of each of its clips appended to the clip list.

The self-check duplicates Bass twice and a copy of it, undoes and redoes, deletes the original and
requires the copies to keep their clips, and finally undoes everything: the project is as before,
except that no id came back.

Before you start: why does duplicating need no new kind of edit, and no new undo code?

## Exercise 4 - Format 3, and lines from a newer version ★★★ (implementation)

[`l15_ex4_starter`, file `exercises/starter/ex4_format3.cpp`]

The starter is Step 4's reader and writer on the types of `exercises/ex4_format3.h`. Change it so
that:

- **Format 3** adds a mute flag to each track (`track 2 "Bass" -6 0.25 1`). Files of format 1 and 2
  still open, with no track muted; the program writes format 3.
- A version can have a **minor number**: `DawLabProject 3.2`. A minor version only adds lines that
  an older program may skip. A file of a newer minor version opens: each line with an unknown
  keyword is kept as it is (with a warning naming its line) and written back on saving, after the
  known lines. Then the file says `DawLabProject 3.2` again, or this program would refuse its own
  file.
- A newer **major** version (4, 4.0) is still refused, and so is an unknown keyword in a version this
  program knows completely (3, 3.0, 2): that file is broken, not newer.

The self-check opens files of formats 1, 2, 3 and 3.2 (with CRLF line ends and a comment in a kept
line), saves and opens them again, and refuses broken headers such as `3.`, `.2` and `3.1.1`.

Think about it: a kept line from 3.2 says `colour 2 "#ff8800"` - a colour for track 2. The user
deletes track 2 in this older version and saves. What will the newer version find, and what should
it do? What does that tell you about which kinds of lines a minor version may add?

## Exercise 5 - An undo history limited by memory ★★★ (implementation)

[`l15_ex5_starter`, file `exercises/starter/ex5_byte_budget.cpp`]

Step 5's history keeps the last 100 entries, whatever they hold. But the inverse of deleting a track
holds its clips, and in the app a clip holds its audio in memory: as long as an undo entry can bring
it back, that audio cannot be freed. Write `BudgetHistory`, which keeps entries within a byte budget:

- `bytes()` is the sum of `changeBytes()` (provided) over all undo and redo entries, kept up to date
  as entries come and go;
- above the budget, the oldest undo entry is dropped first, then, with none left, the redo entry
  farthest away;
- the last remaining entry is never dropped, even when it alone is over the budget.

The starter is Step 5's history without the byte counting. The self-check deletes 4 MB tracks under
a 10 MB budget, makes a thousand small edits that all fit, merges a fader drag, undoes an add (which
makes the redo entry *larger* than the undo entry was), and runs 4000 random edits, undos and redos
comparing `bytes()` with a full recount after each.

Think about it: the budget counts the audio of a deleted clip. What if the same audio is also used
by a clip that is still in the project?

## Exercise 6 - Saving without losing anything ★★☆ (design)

Step 4 saves through a temporary file and a rename. Design, in writing, how the app keeps the user's
work safe beyond that. Cover:

1. **Autosave.** Where it writes (not over the user's file - why not?), how often, and on which
   thread. Writing a project with VST3 state and audio paths takes time: what may the autosave read
   from the session while the user keeps editing?
2. **After a crash.** At the next start, how does the app know that an autosave is newer than the
   project file, and what does it ask the user?
3. **The audio.** The project file names audio files; a recorded or imported file may not be next
   to it yet. What must exist on disk before the project file that names it is renamed into place?
4. **A missing file at opening.** Step 8 opens a project whose audio file is gone with a warning.
   What should the app keep, so that saving that project again does not lose the clip for good?

## Exercise 7 - Reorder tracks by dragging ★★☆ (implementation, JUCE)

[`l15_ex7_starter`, file `exercises/starter/ex7_reorder_tracks.cpp`]

DawLab 15 adds and deletes tracks, but cannot move one. Add it from outside the app:

- `moveTrack(session, id, index)`: the track goes to display position `index` (clamped), as **one**
  undo step "Move Track". The `Session` has no move edit - but `RemoveTrack` followed by an
  `AddTrack` of the track's snapshot (`Session::snapshotTrack`) at the new index is one, and the
  history already knows both inverses.
- `TrackDragger`, a `juce::MouseListener` for the `ArrangementView`: press on a track header, drag
  up or down, release. Less than 4 pixels is a click (which selects); above the lanes means "to the
  top". `getDropIndex()` says where a line would be drawn while dragging.

The self-check moves a track and requires the same id, name, gain, inserts and clips (the same audio
buffers), one undo entry, a working undo and redo, and no entry when nothing moves. Then it sends
mouse events: a click across a lane edge, a drag from lane 0 to lane 2 (the selection follows the
track), a drag up into the ruler, and a press on a clip.

Think about it: what does "remove and add again" cost on the audio thread that a real move edit
would not? When would you add a `MoveTrack` edit to the `Session` instead?

## Exercise 8 - Recent projects and autosave ★★☆ (implementation, JUCE)

[`l15_ex8_starter`, file `exercises/starter/ex8_recent_and_autosave.cpp`]

Exercise 6 designed it; now build two parts of it:

- `RecentProjects`: the last 8 projects, newest first, in the app's settings (a
  `juce::PropertiesFile`), written at once. `follow(main.getProjectFile())` from the UI timer notes
  every newly opened or saved project without touching Open or Save. `addToMenu()` adds the
  submenu "Open Recent" (one item per existing file, then "Clear Menu"; or a disabled "No recent
  projects"), for `MainComponent::onExtendMenu`.
- `Autosaver::poll(now)`: at most once a minute, while the history says the project is modified and
  its text differs from the last autosave, the project is written with `ProjectIO::save` to
  `Song.autosave.dawlab` next to `Song.dawlab` (or `Untitled.autosave.dawlab` in a folder of the
  app's), never over the user's file and without marking it saved. When the project is no longer
  modified - saved, or undone back - the autosave is deleted. `newerAutosave()` finds one that is
  newer than the project: what a crash left behind.

The self-check fills the list past 8, deletes a file, rereads the settings, opens a project through
the menu item's action, and runs the autosave through a minute of polls, a crash (the autosave opens
as the project was), a save, an undo back to the saved state and an untitled project.

## Exercise 9 - Find missing audio before opening ★★★ (implementation, JUCE)

[`l15_ex9_starter`, file `exercises/starter/ex9_relink_audio.cpp`]

Step 8 opens a project whose audio file is gone without its clips; saving it then loses them. Write
what a DAW does before opening:

- `findMissingAudio(project, state)`: the distinct files that are not there, each with the frames its
  clips need (the largest `sourceOffset + length`) and how many clips use it.
- `checkCandidate(missing, file, rate)`: why a file cannot stand in - not an audio file, or too short
  at its own sample rate - or nothing if it can.
- `relink(project, ask)`: for each missing file, first look in the folders where earlier ones were
  found (a renamed media folder: find one, the rest follow), otherwise `ask()` the user; refuse bad
  candidates with a reason; try the found folders once more at the end; then keep the old file as
  `Song.dawlab.bak` and write the new paths (relative inside the project's folder, absolute
  outside).

The self-check renames the media folder of the demo song (23 files), cancels one, offers a file that
is too short, and requires the rest to be found after one good answer; the project then opens with
every clip. It also moves the audio out of the project's folder (absolute paths) and gives a broken
project file.

Think about it: the check only compares lengths. A file with the right name and length can still be
the wrong one. How could the relinker tell? (Look at how Step 8 names its media files.)

---

[Back to the chapter](../README.md) | [Solutions](../solutions/README.md)
