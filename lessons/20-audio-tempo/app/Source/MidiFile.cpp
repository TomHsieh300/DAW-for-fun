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

// The conductor: the time signature, then every tempo at its tick.
Bytes conductorTrack(const Song& song) {
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
    return conductor;
}

// A track's events at tick 0 or later, in playing order: by tick, note offs first at one tick.
std::vector<Event> playingOrder(const Track& track) {
    std::vector<Event> events;
    for (const Event& e : track.events)
        if (e.tick >= 0)
            events.push_back(e);
    std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
        if (a.tick != b.tick)
            return a.tick < b.tick;
        return isNoteOff(a.message) && !isNoteOff(b.message);
    });
    return events;
}

// One track: its name, if it has one, then its events, with running status.
Bytes trackData(const Track& track) {
    Bytes data;
    if (!track.name.empty()) {
        data.insert(data.end(), {0x00, 0xFF, 0x03});
        writeVlq(data, static_cast<std::uint32_t>(track.name.size()));
        data.insert(data.end(), track.name.begin(), track.name.end());
    }
    std::int64_t at = 0;
    int running = 0;
    for (const Event& e : playingOrder(track)) {
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
    return data;
}

} // namespace

std::vector<std::uint8_t> write(const Song& song) {
    Bytes file, header;
    put16(header, 1);
    put16(header, static_cast<int>(song.tracks.size()) + 1);
    put16(header, kTicks);
    chunk(file, "MThd", header);
    chunk(file, "MTrk", conductorTrack(song));
    for (const Track& track : song.tracks)
        chunk(file, "MTrk", trackData(track));
    return file;
}

namespace {

// What read() keeps across the tracks of a file.
struct Reading {
    ReadResult r;
    std::vector<Tempo> tempos; // as read, from every track; settleTempos() puts them in order
    bool haveSignature = false;

