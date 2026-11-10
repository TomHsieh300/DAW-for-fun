// test_midi_file: Standard MIDI Files as the app reads and writes them (chapter 19, Step 5). No JUCE.
//
//   1. A song with tempo changes is written as the exact bytes, and read back.
//   2. Tempos anywhere in a file - in any track, out of order, twice at a tick - become the song's, in order.
//   3. Random songs come back the same.
//   4. Broken and damaged files are refused; nothing is read past the end (AddressSanitizer watches).

#include "MidiFile.h"

#include <dawlab/Check.h>

#include <cstdio>
#include <initializer_list>
#include <random>

using namespace dawlab::app::midifile;
using dawlab::engine::MidiMessage;

namespace {

using Bytes = std::vector<std::uint8_t>;

Bytes bytes(std::initializer_list<int> list) {
    Bytes b;
    for (const int v : list)
        b.push_back(static_cast<std::uint8_t>(v));
    return b;
}

Bytes cat(std::initializer_list<Bytes> parts) {
    Bytes b;
    for (const Bytes& p : parts)
        b.insert(b.end(), p.begin(), p.end());
    return b;
}

Bytes chunkOf(const char* type, const Bytes& data) {
    Bytes b(type, type + 4);
    const auto n = static_cast<std::uint32_t>(data.size());
    b.insert(b.end(), {static_cast<std::uint8_t>(n >> 24), static_cast<std::uint8_t>(n >> 16), static_cast<std::uint8_t>(n >> 8),
                       static_cast<std::uint8_t>(n)});
    b.insert(b.end(), data.begin(), data.end());
    return b;
}

Bytes header(int format, int tracks, int division) {
    return chunkOf("MThd", bytes({0, format, tracks >> 8, tracks & 0xFF, division >> 8, division & 0xFF}));
}

bool sameEvents(const std::vector<Event>& a, const std::vector<Event>& b) {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].tick != b[i].tick || !(a[i].message == b[i].message))
            return false;
    return true;
}

bool sameTempos(const std::vector<Tempo>& a, const std::vector<Tempo>& b) {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].tick != b[i].tick || a[i].bpm != b[i].bpm)
            return false;
    return true;
}

void testBytes() {
    Song song;
    song.numerator = 3;
    song.tempos = {{0, 100.0}, {960, 120.0}, {1440, 60.0}};
    song.tracks.push_back({"Bass", {{480, MidiMessage::noteOn(0, 38, 100)}, {0, MidiMessage::noteOn(0, 36, 100)}, {480, MidiMessage::noteOff(0, 36, 64)}}});
    const Bytes conductor = bytes({0x00, 0xFF, 0x58, 0x04, 0x03, 0x02, 0x18, 0x08, //
                                   0x00, 0xFF, 0x51, 0x03, 0x09, 0x27, 0xC0,       // 600 000 us: 100 BPM at 0
                                   0x87, 0x40, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20, // 960 later: 120 BPM
                                   0x83, 0x60, 0xFF, 0x51, 0x03, 0x0F, 0x42, 0x40, // 480 later: 60 BPM
                                   0x00, 0xFF, 0x2F, 0x00});
    const Bytes bass = bytes({0x00, 0xFF, 0x03, 0x04, 'B', 'a', 's', 's', 0x00, 0x90, 36, 100, 0x83, 0x60, 0x80, 36, 64, 0x00, 0x90, 38, 100,
                              0x00, 0xFF, 0x2F, 0x00});
    const Bytes expected = cat({header(1, 2, 960), chunkOf("MTrk", conductor), chunkOf("MTrk", bass)});
    const Bytes written = write(song);
    DAWLAB_CHECK(written == expected);
    const ReadResult r = read(written);
    DAWLAB_CHECK(r.ok && r.format == 1 && r.division == 960 && r.song.numerator == 3 && r.song.denominator == 4);
    DAWLAB_CHECK(sameTempos(r.song.tempos, song.tempos) && r.song.tracks.size() == 1 && r.song.tracks[0].name == "Bass");
    std::printf("  a song with three tempos: %zu bytes, exactly the expected ones, and read back\n", written.size());
    // No tempo at all is 120 BPM, written and read.
    Song plain;
    plain.tracks.push_back({"", {{0, MidiMessage::noteOn(0, 60, 1)}}});
    const ReadResult p = read(write(plain));
    DAWLAB_CHECK(p.ok && sameTempos(p.song.tempos, {{0, 120.0}}));
}

