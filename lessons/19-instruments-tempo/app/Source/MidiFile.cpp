#include "MidiFile.h"

#include <dawlab/engine/TempoMap.h>

#include <algorithm>
#include <cmath>

namespace dawlab::app::midifile {

namespace {

using Bytes = std::vector<std::uint8_t>;
constexpr int kTicks = 960;

int dataBytes(int status) noexcept {
    const int kind = status & 0xF0;
    return kind == 0xC0 || kind == 0xD0 ? 1 : 2;
}

void put16(Bytes& out, int v) {
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
}

void put32(Bytes& out, std::uint32_t v) {
    for (int shift = 24; shift >= 0; shift -= 8)
        out.push_back(static_cast<std::uint8_t>((v >> shift) & 0xFF));
}

void chunk(Bytes& out, const char* type, const Bytes& data) {
    out.insert(out.end(), type, type + 4);
    put32(out, static_cast<std::uint32_t>(data.size()));
    out.insert(out.end(), data.begin(), data.end());
}

// The longest delta time a file can hold: 4 bytes of 7 bits.
constexpr std::int64_t kMaxDelta = 0x0FFFFFFF;

// A value of at most kMaxDelta.
void writeVlq(Bytes& out, std::uint32_t value) {
    std::uint8_t bytes[4];
    int n = 0;
    do {
        bytes[n++] = static_cast<std::uint8_t>(value & 0x7F);
        value >>= 7;
    } while (value != 0 && n < 4);
    while (n > 1)
        out.push_back(static_cast<std::uint8_t>(bytes[--n] | 0x80));
    out.push_back(bytes[0]);
}

// The delta time before an event, `delta` >= 0 ticks. A longer gap than a delta holds is kMaxDelta, then an empty
// text event, as often as it takes: read() skips the text, and every tick comes back exactly. A meta event cancels
// running status, so `running` starts again after one.
void putDelta(Bytes& out, std::int64_t delta, int& running) {
    for (; delta > kMaxDelta; delta -= kMaxDelta) {
        writeVlq(out, static_cast<std::uint32_t>(kMaxDelta));
        out.insert(out.end(), {0xFF, 0x01, 0x00});
        running = 0;
    }
    writeVlq(out, static_cast<std::uint32_t>(delta));
}

// A tempo event's microseconds a quarter. A tempo write() cannot use (0, below 0, NaN, infinity) is MIDI's default,
// 120 BPM; any other is kept to the engine's range, where 60e6 / bpm always fits the event's 24 bits.
std::uint32_t microsPerQuarter(double bpm) {
    const double usable = std::isfinite(bpm) && bpm > 0.0 ? bpm : 120.0;
    const double kept = std::clamp(usable, engine::TempoMap::kMinBpm, engine::TempoMap::kMaxBpm);
    return static_cast<std::uint32_t>(std::llround(60000000.0 / kept));
}

bool readVlq(const std::uint8_t* data, std::size_t size, std::size_t& pos, std::uint32_t& value) noexcept {
    value = 0;
    for (int i = 0; i < 4; ++i) {
        if (pos >= size)
            return false;
        const std::uint8_t b = data[pos++];
        value = (value << 7) | (b & 0x7Fu);
        if ((b & 0x80) == 0)
            return true;
    }
    return false;
}

std::uint32_t get32(const std::uint8_t* p) noexcept {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) | (static_cast<std::uint32_t>(p[2]) << 8) | p[3];
}

int get16(const std::uint8_t* p) noexcept {
    return (p[0] << 8) | p[1];
}

ReadResult refuse(const char* why) {
    ReadResult r;
    r.error = why;
    return r;
}

bool isNoteOff(const engine::MidiMessage& m) noexcept {
    return m.kind() == engine::MidiKind::NoteOff;
}

} // namespace