    // A tick of the file's division in ours (960 a quarter), to the nearest.
    std::int64_t toOurs(std::uint64_t tick) const {
        return static_cast<std::int64_t>((tick * kTicks + static_cast<std::uint64_t>(r.division) / 2) / static_cast<std::uint64_t>(r.division));
    }
};

// One track chunk, d[p .. end), read event by event.
struct TrackReader {
    const std::uint8_t* d = nullptr;
    std::size_t p = 0;
    std::size_t end = 0;
    Track track;
    bool named = false;
    std::uint64_t tick = 0;
    int running = 0; // the running status; 0: none yet
};

// The header chunk: the format, the number of tracks and the division. Returns why the file cannot be read, or
// nullptr; `pos` is then where the first chunk after the header starts.
const char* readHeader(const Bytes& file, ReadResult& r, int& numTracks, std::size_t& pos) {
    const std::uint8_t* d = file.data();
    const std::size_t size = file.size();
    if (size < 14 || !std::equal(d, d + 4, "MThd"))
        return "not a MIDI file: it does not start with MThd";
    const std::uint32_t headerLength = get32(d + 4);
    if (headerLength < 6 || headerLength > size - 8)
        return "the header chunk is cut short";
    r.format = get16(d + 8);
    numTracks = get16(d + 10);
    r.division = get16(d + 12);
    if (r.format > 1)
        return r.format == 2 ? "format 2 (independent patterns) is not supported" : "unknown format";
    if ((r.division & 0x8000) != 0)
        return "SMPTE time is not supported";
    if (r.division == 0)
        return "division 0";
    pos = 8 + headerLength;
    return nullptr;
}

// The meta events a song keeps: every tempo, the file's first time signature, a track's first name.
void keepMeta(Reading& in, TrackReader& t, int type, const std::uint8_t* m, std::uint32_t length) {
    if (type == 0x51 && length >= 3) {
        const double micros = static_cast<double>((m[0] << 16) | (m[1] << 8) | m[2]);
        if (micros > 0.0)
            in.tempos.push_back({in.toOurs(t.tick), 60000000.0 / micros});
    } else if (type == 0x58 && length >= 2 && !in.haveSignature && m[1] < 8) {
        in.haveSignature = true;
        in.r.song.numerator = m[0];
        in.r.song.denominator = 1 << m[1];
    } else if (type == 0x03 && !t.named) {
        t.named = true;
        t.track.name.assign(reinterpret_cast<const char*>(m), length);
    }
}

// A meta event (FF type length data) or a SysEx (F0 or F7, length data), which is only counted. The end-of-track
// meta event sets `endOfTrack`.
const char* readMetaOrSysEx(Reading& in, TrackReader& t, bool& endOfTrack) {
    const std::uint8_t first = t.d[t.p++];
    int type = -1;
    if (first == 0xFF) {
        if (t.p >= t.end)
            return "a meta event runs past its track";
        type = t.d[t.p++];
    }
    std::uint32_t length = 0;
    if (!readVlq(t.d, t.end, t.p, length) || length > t.end - t.p)
        return "a meta or SysEx event runs past its track";
    const std::uint8_t* m = t.d + t.p;
    t.p += length;
    t.running = 0; // a meta event or a SysEx cancels running status
    if (first != 0xFF)
        ++in.r.skipped;
    else if (type == 0x2F)
        endOfTrack = true;
    else
        keepMeta(in, t, type, m, length);
    return nullptr;
}

// A channel message, with its own status byte or the running status.
const char* readChannelMessage(const Reading& in, TrackReader& t) {
    const std::uint8_t first = t.d[t.p];
    int status = t.running;
    if (first >= 0x80) {
        if (first >= 0xF0)
            return "a system message that does not belong in a file";
        status = first;
        ++t.p;
    } else if (t.running == 0) {
        return "a data byte without a status (no running status)";
    }
    t.running = status;
    const int n = dataBytes(status);
    if (t.end - t.p < static_cast<std::size_t>(n))
        return "a channel message runs past its track";
    const std::uint8_t* data = t.d + t.p;
    if (data[0] >= 0x80 || (n == 2 && data[1] >= 0x80))
        return "a status byte where a data byte belongs";
    t.track.events.push_back({in.toOurs(t.tick), {static_cast<std::uint8_t>(status), data[0], n == 2 ? data[1] : std::uint8_t{0}}});
    t.p += static_cast<std::size_t>(n);
    return nullptr;
}

// One event: its delta time, then a meta event, a SysEx or a channel message.
const char* readEvent(Reading& in, TrackReader& t, bool& endOfTrack) {
    std::uint32_t delta = 0;
    if (!readVlq(t.d, t.end, t.p, delta))
        return "a delta time runs past its track or has more than 4 bytes";
    t.tick += delta;
    if (t.p >= t.end)
        return "an event runs past its track";
    const std::uint8_t first = t.d[t.p];
    if (first == 0xFF || first == 0xF0 || first == 0xF7)
        return readMetaOrSysEx(in, t, endOfTrack);
    return readChannelMessage(in, t);
}

// One track chunk's events, d[begin .. end), up to its end-of-track event or its last byte. A track with no channel
// message is not kept.
const char* readTrack(Reading& in, const std::uint8_t* d, std::size_t begin, std::size_t end) {
    TrackReader t;
    t.d = d;
    t.p = begin;
    t.end = end;
    bool endOfTrack = false;
    while (t.p < t.end && !endOfTrack)
        if (const char* error = readEvent(in, t, endOfTrack))
            return error;
    if (!t.track.events.empty())
        in.r.song.tracks.push_back(std::move(t.track));
    return nullptr;
}

// The chunks from `pos` on, until `numTracks` track chunks are read. A chunk of another type is skipped and counted.
const char* readChunks(Reading& in, const Bytes& file, std::size_t pos, int numTracks) {
    const std::uint8_t* d = file.data();
    const std::size_t size = file.size();
    for (int found = 0; found < numTracks;) {
        if (size - pos < 8)
            return "fewer tracks than the header says, or a chunk cut short";
        const std::uint32_t length = get32(d + pos + 4);
        if (length > size - pos - 8)
            return "a chunk runs past the end of the file";
        const bool isTrack = std::equal(d + pos, d + pos + 4, "MTrk");
        const std::size_t begin = pos + 8;
        pos = begin + length;
        if (!isTrack) {
            ++in.r.skipped;
            continue;
        }
        ++found;
        if (const char* error = readTrack(in, d, begin, pos))
            return error;
    }
    return nullptr;
}

// The tempos in tick order; at one tick the last one read stays. A song starts with a tempo at tick 0.
void settleTempos(std::vector<Tempo> tempos, Song& song) {
    std::stable_sort(tempos.begin(), tempos.end(), [](const Tempo& a, const Tempo& b) { return a.tick < b.tick; });
    for (const Tempo& t : tempos) {
        if (!song.tempos.empty() && song.tempos.back().tick == t.tick)
            song.tempos.back() = t;
        else if (song.tempos.empty() || song.tempos.back().bpm != t.bpm) // the same tempo again is no change
            song.tempos.push_back(t);
    }
    if (song.tempos.empty() || song.tempos.front().tick != 0)
        song.tempos.insert(song.tempos.begin(), {0, 120.0}); // MIDI's default until the first tempo
}

} // namespace

ReadResult read(const std::vector<std::uint8_t>& file) {
    Reading in;
    int numTracks = 0;
    std::size_t pos = 0;
    if (const char* error = readHeader(file, in.r, numTracks, pos))
        return refuse(error);
    if (const char* error = readChunks(in, file, pos, numTracks))
        return refuse(error);
    settleTempos(std::move(in.tempos), in.r.song);
    in.r.ok = true;
    return std::move(in.r);
}

} // namespace dawlab::app::midifile