void testTemposAnywhere() {
    // Format 1 at 480 a quarter: a tempo in the second track, one before it in the first, two at one tick, the same
    // tempo again, and none at tick 0.
    const Bytes one = bytes({0x83, 0x60, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20, // tick 480 (ours 960): 120
                             0x00, 0xFF, 0x51, 0x03, 0x0F, 0x42, 0x40,       // the same tick: 60 replaces it
                             0x00, 0x90, 60, 100, 0x00, 0xFF, 0x2F, 0x00});
    const Bytes two = bytes({0x81, 0x70, 0xFF, 0x51, 0x03, 0x09, 0x27, 0xC0, // tick 240 (ours 480): 100
                             0x87, 0x40, 0xFF, 0x51, 0x03, 0x0F, 0x42, 0x40, // tick 1200 (ours 2400): 60 again - no change
                             0x00, 0x91, 61, 100, 0x00, 0xFF, 0x2F, 0x00});
    const ReadResult r = read(cat({header(1, 2, 480), chunkOf("MTrk", one), chunkOf("MTrk", two)}));
    DAWLAB_CHECK(r.ok && r.song.tracks.size() == 2);
    DAWLAB_CHECK(sameTempos(r.song.tempos, {{0, 120.0}, {480, 100.0}, {960, 60.0}}));
    std::printf("  tempos in two tracks, out of order, two at one tick, none at 0: %zu tempos in order\n", r.song.tempos.size());
}

