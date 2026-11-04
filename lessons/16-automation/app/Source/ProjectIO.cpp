#include "ProjectIO.h"

#include "Exporter.h"
#include "MediaLoader.h"
#include "VstInsert.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
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
        InsertState ins;
        ins.slot = slot;
        ins.bypassed = st.effect->isBypassed();
        if (st.kind == SlotKind::Vst3 && st.vst != nullptr) {
            ins.vst3 = true;
            host::HostedPlugin& plugin = st.vst->getPlugin();
            ins.type = plugin.getInstance().getPluginDescription().fileOrIdentifier.toStdString();
            ins.state = plugin.getState().toBase64Encoding().toStdString();
        } else {
            ins.type = effectTypeName(st.type);
            for (int i = 0; i < st.effect->getNumParameters(); ++i)
                ins.params.push_back({st.effect->getParameterInfo(i).name, session.knobValue(*st.effect, i)});
        }
        s.inserts.push_back(std::move(ins));
    }
    // Automation: in a fixed order, so that saving an opened project gives the same text again.
    for (const auto& [address, content] : session.getLanesOf(ref)) {
        project::LaneState lane;
        lane.slot = session.slotOfEffect(ref, address.effect);
        lane.parameter = address.parameter.toStdString();
        lane.mode = content.mode;
        lane.defaultValue = content.curve.defaultValue();
        for (const engine::AutomationPoint& point : content.curve.points())
            lane.points.push_back({point.position, point.value, point.shape == engine::Shape::Hold});
        s.lanes.push_back(std::move(lane));
    }
    std::sort(s.lanes.begin(), s.lanes.end(), [](const project::LaneState& a, const project::LaneState& b) {
        return a.slot != b.slot ? a.slot < b.slot : a.parameter < b.parameter;
    });
    return s;
}

// Reads an audio file as it is when it is already at `rate` (a project's own 32-bit float WAVs:
// every sample, every channel exactly), otherwise through MediaLoader (resampled to `rate`).
std::shared_ptr<const AudioBuffer> readAudio(const juce::File& file, double rate, juce::String& error) {
    if (!file.existsAsFile()) {
        error = "not found";
        return nullptr;
    }
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader != nullptr && reader->sampleRate == rate && reader->numChannels >= 1 && reader->numChannels <= 2 &&
        reader->lengthInSamples > 0 && reader->lengthInSamples < (1LL << 31)) {
        const int channels = static_cast<int>(reader->numChannels);
        const int n = static_cast<int>(reader->lengthInSamples);
        juce::AudioBuffer<float> raw(channels, n);
        if (!reader->read(&raw, 0, n, 0, true, true)) {
            error = "read error";
            return nullptr;
        }
        auto buffer = std::make_shared<AudioBuffer>(channels, n);
        for (int ch = 0; ch < channels; ++ch)
            std::memcpy(buffer->getWritePointer(ch), raw.getReadPointer(ch), sizeof(float) * static_cast<size_t>(n));
        return buffer;
    }
    const LoadedMedia media = MediaLoader::readAudioFile(file, rate);
    if (media.audio == nullptr)
        error = media.error;
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

ProjectIO::Captured ProjectIO::capture(const Session& session, const juce::String& mediaFolderName) {
    Captured out;
    ProjectState& p = out.state;
    const Transport& tr = session.getEngine().getTransport();
    p.sampleRate = session.getClipSampleRate(); // the rate the clips are at (= the engine's, normally)
    p.tempo = tr.getTempo();
    p.beatsPerBar = tr.getTimeSignature().numerator;
    p.beatUnit = tr.getTimeSignature().denominator;
    p.loopStart = tr.getLoopStart();
    p.loopEnd = tr.getLoopEnd();
    p.loopEnabled = tr.isLoopEnabled();

    std::map<const AudioBuffer*, juce::String> names;
    for (int lane = 0; lane < session.getNumTracks(); ++lane) {
        const TrackId id = session.getTrackId(lane);
        TrackState t;
        t.id = TrackId{static_cast<std::uint32_t>(lane + 1)}; // the file's own references, 1..N
        t.format = session.getTrackFormat(id);
        t.strip = captureStrip(session, session.trackRef(id));
        p.tracks.push_back(std::move(t));
        for (const Clip& c : session.getClips(id)) {
            if (c.source == nullptr)
                continue;
            auto it = names.find(c.source.get());
            if (it == names.end()) {
                const juce::String name = mediaFolderName + "/" + mediaFileName(*c.source);
                it = names.emplace(c.source.get(), name).first;
                if (std::none_of(out.media.begin(), out.media.end(), [&name](const auto& m) { return m.first == name; }))
                    out.media.emplace_back(name, c.source);
            }
            ClipState cs;
            cs.track = p.tracks.back().id;
            cs.start = c.start;
            cs.length = c.length;
            cs.file = it->second.toStdString();
            cs.sourceOffset = c.sourceOffset;
            cs.fadeIn = c.fadeInSamples;
            cs.fadeOut = c.fadeOutSamples;
            cs.equalPowerFades = c.fadeShape == FadeShape::EqualPower;
            cs.gainDb = c.gainDb;
            cs.muted = c.muted;
            cs.name = c.name;
            p.clips.push_back(std::move(cs));
        }
    }
    p.nextTrackId = static_cast<std::uint32_t>(p.tracks.size() + 1);
    for (int b = 0; b < kNumBuses; ++b)
        p.buses[static_cast<size_t>(b)] = captureStrip(session, ChannelRef::bus(b));
    p.master = captureStrip(session, ChannelRef::master());
    return out;
}

