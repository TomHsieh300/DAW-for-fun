#include "Session.h"

#include "MediaLoader.h"
#include "VstInsert.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace dawlab::app {

using namespace dawlab::engine;

int stripIndexOf(ChannelRef ref) noexcept {
    switch (ref.kind) {
    case ChannelKind::Track: return std::clamp(ref.index, 0, kNumTracks - 1);
    case ChannelKind::Bus: return kNumTracks + std::clamp(ref.index, 0, kNumBuses - 1);
    case ChannelKind::Master: return kNumTracks + kNumBuses;
    }
    return 0;
}

ChannelRef stripRefAt(int stripIndex) noexcept {
    if (stripIndex < kNumTracks)
        return ChannelRef::track(std::max(0, stripIndex));
    if (stripIndex < kNumTracks + kNumBuses)
        return ChannelRef::bus(stripIndex - kNumTracks);
    return ChannelRef::master();
}

namespace {
const InsertSlotState kEmptySlot{};
const std::vector<Clip> kNoClips{};

bool validTrack(int t) noexcept { return t >= 0 && t < kNumTracks; }
bool validSlot(int s) noexcept { return s >= 0 && s < kInsertSlotsPerStrip; }

juce::String defaultStripName(ChannelRef ref) {
    switch (ref.kind) {
    case ChannelKind::Track: return "Track " + juce::String(ref.index + 1);
    case ChannelKind::Bus: return "Bus " + juce::String::charToString(static_cast<juce::juce_wchar>('A' + ref.index));
    case ChannelKind::Master: return "Master";
    }
    return {};
}
} // namespace

Session::Session(Engine& engine) : engine_(engine) {}

Session::~Session() {
    // The engine owns the effects and outlives us; nothing to free here. Editor windows were
    // closed by their owner (MainComponent) before the engine goes away.
    onInsertWillBeRemoved = nullptr;
}

Session::StripModel& Session::strip(ChannelRef ref) { return strips_[static_cast<size_t>(stripIndexOf(ref))]; }
const Session::StripModel& Session::strip(ChannelRef ref) const {
    return strips_[static_cast<size_t>(stripIndexOf(ref))];
}

// ---- project ---------------------------------------------------------------------------------

void Session::removeAllInserts() {
    for (int s = 0; s < kNumStrips; ++s) {
        const ChannelRef ref = stripRefAt(s);
        for (int slot = kInsertSlotsPerStrip - 1; slot >= 0; --slot)
            if (!getSlot(ref, slot).isEmpty())
                clearInsert(ref, slot);
    }
}

void Session::resetMixerSettings() {
    Mixer& mix = engine_.getMixer();
    for (ChannelRef ref : Engine::allChannels()) {
        ChannelStrip* s = mix.getStrip(ref);
        if (s == nullptr)
            continue;
        s->setName(defaultStripName(ref).toStdString());
        if (ref.kind == ChannelKind::Track)
            mix.getTrack(ref.index).setFormat(TrackFormat::Mono); // until a loaded file says otherwise
        s->setGainDb(0.0f);
        s->setPan(0.0f);
        s->setMute(false);
        s->setSolo(false);
        for (int b = 0; b < kNumBuses; ++b) {
            s->setSendLevelDb(b, kMinusInfinityDb);
            s->setSendPreFader(b, false);
        }
    }
}

void Session::newProject() {
    removeAllInserts();
    clipSampleRate_ = engine_.getSampleRate(); // no clips: nothing to convert
    for (int t = 0; t < kNumTracks; ++t) {
        clips_[static_cast<size_t>(t)].clear();
        pushClips(t);
    }
    resetMixerSettings();
    Transport& tr = engine_.getTransport();
    tr.stop();
    tr.setTempo(120.0);
    tr.setTimeSignature(4, 4);
    tr.setLoopRangeBars(1, 4);
    tr.setLoopEnabled(false);
    tr.setPosition(0);
    // A safety limiter on the master, so that a new project can never clip the converter.
    if (InsertEffect* lim = setBuiltinInsert(ChannelRef::master(), kInsertSlotsPerStrip - 1, EffectType::Limiter))
        lim->setParameterByName("Ceiling", -1.0f);
    engine_.requestReset();
    projectName_ = "Untitled";
    sendChangeMessage();
}

