# Step 1 - Stable track IDs

**English** | [繁體中文](step-01-track-ids.zh-TW.md)

[Overview](../README.md) | Previous: [Chapter 14](../../14-daw-8track/README.md) | Next: Step 2 - A pool of tracks (planned)

## Goal

Give every track an identity that survives adding, deleting and reordering tracks, so that
everything that refers to a track - an undo entry, a command on its way to the audio thread, the
GUI's selection, a saved project - keeps referring to the *same* track.

## Design

### Why an index stops working

In chapter 14 a track *is* its index: the mixer holds `std::array<Track, 8>`, and `ChannelRef`,
`Session::clips_`, the arrangement lanes and the commands all say "track 1". Nothing ever moves,
so the index is a perfectly good name.

Now let the user delete a track. Four tracks, and two edits recorded while they were there
(think of two entries in an undo history, or two commands still in the queue):

<div align="center">

| | Drums | Bass | Keys | Vox |
|---|---|---|---|---|
| index before | 0 | 1 | 2 | 3 |
| id | 1 | 2 | 3 | 4 |
| index after deleting Drums | - | 0 | 1 | 2 |

</div>

The edits were "Bass to -6 dB" and "Vox to -3 dB". Stored as indices they read "track 1 to -6 dB"
and "track 3 to -3 dB". After the delete, track 1 is *Keys* and track 3 does not exist: one edit
hits the wrong track, the other is lost. Neither failure makes a sound or crashes - the wrong
track simply gets quieter. That is what makes index bugs so expensive to find.

### Four ways to name a track

<div align="center">

| Name a track by... | Survives delete / reorder? | Problem |
|---|---|---|
| its index | no | shifts whenever a track above it is deleted or moved |
| a pointer to it | as long as it lives | dangles after a delete; a `std::vector` may move all tracks when it grows; cannot be saved to a file |
| its name | yes | not unique ("Audio 1" twice), and the user renames tracks |
| **an id** | **yes** | none - it is only a number, so it needs a lookup |

</div>

An id is a plain number that the track list hands out when the track is created and that never
changes afterwards. It can be copied into a command, written into a project file and compared
across threads, because it is just an integer.

### Never reuse an id

The counter only counts up. If the list reused the id of a deleted track, a stale reference (an
old undo entry for the deleted Vox, id 4) would suddenly find the *new* track that got id 4 and
change it - the same silent bug as with indices, only rarer and therefore worse. With 32 bits and
one new track per second, the counter lasts 136 years.

`TrackId{}` (value 0) means "no track"; a lookup with it always fails, so a default-constructed
reference can never hit a real track.

### Identity, slot and position

An id answers *which* track. Two other numbers will appear in this chapter, and keeping the three
apart is the point of this step:

- **id** - which track this is. Never changes. Used by everything outside the audio thread.
- **position** (display index) - where the track is drawn, 0 = top. Changes on every move or
  delete. Only the GUI should care.
- **slot** (Step 2) - where the track's pre-allocated audio objects live in the engine's pool.
  Fixed for the track's lifetime, so the audio thread can use it without lookups.

The audio thread never searches for an id: the UI thread translates id -> slot once and sends the
slot. Ids are for the model; slots are for the engine.

## The code

`steps/step01-track-ids/minidaw.h`:

```cpp
struct TrackId {
    std::uint32_t value = 0;                       // 0 = no track
    constexpr bool isValid() const noexcept { return value != 0; }
};

class TrackList {
public:
    TrackId add(std::string name);                 // id = nextId_++, append
    bool remove(TrackId id);                       // erase; the counter is not touched
    int indexOf(TrackId id) const noexcept;        // display position, -1 if gone
    bool move(TrackId id, int newIndex);           // reorder; the id stays
    TrackInfo* find(TrackId id) noexcept;          // never returns a *different* track
    ...
private:
    std::vector<TrackInfo> tracks_;                // in display order
    std::uint32_t nextId_ = 1;                     // only ever counts up
};
```

`indexOf()` is a linear search. That is deliberate: the track list lives on the UI thread, holds
at most a few hundred tracks, and is searched only when the user does something. A hash map from
id to position would have to be rebuilt after every move or delete anyway.

`move(id, newIndex)` takes the track out and inserts it again, so `newIndex` is a position in the
list *without* the moved track; that is what makes `indexOf(id) == newIndex` true afterwards.
Out-of-range targets are clamped, the way dragging a track below the last one puts it last.

## Your turn

`steps/step01-track-ids/minidaw.h` has `TrackId`, `TrackInfo`, the data members and helpers of
`TrackList`, and two kinds of recorded edits with `applyEdits()` (by id) and `applyIndexedEdits()`
(by index, the chapter-14 way).

<div align="center">

| | |
|---|---|
| You write | `TrackList::add()`, `remove()`, `indexOf()` and `move()` |
| File | `steps/step01-track-ids/starter/step01.cpp` |
| Run | `l15_step01_starter`; it prints `[FAIL] l15_step01 (18 of 25 checks failed)` until you are done |
| Solution | `steps/step01-track-ids/solution/step01.cpp` (ctest `l15_step01_solution`) |

</div>

Expected output (verified) - the worked example above, then id reuse, reordering, and 2000 random
operations compared against a reference model:

```
Step 1: stable track IDs (build-along)
  ids: Drums 1, Bass 2, Keys 3, Vox 4 (expected 1, 2, 3, 4)
  after deleting Drums: Bass, Keys, Vox (expected Bass, Keys, Vox)
  by index: 1 of 2 applied, Bass 0.0 dB, Keys -6.0 dB, Vox 0.0 dB (the chapter-14 way: wrong)
  by id:    2 of 2 applied, Bass -6.0 dB, Keys 0.0 dB, Vox -3.0 dB (expected -6.0, 0.0, -3.0)
  delete Vox (id 4), add Pad: Pad gets id 5 (expected 5, not 4); Drums found: no, Vox found: no (expected no, no)
  move Pad to 0, Bass to 99, Keys to -5: Keys, Pad, Bass (expected Keys, Pad, Bass); Bass still -6.0 dB
  2000 random add / remove / move: 0 mismatches, 0 reused ids, 289 tracks left (expected 0, 0)
[PASS] l15_step01 (25 checks passed)
```

## What you learned

- An index names a *place*, not a track; it breaks as soon as tracks can be deleted or moved.
- A track id is handed out once, never changes and is never reused; 0 means "no track".
- A lookup by id either finds the right track or nothing - never a different one.
- Keep three numbers apart: id (which track), position (where it is drawn), slot (where its audio
  objects live, Step 2).
