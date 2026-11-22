#include "ProjectIO.h"

#include "Exporter.h"
#include "MediaLoader.h"
#include "VstInsert.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <string_view>

namespace dawlab::app {

using namespace dawlab::engine;
using project::ClipState;
using project::InsertState;
using project::ProjectState;
using project::StripState;
using project::TrackState;

namespace {

// The media file of a buffer is named after its content (64-bit FNV-1a over the channel count
// and the samples). A buffer that did not change keeps its name and is not written again, and
// new audio never overwrites a file the previous version of the project still points at.
juce::String mediaFileName(const AudioBuffer& b) {
    std::uint64_t h = 1469598103934665603ULL;
    const auto mix = [&h](const void* data, size_t n) {
        const auto* p = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < n; ++i) {
            h ^= p[i];
            h *= 1099511628211ULL;
        }
    };
    const int channels = b.getNumChannels(), samples = b.getNumSamples();
    mix(&channels, sizeof(channels));
    mix(&samples, sizeof(samples));
    for (int ch = 0; ch < channels; ++ch)
        mix(b.getReadPointer(ch), sizeof(float) * static_cast<size_t>(samples));
    char name[32];
    std::snprintf(name, sizeof(name), "%016llx.wav", static_cast<unsigned long long>(h));
    return name;
}

// A plugin insert: its file and its state. No test loads a plugin, so this part is kept this small.
void captureVst3(InsertState& ins, const InsertSlotState& st) {
    ins.vst3 = true;
    host::HostedPlugin& plugin = st.vst->getPlugin();
    ins.type = plugin.getInstance().getPluginDescription().fileOrIdentifier.toStdString();
    ins.state = plugin.getState().toBase64Encoding().toStdString();
}

// The effect in one slot: a plugin, or a built-in effect with each of its knobs.
InsertState captureInsert(const Session& session, const InsertSlotState& st, int slot) {
    InsertState ins;
    ins.slot = slot;
    ins.bypassed = st.effect->isBypassed();
    if (st.kind == SlotKind::Vst3 && st.vst != nullptr) {
        captureVst3(ins, st);
    } else {
        ins.type = effectTypeName(st.type);
        for (int i = 0; i < st.effect->getNumParameters(); ++i)
            ins.params.push_back({st.effect->getParameterInfo(i).name, session.knobValue(*st.effect, i)});
    }
    return ins;
}

// The strip's automation, in a fixed order: saving an opened project gives the same text again.
std::vector<project::LaneState> captureLanes(const Session& session, ChannelRef ref) {
    std::vector<project::LaneState> lanes;
    for (const auto& [address, content] : session.getLanesOf(ref)) {
        project::LaneState lane;
        lane.slot = session.slotOfEffect(ref, address.effect);
        lane.parameter = address.parameter.toStdString();
        lane.mode = content.mode;
        lane.defaultValue = content.curve.defaultValue();
        for (const engine::AutomationPoint& point : content.curve.points())
            lane.points.push_back({point.position, point.value, point.shape == engine::Shape::Hold});
        lanes.push_back(std::move(lane));
    }
    std::sort(lanes.begin(), lanes.end(), [](const project::LaneState& a, const project::LaneState& b) {
        return a.slot != b.slot ? a.slot < b.slot : a.parameter < b.parameter;
    });
    return lanes;
}

StripState captureStrip(const Session& session, ChannelRef ref) {
    StripState s;
    const ChannelStrip* cs = session.getEngine().getStrip(ref);
    if (cs == nullptr)
        return s;
    s.name = cs->getName();
    s.gainDb = cs->getGainDb();
    s.pan = cs->getPan();
    s.mute = cs->isMuted();
    s.solo = ref.kind == ChannelKind::Track && cs->isSoloed();
    for (int b = 0; b < kNumBuses; ++b)
        s.sends[static_cast<size_t>(b)] = {cs->getSendLevelDb(b), cs->isSendPreFader(b)};
    for (int slot = 0; slot < kInsertSlotsPerStrip; ++slot) {
        const InsertSlotState& st = session.getSlot(ref, slot);
        if (st.isEmpty())
            continue;
        s.inserts.push_back(captureInsert(session, st, slot));
    }
    s.lanes = captureLanes(session, ref);
    return s;
}

// Reads an audio file as it is when it is already at `rate` (a project's own 32-bit float WAVs:
// every sample, every channel exactly), otherwise through MediaLoader (resampled to `rate`). Both
// ways go through MediaLoader::readChecked; `nonFinite` counts the NaN or Inf samples it set to 0.
std::shared_ptr<const AudioBuffer> readAudio(const juce::File& file, double rate, juce::String& error, int& nonFinite) {
    nonFinite = 0;
    if (!file.existsAsFile()) {
        error = "not found";
        return nullptr;
    }
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader != nullptr && reader->sampleRate == rate && reader->numChannels >= 1 && reader->numChannels <= 2) {
        auto buffer = std::make_shared<AudioBuffer>();
        if (!MediaLoader::readChecked(*reader, false, *buffer, error, nonFinite))
            return nullptr;
        return buffer;
    }
    const LoadedMedia media = MediaLoader::readAudioFile(file, rate);
    if (media.audio == nullptr)
        error = media.error;
    nonFinite = media.nonFiniteSamples;
    return media.audio;
}