int Session::applyDemoSong(const DemoSong& song) {
    removeAllInserts();
    clipSampleRate_ = song.sampleRate; // every clip is replaced by one at the song's rate
    resetMixerSettings();

    Transport& tr = engine_.getTransport();
    tr.stop();
    tr.setTempo(song.bpm);
    tr.setTimeSignature(song.beatsPerBar, 4);
    tr.setLoopRangeBars(1, song.bars);
    tr.setLoopEnabled(false);
    tr.setPosition(0);

    Mixer& mix = engine_.getMixer();
    for (int t = 0; t < kNumTracks; ++t) {
        mix.getTrack(t).setName(song.trackNames[static_cast<size_t>(t)]);
        std::vector<Clip> list = song.clips[static_cast<size_t>(t)];
        std::stable_sort(list.begin(), list.end(), [](const Clip& a, const Clip& b) { return a.start < b.start; });
        clips_[static_cast<size_t>(t)] = std::move(list);
        pushClips(t);
    }
    for (int b = 0; b < kNumBuses; ++b)
        mix.getBus(b).setName(song.busNames[static_cast<size_t>(b)]);

    // Inserts go into the next free GUI slot of their strip, in the song's order.
    int unknown = 0;
    for (const DemoInsert& d : song.inserts) {
        int slot = 0;
        while (slot < kInsertSlotsPerStrip && !getSlot(d.channel, slot).isEmpty())
            ++slot;
        if (slot == kInsertSlotsPerStrip) {
            ++unknown; // more inserts than GUI slots: count it as "not applied"
            continue;
        }
        InsertEffect* fx = setBuiltinInsert(d.channel, slot, d.type);
        if (fx == nullptr) {
            ++unknown;
            continue;
        }
        for (const auto& p : d.parameters)
            if (!fx->setParameterByName(p.first, p.second))
                ++unknown;
    }

    for (int t = 0; t < kNumTracks; ++t) {
        Track& s = mix.getTrack(t);
        s.setFormat(song.trackFormat[static_cast<size_t>(t)]);
        s.setGainDb(song.trackGainDb[static_cast<size_t>(t)]);
        s.setPan(song.trackPan[static_cast<size_t>(t)]);
        for (int b = 0; b < kNumBuses; ++b)
            s.setSendLevelDb(b, song.sendDb[static_cast<size_t>(t)][static_cast<size_t>(b)]);
    }
    for (int b = 0; b < kNumBuses; ++b)
        mix.getBus(b).setGainDb(song.busGainDb[static_cast<size_t>(b)]);

    engine_.requestReset(); // fresh meters, no tails of the previous project
    projectName_ = "Demo song";
    sendChangeMessage();
    return unknown;
}

// ---- clips ---------------------------------------------------------------------------------------

const std::vector<Clip>& Session::getClips(int track) const {
    return validTrack(track) ? clips_[static_cast<size_t>(track)] : kNoClips;
}

void Session::pushClips(int track) {
    // setTrackClips() copies the vector into a new heap object for the audio thread.
    engine_.setTrackClips(track, clips_[static_cast<size_t>(track)]);
}

void Session::setClips(int track, std::vector<Clip> clips) {
    if (!validTrack(track))
        return;
    std::stable_sort(clips.begin(), clips.end(), [](const Clip& a, const Clip& b) { return a.start < b.start; });
    clips_[static_cast<size_t>(track)] = std::move(clips);
    pushClips(track);
    sendChangeMessage();
}

void Session::setTrackAudio(int track, std::shared_ptr<const AudioBuffer> audio, const juce::String& clipName,
                            int64_t start) {
    if (!validTrack(track) || audio == nullptr)
        return;
    Clip c = Clip::fromBuffer(std::move(audio), std::max<int64_t>(0, start), clipName.toStdString());
    // 5 ms fades: an audio file rarely starts and ends exactly at a zero crossing.
    const int fade = static_cast<int>(std::lround(0.005 * getClipSampleRate()));
    c.fadeInSamples = static_cast<int>(std::min<int64_t>(fade, c.length / 2));
    c.fadeOutSamples = c.fadeInSamples;
    setClips(track, {c});
    if (clipName.isNotEmpty())
        setStripName(ChannelRef::track(track), clipName.upToFirstOccurrenceOf(".", false, false));
}

int Session::moveClip(int track, int clipIndex, int64_t newStart) {
    if (!validTrack(track))
        return -1;
    auto& list = clips_[static_cast<size_t>(track)];
    if (clipIndex < 0 || clipIndex >= static_cast<int>(list.size()))
        return -1;
    Clip moved = list[static_cast<size_t>(clipIndex)];
    moved.start = std::max<int64_t>(0, newStart);
    list.erase(list.begin() + clipIndex);
    // Keep the list sorted: insert after every clip that starts at or before the new start.
    auto pos = std::upper_bound(list.begin(), list.end(), moved.start,
                                [](int64_t s, const Clip& c) { return s < c.start; });
    const int newIndex = static_cast<int>(pos - list.begin());
    list.insert(pos, std::move(moved));
    pushClips(track);
    sendChangeMessage();
    return newIndex;
}

