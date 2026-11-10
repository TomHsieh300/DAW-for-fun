// Exercise 3 (starter): Standard MIDI Files.
//
// Fill in the TODOs. Until then the self-check prints FAIL (expected). The solution is
// solutions/ex3_smf.cpp. What the functions must do is in ../ex3_smf.h.

#include "../ex3_smf.h"

namespace ex3 {

void writeVlq(std::vector<std::uint8_t>& out, std::uint32_t value) {
    // TODO: 7 bits a byte, the most significant group first, bit 7 set on every byte but the last.
    (void)out;
    (void)value;
}

bool readVlq(const std::uint8_t* data, std::size_t size, std::size_t& pos, std::uint32_t& value) noexcept {
    // TODO: at most 4 bytes, never read data[size] or beyond.
    (void)data;
    (void)size;
    (void)pos;
    value = 0;
    return false;
}

std::vector<std::uint8_t> writeSmf(const Song& song) {
    // TODO: MThd, the conductor track, then one MTrk per track (see the header for the exact bytes).
    //       Chunk lengths are 32-bit big-endian: write the track's data first, then its chunk.
    (void)song;
    return {};
}

SmfRead readSmf(const std::vector<std::uint8_t>& file) {
    // TODO: the header, then the MTrk chunks (skip others); per event a delta, then a meta event, a SysEx or a
    //       channel message (with running status). Ticks: llround(absolute x 960 / division). Check every
    //       length against what is left before reading.
    (void)file;
    SmfRead r;
    r.error = "not done yet";
    return r;
}

} // namespace ex3

#include "../ex3_check.h"
