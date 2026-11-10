#pragma once
// Exercise 3 - the self-check (main). Included at the end of the starter and of the solution.

#include "ex3_smf.h"

#include <dawlab/Check.h>

#include <cstdio>
#include <initializer_list>
#include <random>

namespace ex3::check {

using Bytes = std::vector<std::uint8_t>;

inline Bytes bytes(std::initializer_list<int> list) {
    Bytes b;
    for (const int v : list)
        b.push_back(static_cast<std::uint8_t>(v));
    return b;
}

inline Bytes cat(std::initializer_list<Bytes> parts) {
    Bytes b;
    for (const Bytes& p : parts)
        b.insert(b.end(), p.begin(), p.end());
    return b;
}

inline Bytes chunkOf(const char* type, const Bytes& data) {
    Bytes b(type, type + 4);
    const auto n = static_cast<std::uint32_t>(data.size());
    b.insert(b.end(), {static_cast<std::uint8_t>(n >> 24), static_cast<std::uint8_t>(n >> 16), static_cast<std::uint8_t>(n >> 8),
                       static_cast<std::uint8_t>(n)});
    b.insert(b.end(), data.begin(), data.end());
    return b;
}

inline Bytes header(int format, int tracks, int division) {
    return chunkOf("MThd", bytes({0, format, tracks >> 8, tracks & 0xFF, division >> 8, division & 0xFF}));
}

inline Bytes cut(Bytes b, size_t n) {
    b.resize(b.size() - n);
    return b;
}

inline bool same(const TimedEvent& a, const TimedEvent& b) {
    return a.tick == b.tick && a.message.status == b.message.status && a.message.data1 == b.message.data1 && a.message.data2 == b.message.data2;
}

inline bool sameEvents(const std::vector<TimedEvent>& a, const std::vector<TimedEvent>& b) {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (!same(a[i], b[i]))
            return false;
    return true;
}

inline void checkVlq() {
    const struct {
        std::uint32_t value;
        Bytes encoded;
    } table[] = {
        {0x00, bytes({0x00})},
        {0x40, bytes({0x40})},
        {0x7F, bytes({0x7F})},
        {0x80, bytes({0x81, 0x00})},
        {0x2000, bytes({0xC0, 0x00})},
        {0x3FFF, bytes({0xFF, 0x7F})},
        {0x4000, bytes({0x81, 0x80, 0x00})},
        {0x1FFFFF, bytes({0xFF, 0xFF, 0x7F})},
        {0x200000, bytes({0x81, 0x80, 0x80, 0x00})},
        {0x0FFFFFFF, bytes({0xFF, 0xFF, 0xFF, 0x7F})},
    };
    int good = 0;
    for (const auto& row : table) {
        Bytes out{0xAA};
        writeVlq(out, row.value);
        const bool written = out == cat({bytes({0xAA}), row.encoded});
        Bytes in = cat({row.encoded, bytes({0x55})});
        std::size_t pos = 0;
        std::uint32_t value = 12345;
        const bool read = readVlq(in.data(), in.size(), pos, value) && value == row.value && pos == row.encoded.size();
        DAWLAB_CHECK(written && read);
        good += written && read ? 1 : 0;
    }
    std::printf("  %d of 10 numbers of the standard's table written and read back\n", good);
    const Bytes five = bytes({0xFF, 0xFF, 0xFF, 0xFF, 0x7F});
    const Bytes cut = bytes({0x81, 0x80});
    std::size_t pos = 0;
    std::uint32_t v = 0;
    DAWLAB_CHECK(!readVlq(five.data(), five.size(), pos, v));
    pos = 0;
    DAWLAB_CHECK(!readVlq(cut.data(), cut.size(), pos, v));
    pos = 0; // a number that would end after `size`: the byte there is not the reader's
    const Bytes longer = bytes({0x81, 0x80, 0x00});
    DAWLAB_CHECK(!readVlq(longer.data(), 2, pos, v));
    pos = 1; // in the middle of a buffer
    const Bytes mid = bytes({0x00, 0x83, 0x60, 0x00});
    DAWLAB_CHECK(readVlq(mid.data(), mid.size(), pos, v) && v == 480 && pos == 3);
}

inline Song littleSong() {
    Song song;
    song.bpm = 100.0;
    song.numerator = 3;
    song.denominator = 4;
    SmfTrack bass;
    bass.name = "Bass";
    bass.events = {
        {480, MidiMessage::noteOn(0, 38, 100)},
        {0, MidiMessage::noteOn(0, 36, 100)},
        {480, MidiMessage::noteOff(0, 36, 64)},
        {960, MidiMessage::controller(0, 64, 127)},
        {960, MidiMessage::noteOn(0, 38, 0)}, // a note off, as keyboards send it
    };
    song.tracks.push_back(bass);
    return song;
}

inline Bytes littleFile() {
    const Bytes conductor = bytes({0x00, 0xFF, 0x58, 0x04, 0x03, 0x02, 0x18, 0x08, //
                                   0x00, 0xFF, 0x51, 0x03, 0x09, 0x27, 0xC0,       //
                                   0x00, 0xFF, 0x2F, 0x00});
    const Bytes bass = bytes({0x00, 0xFF, 0x03, 0x04, 'B', 'a', 's', 's', //
                              0x00, 0x90, 36, 100,                      // tick 0: note on
                              0x83, 0x60, 0x80, 36, 64,                 // tick 480: its note off first
                              0x00, 0x90, 38, 100,                      //           then the next note
                              0x83, 0x60, 38, 0,                        // tick 960: running status, note off first
                              0x00, 0xB0, 64, 127,                      //           then the pedal
                              0x00, 0xFF, 0x2F, 0x00});
    return cat({header(1, 2, 960), chunkOf("MTrk", conductor), chunkOf("MTrk", bass)});
}

inline void checkWrite() {
    const Bytes written = writeSmf(littleSong());
    const Bytes expected = littleFile();
    DAWLAB_CHECK(written == expected);
    if (written != expected) {
        std::printf("  expected %zu bytes, written %zu:", expected.size(), written.size());
        for (const std::uint8_t b : written)
            std::printf(" %02X", b);
        std::printf("\n");
    } else {
        std::printf("  a song of one track, 3/4 at 100 BPM: %zu bytes, exactly the expected ones (running status, note offs "
                    "first)\n",
                    written.size());
    }
    Song plain; // 4/4 at 120 BPM, a track without a name: no name event
    plain.tracks.push_back({"", {{0, MidiMessage::noteOn(0, 60, 100)}}});
    const Bytes plainFile = cat({header(1, 2, 960),
                                 chunkOf("MTrk", bytes({0x00, 0xFF, 0x58, 0x04, 4, 2, 0x18, 0x08, 0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20, 0x00, 0xFF, 0x2F, 0x00})),
                                 chunkOf("MTrk", bytes({0x00, 0x90, 60, 100, 0x00, 0xFF, 0x2F, 0x00}))});
    DAWLAB_CHECK(writeSmf(plain) == plainFile);
    Song negative;
    negative.tracks.push_back({"", {{-10, MidiMessage::noteOn(0, 60, 1)}, {0, MidiMessage::noteOn(0, 61, 1)}}});
    const SmfRead r = readSmf(writeSmf(negative));
    DAWLAB_CHECK(r.ok && r.song.tracks.size() == 1 && r.song.tracks[0].events.size() == 1 && r.song.tracks[0].events[0].message.data1 == 61);
}

inline void checkRead() {
    SmfRead r = readSmf(littleFile());
    DAWLAB_CHECK(r.ok && r.error.empty() && r.format == 1 && r.division == 960);
    DAWLAB_CHECK(r.song.bpm == 100.0 && r.song.numerator == 3 && r.song.denominator == 4 && r.tempoChanges == 0);
    DAWLAB_CHECK(r.song.tracks.size() == 1);
    if (r.song.tracks.size() == 1) {
        std::vector<TimedEvent> expected = littleSong().tracks[0].events;
        minidaw::sortEvents(expected);
        DAWLAB_CHECK(r.song.tracks[0].name == "Bass" && sameEvents(r.song.tracks[0].events, expected));
    }

    // A file as another program writes it: format 0, 96 ticks a quarter, a long header, an unknown chunk, a
    // SysEx, running status across a note off sent as a note on, a program change with running status.
    const Bytes track = bytes({0x00, 0xF0, 0x05, 0x7E, 0x7F, 0x09, 0x01, 0xF7, // GM on: skipped
                               0x00, 0xF7, 0x02, 0x01, 0x02,                   // an escape: skipped too
                               0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20,       // 500 000 us: 120 BPM
                               0x00, 0xC0, 0x05,                               // program 5
                               0x00, 0x06,                                     // program 6, running status
                               0x00, 0x91, 60, 90,                             // channel 2
                               0x60, 60, 0,                                    // 96 later: off by running status
                               0x81, 0x00, 0xE1, 0x00, 0x50,                   // 128 later: a bend
                               0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20,       // the same tempo: no change
                               0x10, 0xFF, 0x51, 0x03, 0x0F, 0x42, 0x40,       // 60 BPM: a change, ignored
                               0x00, 0xFF, 0x2F, 0x00,                         //
                               0x12, 0x34});                                   // after the end: ignored
    const Bytes longHeader = chunkOf("MThd", bytes({0, 0, 0, 1, 0, 96, 0xAB, 0xCD}));
    r = readSmf(cat({longHeader, chunkOf("XFIH", bytes({1, 2, 3})), chunkOf("MTrk", track)}));
    DAWLAB_CHECK(r.ok && r.format == 0 && r.division == 96 && r.song.bpm == 120.0 && r.tempoChanges == 1 && r.skipped == 3);
    const std::vector<TimedEvent> expected = {
        {0, {0xC0, 5, 0}}, {0, {0xC0, 6, 0}}, {0, {0x91, 60, 90}}, {960, {0x91, 60, 0}}, {2240, {0xE1, 0x00, 0x50}},
    };
    DAWLAB_CHECK(r.song.tracks.size() == 1 && sameEvents(r.song.tracks[0].events, expected));
    std::printf("  a format-0 file at 96 a quarter with SysEx, an unknown chunk and running status: %s\n",
                r.ok && r.song.tracks.size() == 1 && sameEvents(r.song.tracks[0].events, expected) ? "read" : "WRONG");

    // Division 7: seven events a tick apart. Converting each delta (137.14 -> 137) drifts to 959; converting the
    // absolute tick ends on the beat.
    Bytes seven;
    for (int i = 0; i < 7; ++i)
        seven.insert(seven.end(), {0x01, 0x90, 60, 1});
    r = readSmf(cat({header(0, 1, 7), chunkOf("MTrk", seven)}));
    DAWLAB_CHECK(r.ok && r.song.tracks.size() == 1 && r.song.tracks[0].events.size() == 7);
    if (r.ok && r.song.tracks.size() == 1 && r.song.tracks[0].events.size() == 7) {
        DAWLAB_CHECK(r.song.tracks[0].events[0].tick == 137 && r.song.tracks[0].events[3].tick == 549 && r.song.tracks[0].events[6].tick == 960);
        std::printf("  division 7, a tick apart: ticks 137, 274, ... %lld (summed deltas would end on 959)\n",
                    static_cast<long long>(r.song.tracks[0].events[6].tick));
    }

    // Format 1 with a conductor that has a name: two tracks, and the time signature 6/8.
    const Bytes conductor = bytes({0x00, 0xFF, 0x03, 0x02, 'C', 'o', 0x00, 0xFF, 0x58, 0x04, 6, 3, 24, 8, 0x00, 0xFF, 0x2F, 0x00});
    const Bytes a = bytes({0x00, 0xFF, 0x58, 0x04, 4, 2, 24, 8, 0x00, 0x90, 40, 80, 0x00, 0xFF, 0x2F, 0x00}); // a later signature
    const Bytes b = bytes({0x00, 0xFF, 0x03, 0x01, 'B', 0x00, 0xFF, 0x03, 0x01, 'X', 0x00, 0x95, 41, 81, 0x00, 0xFF, 0x2F, 0x00}); // the first name
    const Bytes empty = bytes({0x00, 0xFF, 0x03, 0x01, 'E', 0x00, 0xFF, 0x2F, 0x00});
    r = readSmf(cat({header(1, 4, 480), chunkOf("MTrk", conductor), chunkOf("MTrk", a), chunkOf("MTrk", empty), chunkOf("MTrk", b)}));
    DAWLAB_CHECK(r.ok && r.song.tracks.size() == 2 && r.song.numerator == 6 && r.song.denominator == 8 && r.song.bpm == 120.0);
    DAWLAB_CHECK(r.song.tracks.size() == 2 && r.song.tracks[0].name.empty() && r.song.tracks[1].name == "B" &&
                 r.song.tracks[1].events.size() == 1 && r.song.tracks[1].events[0].message.status == 0x95);
}

inline void checkRefusals() {
    const Bytes ok = bytes({0x00, 0x90, 60, 100, 0x00, 0xFF, 0x2F, 0x00});
    const struct {
        const char* what;
        Bytes file;
    } bad[] = {
        {"nothing", Bytes{}},
        {"a WAV file", cat({bytes({'R', 'I', 'F', 'F', 0, 0, 0, 4, 'W', 'A', 'V', 'E', 0, 0})})},
        {"format 2", cat({header(2, 1, 96), chunkOf("MTrk", ok)})},
        {"SMPTE time", cat({header(0, 1, 0xE728), chunkOf("MTrk", ok)})},
        {"division 0", cat({header(0, 1, 0), chunkOf("MTrk", ok)})},
        {"a header cut short", cut(header(0, 1, 96), 1)},
        {"a header longer than the file", cat({bytes({'M', 'T', 'h', 'd', 0, 0, 1, 0, 0, 0, 0, 1, 0, 96})})},
        {"no track at all", header(0, 1, 96)},
        {"fewer tracks than said", cat({header(1, 3, 96), chunkOf("MTrk", ok), chunkOf("MTrk", ok)})},
        {"a chunk past the end", cat({header(0, 1, 96), bytes({'M', 'T', 'r', 'k', 0, 0, 0, 9}), ok})},
        {"a delta of 5 bytes", cat({header(0, 1, 96), chunkOf("MTrk", bytes({0x81, 0x81, 0x81, 0x81, 0x01, 0x90, 60, 1}))})},
        {"a delta cut short", cat({header(0, 1, 96), chunkOf("MTrk", bytes({0x00, 0x90, 60, 1, 0x81}))})},
        {"data without a status", cat({header(0, 1, 96), chunkOf("MTrk", bytes({0x00, 60, 100}))})},
        {"running status after a meta", cat({header(0, 1, 96), chunkOf("MTrk", bytes({0x00, 0x90, 60, 1, 0x00, 0xFF, 0x01, 0x00, 0x00, 61, 1}))})},
        {"running status after a SysEx", cat({header(0, 1, 96), chunkOf("MTrk", bytes({0x00, 0x90, 60, 1, 0x00, 0xF0, 0x01, 0xF7, 0x00, 61, 1}))})},
        {"a status as the second data byte", cat({header(0, 1, 96), chunkOf("MTrk", bytes({0x00, 0x90, 60, 0x90}))})},
        {"a status as the first data byte", cat({header(0, 1, 96), chunkOf("MTrk", bytes({0x00, 0x90, 0x80, 60}))})},
        {"a song position in a file", cat({header(0, 1, 96), chunkOf("MTrk", bytes({0x00, 0xF2, 0x00, 0x00}))})},
        {"a clock in a file", cat({header(0, 1, 96), chunkOf("MTrk", bytes({0x00, 0xF8}))})},
        {"a message cut short", cat({header(0, 1, 96), chunkOf("MTrk", bytes({0x00, 0x90, 60}))})},
        {"an event without its message", cat({header(0, 1, 96), chunkOf("MTrk", bytes({0x00}))})},
        {"a meta event past its track", cat({header(0, 1, 96), chunkOf("MTrk", bytes({0x00, 0xFF, 0x03, 0x10, 'a'}))})},
        {"a meta type past its track", cat({header(0, 1, 96), chunkOf("MTrk", bytes({0x00, 0xFF}))})},
    };
    int refused = 0;
    for (const auto& f : bad) {
        const SmfRead r = readSmf(f.file);
        const bool good = !r.ok && !r.error.empty();
        if (!good)
            std::printf("  NOT refused: %s\n", f.what);
        DAWLAB_CHECK(good);
        refused += good ? 1 : 0;
    }
    std::printf("  %d of %zu broken files refused, each with a reason\n", refused, sizeof(bad) / sizeof(bad[0]));
}

inline void checkRoundTrips() {
    std::mt19937 rng(18);
    auto pick = [&rng](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
    int good = 0;
    const int runs = 2000;
    for (int run = 0; run < runs; ++run) {
        Song song;
        song.bpm = 60000000.0 / pick(250000, 1500000);
        song.numerator = pick(1, 12);
        song.denominator = 1 << pick(0, 5);
        const int numTracks = pick(0, 4);
        for (int t = 0; t < numTracks; ++t) {
            SmfTrack track;
            if (pick(0, 1) == 1)
                track.name = std::string(static_cast<size_t>(pick(1, 200)), static_cast<char>('a' + t));
            const int n = pick(0, 60);
            for (int i = 0; i < n; ++i) {
                const int ch = pick(0, 15);
                std::int64_t tick = pick(0, 3) == 0 ? 0 : pick(0, 2000000);
                MidiMessage m;
                switch (pick(0, 6)) {
                case 0: m = MidiMessage::noteOn(ch, pick(0, 127), pick(0, 127)); break;
                case 1: m = MidiMessage::noteOff(ch, pick(0, 127), pick(0, 127)); break;
                case 2: m = MidiMessage::controller(ch, pick(0, 127), pick(0, 127)); break;
                case 3: m = MidiMessage::pitchBend(ch, pick(-8192, 8191)); break;
                case 4: m = {static_cast<std::uint8_t>(0xC0 | ch), static_cast<std::uint8_t>(pick(0, 127)), 0}; break;
                case 5: m = {static_cast<std::uint8_t>(0xD0 | ch), static_cast<std::uint8_t>(pick(0, 127)), 0}; break;
                default: m = {static_cast<std::uint8_t>(0xA0 | ch), static_cast<std::uint8_t>(pick(0, 127)), static_cast<std::uint8_t>(pick(0, 127))}; break;
                }
                track.events.push_back({tick, m});
            }
            song.tracks.push_back(track);
        }
        const SmfRead r = readSmf(writeSmf(song));
        bool equal = r.ok && r.format == 1 && r.division == kTicksPerQuarter && r.song.bpm == song.bpm &&
                    r.song.numerator == song.numerator && r.song.denominator == song.denominator;
        size_t k = 0;
        for (const SmfTrack& t : song.tracks) {
            if (t.events.empty())
                continue; // an empty track is not a track of the song
            std::vector<TimedEvent> expected = t.events;
            minidaw::sortEvents(expected);
            equal = equal && k < r.song.tracks.size() && r.song.tracks[k].name == t.name && sameEvents(r.song.tracks[k].events, expected);
            ++k;
        }
        equal = equal && k == r.song.tracks.size();
        good += equal ? 1 : 0;
    }
    std::printf("  %d of %d random songs (every channel message, names, empty tracks) came back the same\n", good, runs);
    DAWLAB_CHECK(good == runs);
}

inline void checkUntrusted() {
    // Every file cut short is refused; random damage never crashes (ASan and MSVC's checked iterators watch).
    const Bytes file = littleFile();
    int refused = 0;
    for (size_t n = 0; n < file.size(); ++n)
        refused += readSmf(Bytes(file.begin(), file.begin() + static_cast<std::ptrdiff_t>(n))).ok ? 0 : 1;
    DAWLAB_CHECK(refused == static_cast<int>(file.size()));
    std::mt19937 rng(7);
    int read = 0;
    const int runs = 20000;
    for (int run = 0; run < runs; ++run) {
        Bytes damaged = file;
        const int hits = std::uniform_int_distribution<int>(1, 6)(rng);
        for (int h = 0; h < hits; ++h)
            damaged[std::uniform_int_distribution<size_t>(0, damaged.size() - 1)(rng)] = static_cast<std::uint8_t>(rng());
        if (run % 3 == 0)
            damaged.resize(std::uniform_int_distribution<size_t>(0, damaged.size())(rng));
        read += readSmf(damaged).ok ? 1 : 0;
    }
    std::printf("  %zu files cut short: %d refused; %d damaged files: no crash (%d still readable)\n", file.size(), refused, runs, read);
}

} // namespace ex3::check

int main() {
    using namespace ex3::check;
    std::printf("Exercise 3 - Standard MIDI Files\n");
    checkVlq();
    checkWrite();
    checkRead();
    checkRefusals();
    checkRoundTrips();
    checkUntrusted();
    return dawlab::test::summary("l18_ex3");
}