int64_t Session::getSongEndSample() const {
    int64_t end = 0;
    for (const auto& list : clips_)
        for (const Clip& c : list)
            end = std::max(end, c.end());
    return end;
}

juce::String Session::getStripName(ChannelRef ref) const {
    const ChannelStrip* s = engine_.getMixer().getStrip(ref);
    return s ? juce::String(s->getName()) : juce::String();
}

void Session::setStripName(ChannelRef ref, const juce::String& name) {
    if (ChannelStrip* s = engine_.getMixer().getStrip(ref)) {
        s->setName(name.toStdString()); // the audio thread never reads names
        sendChangeMessage();
    }
}

TrackFormat Session::getTrackFormat(int track) const {
    return validTrack(track) ? engine_.getMixer().getTrack(track).getFormat() : TrackFormat::Mono;
}

void Session::setTrackFormat(int track, TrackFormat format) {
    if (!validTrack(track) || getTrackFormat(track) == format)
        return;
    // An atomic that the audio thread reads once per block: no command needed. The pan mode
    // follows (pan law / balance); the pan position stays where it is.
    engine_.getMixer().getTrack(track).setFormat(format);
    sendChangeMessage();
}

// ---- insert slots --------------------------------------------------------------------------------

const InsertSlotState& Session::getSlot(ChannelRef ref, int uiSlot) const {
    return validSlot(uiSlot) ? strip(ref).slots[static_cast<size_t>(uiSlot)] : kEmptySlot;
}

int Session::chainIndexOf(ChannelRef ref, int uiSlot) const noexcept {
    int index = 0;
    const auto& slots = strip(ref).slots;
    for (int s = 0; s < uiSlot && s < kInsertSlotsPerStrip; ++s)
        if (!slots[static_cast<size_t>(s)].isEmpty())
            ++index;
    return index;
}

int Session::getNumUsedSlots(ChannelRef ref) const noexcept {
    int n = 0;
    for (const auto& s : strip(ref).slots)
        n += s.isEmpty() ? 0 : 1;
    return n;
}

InsertEffect* Session::setBuiltinInsert(ChannelRef ref, int uiSlot, EffectType type) {
    if (!validSlot(uiSlot) || engine_.getMixer().getStrip(ref) == nullptr)
        return nullptr;
    std::unique_ptr<InsertEffect> fx = createEffect(type);
    if (!fx)
        return nullptr;
    InsertSlotState& slot = strip(ref).slots[static_cast<size_t>(uiSlot)];
    const int chainIndex = chainIndexOf(ref, uiSlot);

    InsertEffect* observer = nullptr;
    if (slot.isEmpty()) {
        observer = engine_.addInsert(ref, chainIndex, std::move(fx));
    } else {
        if (onInsertWillBeRemoved)
            onInsertWillBeRemoved(slot.effect);
        observer = engine_.replaceInsert(ref, chainIndex, std::move(fx));
        if (observer == nullptr) {
            // The old effect is still in the chain (the command was dropped); keep showing it.
            return nullptr;
        }
    }
    if (observer == nullptr)
        return nullptr;
    slot.kind = SlotKind::Builtin;
    slot.type = type;
    slot.effect = observer;
    slot.vst = nullptr;
    slot.name = observer->getName();
    sendChangeMessage();
    return observer;
}

InsertEffect* Session::setVstInsert(ChannelRef ref, int uiSlot, std::unique_ptr<VstInsert> plugin) {
    if (!validSlot(uiSlot) || plugin == nullptr || engine_.getMixer().getStrip(ref) == nullptr)
        return nullptr;
    InsertSlotState& slot = strip(ref).slots[static_cast<size_t>(uiSlot)];
    const int chainIndex = chainIndexOf(ref, uiSlot);
    VstInsert* vst = plugin.get();
    const juce::String name = juce::String::fromUTF8(plugin->getName());

    InsertEffect* observer = nullptr;
    if (slot.isEmpty()) {
        observer = engine_.addInsert(ref, chainIndex, std::move(plugin));
    } else {
        if (onInsertWillBeRemoved)
            onInsertWillBeRemoved(slot.effect);
        observer = engine_.replaceInsert(ref, chainIndex, std::move(plugin));
    }
    if (observer == nullptr)
        return nullptr;
    slot.kind = SlotKind::Vst3;
    slot.effect = observer;
    slot.vst = vst;
    slot.name = name;
    sendChangeMessage();
    return observer;
}

void Session::clearInsert(ChannelRef ref, int uiSlot) {
    if (!validSlot(uiSlot))
        return;
    InsertSlotState& slot = strip(ref).slots[static_cast<size_t>(uiSlot)];
    if (slot.isEmpty())
        return;
    if (onInsertWillBeRemoved)
        onInsertWillBeRemoved(slot.effect);
    // The effect stays alive until the audio thread has unlinked it and our timer has run
    // Engine::collectGarbage(). We forget the observer pointer right now.
    engine_.removeInsert(ref, chainIndexOf(ref, uiSlot));
    slot = InsertSlotState{};
    sendChangeMessage();
}