std::vector<std::uint8_t> write(const Song& song) {
    Bytes file, header;
    put16(header, 1);
    put16(header, static_cast<int>(song.tracks.size()) + 1);
    put16(header, kTicks);
    chunk(file, "MThd", header);

    // The conductor: the time signature, then every tempo at its tick.
    Bytes conductor;
    int log2 = 0;
    while ((1 << (log2 + 1)) <= song.denominator)
        ++log2;
    conductor.insert(conductor.end(), {0x00, 0xFF, 0x58, 0x04, static_cast<std::uint8_t>(song.numerator), static_cast<std::uint8_t>(log2), 0x18, 0x08});
    std::vector<Tempo> tempos = song.tempos;
    if (tempos.empty())
        tempos.push_back({0, 120.0});
    std::int64_t last = 0;
    int noRunning = 0; // the conductor has only meta events
    for (const Tempo& t : tempos) {
        const std::int64_t tick = std::max<std::int64_t>(t.tick, last);
        const std::uint32_t micros = microsPerQuarter(t.bpm);
        putDelta(conductor, tick - last, noRunning);
        conductor.insert(conductor.end(), {0xFF, 0x51, 0x03, static_cast<std::uint8_t>(micros >> 16), static_cast<std::uint8_t>(micros >> 8),
                                           static_cast<std::uint8_t>(micros)});
        last = tick;
    }
    conductor.insert(conductor.end(), {0x00, 0xFF, 0x2F, 0x00});
    chunk(file, "MTrk", conductor);

    for (const Track& track : song.tracks) {
        Bytes data;
        if (!track.name.empty()) {
            data.insert(data.end(), {0x00, 0xFF, 0x03});
            writeVlq(data, static_cast<std::uint32_t>(track.name.size()));
            data.insert(data.end(), track.name.begin(), track.name.end());
        }
        std::vector<Event> events;
        for (const Event& e : track.events)
            if (e.tick >= 0)
                events.push_back(e);
        // Playing order: by tick, note offs first at one tick.
        std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
            if (a.tick != b.tick)
                return a.tick < b.tick;
            return isNoteOff(a.message) && !isNoteOff(b.message);
        });
        std::int64_t at = 0;
        int running = 0;
        for (const Event& e : events) {
            putDelta(data, e.tick - at, running);
            at = e.tick;
            if (e.message.status != running) {
                data.push_back(e.message.status);
                running = e.message.status;
            }
            data.push_back(e.message.data1);
            if (dataBytes(e.message.status) == 2)
                data.push_back(e.message.data2);
        }
        data.insert(data.end(), {0x00, 0xFF, 0x2F, 0x00});
        chunk(file, "MTrk", data);
    }
    return file;
}

