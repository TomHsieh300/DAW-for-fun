# Step 5 - Undo and redo

**English** | [繁體中文](step-05-undo.zh-TW.md)

[Overview](../README.md) | Previous: [Step 4 - Saving and opening projects](step-04-project-files.md) | Next: [Step 6 - The engine with a track pool](step-06-engine-pool.md)

## Goal

Let the user take back any change to the project and do it again - one change at a time, a fader
drag as a single step, several tracks deleted at once as a single step - and know at any moment
whether the project differs from what was last saved.

## Design

### Edits are values, and every edit returns its inverse

There are two classic ways to build undo:

<div align="center">

| Approach | How undo works | Cost |
|---|---|---|
| snapshots | copy the whole project before every change; undo puts the copy back | memory and time grow with the project, not with the change; a fader drag copies every clip |
| **commands** | record what changed; undo performs the opposite change | small entries; every kind of change needs a correct inverse |

</div>

This step uses commands, with one twist that keeps them small and hard to get wrong. An `Edit` is
a plain value - a `std::variant` of `AddTrack`, `RemoveTrack`, `SetGain`, `RenameTrack` and
`MoveTrack`, each naming its track by id. One function carries an edit out and returns the edit
that takes it back:

```cpp
std::optional<Edit> applyEdit(Project& p, const Edit& edit); // the inverse, or nullopt
```

The inverse is an ordinary `Edit` again. So undo is "apply the inverse", and applying the inverse
returns the inverse of the inverse - which is exactly the redo. There is one piece of code per
kind of edit, used for doing, undoing and redoing.

<div align="center">

| Edit | Remembers before changing | Inverse |
|---|---|---|
| `AddTrack` | - | `RemoveTrack` |
| `RemoveTrack` | the track, its position, its clips with their positions | `AddTrack` with all of that |
| `SetGain` | the old gain | `SetGain` with the old gain |
| `RenameTrack` | the old name | `RenameTrack` with the old name |
| `MoveTrack` | the old position | `MoveTrack` to the old position |

</div>

`RemoveTrack` deletes the track's clips as well, so its inverse carries them back - each at its old
position in the clip list, so that undo restores the project exactly, order included.

### Ids make the history reliable

An undo entry says "set the gain of track 2", never "of the second track". It still finds Bass
after other tracks were added, deleted or moved in between. And deleting Bass and undoing it
brings Bass back with the **same** id, so older entries that name Bass keep working. That is the
reason Step 1 never reuses an id.

### One thing is deliberately not undone: the id counter

Undoing "add Keys (id 3)" removes Keys - but `nextTrackId` stays at 4. If undo lowered the counter,
the next new track would get id 3, and a redo entry, a clipboard or a saved file that still says
"track 3" would find the wrong track. So "the project after undo" means the same tracks and clips,
not the same counter; the self-check compares exactly that, and checks separately that the
counter never goes down. In general: not everything in a model is user state. Allocators, caches
and counters stay where they are.

### One user action, one undo

A `Change` is a list of edits that belong to one action - deleting three selected tracks is one
change. `History::perform()` applies a change **all or nothing** (`applyAll()`): if the third edit
does not apply, the first two are taken back, and neither the project nor the history changes.
Undo applies the inverses in reverse order: the last edit is taken back first, as with anything
stacked.

### What goes into the history

<div align="center">

| Into the history | Not into the history |
|---|---|
| add, delete, rename, move a track | play, stop, locate, loop on/off |
| gain, pan, mute settings | scrolling, zoom, which track is selected |
| adding, moving, cutting clips | the CPU meter, peak meters |
| | opening the mixer window |

</div>

The rule: what is saved in the project file (Step 4) is undoable; what only shows or plays the
project is not. Someone who undoes "set gain" does not want playback to jump back as well.

### A fader drag is one step

A fader drag sends a gain change for every mouse move - 50 changes for one gesture. Nobody wants to
press Ctrl+Z 50 times. Each change is performed with a **merge key** ("the fader of track 2"); a
change with the same key as the newest entry is applied but not recorded, because the newest
entry's inverse already leads back to the gain before the drag. The merge ends when:

- the mouse button is released (`endMerge()`),
- any other change is performed,
- undo or redo is used, or
- the project is saved (`markSaved()`), so that continuing a drag after saving counts as a change.

### A new edit clears redo

After undoing two steps, the redo stack holds them. If the user now makes a new change, those two
steps belong to a future that no longer exists: the new change is applied to a different project
than the one they were recorded against. `perform()` clears the redo stack. (Some programs keep
an undo *tree* instead; that is a lot more machinery for a rarely used feature.)

### Limits, saving, and changes behind the history's back

- **Limit.** The history keeps the newest 100 entries (a constructor argument) and drops the
  oldest. A `std::deque` makes dropping from the front cheap.
- **Modified since save.** Every entry gets a serial number that is never reused, and an entry
  keeps its serial through undo and redo. `markSaved()` remembers the serial of the newest
  entry; the project is modified when the newest entry is a different one. That stays correct
  after undo, redo, new edits and dropped entries - and it is what puts the `*` in the window
  title in Step 7.
- **Changes behind its back.** If something changes the project without going through the
  history (a bug elsewhere), an old entry may no longer apply. `undo()` then refuses, leaves the
  project exactly as it is (all or nothing again) and drops that entry, instead of half-applying
  it.

### A C++ trap: `apply` and `std::apply`