void testRoundTrips() {
    std::mt19937 rng(19);
    auto pick = [&rng](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
    int good = 0;
    const int runs = 1000;
    for (int run = 0; run < runs; ++run) {
        Song song;
        song.numerator = pick(1, 12);
        song.denominator = 1 << pick(0, 5);
        std::int64_t tick = 0;
        double last = 0.0;
        for (int i = 0, n = pick(1, 20); i < n; ++i) {
            const double bpm = 60000000.0 / pick(250000, 1500000); // a whole number of microseconds comes back exactly
            if (bpm != last)
                song.tempos.push_back({tick, bpm});
            last = bpm;
            tick += pick(1, 5000);
        }
        for (int t = 0, numTracks = pick(1, 4); t < numTracks; ++t) {
            Track track;
            if (pick(0, 1) == 1)
                track.name = std::string(static_cast<size_t>(pick(1, 200)), static_cast<char>('a' + t));
            for (int i = 0, n = pick(1, 50); i < n; ++i) {
                const int ch = pick(0, 15);
                const std::int64_t at = pick(0, 3) == 0 ? 0 : pick(0, 2000000);
                MidiMessage m;
                switch (pick(0, 4)) {
                case 0: m = MidiMessage::noteOn(ch, pick(0, 127), pick(0, 127)); break;
                case 1: m = MidiMessage::noteOff(ch, pick(0, 127), pick(0, 127)); break;
                case 2: m = MidiMessage::controller(ch, pick(0, 127), pick(0, 127)); break;
                case 3: m = MidiMessage::pitchBend(ch, pick(-8192, 8191)); break;
                default: m = {static_cast<std::uint8_t>(0xC0 | ch), static_cast<std::uint8_t>(pick(0, 127)), 0}; break;
                }
                track.events.push_back({at, m});
            }
            song.tracks.push_back(track);
        }
        const ReadResult r = read(write(song));
        bool equal = r.ok && sameTempos(r.song.tempos, song.tempos) && r.song.numerator == song.numerator &&
                     r.song.denominator == song.denominator && r.song.tracks.size() == song.tracks.size();
        for (size_t t = 0; equal && t < song.tracks.size(); ++t) {
            std::vector<Event> expected = song.tracks[t].events;
            std::stable_sort(expected.begin(), expected.end(), [](const Event& a, const Event& b) {
                if (a.tick != b.tick)
                    return a.tick < b.tick;
                return a.message.kind() == dawlab::engine::MidiKind::NoteOff && b.message.kind() != dawlab::engine::MidiKind::NoteOff;
            });
            equal = r.song.tracks[t].name == song.tracks[t].name && sameEvents(r.song.tracks[t].events, expected);
        }
        good += equal ? 1 : 0;
    }
    std::printf("  %d of %d random songs (tempos, names, every kind of message) came back the same\n", good, runs);
    DAWLAB_CHECK(good == runs);
}

void testUntrusted() {
    const Bytes ok = bytes({0x00, 0x90, 60, 100, 0x00, 0xFF, 0x2F, 0x00});
    const struct {
        const char* what;
        Bytes file;
    } bad[] = {
        {"nothing", Bytes{}},
        {"a WAV file", bytes({'R', 'I', 'F', 'F', 0, 0, 0, 4, 'W', 'A', 'V', 'E', 0, 0})},
        {"format 2", cat({header(2, 1, 96), chunkOf("MTrk", ok)})},
        {"SMPTE time", cat({header(0, 1, 0xE728), chunkOf("MTrk", ok)})},
        {"division 0", cat({header(0, 1, 0), chunkOf("MTrk", ok)})},
        {"no track", header(0, 1, 96)},
        {"a chunk past the end", cat({header(0, 1, 96), bytes({'M', 'T', 'r', 'k', 0, 0, 0, 9}), ok})},
        {"a delta of 5 bytes", cat({header(0, 1, 96), chunkOf("MTrk", bytes({0x81, 0x81, 0x81, 0x81, 0x01, 0x90, 60, 1}))})},
        {"data without a status", cat({header(0, 1, 96), chunkOf("MTrk", bytes({0x00, 60, 100}))})},
        {"running status after a meta", cat({header(0, 1, 96), chunkOf("MTrk", bytes({0x00, 0x90, 60, 1, 0x00, 0xFF, 0x01, 0x00, 0x00, 61, 1}))})},
        {"a status as data", cat({header(0, 1, 96), chunkOf("MTrk", bytes({0x00, 0x90, 60, 0x90}))})},
        {"a clock in a file", cat({header(0, 1, 96), chunkOf("MTrk", bytes({0x00, 0xF8}))})},
        {"a message cut short", cat({header(0, 1, 96), chunkOf("MTrk", bytes({0x00, 0x90, 60}))})},
        {"a meta event past its track", cat({header(0, 1, 96), chunkOf("MTrk", bytes({0x00, 0xFF, 0x51, 0x10, 'a'}))})},
    };
    int refused = 0;
    for (const auto& f : bad) {
        const ReadResult r = read(f.file);
        if (r.ok || r.error.empty())
            std::printf("  NOT refused: %s\n", f.what);
        refused += !r.ok && !r.error.empty() ? 1 : 0;
    }
    DAWLAB_CHECK(refused == static_cast<int>(sizeof bad / sizeof bad[0]));
    // A tempo of 0 microseconds is skipped, not divided by.
    const ReadResult zero = read(cat({header(0, 1, 96), chunkOf("MTrk", bytes({0x00, 0xFF, 0x51, 0x03, 0, 0, 0, 0x00, 0x90, 60, 1}))}));
    DAWLAB_CHECK(zero.ok && sameTempos(zero.song.tempos, {{0, 120.0}}));

    Song song;
    song.tempos = {{0, 100.0}, {960, 120.0}};
    song.tracks.push_back({"T", {{0, MidiMessage::noteOn(0, 60, 100)}, {480, MidiMessage::noteOff(0, 60)}}});
    const Bytes file = write(song);
    int cut = 0;
    for (size_t n = 0; n < file.size(); ++n)
        cut += read(Bytes(file.begin(), file.begin() + static_cast<std::ptrdiff_t>(n))).ok ? 0 : 1;
    DAWLAB_CHECK(cut == static_cast<int>(file.size()));
    std::mt19937 rng(7);
    int readable = 0;
    const int runs = 20000;
    for (int run = 0; run < runs; ++run) {
        Bytes damaged = file;
        for (int h = 0, hits = std::uniform_int_distribution<int>(1, 6)(rng); h < hits; ++h)
            damaged[std::uniform_int_distribution<size_t>(0, damaged.size() - 1)(rng)] = static_cast<std::uint8_t>(rng());
        if (run % 3 == 0)
            damaged.resize(std::uniform_int_distribution<size_t>(0, damaged.size())(rng));
        const ReadResult r = read(damaged);
        readable += r.ok ? 1 : 0;
        // Whatever came out is a song the app can take: tempos in order from tick 0, positive.
        bool sane = !r.ok || (!r.song.tempos.empty() && r.song.tempos.front().tick == 0);
        for (size_t i = 0; sane && r.ok && i < r.song.tempos.size(); ++i)
            sane = r.song.tempos[i].bpm > 0.0 && (i == 0 || r.song.tempos[i].tick > r.song.tempos[i - 1].tick);
        if (!sane) {
            DAWLAB_CHECK(sane);
            break;
        }
    }
    std::printf("  %d of %zu broken files refused, %zu files cut short refused, %d damaged files: no crash (%d still readable)\n", refused,
                sizeof bad / sizeof bad[0], file.size(), runs, readable);
}

} // namespace

int main() {
    std::printf("l19_test_midi_file: Standard MIDI Files with tempo changes\n");
    testBytes();
    testTemposAnywhere();
    testRoundTrips();
    testUntrusted();
    return dawlab::test::summary("l19_test_midi_file");
}