bool writeTextFile(const juce::File& file, const std::string& text, juce::String& error) {
    juce::TemporaryFile tmp(file);
    {
        juce::FileOutputStream out(tmp.getFile());
        if (!out.openedOk() || !out.write(text.data(), text.size())) {
            error = "cannot write " + tmp.getFile().getFullPathName();
            return false;
        }
        out.flush();
        if (out.getStatus().failed()) {
            error = "write error: " + out.getStatus().getErrorMessage();
            return false;
        }
    }
    if (!tmp.overwriteTargetFileWithTemporary()) {
        error = "cannot replace " + file.getFullPathName();
        return false;
    }
    return true;
}

} // namespace

juce::File ProjectIO::mediaFolderFor(const juce::File& projectFile) {
    return projectFile.getSiblingFile(projectFile.getFileNameWithoutExtension() + "_media");
}

// ---- save ---------------------------------------------------------------------------------------------

namespace {

// Each buffer's file in the media folder, by the buffer.
using MediaNames = std::map<const AudioBuffer*, juce::String>;

// The song: the rate the clips are at, the tempo and the time signature, the loop.
void captureSong(const Session& session, ProjectState& p) {
    const Transport& tr = session.getEngine().getTransport();
    p.sampleRate = session.getClipSampleRate(); // the rate the clips are at (= the engine's, normally)
    p.tempo = tr.getTempo();
    p.beatsPerBar = tr.getTimeSignature().numerator;
    p.beatUnit = tr.getTimeSignature().denominator;
    p.loopStart = tr.getLoopStart();
    p.loopEnd = tr.getLoopEnd();
    p.loopEnabled = tr.isLoopEnabled();
}

// The track in display position `lane`, with the file's own id for it: lane + 1.
TrackState captureTrack(const Session& session, int lane) {
    const TrackId id = session.getTrackId(lane);
    TrackState t;
    t.id = TrackId{static_cast<std::uint32_t>(lane + 1)}; // the file's own references, 1..N
    t.format = session.getTrackFormat(id);
    t.strip = captureStrip(session, session.trackRef(id));
    return t;
}

// The file of a clip's audio, in the media folder. The first clip that plays a buffer names it and adds it to
// `media`, once even when two buffers hold the same samples.
juce::String mediaNameOf(const Clip& c, const juce::String& mediaFolderName, MediaNames& names, ProjectIO::Captured& out) {
    auto it = names.find(c.source.get());
    if (it == names.end()) {
        const juce::String name = mediaFolderName + "/" + mediaFileName(*c.source);
        it = names.emplace(c.source.get(), name).first;
        if (std::none_of(out.media.begin(), out.media.end(), [&name](const auto& m) { return m.first == name; }))
            out.media.emplace_back(name, c.source);
    }
    return it->second;
}

// An audio clip on `track` that plays `file`.
ClipState captureClip(const Clip& c, TrackId track, const juce::String& file) {
    ClipState cs;
    cs.track = track;
    cs.start = c.start;
    cs.length = c.length;
    cs.file = file.toStdString();
    cs.sourceOffset = c.sourceOffset;
    cs.fadeIn = c.fadeInSamples;
    cs.fadeOut = c.fadeOutSamples;
    cs.equalPowerFades = c.fadeShape == FadeShape::EqualPower;
    cs.gainDb = c.gainDb;
    cs.muted = c.muted;
    cs.name = c.name;
    return cs;
}

} // namespace

