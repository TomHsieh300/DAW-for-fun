#pragma once
// Exercise 5 - the self-check (main). Included at the end of the starter and of the solution.

#include "ex5_arpeggiator.h"

#include <dawlab/Check.h>

#include <cstdio>
#include <functional>
#include <random>
#include <string>

namespace ex5::check {

struct At {
    std::int64_t sample = 0;
    MidiMessage message;
};

inline std::string describe(const std::vector<At>& events) {
    std::string s;
    char buf[64];
    for (const At& e : events) {
        const Kind k = e.message.kind();
        if (k == Kind::NoteOn)
            std::snprintf(buf, sizeof buf, "on%d@%lld ", e.message.note(), static_cast<long long>(e.sample));
        else if (k == Kind::NoteOff)
            std::snprintf(buf, sizeof buf, "off%d@%lld ", e.message.note(), static_cast<long long>(e.sample));
        else
            std::snprintf(buf, sizeof buf, "%02X@%lld ", e.message.status, static_cast<long long>(e.sample));
        s += buf;
    }
    return s;
}

struct Result {
    std::vector<At> events;
    bool allocated = false; // `out` grew past what was reserved
    bool inOrder = true;    // every block's events inside it, in order, note offs first at one offset
};

// Plays `keys` (sorted by sample) through an arpeggiator from sample `start` to `end`, in blocks whose sizes
// `nextBlock` returns.
inline Result run(Arpeggiator& arp, const Tempo& tempo, const std::vector<At>& keys, std::int64_t start, std::int64_t end,
                  const std::function<int()>& nextBlock) {
    Result r;
    std::vector<BlockEvent> input, out;
    input.reserve(256);
    out.reserve(1024);
    size_t k = 0;
    while (k < keys.size() && keys[k].sample < start)
        ++k;
    for (std::int64_t s = start; s < end;) {
        const int n = static_cast<int>(std::min<std::int64_t>(nextBlock(), end - s));
        input.clear();
        for (; k < keys.size() && keys[k].sample < s + n; ++k)
            input.push_back({static_cast<int>(keys[k].sample - s), keys[k].message});
        arp.process(s, n, tempo, input, out);
        r.allocated = r.allocated || out.capacity() != 1024;
        for (size_t i = 0; i < out.size(); ++i) {
            const bool inside = out[i].offset >= 0 && out[i].offset < n;
            const bool after = i == 0 || out[i - 1].offset < out[i].offset ||
                               (out[i - 1].offset == out[i].offset &&
                                (out[i - 1].message.kind() == Kind::NoteOff || out[i].message.kind() != Kind::NoteOff));
            r.inOrder = r.inOrder && inside && after;
        }
        for (const BlockEvent& e : out)
            r.events.push_back({s + e.offset, e.message});
        s += n;
    }
    return r;
}

inline Result run(const ArpSettings& settings, const Tempo& tempo, const std::vector<At>& keys, std::int64_t end, int block = 512) {
    Arpeggiator arp;
    arp.setSettings(settings);
    return run(arp, tempo, keys, 0, end, [block] { return block; });
}

inline At on(std::int64_t at, int note, int velocity = 100, int channel = 0) {
    return {at, MidiMessage::noteOn(channel, note, velocity)};
}
inline At off(std::int64_t at, int note) {
    return {at, MidiMessage::noteOff(0, note)};
}

// The notes `pitches` one a step from step `first`, a step = 6000 samples, each `gate` samples long.
inline std::string expected(const std::vector<int>& pitches, std::int64_t first = 0, std::int64_t gate = 3000) {
    std::vector<At> e;
    for (size_t i = 0; i < pitches.size(); ++i) {
        const std::int64_t at = first + static_cast<std::int64_t>(i) * 6000;
        e.push_back(on(at, pitches[i]));
        e.push_back(off(at + gate, pitches[i]));
    }
    return describe(e);
}

const Tempo kTempo{48000.0, 120.0}; // a 1/16 (240 ticks) is 6000 samples

inline bool same(const char* what, const std::string& got, const std::string& want) {
    const bool ok = got == want;
    std::printf("  %-52s %s\n", what, ok ? "yes" : "NO");
    if (!ok)
        std::printf("    got:  %s\n    want: %s\n", got.c_str(), want.c_str());
    return ok;
}

inline void checkModes() {
    const std::vector<At> chord{on(0, 60), on(0, 64), on(0, 67)};
    ArpSettings s;
    Result r = run(s, kTempo, chord, 48000);
    DAWLAB_CHECK(same("Up, C E G held, 1/16 at half gate:", describe(r.events), expected({60, 64, 67, 60, 64, 67, 60, 64})));
    s.mode = ArpMode::Down;
    r = run(s, kTempo, chord, 36000);
    DAWLAB_CHECK(same("Down:", describe(r.events), expected({67, 64, 60, 67, 64, 60})));
    s.mode = ArpMode::UpDown;
    r = run(s, kTempo, chord, 48000);
    DAWLAB_CHECK(same("UpDown:", describe(r.events), expected({60, 64, 67, 64, 60, 64, 67, 64})));
    r = run(s, kTempo, {on(0, 60)}, 18000);
    DAWLAB_CHECK(same("UpDown, one key:", describe(r.events), expected({60, 60, 60})));
    r = run(s, kTempo, {on(0, 64), on(0, 60)}, 24000);
    DAWLAB_CHECK(same("UpDown, two keys:", describe(r.events), expected({60, 64, 60, 64})));
    s.mode = ArpMode::AsPlayed;
    r = run(s, kTempo, {on(0, 67), on(0, 60), on(0, 64)}, 36000);
    DAWLAB_CHECK(same("AsPlayed, G C E:", describe(r.events), expected({67, 60, 64, 67, 60, 64})));
    r = run(s, kTempo, {on(0, 60), on(0, 60), on(0, 64)}, 24000);
    DAWLAB_CHECK(same("AsPlayed, C pressed twice, then E:", describe(r.events), expected({60, 64, 60, 64})));
    s.mode = ArpMode::Up;
    s.octaves = 2;
    r = run(s, kTempo, {on(0, 64), on(0, 60)}, 30000);
    DAWLAB_CHECK(same("Up, two octaves of C E:", describe(r.events), expected({60, 64, 72, 76, 60})));
    s.mode = ArpMode::UpDown;
    r = run(s, kTempo, {on(0, 64), on(0, 60)}, 36000);
    DAWLAB_CHECK(same("UpDown, two octaves of C E:", describe(r.events), expected({60, 64, 72, 76, 72, 64})));
    s.mode = ArpMode::Down;
    s.octaves = 3;
    r = run(s, kTempo, {on(0, 110)}, 18000);
    DAWLAB_CHECK(same("Down, three octaves of 110 (134 is no note):", describe(r.events), expected({122, 110, 122})));
    s.mode = ArpMode::Up;
    s.octaves = 2;
    r = run(s, kTempo, {on(0, 115)}, 12000);
    DAWLAB_CHECK(same("Up, two octaves of 115 (127 is the top note):", describe(r.events), expected({115, 127})));
    s = ArpSettings{};
    s.mode = ArpMode::AsPlayed;
    std::vector<At> many;
    for (int p = 40; p <= 56; ++p)
        many.push_back(on(0, p));
    r = run(s, kTempo, many, 12000);
    DAWLAB_CHECK(same("17 keys: the oldest is pushed out:", describe(r.events), expected({41, 42})));
}

inline void checkTiming() {
    ArpSettings s;
    // A key at a step's own sample plays in it; one a sample later waits for the next step.
    Result r = run(s, kTempo, {on(0, 60), on(6000, 64), on(6001, 67)}, 24000);
    DAWLAB_CHECK(same("keys at a step and a sample after it:", describe(r.events), expected({60, 64, 67, 60})));
    // All keys up at 20 000, while the note of step 3 sounds: it ends at its gate. A new key at 30 000 (a step)
    // starts the pattern from the beginning.
    r = run(s, kTempo, {on(0, 60), on(0, 64), off(14000, 60), off(14000, 64), on(30000, 65), on(30000, 62), off(37000, 62), off(37000, 65)}, 48000);
    DAWLAB_CHECK(same("all keys up, then a new chord at a step:", describe(r.events),
                      expected({60, 64, 60}) + expected({62, 65}, 30000)));
    // The note comes on the channel of its key.
    r = run(s, kTempo, {on(0, 60, 90, 5)}, 6000);
    DAWLAB_CHECK(r.events.size() == 2 && r.events[0].message.channel() == 5 && r.events[0].message.velocity() == 90 &&
                 r.events[1].message.channel() == 5);
    // Gate 1: a note ends at the next step, before the next note starts at the same sample.
    s.gate = 1.0;
    r = run(s, kTempo, {on(0, 60), on(0, 64), off(17000, 60), off(17000, 64)}, 18001);
    DAWLAB_CHECK(same("gate 1: the note off first at the next step:", describe(r.events),
                      "on60@0 off60@6000 on64@6000 off64@12000 on60@12000 off60@18000 "));
    // Gate 0.001 is one tick: 25 samples.
    s.gate = 0.001;
    r = run(s, kTempo, {on(0, 60)}, 6000);
    DAWLAB_CHECK(same("a tiny gate is still one tick long:", describe(r.events), expected({60}, 0, 25)));
    // A 1/8 (480 ticks) at 97 BPM and 44.1 kHz: every step at Step 1's sample.
    s = ArpSettings{};
    s.rate = 480;
    const Tempo odd{44100.0, 97.0};
    r = run(s, odd, {on(0, 60)}, 300000, 333);
    bool exact = r.events.size() >= 20;
    for (size_t i = 0; i < r.events.size(); i += 2)
        exact = exact && r.events[i].sample == minidaw::sampleOfTick(static_cast<std::int64_t>(i / 2) * 480, odd) &&
                i + 1 < r.events.size() && r.events[i + 1].sample == minidaw::sampleOfTick(static_cast<std::int64_t>(i / 2) * 480 + 240, odd);
    std::printf("  1/8 at 97 BPM, 44.1 kHz, blocks of 333: %zu events, every one on its exact sample: %s\n", r.events.size(), exact ? "yes" : "NO");
    DAWLAB_CHECK(exact);
}

inline void checkPassAndReset() {
    ArpSettings s;
    // A controller passes at its own sample; a bend at a step comes before the step's note, after its note off.
    s.gate = 1.0;
    Result r = run(s, kTempo, {on(0, 60), {1234, MidiMessage::controller(0, 1, 64)}, {6000, MidiMessage::pitchBend(0, 100)}, off(11000, 60)}, 12001);
    DAWLAB_CHECK(same("a controller and a bend pass through:", describe(r.events), "on60@0 B0@1234 off60@6000 E0@6000 on60@6000 off60@12000 "));
    // A controller before a note off that is taken only at the next step: still in order.
    s.gate = 0.5;
    r = run(s, kTempo, {on(0, 60), {4000, MidiMessage::controller(0, 1, 1)}, off(5000, 60)}, 12000, 12000);
    DAWLAB_CHECK(same("a controller after a note off, in one block:", describe(r.events), "on60@0 off60@3000 B0@4000 "));
    r = run(s, kTempo, {on(0, 60), {2000, MidiMessage::controller(0, 1, 1)}, off(5000, 60)}, 12000, 12000);
    DAWLAB_CHECK(same("a controller before a note off, in one block:", describe(r.events), "on60@0 B0@2000 off60@3000 "));
    DAWLAB_CHECK(r.inOrder);
    // The transport jumps at 7000, back to 0: the note of step 1 ends at once, and the pattern starts again.
    Arpeggiator arp;
    s = ArpSettings{};
    arp.setSettings(s);
    const std::vector<At> keys{on(0, 60), on(0, 64), on(0, 67)};
    r = run(arp, kTempo, keys, 0, 7000, [] { return 512; });
    std::vector<BlockEvent> out;
    out.reserve(16);
    arp.reset(out);
    const bool ended = out.size() == 1 && out[0].offset == 0 && out[0].message.kind() == Kind::NoteOff && out[0].message.note() == 64;
    DAWLAB_CHECK(ended && !arp.sounding() && arp.numHeld() == 3);
    const Result again = run(arp, kTempo, {}, 0, 12000, [] { return 512; });
    DAWLAB_CHECK(same("a jump: the note ends, the pattern starts again:", describe(r.events) + "| " + describe(again.events),
                      expected({60, 64}).substr(0, expected({60, 64}).size() - std::string("off64@9000 ").size()) + "| " + expected({60, 64})));
    out.clear();
    arp.reset(out);
    arp.reset(out); // nothing sounds: nothing to end
    DAWLAB_CHECK(out.empty());
}

inline void checkBlocksAndBalance() {
    std::mt19937 rng(5);
    auto pick = [&rng](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
    int differ = 0, unbalanced = 0, overlap = 0, grew = 0, disorder = 0;
    const int runs = 400;
    for (int run_ = 0; run_ < runs; ++run_) {
        ArpSettings s;
        s.rate = std::vector<int>{120, 160, 240, 320, 480}[static_cast<size_t>(pick(0, 4))];
        s.gate = pick(1, 100) / 100.0;
        s.mode = static_cast<ArpMode>(pick(0, 3));
        s.octaves = pick(1, 3);
        const Tempo tempo{run_ % 2 == 0 ? 48000.0 : 44100.0, static_cast<double>(pick(60, 180))};
        std::vector<At> keys;
        bool down[128] = {};
        for (int i = 0; i < 40; ++i) {
            const std::int64_t step = minidaw::sampleOfTick(static_cast<std::int64_t>(pick(0, 40)) * s.rate, tempo);
            const std::int64_t at = pick(0, 3) == 0 && step <= 200000 ? step : pick(0, 200000);
            const int p = pick(48, 72);
            const int kind = pick(0, 9);
            if (kind == 0)
                keys.push_back({at, MidiMessage::controller(0, 74, pick(0, 127))});
            else
                keys.push_back({at, kind < 6 ? MidiMessage::noteOn(pick(0, 1), p, pick(1, 127)) : MidiMessage::noteOff(0, p)});
        }
        std::stable_sort(keys.begin(), keys.end(), [](const At& a, const At& b) { return a.sample < b.sample; });
        for (const At& k : keys)
            if (k.message.kind() == Kind::NoteOn)
                down[k.message.note()] = true;
        for (int p = 0; p < 128; ++p) // every key up at the end
            if (down[p])
                keys.push_back(off(210000, p));
        const std::int64_t end = 260000;
        Arpeggiator a, b;
        a.setSettings(s);
        b.setSettings(s);
        const Result whole = run(a, tempo, keys, 0, end, [] { return 1 << 30; });
        const Result pieces = run(b, tempo, keys, 0, end, [&] { return pick(0, 4) == 0 ? 1 : pick(1, 700); });
        differ += describe(whole.events) == describe(pieces.events) ? 0 : 1;
        grew += whole.allocated || pieces.allocated ? 1 : 0;
        disorder += whole.inOrder && pieces.inOrder ? 0 : 1;
        int sounding = 0;
        for (const At& e : pieces.events) {
            if (e.message.kind() == Kind::NoteOn)
                ++sounding;
            else if (e.message.kind() == Kind::NoteOff)
                --sounding;
            overlap += sounding > 1 ? 1 : 0;
        }
        unbalanced += sounding != 0 ? 1 : 0;
    }
    std::printf("  %d random songs (keys at and between steps, every mode, rate and gate) in blocks of 1 to 700 samples:\n"
                "    %d differ from one block, %d leave a note hanging, %d play two notes at once, %d made `out` grow,\n"
                "    %d put an event out of its block or out of order\n",
                runs, differ, unbalanced, overlap, grew, disorder);
    DAWLAB_CHECK(differ == 0 && unbalanced == 0 && overlap == 0 && grew == 0 && disorder == 0);
}

} // namespace ex5::check

int main() {
    using namespace ex5::check;
    std::printf("Exercise 5 - an arpeggiator\n");
    checkModes();
    checkTiming();
    checkPassAndReset();
    checkBlocksAndBalance();
    return dawlab::test::summary("l18_ex5");
}
