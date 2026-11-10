#pragma once
// Exercise 3 - Standard MIDI Files.
//
// Every DAW, notation program and hardware sequencer reads and writes the Standard MIDI File (.mid), so it is
// how notes come into DawLab and leave it. The format is old (1988) and compact: chunks of bytes, times as
// differences in a variable-length number, and a status byte that may be left out.
//
//   File      "MThd" chunk, then the "MTrk" chunks. A chunk: 4 ASCII bytes, a 32-bit big-endian length, the data.
//             Chunks of another type may appear anywhere and are skipped by their length.
//   MThd      16-bit big-endian: format (0 = one track, 1 = several tracks played together, 2 = independent
//             patterns), number of tracks, division (bit 15 clear: ticks per quarter note; set: SMPTE frames).
//             A length larger than 6 is allowed: the extra bytes are skipped.
//   MTrk      events, each: a delta time (ticks since the last event, a VLQ) and the event:
//               channel message  status 0x80-0xEF and its data bytes: two, but one for 0xCn (program change) and
//                                0xDn (channel pressure). RUNNING STATUS: a channel message may leave its status out
//                                when it is the same as the last channel message's; the first byte is then a
//                                data byte (< 0x80).
//               meta event       0xFF, type, VLQ length, data. 0x2F (length 0) ends the track; 0x51 (3 bytes) is the
//                                tempo in microseconds per quarter note, big-endian; 0x58 (4 bytes) the time
//                                signature: numerator, log2(denominator), clocks per click (24), 32nds per quarter
//                                (8); 0x03 the track's name. Others are skipped.
//               SysEx            0xF0 or 0xF7, VLQ length, data: skipped here.
//             Meta events and SysEx cancel running status.
//   VLQ       7 bits a byte, most significant first; bit 7 set on every byte but the last. At most 4 bytes
//             (0x0FFFFFFF).
//
// DawLab's notes are in ticks of 960 a quarter; a file's division may be anything (96, 480 and 960 are
// common). Convert each event's ABSOLUTE tick - summing converted deltas would drift - rounding to the nearest.
//
// What is read is untrusted: a file from the internet, cut short, or not a MIDI file at all. The reader may
// refuse it, but it must never read past the end, loop forever or crash.

#include "model.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ex3 {

using minidaw::kTicksPerQuarter;
using minidaw::MidiMessage;
using minidaw::TimedEvent;

struct SmfTrack {
    std::string name;               // the first 0x03 meta event of the track, or empty
    std::vector<TimedEvent> events; // channel messages only, in ticks of 960, in the file's order
};

struct Song {
    double bpm = 120.0;
    int numerator = 4, denominator = 4;
    std::vector<SmfTrack> tracks;
};

struct SmfRead {
    bool ok = false;
    std::string error;   // why not, when !ok
    Song song;
    int format = 0;
    int division = 0;    // the file's ticks per quarter note
    int tempoChanges = 0; // tempo meta events after the first that set another tempo: DawLab keeps one tempo
    int skipped = 0;      // SysEx events and unknown chunks that were skipped
};

// ---- YOUR TURN (starter/ex3_smf.cpp) -----------------------------------------------------------------------
// A VLQ at the end of `out`. value <= 0x0FFFFFFF.
void writeVlq(std::vector<std::uint8_t>& out, std::uint32_t value);
// Reads a VLQ at data[pos], moves pos past it. False (pos unspecified) if it runs past `size` or has more than
// 4 bytes.
bool readVlq(const std::uint8_t* data, std::size_t size, std::size_t& pos, std::uint32_t& value) noexcept;

// The file, exactly so (the self-check compares bytes):
//   MThd, length 6, format 1, 1 + tracks.size() tracks, division 960.
//   Track 0, the conductor: at delta 0 the time signature (FF 58 04 nn dd 18 08, dd = log2(denominator)), then
//   the tempo (FF 51 03, llround(60 000 000 / bpm) big-endian), then the end (FF 2F 00).
//   One MTrk per song track: its name (FF 03 len bytes) at delta 0 if not empty; the events in Step 1's playing
//   order (sortEvents) with running status - the status byte only when it differs from the previous event's -
//   and the data bytes of their kind; the end at delta 0 after the last event.
// Events with a negative tick are not written.
std::vector<std::uint8_t> writeSmf(const Song& song);

// Reads a file of format 0 or 1 with a division in ticks. A track with no channel message (the conductor of
// format 1, an empty track) is not a track of the song. The first tempo found anywhere is the song's (120 if
// none), the first time signature too (4/4 if none). Refuses: a first chunk that is not MThd, format 2, SMPTE
// division, division 0, a chunk or event that runs past the end, a VLQ over 4 bytes, a data byte where a status
// is needed and no running status is set, a status byte where a data byte is needed, the status bytes 0xF1-0xF6
// and 0xF8-0xFE, and fewer MTrk chunks than the header says.
SmfRead readSmf(const std::vector<std::uint8_t>& file);

} // namespace ex3
