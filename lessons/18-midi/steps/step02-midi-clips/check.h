#pragma once
// check.h - the self-check of Step 2 (playing a MIDI clip). Defines main(); included at the END of
// starter/step02.cpp and solution/step02.cpp, so both run exactly the same checks.
//
// Two kinds of checks. Straight playback must give exactly the events of the clips, wherever the blocks
// fall. And whatever the transport and the user do - stop, seek, loop, mute, delete, edit - every note on
// gets exactly one note off, and nothing sounds after the transport stopped.

#include <dawlab/Common.h>

#include <cstdint>
#include <cstdio>
#include <random>
#include <tuple>
#include <vector>

namespace step02_check {

using namespace minidaw;

const Tempo kTempo{48000.0, 120.0}; // 25 samples a tick

// An instrument that complains: a note off for a note that is not on, events out of order or outside the
// block. It counts what sounds per channel and pitch.
struct StrictInstrument {
    int held[16][128] = {};
    long errors = 0;
    long ons = 0, offs = 0;

    void block(const std::vector<BlockEvent>& events, int numSamples) {
        int last = 0;
        for (const BlockEvent& e : events) {
            if (e.offset < last || e.offset >= numSamples)
                ++errors;
            last = e.offset;
            const Kind k = e.message.kind();
            int& h = held[e.message.channel()][e.message.note()];
            if (k == Kind::NoteOn) {
                ++h;
                ++ons;
            } else if (k == Kind::NoteOff) {
                ++offs;
                if (h == 0)
                    ++errors;
                else
                    --h;
            }
        }
    }
    int sounding() const {
        int n = 0;
        for (const auto& ch : held)
            for (const int h : ch)
                n += h;
        return n;
    }
};

// An event at an absolute sample, comparable: at one sample note offs first, then the rest by content.
using Flat = std::tuple<std::int64_t, int, int, int, int, int>; // sample, rank, status, data1, data2, -
inline Flat flat(std::int64_t sample, const MidiMessage& m) {
    return {sample, m.kind() == Kind::NoteOff ? 0 : 1, m.kind() == Kind::NoteOff ? (0x80 | m.channel()) : m.status, m.data1,
            m.kind() == Kind::NoteOff ? 0 : m.data2, 0};
}

// The clips' events written out by hand: what straight playback from 0 must give.
inline std::vector<Flat> reference(const std::vector<MidiClip>& clips) {
    std::vector<Flat> out;
    for (const MidiClip& c : clips) {
        if (c.muted)
            continue;
        for (const Note& n : c.notes) {
            if (n.start < c.offset || n.start >= c.offset + c.length)
                continue;
            const std::int64_t on = c.start + n.start - c.offset;
            const std::int64_t off = std::min(on + n.length, c.end());
            out.push_back(flat(sampleOfTick(on, kTempo), MidiMessage::noteOn(n.channel, n.pitch, n.velocity)));
            out.push_back(flat(sampleOfTick(off, kTempo), MidiMessage::noteOff(n.channel, n.pitch)));
        }
        for (const ControllerPoint& p : c.controllers)
            if (p.tick >= c.offset && p.tick < c.offset + c.length)
                out.push_back(flat(sampleOfTick(c.start + p.tick - c.offset, kTempo), MidiMessage::controller(p.channel, p.number, p.value)));
    }
    std::sort(out.begin(), out.end());
    return out;
}

inline MidiClip randomClip(std::mt19937& rng, int id) {
    std::uniform_int_distribution<std::int64_t> where(0, 16 * kTicksPerQuarter), len(1, 3 * kTicksPerQuarter);
    std::uniform_int_distribution<int> pitch(40, 80), ch(0, 2), count(5, 40), cc(0, 127);
    MidiClip c;
    c.id = id;
    c.start = where(rng);
    c.offset = id % 2 == 0 ? 0 : where(rng) / 4; // a trimmed start
    c.length = 2 * kTicksPerQuarter + where(rng);
    const int n = count(rng);
    for (int i = 0; i < n; ++i) {
        // Some exactly back to back on one pitch, some longer than the window, some of one tick.
        const std::int64_t s = i % 6 == 1 && !c.notes.empty() ? c.notes.back().start + c.notes.back().length : where(rng);
        const int p = i % 6 == 1 && !c.notes.empty() ? c.notes.back().pitch : pitch(rng);
        c.notes.push_back({s, i % 9 == 0 ? 1 : len(rng) * (i % 13 == 0 ? 8 : 1), p, 1 + i % 127, i % 6 == 1 && i > 0 ? c.notes.back().channel : ch(rng)});
    }
    std::stable_sort(c.notes.begin(), c.notes.end(), [](const Note& a, const Note& b) { return a.start < b.start; });
    for (int i = 0; i < 12; ++i)
        c.controllers.push_back({where(rng), i % 2 == 0 ? 1 : 74, cc(rng), ch(rng)});
    std::stable_sort(c.controllers.begin(), c.controllers.end(), [](const ControllerPoint& a, const ControllerPoint& b) { return a.tick < b.tick; });
    return c;
}

inline void checkPlace() {
    MidiClip c;
    c.start = 1000;
    c.length = 500;
    c.offset = 200; // the window shows material 200..700 at timeline 1000..1500
    std::int64_t on = -1, off = -1;
    DAWLAB_CHECK(c.place({300, 100, 60, 100, 0}, on, off) && on == 1100 && off == 1200);
    DAWLAB_CHECK(c.place({200, 10, 60, 100, 0}, on, off) && on == 1000 && off == 1010);
    DAWLAB_CHECK(c.place({650, 400, 60, 100, 0}, on, off) && on == 1450 && off == 1500); // ends at the window's end
    DAWLAB_CHECK(c.place({699, 1, 60, 100, 0}, on, off) && on == 1499 && off == 1500);
    DAWLAB_CHECK(!c.place({199, 50, 60, 100, 0}, on, off)); // starts before the trimmed start
    DAWLAB_CHECK(!c.place({700, 50, 60, 100, 0}, on, off)); // after the window
}

// Plays from 0 to `length` in blocks, returns every event at its absolute sample.
inline std::vector<Flat> playStraight(const std::vector<MidiClip>& clips, std::int64_t length, int blockSize, std::mt19937* rng,
                                      long& errors, int& sounding) {
    ClipPlayer player;
    StrictInstrument instrument;
    MiniTransport tr;
    tr.play();
    std::vector<BlockEvent> events;
    events.reserve(8192);
    const size_t capacity = events.capacity();
    std::vector<Flat> all;
    std::uniform_int_distribution<int> size(1, 1024);
    for (std::int64_t done = 0; done < length;) {
        const int n = static_cast<int>(std::min<std::int64_t>(rng != nullptr ? size(*rng) : blockSize, length - done));
        const Block b = tr.advance(n);
        player.process(clips, b, kTempo, events);
        instrument.block(events, n);
        for (const BlockEvent& e : events)
            all.push_back(flat(done + e.offset, e.message));
        done += n;
    }
    errors = instrument.errors + (events.capacity() != capacity ? 1000 : 0);
    sounding = instrument.sounding();
    std::sort(all.begin(), all.end());
    return all;
}

inline void checkStraight() {
    std::mt19937 rng(218);
    int wrong = 0, runs = 0;
    long events = 0;
    for (int round = 0; round < 6; ++round) {
        std::vector<MidiClip> clips;
        for (int i = 0; i < 3; ++i)
            clips.push_back(randomClip(rng, i + 1));
        // No clip at the very start: playing from 0 would chase its controllers (tested by hand).
        for (MidiClip& c : clips)
            c.start = std::max<std::int64_t>(c.start, 1);
        const std::vector<Flat> ref = reference(clips);
        std::int64_t end = 0;
        for (const MidiClip& c : clips)
            end = std::max(end, c.end());
        const std::int64_t length = sampleOfTick(end, kTempo) + 1000;
        for (const int block : {64, 512, 1000, 0}) {
            long errors = 0;
            int sounding = 0;
            const std::vector<Flat> got = playStraight(clips, length, block, block == 0 ? &rng : nullptr, errors, sounding);
            wrong += got == ref && errors == 0 && sounding == 0 ? 0 : 1;
            ++runs;
            events += static_cast<long>(got.size());
        }
    }
    std::printf("  %d straight runs (%ld events) in blocks of 64, 512, 1000 and random sizes: %d differ from the clips\n", runs, events, wrong);
    DAWLAB_CHECK(wrong == 0);
}

// One clip, one bar: C4 for the whole bar, E4 for the first half, the mod wheel at 10 then 20.
inline std::vector<MidiClip> oneBar() {
    MidiClip c;
    c.id = 7;
    c.start = 0;
    c.length = 4 * kTicksPerQuarter;
    c.notes = {{0, 4 * kTicksPerQuarter, 60, 100, 0}, {0, 2 * kTicksPerQuarter, 64, 90, 0}};
    c.controllers = {{0, 1, 10, 0}, {2 * kTicksPerQuarter, 1, 20, 0}};
    return {c};
}

inline int count(const std::vector<BlockEvent>& e, Kind k) {
    int n = 0;
    for (const BlockEvent& x : e)
        n += x.message.kind() == k ? 1 : 0;
    return n;
}

inline void checkByHand() {
    std::vector<MidiClip> clips = oneBar();
    ClipPlayer player;
    MiniTransport tr;
    std::vector<BlockEvent> ev;
    ev.reserve(4096);

    // Start: the mod wheel to 10 before the two notes, at offset 0 - once, not chased and sent again.
    tr.play();
    player.process(clips, tr.advance(512), kTempo, ev);
    DAWLAB_CHECK(ev.size() == 3 && count(ev, Kind::Controller) == 1 && count(ev, Kind::NoteOn) == 2);
    DAWLAB_CHECK(!ev.empty() && ev[0].message.kind() == Kind::Controller && ev[0].message.velocity() == 10);
    DAWLAB_CHECK(ev.size() == 3 && ev[1].message.note() == 60 && ev[2].message.note() == 64); // in the order found
    DAWLAB_CHECK(player.soundingCount() == 2);

    // A seek into the middle of the bar (tick 2400): both end at once, the wheel is chased to 20, and C4,
    // which is still in the clip there, is not started half-way.
    tr.seek(60000);
    player.process(clips, tr.advance(512), kTempo, ev);
    DAWLAB_CHECK(count(ev, Kind::NoteOff) == 2 && count(ev, Kind::NoteOn) == 0 && count(ev, Kind::Controller) == 1);
    DAWLAB_CHECK(ev.size() == 3 && ev[0].offset == 0 && ev[2].message.kind() == Kind::Controller && ev[2].message.velocity() == 20);
    DAWLAB_CHECK(player.soundingCount() == 0);

    // Back to 0 and play: on again. Then stop: off in the stop's block, and nothing more after.
    tr.seek(0);
    player.process(clips, tr.advance(512), kTempo, ev);
    DAWLAB_CHECK(count(ev, Kind::NoteOn) == 2);
    tr.stop();
    player.process(clips, tr.advance(512), kTempo, ev);
    DAWLAB_CHECK(count(ev, Kind::NoteOff) == 2 && ev.size() == 2 && ev[0].offset == 0);
    player.process(clips, tr.advance(512), kTempo, ev);
    DAWLAB_CHECK(ev.empty());

    // Muted from the start: nothing at all, not even a chased controller after a seek.
    clips[0].muted = true;
    tr.seek(0);
    tr.play();
    int heard = 0;
    for (int i = 0; i < 8; ++i) {
        if (i == 4)
            tr.seek(60000);
        player.process(clips, tr.advance(512), kTempo, ev);
        heard += static_cast<int>(ev.size());
    }
    DAWLAB_CHECK(heard == 0);
    clips[0].muted = false;
    tr.stop();
    player.process(clips, tr.advance(512), kTempo, ev);

    // Muted while sounding: off in the next block. Unmuted: not started again.
    tr.seek(0);
    tr.play();
    player.process(clips, tr.advance(512), kTempo, ev);
    clips[0].muted = true;
    player.process(clips, tr.advance(512), kTempo, ev);
    DAWLAB_CHECK(count(ev, Kind::NoteOff) == 2 && ev.size() == 2 && ev[0].offset == 0);
    clips[0].muted = false;
    player.process(clips, tr.advance(512), kTempo, ev);
    DAWLAB_CHECK(ev.empty());

    // Deleted while sounding: off. A clip with the same notes but a new id is another clip.
    tr.seek(0);
    player.process(clips, tr.advance(512), kTempo, ev);
    DAWLAB_CHECK(count(ev, Kind::NoteOn) == 2);
    std::vector<MidiClip> none;
    player.process(none, tr.advance(512), kTempo, ev);
    DAWLAB_CHECK(count(ev, Kind::NoteOff) == 2 && ev.size() == 2);
    tr.seek(0);
    player.process(clips, tr.advance(512), kTempo, ev);
    clips[0].id = 8;
    player.process(clips, tr.advance(512), kTempo, ev);
    DAWLAB_CHECK(count(ev, Kind::NoteOff) == 2);
    tr.stop();
    player.process(clips, tr.advance(512), kTempo, ev);

    // A loop over a little more than 1.5 beats (36100 samples: the wraps fall inside blocks): E4 and C4 end at the wrap and
    // start again at its sample; the wheel is chased to 10 there.
    clips = oneBar();
    tr.seek(0);
    tr.setLoop(0, 36100, true);
    tr.play();
    StrictInstrument instrument;
    int wraps = 0, onsAtWrap = 0, offsAtWrap = 0;
    for (std::int64_t done = 0; done < 110000; done += 1000) {
        const Block b = tr.advance(1000);
        player.process(clips, b, kTempo, ev);
        instrument.block(ev, 1000);
        if (b.numSegments == 2) {
            ++wraps;
            for (const BlockEvent& e : ev)
                if (e.offset == b.segments[1].offset) {
                    onsAtWrap += e.message.kind() == Kind::NoteOn ? 1 : 0;
                    offsAtWrap += e.message.kind() == Kind::NoteOff ? 1 : 0;
                }
        }
    }
    DAWLAB_CHECK(wraps == 3 && onsAtWrap == 6 && offsAtWrap == 6 && instrument.errors == 0);
    std::printf("  a loop of 36100 samples, 3 wraps: %d note offs and %d note ons at the wrap samples\n", offsAtWrap, onsAtWrap);
    tr.stop();
    player.process(clips, tr.advance(512), kTempo, ev);
    instrument.block(ev, 512);
    DAWLAB_CHECK(instrument.sounding() == 0);

    // A trimmed start hides a note that started before it; a note longer than the window ends with it.
    MidiClip t;
    t.id = 9;
    t.start = 0;
    t.offset = 480;
    t.length = 960;
    t.notes = {{0, 960, 50, 100, 0}, {960, 5000, 52, 100, 0}};
    clips = {t};
    tr.setLoop(0, 0, false);
    tr.seek(0);
    tr.play();
    instrument = StrictInstrument{};
    std::int64_t offAt = -1;
    for (std::int64_t done = 0; done < 48000; done += 512) {
        player.process(clips, tr.advance(512), kTempo, ev);
        instrument.block(ev, 512);
        for (const BlockEvent& e : ev) {
            DAWLAB_CHECK(e.message.note() == 52);
            if (e.message.kind() == Kind::NoteOff)
                offAt = done + e.offset;
        }
    }
    DAWLAB_CHECK(instrument.ons == 1 && instrument.offs == 1 && offAt == sampleOfTick(960, kTempo));

    // One pitch played again right where it ends: at that sample the note off comes before the note on.
    MidiClip again;
    again.id = 11;
    again.length = 4 * kTicksPerQuarter;
    again.notes = {{0, 960, 62, 100, 0}, {960, 960, 62, 100, 0}};
    clips = {again};
    ClipPlayer twice;
    tr.seek(0);
    tr.play();
    int offThenOn = 0;
    for (std::int64_t done = 0; done < 48000; done += 512) {
        twice.process(clips, tr.advance(512), kTempo, ev);
        for (size_t i = 0; i + 1 < ev.size(); ++i)
            if (done + ev[i].offset == 24000 && ev[i].offset == ev[i + 1].offset && ev[i].message.kind() == Kind::NoteOff &&
                ev[i + 1].message.kind() == Kind::NoteOn)
                ++offThenOn;
    }
    DAWLAB_CHECK(offThenOn == 1);
    tr.stop();
    twice.process(clips, tr.advance(512), kTempo, ev);

    // A segment with more events than its list holds: controllers are dropped, and counted.
    MidiClip crowded;
    crowded.id = 12;
    crowded.start = 100;
    crowded.length = 4 * kTicksPerQuarter;
    for (int i = 0; i < 2000; ++i)
        crowded.controllers.push_back({0, i % 128, i % 100, i / 128});
    clips = {crowded};
    ClipPlayer flooded;
    tr.seek(0);
    tr.play();
    int sent = 0;
    for (int i = 0; i < 8; ++i) {
        flooded.process(clips, tr.advance(512), kTempo, ev);
        sent += count(ev, Kind::Controller);
    }
    DAWLAB_CHECK(sent + flooded.droppedEvents() == 2000 && flooded.droppedEvents() > 0 && sent > 800);
    tr.stop();
    flooded.process(clips, tr.advance(512), kTempo, ev);

    // More notes at once than the table holds: the rest are not played, and every one played ends.
    MidiClip big;
    big.id = 10;
    big.length = 4 * kTicksPerQuarter;
    for (int i = 0; i < 200; ++i)
        big.notes.push_back({0, 2 * kTicksPerQuarter, i % 128, 100, i / 128});
    clips = {big};
    ClipPlayer full;
    tr.seek(0);
    tr.play();
    instrument = StrictInstrument{};
    for (int i = 0; i < 20; ++i) {
        full.process(clips, tr.advance(512), kTempo, ev);
        instrument.block(ev, 512);
    }
    tr.stop();
    full.process(clips, tr.advance(512), kTempo, ev);
    instrument.block(ev, 512);
    DAWLAB_CHECK(instrument.ons == ClipPlayer::kMaxSounding && full.droppedNotes() == 200 - ClipPlayer::kMaxSounding);
    DAWLAB_CHECK(instrument.offs == ClipPlayer::kMaxSounding && instrument.sounding() == 0 && instrument.errors == 0);
}

// Everything at once, at random: whatever happens, no note off without a note on, and silence after a stop.
inline void checkFuzz() {
    std::mt19937 rng(18218);
    std::uniform_int_distribution<int> op(0, 39), block(16, 1024), pick(0, 3);
    std::uniform_int_distribution<std::int64_t> where(0, 18LL * 24000);
    long errors = 0, ons = 0;
    int stillSounding = 0;
    for (int round = 0; round < 30; ++round) {
        std::vector<MidiClip> clips;
        int nextId = 1;
        for (int i = 0; i < 4; ++i)
            clips.push_back(randomClip(rng, nextId++));
        ClipPlayer player;
        StrictInstrument instrument;
        MiniTransport tr;
        std::vector<BlockEvent> ev;
        ev.reserve(16384);
        tr.play();
        for (int step = 0; step < 2500; ++step) {
            switch (op(rng)) {
            case 0: tr.stop(); break;
            case 1: tr.play(); break;
            case 2: tr.seek(where(rng)); break;
            case 3: {
                const std::int64_t a = where(rng);
                tr.setLoop(a, a + 1024 + where(rng) / 4, pick(rng) != 0);
                break;
            }
            case 4: clips[static_cast<size_t>(pick(rng))].muted ^= true; break;
            case 5: clips[static_cast<size_t>(pick(rng))] = randomClip(rng, nextId++); break; // deleted, another in its place
            case 6: { // edited: the same clip, other notes
                MidiClip& c = clips[static_cast<size_t>(pick(rng))];
                const int id = c.id;
                c = randomClip(rng, id);
                break;
            }
            default: break;
            }
            const int n = block(rng);
            player.process(clips, tr.advance(n), kTempo, ev);
            instrument.block(ev, n);
        }
        tr.stop();
        player.process(clips, tr.advance(256), kTempo, ev);
        instrument.block(ev, 256);
        errors += instrument.errors;
        ons += instrument.ons;
        stillSounding += instrument.sounding() + player.soundingCount();
    }
    std::printf("  30 random sessions, %ld note ons: %ld unmatched or misplaced events, %d notes left sounding (expected 0, 0)\n", ons, errors,
                stillSounding);
    DAWLAB_CHECK(errors == 0 && stillSounding == 0 && ons > 1000);
}

} // namespace step02_check

int main() {
    using namespace step02_check;
    std::printf("Step 2: playing a MIDI clip\n");
    checkPlace();
    if (dawlab::test::counters().failures > 0)
        return dawlab::test::summary("l18_step02");
    checkByHand();
    checkStraight();
    checkFuzz();
    return dawlab::test::summary("l18_step02");
}