ProjectIO::Captured ProjectIO::capture(const Session& session, const juce::String& mediaFolderName) {
    Captured out;
    ProjectState& p = out.state;
    captureSong(session, p);

    MediaNames names;
    for (int lane = 0; lane < session.getNumTracks(); ++lane) {
        const TrackId id = session.getTrackId(lane);
        p.tracks.push_back(captureTrack(session, lane));
        for (const Clip& c : session.getClips(id)) {
            if (c.source == nullptr)
                continue;
            p.clips.push_back(captureClip(c, p.tracks.back().id, mediaNameOf(c, mediaFolderName, names, out)));
        }
    }
    p.nextTrackId = static_cast<std::uint32_t>(p.tracks.size() + 1);
    for (int b = 0; b < kNumBuses; ++b)
        p.buses[static_cast<size_t>(b)] = captureStrip(session, ChannelRef::bus(b));
    p.master = captureStrip(session, ChannelRef::master());
    return out;
}

namespace {

bool makeMediaFolder(const juce::File& mediaDir, juce::String& error) {
    const juce::Result made = mediaDir.createDirectory();
    if (made.failed()) {
        error = "cannot create " + mediaDir.getFullPathName() + ": " + made.getErrorMessage();
        return false;
    }
    return true;
}

// Each buffer whose file is not there yet, written next to it and moved in when complete. `used` gets the name of
// every file the new version plays.
bool writeMedia(const ProjectIO::Captured& c, const juce::File& projectFolder, std::set<juce::String>& used, juce::String& error) {
    for (const auto& [name, buffer] : c.media) {
        const juce::File target = projectFolder.getChildFile(name);
        used.insert(target.getFileName());
        if (target.existsAsFile())
            continue;
        juce::TemporaryFile tmp(target);
        juce::String why;
        if (!Exporter::writeWavFile(tmp.getFile(), *buffer, c.state.sampleRate, 32, why) ||
            !tmp.overwriteTargetFileWithTemporary()) {
            error = "cannot write " + target.getFullPathName() + (why.isNotEmpty() ? ": " + why : juce::String());
            return false;
        }
    }
    return true;
}

// The files of the media folder that are not `used`, and the folder itself once a project without audio left it empty.
void removeUnusedMedia(const juce::File& mediaDir, const std::set<juce::String>& used, bool noMedia) {
    if (!mediaDir.isDirectory())
        return;
    for (const juce::File& f : mediaDir.findChildFiles(juce::File::findFiles, false, "*.wav"))
        if (used.count(f.getFileName()) == 0)
            f.deleteFile();
    if (noMedia && mediaDir.getNumberOfChildFiles(juce::File::findFilesAndDirectories) == 0)
        mediaDir.deleteFile();
}

} // namespace

bool ProjectIO::save(const Session& session, const juce::File& file, juce::String& error) {
    const juce::File mediaDir = mediaFolderFor(file);
    const Captured c = capture(session, mediaDir.getFileName());
    if (!c.media.empty() && !makeMediaFolder(mediaDir, error))
        return false;
    // 1. The audio. A file with this content's name is already there: nothing to write.
    std::set<juce::String> used;
    if (!writeMedia(c, file.getParentDirectory(), used, error))
        return false;
    // 2. The text: from here on the new version is complete.
    if (!writeTextFile(file, project::writeProject(c.state), error))
        return false;
    // 3. Audio the new version does not use any more.
    removeUnusedMedia(mediaDir, used, c.media.empty());
    return true;
}

// ---- open ---------------------------------------------------------------------------------------------