The first draft of this step called the function `apply(Project&, const Edit&)`. It did not
compile: `Edit` is a `std::variant`, so argument-dependent lookup also searches namespace `std`
and finds `std::apply(F&&, Tuple&&)` from `<tuple>`. That template is an exact match for any
arguments, wins the overload resolution, and fails deep inside the standard library. A function
whose arguments come from `std` should not have a name that `std` already uses for a template -
hence `applyEdit`.

### How this maps to JUCE

`juce::UndoManager` stores `juce::UndoableAction` objects with virtual `perform()` and `undo()`,
groups them into transactions with `beginNewTransaction()`, and merges with
`UndoableAction::createCoalescedAction()`. The ideas are the same - an action, a transaction for
one user action, merging, a limit - and Step 7 can wrap the `History` in that interface or use
`UndoManager` directly with actions that hold an `Edit`. This step builds its own small history
to keep chapter 15 free of JUCE until the app, and because 150 lines show the whole mechanism.

## The code

`steps/step05-undo/minidaw.h` (abridged):

```cpp
struct AddTrack    { TrackState track; int index; std::vector<std::pair<int, ClipState>> clips; };
struct RemoveTrack { TrackId id; };
struct SetGain     { TrackId id; float gainDb; };
struct RenameTrack { TrackId id; std::string name; };
struct MoveTrack   { TrackId id; int index; };
using Edit = std::variant<AddTrack, RemoveTrack, SetGain, RenameTrack, MoveTrack>;
using Change = std::vector<Edit>;                       // one user action

AddTrack newTrack(const Project& p, std::string name);  // provided: uses nextTrackId
std::optional<Edit> applyEdit(Project& p, const Edit& edit);           // YOUR TURN
bool applyAll(Project& p, const Change& change, Change& inverses);     // provided: all or nothing

class History {
public:
    bool perform(Project& p, const Change& change, std::uint32_t mergeKey = 0); // YOUR TURN
    bool undo(Project& p);                                                     // YOUR TURN
    bool redo(Project& p);                                                     // YOUR TURN
    void endMerge();  void markSaved();  bool isModified() const;              // provided
private:
    struct Entry { Change edits; std::uint32_t mergeKey; std::uint64_t serial; };
    std::deque<Entry> undo_;   // oldest first
    std::vector<Entry> redo_;  // the next redo last
    ...
};
```

## Your turn

<div align="center">

| | |
|---|---|
| You write | `applyEdit()`, `History::perform()`, `History::undo()` and `History::redo()` |
| File | `steps/step05-undo/starter/step05.cpp` |
| Run | `l15_step05_starter`; it prints `[FAIL] l15_step05 (17 of 22 checks failed)` until you are done |
| Solution | `steps/step05-undo/solution/step05.cpp` (ctest `l15_step05_solution`) |

</div>

Write `applyEdit()` as one small `applyKind()` overload per kind of edit and dispatch with
`std::visit`. Each overload checks first and changes the project only when the edit applies. In
`undo()`, reverse a copy of the entry's edits, `applyAll()` it, and reverse the inverses you get
back, so the redo entry is in the original order again.

The last part of the check performs 3000 random operations - changes, fader drags, undos, redos,
mouse-ups - and compares the project after every one with a reference model that simply keeps a
copy of every state. It also checks that every path was actually taken: a random test that never
merges proves nothing about merging.

Expected output (verified):

```
Step 5: undo and redo (build-along)
  add Keys, Bass to -6 dB, delete Drums: Bass, Keys; 1 clip(s) left (expected Bass, Keys; 1)
  undo: Drums, Bass, Keys, Drums is id 1 again, clips in the old order: yes (expected Drums, Bass, Keys; 1; yes)
  undo, undo: back to the start yes, nextTrackId still 4 (expected yes, 4); a 4th undo: nothing to undo
  redo x3: the same as before the undos yes, Keys has id 3 again (expected yes, 3)
  undo the add of Keys, then add Pad: Pad's id 4 (expected 4); redo still possible: no (expected no)
  6 kinds of edit undone by their inverse: 6 of 6; 6 edits that do not apply: refused and harmless yes
  a fader drag of 50 steps: 1 undo entry, a second drag: 2; undo -> -10.0 dB, undo -> 0.0 dB
  (expected 1, 2, -10.0, 0.0)
  drag, undo, drag the same fader again: 2 undo entries; undo -> Drums -5.0 dB, Bass 0.0 dB
  (expected 2, -5.0, 0.0)
  delete Drums and Bass in one action, one undo: both back yes; a change with a failing edit: refused, gain 0.0 dB,
  history unchanged yes (expected yes, refused, 0.0, yes)
  history limit 10, 25 edits: 10 undos possible, then the state after edit 15: yes (expected 10, yes)
  modified: new no, edit yes, save no, undo yes, redo no, drag on after a save yes
  (expected no, yes, no, yes, no, yes)
  undo after Bass was deleted outside the history: refused, project unchanged yes, entry dropped yes
  (expected refused, yes, yes)
  3000 random operations (2733 changes, 1256 merged, 294 refused, 621 undos, 153 redos): 0 mismatches with the
  reference model, id counter went down 0 times (expected 0, 0)
[PASS] l15_step05 (22 checks passed)
```

## What you learned

- Make edits plain values and let applying an edit return its inverse: undo and redo are then
  the same operation, with one piece of code per kind of edit.
- Name tracks by id in every entry; bring a deleted track back with the same id and its clips in
  their old places.
- Not everything is undoable state: the id counter must never go back.
- Apply a user action all or nothing, and undo its edits last first.
- Merge a drag into one entry, and end the merge on mouse-up, other changes, undo, redo and save.
- A new edit clears redo; a serial number per entry tells whether the project was modified since
  the last save.
- Test undo against a model that keeps every state, and check that the random test reached every
  path.
