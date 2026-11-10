// Exercise 9 (solution): MIDI files in DawLab 18.
//
// Build and run:  l18_ex9_solution   (ctest, label "solution"; needs -Juce)
// Your version:   exercises/starter/ex9_midi_files.cpp -> l18_ex9_starter

#include "../exercises/ex9_midi_files.h"

#include <deque>
#include <map>

namespace ex9 {

using namespace dawlab;
using namespace dawlab::app;

namespace {

constexpr std::int64_t kBar = 4 * 960;

minidaw::MidiMessage bytesOf(int status, int a, int b) {
    return {static_cast<std::uint8_t>(status), static_cast<std::uint8_t>(a & 0x7F), static_cast<std::uint8_t>(b & 0x7F)};
}

} // namespace

Exported songOf(MainComponent& main) {
    Session& session = main.getSession();
    Exported out;
    out.song.bpm = main.getEngine().getTransport().getTempo();
    for (int t = 0; t < session.getNumTracks(); ++t) {
        const engine::TrackId id = session.getTrackId(t);
        if (!session.isInstrument(id))
            continue;
        ex3::SmfTrack track;
        track.name = session.getStripName(session.trackRef(id)).toStdString();
        for (const MidiClipModel& clip : session.getMidiClips(id)) {
            if (clip.muted)
                continue;
            const std::int64_t from = clip.offset, to = clip.offset + clip.length;
            auto place = [&clip](std::int64_t material) { return clip.start + material - clip.offset; };
            for (const engine::Note& n : clip.notes()) {
                if (n.start < from || n.start >= to)
                    continue;
                const std::int64_t end = std::min(n.start + n.length, to);
                track.events.push_back({place(n.start), bytesOf(0x90 | n.channel, n.pitch, n.velocity)});
                track.events.push_back({place(end), bytesOf(0x80 | n.channel, n.pitch, 0)});
                ++out.notes;
            }
            for (const engine::ControllerPoint& c : clip.controllers)
                if (c.tick >= from && c.tick < to)
                    track.events.push_back({place(c.tick), bytesOf(0xB0 | c.channel, c.number, c.value)});
        }
        if (!track.events.empty())
            out.song.tracks.push_back(std::move(track));
    }
    return out;
}

Imported importSong(MainComponent& main, const ex3::Song& song) {
    Imported result;
    if (song.tracks.empty()) {
        result.error = "the file has no track with notes";
        return result;
    }
    Session& session = main.getSession();
    std::vector<Edit> edits;
    const std::uint32_t firstId = session.makeNewTrack(engine::TrackFormat::Mono).track.id.value;
    for (size_t t = 0; t < song.tracks.size(); ++t) {
        const ex3::SmfTrack& track = song.tracks[t];
        MidiClipModel clip;
        std::map<std::pair<int, int>, std::deque<size_t>> open; // (channel, pitch) -> notes not yet ended, oldest first
        std::int64_t last = 0;
        for (const ex3::TimedEvent& e : track.events) {
            const minidaw::MidiMessage& m = e.message;
            last = std::max(last, e.tick);
            const int status = m.status & 0xF0, channel = m.status & 0x0F;
            const bool on = status == 0x90 && m.data2 > 0, off = status == 0x80 || (status == 0x90 && m.data2 == 0);
            if (on) {
                open[{channel, m.data1}].push_back(clip.played.size());
                clip.played.push_back({e.tick, 0, m.data1, m.data2, channel});
            } else if (off) {
                auto it = open.find({channel, m.data1});
                if (it != open.end() && !it->second.empty()) {
                    engine::Note& n = clip.played[it->second.front()];
                    n.length = std::max<std::int64_t>(e.tick - n.start, 1);
                    it->second.pop_front();
                }
            } else if (status == 0xB0) {
                clip.controllers.push_back({e.tick, m.data1, m.data2, channel});
            } else {
                ++result.dropped;
            }
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
    edits.push_back(edit::SetTempo{song.bpm});
    if (!session.perform(std::move(edits), "Import MIDI")) {
        result.error = "the project has no room for " + juce::String(static_cast<int>(song.tracks.size())) + " more tracks";
        result.tracks.clear();
        return result;
    }
    result.ok = true;
    return result;
}

} // namespace ex9

#include "../exercises/ex9_check.h"