namespace {

// The end of every restored insert: the engine refused it (a warning), or it gets its bypass.
void finishInsert(Session& session, ChannelRef ref, const InsertState& ins, InsertEffect* fx, const juce::String& where,
                  juce::StringArray& warnings) {
    if (fx == nullptr)
        warnings.add(where + ": slot " + juce::String(ins.slot + 1) + " refused by the engine");
    else
        session.setInsertBypassed(ref, ins.slot, ins.bypassed);
}

// A VST3 insert: loaded from its file, then given its saved state. Skipped (with a warning) without a
// plugin host or when the file does not load.
void restorePluginInsert(Session& session, host::PluginHost* pluginHost, ChannelRef ref, const InsertState& ins,
                         const juce::String& where, juce::StringArray& warnings) {
    const Engine& engine = session.getEngine();
    const juce::File pluginFile(juce::String::fromUTF8(ins.type.c_str()));
    if (pluginHost == nullptr) {
        warnings.add(where + ": plugin " + pluginFile.getFileName() + " skipped (no plugin host)");
        return;
    }
    juce::String why;
    auto instance = pluginHost->loadFromFile(pluginFile, engine.getSampleRate(), engine.getMaxBlockSize(), why);
    if (instance == nullptr) {
        warnings.add(where + ": plugin " + pluginFile.getFileName() + " not loaded (" + why + ")");
        return;
    }
    auto hosted = std::make_unique<host::HostedPlugin>(std::move(instance));
    juce::MemoryBlock state;
    if (!ins.state.empty() && state.fromBase64Encoding(juce::String(ins.state)))
        hosted->setState(state);
    finishInsert(session, ref, ins, session.setVstInsert(ref, ins.slot, std::make_unique<VstInsert>(std::move(hosted))), where,
                 warnings);
}

// A built-in insert and its parameters. Skipped (with a warning) when the effect is unknown; a parameter
// it does not have is a warning too.
void restoreBuiltinInsert(Session& session, ChannelRef ref, const InsertState& ins, const juce::String& where,
                          juce::StringArray& warnings) {
    EffectType type;
    if (!effectTypeFromName(ins.type.c_str(), type)) {
        warnings.add(where + ": unknown effect \"" + juce::String(ins.type) + "\" skipped");
        return;
    }
    InsertEffect* fx = session.setBuiltinInsert(ref, ins.slot, type);
    if (fx != nullptr)
        for (const project::ParamState& prm : ins.params)
            if (!fx->setParameterByName(prm.name.c_str(), prm.value))
                warnings.add(where + ": " + fx->getName() + " has no parameter \"" + juce::String(prm.name) + "\"");
    finishInsert(session, ref, ins, fx, where, warnings);
}

void restoreInserts(Session& session, host::PluginHost* pluginHost, ChannelRef ref, const StripState& s,
                    juce::StringArray& warnings) {
    const juce::String where = session.getStripName(ref);
    for (const InsertState& ins : s.inserts) {
        if (ins.vst3)
            restorePluginInsert(session, pluginHost, ref, ins, where, warnings);
        else
            restoreBuiltinInsert(session, ref, ins, where, warnings);
    }
}

void restoreLanes(Session& session, ChannelRef ref, const StripState& s, juce::StringArray& warnings) {
    const juce::String where = session.getStripName(ref);
    for (const project::LaneState& lane : s.lanes) {
        const juce::String parameter = juce::String::fromUTF8(lane.parameter.c_str());
        LaneAddress address{StripTarget::of(ref), engine::EffectId{}, parameter};
        if (lane.slot >= 0) {
            const InsertSlotState& slot = session.getSlot(ref, lane.slot);
            if (slot.isEmpty()) { // its plugin did not load: that was already a warning, this is what it costs
                warnings.add(where + ": automation of \"" + parameter + "\" left out, its effect is missing");
                continue;
            }
            address.effect = slot.effect->getEffectId();
        }
        LaneContent content;
        content.mode = lane.mode;
        content.curve = engine::AutomationCurve(lane.defaultValue);
        for (const project::PointState& point : lane.points)
            content.curve.add({point.position, point.value, point.hold ? engine::Shape::Hold : engine::Shape::Linear});
        session.setLane(address, std::move(content));
        if (!session.isLaneLive(address))
            warnings.add(where + ": no parameter \"" + parameter + "\", so its automation stays silent (it is kept and saved again)");
    }
}

void restoreStrip(Session& session, ChannelRef ref, const StripState& s) {
    ChannelStrip* cs = session.getEngine().getStrip(ref);
    if (cs == nullptr)
        return;
    if (!s.name.empty())
        session.setStripName(ref, juce::String::fromUTF8(s.name.c_str()));
    cs->setGainDb(s.gainDb);
    cs->setPan(s.pan);
    cs->setMute(s.mute);
    if (ref.kind == ChannelKind::Track)
        cs->setSolo(s.solo);
    for (int b = 0; b < kNumBuses; ++b) {
        cs->setSendLevelDb(b, s.sends[static_cast<size_t>(b)].levelDb);
        cs->setSendPreFader(b, s.sends[static_cast<size_t>(b)].preFader);
    }
}

// A clip's file -> its audio (nullptr: it could not be read).
using AudioFiles = std::map<std::string, std::shared_ptr<const AudioBuffer>>;
// The file's track id -> the session's.
using TrackIds = std::map<std::uint32_t, TrackId>;

// The file's text, parsed. If the file cannot be read or does not parse, `error` says why.
project::ReadResult readProjectFile(const juce::File& file, juce::String& error) {
    juce::MemoryBlock data;
    if (!file.existsAsFile() || !file.loadFileAsData(data)) {
        error = "cannot read " + file.getFullPathName();
        return {};
    }
    project::ReadResult parsed =
        project::readProject(std::string_view(static_cast<const char*>(data.getData()), data.getSize()));
    if (!parsed.ok)
        error = file.getFileName() + ", " + juce::String::fromUTF8(parsed.error.c_str());
    return parsed;
}

// Every audio file the clips name, read before the session changes. One that cannot be read is a warning (its clips
// are left out); NaN or Inf samples that were set to 0 are a note.
AudioFiles readAllAudio(const ProjectState& p, const juce::File& projectFile, ProjectIO::OpenResult& r) {
    AudioFiles audio;
    for (const ClipState& c : p.clips) {
        if (audio.count(c.file) != 0)
            continue;
        const juce::String path = juce::String::fromUTF8(c.file.c_str());
        const juce::File f =
            juce::File::isAbsolutePath(path) ? juce::File(path) : projectFile.getParentDirectory().getChildFile(path);
        juce::String why;
        int nonFinite = 0;
        audio[c.file] = readAudio(f, p.sampleRate, why, nonFinite);
        if (audio[c.file] == nullptr) {
            r.warnings.add("audio file " + path + ": " + why + " (its clips are left out)");
        } else {
            ++r.audioFiles;
            if (nonFinite > 0) // a repair, nothing left out: a note, not a warning
                r.notes.add("audio file " + path + ": " + MediaLoader::describeNonFinite(nonFinite));
        }
    }
    return audio;
}

// The tempo, the time signature and the loop.
void restoreTransport(Session& session, const ProjectState& p) {
    Transport& tr = session.getEngine().getTransport();
    tr.setTempo(p.tempo);
    tr.setTimeSignature(p.beatsPerBar, p.beatUnit);
    if (p.loopEnd > p.loopStart)
        tr.setLoopRange(p.loopStart, p.loopEnd);
    tr.setLoopEnabled(p.loopEnabled);
}

// One channel: its strip, its effects and its automation.
void restoreChannel(Session& session, host::PluginHost* pluginHost, ChannelRef ref, const StripState& s,
                    juce::StringArray& warnings) {
    restoreStrip(session, ref, s);
    restoreInserts(session, pluginHost, ref, s, warnings);
    restoreLanes(session, ref, s, warnings);
}

// The buses, then the master.
void restoreBuses(Session& session, host::PluginHost* pluginHost, const ProjectState& p, juce::StringArray& warnings) {
    for (int b = 0; b <= kNumBuses; ++b) {
        const ChannelRef ref = b < kNumBuses ? ChannelRef::bus(b) : ChannelRef::master();
        const StripState& s = b < kNumBuses ? p.buses[static_cast<size_t>(b)] : p.master;
        restoreChannel(session, pluginHost, ref, s, warnings);
    }
}

// The tracks in the file's order. One more than the session holds is left out, with a warning.
TrackIds restoreTracks(Session& session, host::PluginHost* pluginHost, const ProjectState& p,
                       juce::StringArray& warnings) {
    TrackIds ids;
    for (const TrackState& t : p.tracks) {
        const TrackId id = session.addTrack(t.format, juce::String::fromUTF8(t.strip.name.c_str()));
        if (!id.isValid()) {
            warnings.add("track \"" + juce::String::fromUTF8(t.strip.name.c_str()) + "\" left out: " +
                         juce::String(kMaxTracks) + " tracks is the limit");
            continue;
        }
        ids[t.id.value] = id;
        restoreChannel(session, pluginHost, session.trackRef(id), t.strip, warnings);
    }
    return ids;
}

// The clip a ClipState is, on its audio. The text cannot know the audio: a clip may not read past the end of its
// source. One that starts after the end is left out (false), a longer one is shortened, each with a warning.
bool clipOf(const ClipState& cs, const std::shared_ptr<const AudioBuffer>& source, Clip& c, juce::StringArray& warnings) {
    c.source = source;
    c.start = cs.start;
    c.length = cs.length;
    c.sourceOffset = cs.sourceOffset;
    c.gainDb = cs.gainDb;
    c.fadeInSamples = cs.fadeIn;
    c.fadeOutSamples = cs.fadeOut;
    c.fadeShape = cs.equalPowerFades ? FadeShape::EqualPower : FadeShape::Linear;
    c.muted = cs.muted;
    c.name = cs.name;
    const std::int64_t available = source->getNumSamples() - c.sourceOffset;
    if (available <= 0) {
        warnings.add("clip \"" + juce::String(cs.name) + "\" starts after the end of " + juce::String(cs.file) + ": left out");
        return false;
    }
    if (c.length > available) {
        warnings.add("clip \"" + juce::String(cs.name) + "\" was longer than " + juce::String(cs.file) + ": shortened");
        c.length = available;
    }
    return true;
}

// The audio clips of the tracks that are in the session, on the audio that could be read.
void restoreClips(Session& session, const ProjectState& p, const AudioFiles& audio, const TrackIds& ids,
                  juce::StringArray& warnings) {
    std::map<std::uint32_t, std::vector<Clip>> clips;
    for (const ClipState& cs : p.clips) {
        const std::shared_ptr<const AudioBuffer>& source = audio.at(cs.file);
        Clip c;
        if (source == nullptr || ids.count(cs.track.value) == 0 || !clipOf(cs, source, c, warnings))
            continue;
        clips[cs.track.value].push_back(std::move(c));
    }
    for (auto& [fileId, list] : clips)
        session.setClips(ids.at(fileId), std::move(list));
}

} // namespace

ProjectIO::OpenResult ProjectIO::open(Session& session, host::PluginHost* pluginHost, const juce::File& file) {
    OpenResult r;
    const project::ReadResult parsed = readProjectFile(file, r.error);
    if (!parsed.ok)
        return r;
    const ProjectState& p = parsed.project;
    r.formatVersion = parsed.formatVersion;

    // Every audio file is read before the session changes (a missing one is a warning).
    const AudioFiles audio = readAllAudio(p, file, r);

    // From here on the old project is replaced.
    session.clearProject();
    restoreTransport(session, p);
    restoreBuses(session, pluginHost, p, r.warnings);
    const TrackIds ids = restoreTracks(session, pluginHost, p, r.warnings);
    restoreClips(session, p, audio, ids, r.warnings);

    // Positions and audio are at the file's rate; the engine may run at another one.
    if (p.sampleRate != session.getEngine().getSampleRate())
        session.handleSampleRateChange(p.sampleRate, session.getEngine().getSampleRate());
    session.setProjectName(file.getFileNameWithoutExtension());
    r.ok = true;
    return r;
}

} // namespace dawlab::app