ReadResult read(const std::vector<std::uint8_t>& file) {
    const std::uint8_t* d = file.data();
    const std::size_t size = file.size();
    if (size < 14 || !std::equal(d, d + 4, "MThd"))
        return refuse("not a MIDI file: it does not start with MThd");
    const std::uint32_t headerLength = get32(d + 4);
    if (headerLength < 6 || headerLength > size - 8)
        return refuse("the header chunk is cut short");
    ReadResult r;
    r.format = get16(d + 8);
    const int numTracks = get16(d + 10);
    r.division = get16(d + 12);
    if (r.format > 1)
        return refuse(r.format == 2 ? "format 2 (independent patterns) is not supported" : "unknown format");
    if ((r.division & 0x8000) != 0)
        return refuse("SMPTE time is not supported");
    if (r.division == 0)
        return refuse("division 0");
    const auto toOurs = [&r](std::uint64_t tick) {
        return static_cast<std::int64_t>((tick * kTicks + static_cast<std::uint64_t>(r.division) / 2) / static_cast<std::uint64_t>(r.division));
    };

    bool haveSignature = false;
    std::vector<Tempo> tempos;
    std::size_t pos = 8 + headerLength;
    for (int found = 0; found < numTracks;) {
        if (size - pos < 8)
            return refuse("fewer tracks than the header says, or a chunk cut short");
        const std::uint32_t length = get32(d + pos + 4);
        if (length > size - pos - 8)
            return refuse("a chunk runs past the end of the file");
        const bool isTrack = std::equal(d + pos, d + pos + 4, "MTrk");
        const std::size_t end = pos + 8 + length;
        std::size_t p = pos + 8;
        pos = end;
        if (!isTrack) {
            ++r.skipped;
            continue;
        }
        ++found;

        Track track;
        bool named = false;
        std::uint64_t tick = 0;
        int running = 0;
        while (p < end) {
            std::uint32_t delta = 0;
            if (!readVlq(d, end, p, delta))
                return refuse("a delta time runs past its track or has more than 4 bytes");
            tick += delta;
            if (p >= end)
                return refuse("an event runs past its track");
            const std::uint8_t first = d[p];
            if (first == 0xFF || first == 0xF0 || first == 0xF7) {
                ++p;
                int type = -1;
                if (first == 0xFF) {
                    if (p >= end)
                        return refuse("a meta event runs past its track");
                    type = d[p++];
                }
                std::uint32_t metaLength = 0;
                if (!readVlq(d, end, p, metaLength) || metaLength > end - p)
                    return refuse("a meta or SysEx event runs past its track");
                const std::uint8_t* m = d + p;
                p += metaLength;
                running = 0; // a meta event or a SysEx cancels running status
                if (first != 0xFF) {
                    ++r.skipped;
                } else if (type == 0x2F) {
                    break;
                } else if (type == 0x51 && metaLength >= 3) {
                    const double micros = static_cast<double>((m[0] << 16) | (m[1] << 8) | m[2]);
                    if (micros > 0.0)
                        tempos.push_back({toOurs(tick), 60000000.0 / micros});
                } else if (type == 0x58 && metaLength >= 2 && !haveSignature && m[1] < 8) {
                    haveSignature = true;
                    r.song.numerator = m[0];
                    r.song.denominator = 1 << m[1];
                } else if (type == 0x03 && !named) {
                    named = true;
                    track.name.assign(reinterpret_cast<const char*>(m), metaLength);
                }
                continue;
            }
            int status = running;
            if (first >= 0x80) {
                if (first >= 0xF0)
                    return refuse("a system message that does not belong in a file");
                status = first;
                ++p;
            } else if (running == 0) {
                return refuse("a data byte without a status (no running status)");
            }
            running = status;
            const int n = dataBytes(status);
            if (end - p < static_cast<std::size_t>(n))
                return refuse("a channel message runs past its track");
            if (d[p] >= 0x80 || (n == 2 && d[p + 1] >= 0x80))
                return refuse("a status byte where a data byte belongs");
            track.events.push_back({toOurs(tick), {static_cast<std::uint8_t>(status), d[p], n == 2 ? d[p + 1] : std::uint8_t{0}}});
            p += static_cast<std::size_t>(n);
        }
        if (!track.events.empty())
            r.song.tracks.push_back(std::move(track));
    }
    // The tempos in tick order; at one tick the last one read stays. A song starts with a tempo at tick 0.
    std::stable_sort(tempos.begin(), tempos.end(), [](const Tempo& a, const Tempo& b) { return a.tick < b.tick; });
    for (const Tempo& t : tempos) {
        if (!r.song.tempos.empty() && r.song.tempos.back().tick == t.tick)
            r.song.tempos.back() = t;
        else if (r.song.tempos.empty() || r.song.tempos.back().bpm != t.bpm) // the same tempo again is no change
            r.song.tempos.push_back(t);
    }
    if (r.song.tempos.empty() || r.song.tempos.front().tick != 0)
        r.song.tempos.insert(r.song.tempos.begin(), {0, 120.0}); // MIDI's default until the first tempo
    r.ok = true;
    return r;
}

} // namespace dawlab::app::midifile
