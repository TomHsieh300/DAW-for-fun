// Exercise 3 (solution): Standard MIDI Files.
//
// Build and run:  l18_ex3_solution   (ctest, label "solution")
// Your version:   exercises/starter/ex3_smf.cpp -> l18_ex3_starter

#include "../exercises/ex3_smf.h"

#include <algorithm>
#include <cmath>

namespace ex3 {

namespace {

// The data bytes a channel message status carries.
int dataBytes(int status) noexcept {
    const int kind = status & 0xF0;
    return kind == 0xC0 || kind == 0xD0 ? 1 : 2;
}

void put16(std::vector<std::uint8_t>& out, int v) {
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int shift = 24; shift >= 0; shift -= 8)
        out.push_back(static_cast<std::uint8_t>((v >> shift) & 0xFF));
}

void chunk(std::vector<std::uint8_t>& out, const char* type, const std::vector<std::uint8_t>& data) {
    out.insert(out.end(), type, type + 4);
    put32(out, static_cast<std::uint32_t>(data.size()));
    out.insert(out.end(), data.begin(), data.end());
}

std::uint32_t get32(const std::uint8_t* p) noexcept {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) | (static_cast<std::uint32_t>(p[2]) << 8) | p[3];
}

int get16(const std::uint8_t* p) noexcept {
    return (p[0] << 8) | p[1];
}

SmfRead refuse(const char* why) {
    SmfRead r;
    r.error = why;
    return r;
}

} // namespace

void writeVlq(std::vector<std::uint8_t>& out, std::uint32_t value) {
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
    return false; // a fifth byte would be needed
}

std::vector<std::uint8_t> writeSmf(const Song& song) {
    std::vector<std::uint8_t> file;
    std::vector<std::uint8_t> header;
    put16(header, 1);
    put16(header, static_cast<int>(song.tracks.size()) + 1);
    put16(header, kTicksPerQuarter);
    chunk(file, "MThd", header);

    std::vector<std::uint8_t> conductor;
    int log2 = 0;
    while ((1 << (log2 + 1)) <= song.denominator)
        ++log2;
    const std::uint32_t micros = static_cast<std::uint32_t>(std::llround(60000000.0 / song.bpm));
    conductor.insert(conductor.end(), {0x00, 0xFF, 0x58, 0x04, static_cast<std::uint8_t>(song.numerator), static_cast<std::uint8_t>(log2), 0x18, 0x08});
    conductor.insert(conductor.end(), {0x00, 0xFF, 0x51, 0x03, static_cast<std::uint8_t>(micros >> 16), static_cast<std::uint8_t>(micros >> 8),
                                       static_cast<std::uint8_t>(micros)});
    conductor.insert(conductor.end(), {0x00, 0xFF, 0x2F, 0x00});
    chunk(file, "MTrk", conductor);

    for (const SmfTrack& track : song.tracks) {
        std::vector<std::uint8_t> data;
        if (!track.name.empty()) {
            data.insert(data.end(), {0x00, 0xFF, 0x03});
            writeVlq(data, static_cast<std::uint32_t>(track.name.size()));
            data.insert(data.end(), track.name.begin(), track.name.end());
        }
        std::vector<TimedEvent> events;
        for (const TimedEvent& e : track.events)
            if (e.tick >= 0)
                events.push_back(e);
        minidaw::sortEvents(events);
        std::int64_t last = 0;
        int running = 0;
        for (const TimedEvent& e : events) {
            writeVlq(data, static_cast<std::uint32_t>(e.tick - last));
            last = e.tick;
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

SmfRead readSmf(const std::vector<std::uint8_t>& file) {
    const std::uint8_t* d = file.data();
    const std::size_t size = file.size();
    if (size < 14 || !std::equal(d, d + 4, "MThd"))
        return refuse("not a MIDI file: it does not start with MThd");
    const std::uint32_t headerLength = get32(d + 4);
    if (headerLength < 6 || headerLength > size - 8)
        return refuse("the header chunk is cut short");
    SmfRead r;
    r.format = get16(d + 8);
    const int numTracks = get16(d + 10);
    r.division = get16(d + 12);
    if (r.format > 1)
        return refuse(r.format == 2 ? "format 2 (independent patterns) is not supported" : "unknown format");
    if ((r.division & 0x8000) != 0)
        return refuse("SMPTE time is not supported");
    if (r.division == 0)
        return refuse("division 0");

    bool haveTempo = false, haveSignature = false;
    double tempoMicros = 0.0;
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

        SmfTrack track;
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
                std::uint32_t length32 = 0;
                if (!readVlq(d, end, p, length32) || length32 > end - p)
                    return refuse("a meta or SysEx event runs past its track");
                const std::uint8_t* m = d + p;
                p += length32;
                running = 0;
                if (first != 0xFF) {
                    ++r.skipped;
                } else if (type == 0x2F) {
                    break;
                } else if (type == 0x51 && length32 >= 3) {
                    const double micros = static_cast<double>((m[0] << 16) | (m[1] << 8) | m[2]);
                    if (micros > 0.0 && !haveTempo) {
                        haveTempo = true;
                        tempoMicros = micros;
                    } else if (micros > 0.0 && micros != tempoMicros) {
                        ++r.tempoChanges;
                    }
                } else if (type == 0x58 && length32 >= 2 && !haveSignature && m[1] < 8) {
                    haveSignature = true;
                    r.song.numerator = m[0];
                    r.song.denominator = 1 << m[1];
                } else if (type == 0x03 && !named) {
                    named = true;
                    track.name.assign(reinterpret_cast<const char*>(m), length32);
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
            MidiMessage message{static_cast<std::uint8_t>(status), d[p], n == 2 ? d[p + 1] : std::uint8_t{0}};
            p += static_cast<std::size_t>(n);
            const std::uint64_t ours = (tick * kTicksPerQuarter + static_cast<std::uint64_t>(r.division) / 2) / static_cast<std::uint64_t>(r.division);
            track.events.push_back({static_cast<std::int64_t>(ours), message});
        }
        if (!track.events.empty())
            r.song.tracks.push_back(std::move(track));
    }
    if (haveTempo)
        r.song.bpm = 60000000.0 / tempoMicros;
    r.ok = true;
    return r;
}

} // namespace ex3

#include "../exercises/ex3_check.h"