void Session::setInsertBypassed(ChannelRef ref, int uiSlot, bool bypassed) {
    const InsertSlotState& slot = getSlot(ref, uiSlot);
    if (!slot.isEmpty()) {
        slot.effect->setBypassed(bypassed); // atomic; PDC follows in the next block
        sendChangeMessage();
    }
}

InsertEffect* Session::findInsert(ChannelRef ref, const char* effectName) const noexcept {
    for (const auto& s : strip(ref).slots)
        if (!s.isEmpty() && std::strcmp(s.effect->getName(), effectName) == 0)
            return s.effect;
    return nullptr;
}

bool Session::hasMasterLimiter() const noexcept { return findInsert(ChannelRef::master(), "Limiter") != nullptr; }

float Session::getMasterCeilingDb() const noexcept {
    if (InsertEffect* lim = findInsert(ChannelRef::master(), "Limiter")) {
        const int i = lim->findParameter("Ceiling");
        if (i >= 0)
            return lim->getParameter(i);
    }
    return 0.0f;
}

void Session::setMasterCeilingDb(float db) noexcept {
    if (InsertEffect* lim = findInsert(ChannelRef::master(), "Limiter"))
        lim->setParameterByName("Ceiling", db);
}

// ---- device changes ------------------------------------------------------------------------------

double Session::getClipSampleRate() const noexcept {
    return clipSampleRate_ > 0.0 ? clipSampleRate_ : engine_.getSampleRate();
}

std::vector<std::shared_ptr<const AudioBuffer>> Session::getClipSources() const {
    std::vector<std::shared_ptr<const AudioBuffer>> sources;
    for (const auto& list : clips_)
        for (const Clip& c : list)
            if (c.source && std::none_of(sources.begin(), sources.end(), [&c](const auto& s) { return s == c.source; }))
                sources.push_back(c.source);
    return sources;
}

void Session::scaleTransportPositions(double oldRate, double newRate) {
    if (oldRate <= 0.0 || newRate <= 0.0 || oldRate == newRate)
        return;
    const double ratio = newRate / oldRate;
    const auto scale = [ratio](int64_t v) { return static_cast<int64_t>(std::llround(static_cast<double>(v) * ratio)); };
    Transport& tr = engine_.getTransport();
    tr.setLoopRange(scale(tr.getLoopStart()), scale(tr.getLoopEnd()));
    tr.setPosition(scale(tr.getPosition()));
}

void Session::handleSampleRateChange(double oldRate, double newRate) {
    if (oldRate <= 0.0 || newRate <= 0.0 || oldRate == newRate)
        return;
    clipSampleRate_ = oldRate;                      // what the caller tells us the clips are at
    applySampleRateConversion(newRate, ConvertedBuffers{}); // resamples every source right here
    scaleTransportPositions(oldRate, newRate);
}

void Session::applySampleRateConversion(double newRate, const ConvertedBuffers& alreadyConverted) {
    const double oldRate = getClipSampleRate();
    if (oldRate <= 0.0 || newRate <= 0.0 || oldRate == newRate)
        return;
    const double ratio = newRate / oldRate;
    const auto scale = [ratio](int64_t v) { return static_cast<int64_t>(std::llround(static_cast<double>(v) * ratio)); };

    // Clips share buffers (the drum loop is one buffer used by four clips): resample each
    // buffer once and keep sharing it. Buffers the loader thread already converted are reused.
    ConvertedBuffers converted = alreadyConverted;
    for (int t = 0; t < kNumTracks; ++t) {
        auto& list = clips_[static_cast<size_t>(t)];
        if (list.empty())
            continue;
        for (Clip& c : list) {
            if (c.source) {
                auto it = converted.find(c.source);
                if (it == converted.end())
                    it = converted
                             .emplace(c.source,
                                      std::make_shared<const AudioBuffer>(resampleBuffer(*c.source, oldRate, newRate)))
                             .first;
                c.source = it->second;
            }
            c.start = scale(c.start);
            c.sourceOffset = scale(c.sourceOffset);
            c.length = scale(c.length);
            if (c.source)
                c.length = std::min<int64_t>(c.length, c.source->getNumSamples() - c.sourceOffset);
            c.fadeInSamples = static_cast<int>(scale(c.fadeInSamples));
            c.fadeOutSamples = static_cast<int>(scale(c.fadeOutSamples));
        }
        pushClips(t);
    }
    clipSampleRate_ = newRate;
    sendChangeMessage();
}

} // namespace dawlab::app
