#include "MidiFileIO.h"

#include "TempoThinning.h"

#include <algorithm>
#include <deque>
#include <map>

namespace dawlab::app {

namespace {

constexpr std::int64_t kBar = 4 * 960;
constexpr std::int64_t kRampStep = 240; // a ramp is written as steps of a sixteenth

engine::MidiMessage bytesOf(int status, int a, int b) {
    return {static_cast<std::uint8_t>(status), static_cast<std::uint8_t>(a & 0x7F), static_cast<std::uint8_t>(b & 0x7F)};
}

// A ramp from point i to the next, as steps of kRampStep ticks. The tempo of each is the one at which the step takes
// exactly as long as the ramp does over it: every step's end is on time.
void addRampSteps(const engine::TempoMap& map, int i, std::vector<midifile::Tempo>& tempos) {
    const std::int64_t end = map.point(i + 1).tick;
    for (std::int64_t t = map.point(i).tick; t < end; t += kRampStep) {
        const std::int64_t step = std::min(kRampStep, end - t);
        const double seconds = map.secondsAt(static_cast<double>(t + step)) - map.secondsAt(static_cast<double>(t));
        tempos.push_back({t, static_cast<double>(step) * 60.0 / (960.0 * seconds)});
    }
}

// Every tempo of the map at its tick; a ramp in steps.
std::vector<midifile::Tempo> temposOf(const engine::TempoMap& map) {
    std::vector<midifile::Tempo> tempos;
    for (int i = 0; i < map.size(); ++i) {
        const engine::TempoPoint& p = map.point(i);
        if (!p.rampToNext || i + 1 == map.size())
            tempos.push_back({p.tick, p.bpm});
        else
            addRampSteps(map, i, tempos);
    }
    return tempos;
}

// What a clip plays, at its place in the song: the notes that start inside it (cut at its end) and its
// controller points. Returns the number of notes.
int addClipEvents(const MidiClipModel& clip, midifile::Track& track) {
    const std::int64_t from = clip.offset, to = clip.offset + clip.length;
    auto place = [&clip](std::int64_t material) { return clip.start + material - clip.offset; };
    int notes = 0;
    for (const engine::Note& n : clip.notes()) { // what the engine plays: after quantize
        if (n.start < from || n.start >= to)
            continue;
        track.events.push_back({place(n.start), bytesOf(0x90 | n.channel, n.pitch, n.velocity)});
        track.events.push_back({place(std::min(n.start + n.length, to)), bytesOf(0x80 | n.channel, n.pitch, 0)});
        ++notes;
    }
    for (const engine::ControllerPoint& c : clip.controllers)
        if (c.tick >= from && c.tick < to)
            track.events.push_back({place(c.tick), bytesOf(0xB0 | c.channel, c.number, c.value)});
    return notes;
}

// An instrument track: its name and what its clips that are not muted play. `notes` counts the notes.
midifile::Track trackOf(const Session& session, engine::TrackId id, int& notes) {
    midifile::Track track;
    track.name = session.getStripName(session.trackRef(id)).toStdString();
    for (const MidiClipModel& clip : session.getMidiClips(id))
        if (!clip.muted)
            notes += addClipEvents(clip, track);
    return track;
}

} // namespace

MidiExport songOf(const Session& session) {
    MidiExport out;
    const engine::Transport& tr = session.getEngine().getTransport();
    out.song.numerator = tr.getTimeSignature().numerator;
    out.song.denominator = tr.getTimeSignature().denominator;

    out.song.tempos = temposOf(session.getTempoMap());
    for (int t = 0; t < session.getNumTracks(); ++t) {
        const engine::TrackId id = session.getTrackId(t);
        if (!session.isInstrument(id))
            continue;
        midifile::Track track = trackOf(session, id, out.notes);
        if (!track.events.empty())
            out.song.tracks.push_back(std::move(track));
    }
    return out;
}

namespace {

// The notes of a clip not yet ended, by (channel, pitch): their indexes in the clip, oldest first.
using OpenNotes = std::map<std::pair<int, int>, std::deque<size_t>>;

// A note off ends the note of its key and channel that started first. One with no such note does nothing.
void endNote(MidiClipModel& clip, OpenNotes& open, int channel, int pitch, std::int64_t tick) {
    const auto it = open.find({channel, pitch});
    if (it == open.end() || it->second.empty())
        return;
    engine::Note& n = clip.played[it->second.front()];
    n.length = std::max<std::int64_t>(tick - n.start, 1);
    it->second.pop_front();
}

// One event of a file's track into the clip: a note on starts a note, a note off ends one, a controller is the
// clip's. Anything else (pitch bend, program changes) is only counted, in `dropped`.
void addEvent(MidiClipModel& clip, OpenNotes& open, const midifile::Event& e, int& dropped) {
    const engine::MidiMessage& m = e.message;
    const int status = m.status & 0xF0, channel = m.status & 0x0F;
    const bool on = status == 0x90 && m.data2 > 0, off = status == 0x80 || (status == 0x90 && m.data2 == 0);
    if (on) {
        open[{channel, m.data1}].push_back(clip.played.size());
        clip.played.push_back({e.tick, 0, m.data1, m.data2, channel});
    } else if (off) {
        endNote(clip, open, channel, m.data1, e.tick);
    } else if (status == 0xB0) {
        clip.controllers.push_back({e.tick, m.data1, m.data2, channel});
    } else {
        ++dropped;
    }
}

// A file's track as one clip from tick 0 to the end of its last bar. A note that is never ended ends with the
// track.
MidiClipModel clipOf(const midifile::Track& track, int& dropped) {
    MidiClipModel clip;
    OpenNotes open;
    std::int64_t last = 0;
    for (const midifile::Event& e : track.events) {
        last = std::max(last, e.tick);
        addEvent(clip, open, e, dropped);
    }
    for (auto& [key, notes] : open)
        for (const size_t i : notes)
            clip.played[i].length = std::max<std::int64_t>(last - clip.played[i].start, 1);
    sortNotes(clip.played);
    std::stable_sort(clip.controllers.begin(), clip.controllers.end(),
                     [](const engine::ControllerPoint& a, const engine::ControllerPoint& b) { return a.tick < b.tick; });
    std::int64_t end = last;
    for (const engine::Note& n : clip.played)
        end = std::max(end, n.start + n.length);
    clip.length = std::max<std::int64_t>((end + kBar - 1) / kBar * kBar, kBar);
    return clip;
}

// The song's tempos as the map's points: jumps. A tempo that only repeats the one before is dropped; more than the
// map can hold are thinned to the fewest points that keep every tick within a tolerance (TempoThinning.h).
tempothin::Fitted tempoMapOf(const midifile::Song& song) {
    std::vector<engine::TempoPoint> points;
    for (const midifile::Tempo& tempo : song.tempos) {
        const double bpm = std::clamp(tempo.bpm, engine::TempoMap::kMinBpm, engine::TempoMap::kMaxBpm);
        if (points.empty() || tempo.tick > points.back().tick)
            points.push_back({points.empty() ? 0 : tempo.tick, bpm, false});
    }
    if (points.empty())
        points.push_back({0, 120.0, false});
    return tempothin::fitTempoPoints(points);
}

} // namespace

MidiImport importSong(Session& session, const midifile::Song& song) {
    MidiImport result;
    if (song.tracks.empty()) {
        result.error = "the file has no track with notes";
        return result;
    }
    std::vector<Edit> edits;
    const std::uint32_t firstId = session.makeNewTrack(engine::TrackFormat::Mono).track.id.value;
    for (size_t t = 0; t < song.tracks.size(); ++t) {
        const midifile::Track& track = song.tracks[t];
        MidiClipModel clip = clipOf(track, result.dropped);
        const juce::String name = track.name.empty() ? "MIDI " + juce::String(static_cast<int>(t) + 1) : juce::String(track.name);
        clip.name = name;
        clip.id = session.newMidiClipId();
        result.notes += static_cast<int>(clip.played.size());

        edit::AddTrack add = session.makeNewTrack(engine::TrackFormat::Mono, -1, name, true);
        add.track.id = engine::TrackId{firstId + static_cast<std::uint32_t>(t)}; // makeNewTrack() names the same next id each time
        add.track.midiClips = {clip};
        result.tracks.push_back(add.track.id);
        edits.push_back(add);
    }

    const tempothin::Fitted fitted = tempoMapOf(song);
    result.thinned = fitted.tolerance > 0.0;
    result.tolerance = fitted.tolerance;
    result.tempos = static_cast<int>(fitted.points.size());
    edits.push_back(edit::SetTempoMap{fitted.points});

    if (!session.perform(std::move(edits), "Import MIDI")) {
        result.error = "the project has no room for " + juce::String(static_cast<int>(song.tracks.size())) + " more tracks";
        result.tracks.clear();
        return result;
    }
    result.ok = true;
    return result;
}

bool exportMidiFile(const Session& session, const juce::File& file) {
    const std::vector<std::uint8_t> bytes = midifile::write(songOf(session).song);
    return file.replaceWithData(bytes.data(), bytes.size());
}

MidiImport importMidiFile(Session& session, const juce::File& file) {
    MidiImport result;
    juce::MemoryBlock data;
    if (!file.loadFileAsData(data)) {
        result.error = "cannot read " + file.getFullPathName();
        return result;
    }
    const auto* p = static_cast<const std::uint8_t*>(data.getData());
    const midifile::ReadResult read = midifile::read(std::vector<std::uint8_t>(p, p + data.getSize()));
    if (!read.ok) {
        result.error = juce::String("not a MIDI file DawLab can read: ") + read.error.c_str();
        return result;
    }
    return importSong(session, read.song);
}

} // namespace dawlab::app