bool ProjectIO::save(const Session& session, const juce::File& file, juce::String& error) {
    const juce::File mediaDir = mediaFolderFor(file);
    const Captured c = capture(session, mediaDir.getFileName());
    if (!c.media.empty()) {
        const juce::Result made = mediaDir.createDirectory();
        if (made.failed()) {
            error = "cannot create " + mediaDir.getFullPathName() + ": " + made.getErrorMessage();
            return false;
        }
    }
    // 1. The audio. A file with this content's name is already there: nothing to write.
    std::set<juce::String> used;
    for (const auto& [name, buffer] : c.media) {
        const juce::File target = file.getParentDirectory().getChildFile(name);
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
    // 2. The text: from here on the new version is complete.
    if (!writeTextFile(file, project::writeProject(c.state), error))
        return false;
    // 3. Audio the new version does not use any more.
    if (mediaDir.isDirectory()) {
        for (const juce::File& f : mediaDir.findChildFiles(juce::File::findFiles, false, "*.wav"))
            if (used.count(f.getFileName()) == 0)
                f.deleteFile();
        if (c.media.empty() && mediaDir.getNumberOfChildFiles(juce::File::findFilesAndDirectories) == 0)
            mediaDir.deleteFile();
    }
    return true;
}

// ---- open ---------------------------------------------------------------------------------------------

namespace {

void restoreInserts(Session& session, host::PluginHost* pluginHost, ChannelRef ref, const StripState& s,
                    juce::StringArray& warnings) {
    const Engine& engine = session.getEngine();
    const juce::String where = session.getStripName(ref);
    for (const InsertState& ins : s.inserts) {
        InsertEffect* fx = nullptr;
        if (ins.vst3) {
            const juce::File pluginFile(juce::String::fromUTF8(ins.type.c_str()));
            if (pluginHost == nullptr) {
                warnings.add(where + ": plugin " + pluginFile.getFileName() + " skipped (no plugin host)");
                continue;
            }
            juce::String why;
            auto instance = pluginHost->loadFromFile(pluginFile, engine.getSampleRate(), engine.getMaxBlockSize(), why);
            if (instance == nullptr) {
                warnings.add(where + ": plugin " + pluginFile.getFileName() + " not loaded (" + why + ")");
                continue;
            }
            auto hosted = std::make_unique<host::HostedPlugin>(std::move(instance));
            juce::MemoryBlock state;
            if (!ins.state.empty() && state.fromBase64Encoding(juce::String(ins.state)))
                hosted->setState(state);
            fx = session.setVstInsert(ref, ins.slot, std::make_unique<VstInsert>(std::move(hosted)));
        } else {
            EffectType type;
            if (!effectTypeFromName(ins.type.c_str(), type)) {
                warnings.add(where + ": unknown effect \"" + juce::String(ins.type) + "\" skipped");
                continue;
            }
            fx = session.setBuiltinInsert(ref, ins.slot, type);
            if (fx != nullptr)
                for (const project::ParamState& prm : ins.params)
                    if (!fx->setParameterByName(prm.name.c_str(), prm.value))
                        warnings.add(where + ": " + fx->getName() + " has no parameter \"" + juce::String(prm.name) + "\"");
        }
        if (fx == nullptr) {
            warnings.add(where + ": slot " + juce::String(ins.slot + 1) + " refused by the engine");
            continue;
        }
        session.setInsertBypassed(ref, ins.slot, ins.bypassed);
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

} // namespace

ProjectIO::OpenResult ProjectIO::open(Session& session, host::PluginHost* pluginHost, const juce::File& file) {
    OpenResult r;
    juce::MemoryBlock data;
    if (!file.existsAsFile() || !file.loadFileAsData(data)) {
        r.error = "cannot read " + file.getFullPathName();
        return r;
    }
    const project::ReadResult parsed =
        project::readProject(std::string_view(static_cast<const char*>(data.getData()), data.getSize()));
    if (!parsed.ok) {
        r.error = file.getFileName() + ", " + juce::String::fromUTF8(parsed.error.c_str());
        return r;
    }
    const ProjectState& p = parsed.project;
    r.formatVersion = parsed.formatVersion;

    // Every audio file is read before the session changes (a missing one is a warning).
    std::map<std::string, std::shared_ptr<const AudioBuffer>> audio;
    for (const ClipState& c : p.clips) {
        if (audio.count(c.file) != 0)
            continue;
        const juce::String path = juce::String::fromUTF8(c.file.c_str());
        const juce::File f = juce::File::isAbsolutePath(path) ? juce::File(path) : file.getParentDirectory().getChildFile(path);
        juce::String why;
        audio[c.file] = readAudio(f, p.sampleRate, why);
        if (audio[c.file] == nullptr)
            r.warnings.add("audio file " + path + ": " + why + " (its clips are left out)");
        else
            ++r.audioFiles;
    }

    // From here on the old project is replaced.
    session.clearProject();
    Transport& tr = session.getEngine().getTransport();
    tr.setTempo(p.tempo);
    tr.setTimeSignature(p.beatsPerBar, p.beatUnit);
    if (p.loopEnd > p.loopStart)
        tr.setLoopRange(p.loopStart, p.loopEnd);
    tr.setLoopEnabled(p.loopEnabled);

    for (int b = 0; b <= kNumBuses; ++b) {
        const ChannelRef ref = b < kNumBuses ? ChannelRef::bus(b) : ChannelRef::master();
        const StripState& s = b < kNumBuses ? p.buses[static_cast<size_t>(b)] : p.master;
        restoreStrip(session, ref, s);
        restoreInserts(session, pluginHost, ref, s, r.warnings);
        restoreLanes(session, ref, s, r.warnings);
    }

    std::map<std::uint32_t, TrackId> ids; // the file's id -> the session's
    for (const TrackState& t : p.tracks) {
        const TrackId id = session.addTrack(t.format, juce::String::fromUTF8(t.strip.name.c_str()));
        if (!id.isValid()) {
            r.warnings.add("track \"" + juce::String::fromUTF8(t.strip.name.c_str()) + "\" left out: " +
                           juce::String(kMaxTracks) + " tracks is the limit");
            continue;
        }
        ids[t.id.value] = id;
        restoreStrip(session, session.trackRef(id), t.strip);
        restoreInserts(session, pluginHost, session.trackRef(id), t.strip, r.warnings);
        restoreLanes(session, session.trackRef(id), t.strip, r.warnings);
    }

    std::map<std::uint32_t, std::vector<Clip>> clips;
    for (const ClipState& cs : p.clips) {
        const auto source = audio[cs.file];
        if (source == nullptr || ids.count(cs.track.value) == 0)
            continue;
        Clip c;
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
        // The text cannot know the audio: a clip may not read past the end of its source.
        const std::int64_t available = source->getNumSamples() - c.sourceOffset;
        if (available <= 0) {
            r.warnings.add("clip \"" + juce::String(cs.name) + "\" starts after the end of " + juce::String(cs.file) + ": left out");
            continue;
        }
        if (c.length > available) {
            r.warnings.add("clip \"" + juce::String(cs.name) + "\" was longer than " + juce::String(cs.file) + ": shortened");
            c.length = available;
        }
        clips[cs.track.value].push_back(std::move(c));
    }
    for (auto& [fileId, list] : clips)
        session.setClips(ids[fileId], std::move(list));

    // Positions and audio are at the file's rate; the engine may run at another one.
    if (p.sampleRate != session.getEngine().getSampleRate())
        session.handleSampleRateChange(p.sampleRate, session.getEngine().getSampleRate());
    session.setProjectName(file.getFileNameWithoutExtension());
    r.ok = true;
    return r;
}

} // namespace dawlab::app
