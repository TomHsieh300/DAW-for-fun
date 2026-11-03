#include "Session.h"

#include "MediaLoader.h"
#include "VstInsert.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace dawlab::app {

using namespace dawlab::engine;

namespace {
const InsertSlotState kEmptySlot{};
const std::vector<Clip> kNoClips{};

bool validSlot(int s) noexcept { return s >= 0 && s < kInsertSlotsPerStrip; }

juce::String defaultStripName(ChannelRef ref) {
    switch (ref.kind) {
    case ChannelKind::Track: return "Track"; // tracks are named by addTrack()
    case ChannelKind::Bus: return "Bus " + juce::String::charToString(static_cast<juce::juce_wchar>('A' + ref.index));
    case ChannelKind::Master: return "Master";
    }
    return {};
}

void sortByStart(std::vector<Clip>& clips) {
    std::stable_sort(clips.begin(), clips.end(), [](const Clip& a, const Clip& b) { return a.start < b.start; });
}
} // namespace

Session::Session(Engine& engine) : engine_(engine) {}

Session::~Session() {
    // The engine owns the effects and outlives us; nothing to free here. Editor windows were
    // closed by their owner (MainComponent) before the engine goes away.
    onInsertWillBeRemoved = nullptr;
}

// ---- tracks ------------------------------------------------------------------------------------

TrackId Session::getTrackId(int index) const noexcept {
    return index >= 0 && index < getNumTracks() ? tracks_[static_cast<size_t>(index)].id : TrackId{};
}

int Session::indexOf(TrackId id) const noexcept {
    for (size_t i = 0; i < tracks_.size(); ++i)
        if (tracks_[i].id == id)
            return static_cast<int>(i);
    return -1;
}

Session::TrackModel* Session::findTrack(TrackId id) noexcept {
    const int i = indexOf(id);
    return i >= 0 ? &tracks_[static_cast<size_t>(i)] : nullptr;
}

const Session::TrackModel* Session::findTrack(TrackId id) const noexcept {
    const int i = indexOf(id);
    return i >= 0 ? &tracks_[static_cast<size_t>(i)] : nullptr;
}

TrackId Session::addTrack(TrackFormat format, const juce::String& name, int index) {
    const TrackId id{nextTrackId_};
    const int at = index >= 0 && index < getNumTracks() ? index : getNumTracks();
    // The default name counts what the user sees ("Track 9" below eight tracks), not the id.
    const juce::String trackName = name.isNotEmpty() ? name : "Track " + juce::String(at + 1);
    if (engine_.addTrack(id, format, trackName.toStdString()).index < 0)
        return TrackId{}; // pool or queue full: nothing changed, the id is still free
    ++nextTrackId_;
    TrackModel track;
    track.id = id;
    tracks_.insert(tracks_.begin() + at, std::move(track));
    sendChangeMessage();
    return id;
}

bool Session::removeTrack(TrackId id) {
    const int index = indexOf(id);
    if (index < 0 || !engine_.removeTrack(id))
        return false;
    // The engine hands the inserts back as garbage; they live until our timer collects it, so
    // the editors that point at them can still close now.
    if (onInsertWillBeRemoved)
        for (const InsertSlotState& slot : tracks_[static_cast<size_t>(index)].strip.slots)
            if (!slot.isEmpty())
                onInsertWillBeRemoved(slot.effect);
    tracks_.erase(tracks_.begin() + index);
    sendChangeMessage();
    return true;
}

void Session::removeAllTracks() {
    while (!tracks_.empty())
        if (!removeTrack(tracks_.back().id))
            break; // the queue is full: removeTrack() changed nothing, so stop instead of spinning
}

// ---- strips --------------------------------------------------------------------------------------

ChannelRef Session::stripRefAt(int stripIndex) const noexcept {
    const int numTracks = getNumTracks();
    if (stripIndex < numTracks)
        return trackRef(getTrackId(std::max(0, stripIndex)));
    if (stripIndex < numTracks + kNumBuses)
        return ChannelRef::bus(stripIndex - numTracks);
    return ChannelRef::master();
}

int Session::stripIndexOf(ChannelRef ref) const noexcept {
    switch (ref.kind) {
    case ChannelKind::Track: return engine_.getStrip(ref) != nullptr ? indexOf(ref.trackId) : -1;
    case ChannelKind::Bus: return getNumTracks() + std::clamp(ref.index, 0, kNumBuses - 1);
    case ChannelKind::Master: return getNumTracks() + kNumBuses;
    }
    return -1;
}

Session::StripModel* Session::findStrip(ChannelRef ref) noexcept {
    return const_cast<StripModel*>(static_cast<const Session*>(this)->findStrip(ref));
}

const Session::StripModel* Session::findStrip(ChannelRef ref) const noexcept {
    switch (ref.kind) {
    case ChannelKind::Track: {
        // A stale reference (the slot now belongs to another track) is refused like by the engine.
        const TrackModel* t = engine_.getStrip(ref) != nullptr ? findTrack(ref.trackId) : nullptr;
        return t ? &t->strip : nullptr;
    }
    case ChannelKind::Bus:
        return ref.index >= 0 && ref.index < kNumBuses ? &fixedStrips_[static_cast<size_t>(ref.index)] : nullptr;
    case ChannelKind::Master: return &fixedStrips_[kNumBuses];
    }
    return nullptr;
}

// ---- project ---------------------------------------------------------------------------------

void Session::removeBusAndMasterInserts() {
    for (int b = 0; b <= kNumBuses; ++b) {
        const ChannelRef ref = b < kNumBuses ? ChannelRef::bus(b) : ChannelRef::master();
        for (int slot = kInsertSlotsPerStrip - 1; slot >= 0; --slot)
            if (!getSlot(ref, slot).isEmpty())
                clearInsert(ref, slot);
    }
}

void Session::resetBusAndMasterSettings() {
    for (int b = 0; b <= kNumBuses; ++b) {
        const ChannelRef ref = b < kNumBuses ? ChannelRef::bus(b) : ChannelRef::master();
        ChannelStrip* s = engine_.getStrip(ref);
        s->setName(defaultStripName(ref).toStdString());
        s->setGainDb(0.0f);
        s->setPan(0.0f);
        s->setMute(false);
        s->setSolo(false);
        for (int send = 0; send < kNumBuses; ++send) {
            s->setSendLevelDb(send, kMinusInfinityDb);
            s->setSendPreFader(send, false);
        }
    }
}

void Session::newProject() {
    // Deleting a track deletes its clips and inserts too; a new track starts with the engine's
    // defaults (Engine::addTrack): fader 0 dB, centre, no sends.
    removeAllTracks();
    removeBusAndMasterInserts();
    clipSampleRate_ = engine_.getSampleRate(); // no clips: nothing to convert
    resetBusAndMasterSettings();
    for (int t = 0; t < kNewProjectTracks; ++t)
        addTrack(TrackFormat::Mono); // until a loaded file says otherwise
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
    removeAllTracks();
    removeBusAndMasterInserts();
    clipSampleRate_ = song.sampleRate; // every clip is replaced by one at the song's rate
    resetBusAndMasterSettings();

    Transport& tr = engine_.getTransport();
    tr.stop();
    tr.setTempo(song.bpm);
    tr.setTimeSignature(song.beatsPerBar, 4);
    tr.setLoopRangeBars(1, song.bars);
    tr.setLoopEnabled(false);
    tr.setPosition(0);

    // The song names its tracks demoTrackId(0..7); here they get new ids from addTrack(), like
    // any track the user adds. `ids` translates.
    int unknown = 0;
    std::array<TrackId, kDemoTracks> ids{};
    for (int t = 0; t < kDemoTracks; ++t) {
        const auto i = static_cast<size_t>(t);
        ids[i] = addTrack(song.trackFormat[i], juce::String(song.trackNames[i]));
        if (!ids[i].isValid()) {
            ++unknown; // the pool is smaller than the song: count the track as "not applied"
            continue;
        }
        Track& s = *engine_.getTrack(ids[i]);
        s.setGainDb(song.trackGainDb[i]);
        s.setPan(song.trackPan[i]);
        for (int b = 0; b < kNumBuses; ++b)
            s.setSendLevelDb(b, song.sendDb[i][static_cast<size_t>(b)]);
        setClips(ids[i], song.clips[i]);
    }
    for (int b = 0; b < kNumBuses; ++b) {
        engine_.getStrip(ChannelRef::bus(b))->setName(song.busNames[static_cast<size_t>(b)]);
        engine_.getStrip(ChannelRef::bus(b))->setGainDb(song.busGainDb[static_cast<size_t>(b)]);
    }

    // Inserts go into the next free GUI slot of their strip, in the song's order.
    for (const DemoInsert& d : song.inserts) {
        ChannelRef ref = d.channel;
        if (ref.kind == ChannelKind::Track)
            for (int t = 0; t < kDemoTracks; ++t)
                if (demoTrackId(t) == d.channel.trackId)
                    ref = trackRef(ids[static_cast<size_t>(t)]);
        int slot = 0;
        while (slot < kInsertSlotsPerStrip && !getSlot(ref, slot).isEmpty())
            ++slot;
        if (slot == kInsertSlotsPerStrip) {
            ++unknown; // more inserts than GUI slots: count it as "not applied"
            continue;
        }
        InsertEffect* fx = setBuiltinInsert(ref, slot, d.type);
        if (fx == nullptr) {
            ++unknown;
            continue;
        }
        for (const auto& p : d.parameters)
            if (!fx->setParameterByName(p.first, p.second))
                ++unknown;
    }

    engine_.requestReset(); // fresh meters, no tails of the previous project
    projectName_ = "Demo song";
    sendChangeMessage();
    return unknown;
}

// ---- clips ---------------------------------------------------------------------------------------

const std::vector<Clip>& Session::getClips(TrackId track) const {
    const TrackModel* t = findTrack(track);
    return t ? t->clips : kNoClips;
}

void Session::pushClips(const TrackModel& track) {
    // setTrackClips() copies the vector into a new heap object for the audio thread.
    engine_.setTrackClips(track.id, track.clips);
}

void Session::setClips(TrackId track, std::vector<Clip> clips) {
    TrackModel* t = findTrack(track);
    if (t == nullptr)
        return;
    sortByStart(clips);
    t->clips = std::move(clips);
    pushClips(*t);
    sendChangeMessage();
}

void Session::setTrackAudio(TrackId track, std::shared_ptr<const AudioBuffer> audio, const juce::String& clipName,
                            int64_t start) {
    if (!hasTrack(track) || audio == nullptr)
        return;
    Clip c = Clip::fromBuffer(std::move(audio), std::max<int64_t>(0, start), clipName.toStdString());
    // 5 ms fades: an audio file rarely starts and ends exactly at a zero crossing.
    const int fade = static_cast<int>(std::lround(0.005 * getClipSampleRate()));
    c.fadeInSamples = static_cast<int>(std::min<int64_t>(fade, c.length / 2));
    c.fadeOutSamples = c.fadeInSamples;
    setClips(track, {c});
    if (clipName.isNotEmpty())
        setStripName(trackRef(track), clipName.upToFirstOccurrenceOf(".", false, false));
}

int Session::moveClip(TrackId track, int clipIndex, int64_t newStart) {
    TrackModel* t = findTrack(track);
    if (t == nullptr)
        return -1;
    auto& list = t->clips;
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
    pushClips(*t);
    sendChangeMessage();
    return newIndex;
}

int64_t Session::getSongEndSample() const {
    int64_t end = 0;
    for (const TrackModel& t : tracks_)
        for (const Clip& c : t.clips)
            end = std::max(end, c.end());
    return end;
}

juce::String Session::getStripName(ChannelRef ref) const {
    const ChannelStrip* s = engine_.getStrip(ref);
    return s ? juce::String(s->getName()) : juce::String();
}

void Session::setStripName(ChannelRef ref, const juce::String& name) {
    if (ChannelStrip* s = engine_.getStrip(ref)) {
        s->setName(name.toStdString()); // the audio thread never reads names
        sendChangeMessage();
    }
}

TrackFormat Session::getTrackFormat(TrackId track) const {
    const ChannelStrip* s = engine_.getStrip(trackRef(track));
    return s ? static_cast<const Track*>(s)->getFormat() : TrackFormat::Mono;
}

void Session::setTrackFormat(TrackId track, TrackFormat format) {
    Track* t = engine_.getTrack(track);
    if (t == nullptr || t->getFormat() == format)
        return;
    // An atomic that the audio thread reads once per block: no command needed. The pan mode
    // follows (pan law / balance); the pan position stays where it is.
    t->setFormat(format);
    sendChangeMessage();
}

// ---- insert slots --------------------------------------------------------------------------------

const InsertSlotState& Session::getSlot(ChannelRef ref, int uiSlot) const {
    const StripModel* strip = findStrip(ref);
    return strip && validSlot(uiSlot) ? strip->slots[static_cast<size_t>(uiSlot)] : kEmptySlot;
}

int Session::chainIndexOf(ChannelRef ref, int uiSlot) const noexcept {
    int index = 0;
    for (int s = 0; s < uiSlot && s < kInsertSlotsPerStrip; ++s)
        if (!getSlot(ref, s).isEmpty())
            ++index;
    return index;
}

int Session::getNumUsedSlots(ChannelRef ref) const noexcept {
    int n = 0;
    for (int s = 0; s < kInsertSlotsPerStrip; ++s)
        n += getSlot(ref, s).isEmpty() ? 0 : 1;
    return n;
}

InsertEffect* Session::setBuiltinInsert(ChannelRef ref, int uiSlot, EffectType type) {
    StripModel* strip = findStrip(ref);
    if (strip == nullptr || !validSlot(uiSlot))
        return nullptr;
    std::unique_ptr<InsertEffect> fx = createEffect(type);
    if (!fx)
        return nullptr;
    InsertSlotState& slot = strip->slots[static_cast<size_t>(uiSlot)];
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
    StripModel* strip = findStrip(ref);
    if (strip == nullptr || !validSlot(uiSlot) || plugin == nullptr)
        return nullptr;
    InsertSlotState& slot = strip->slots[static_cast<size_t>(uiSlot)];
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
    StripModel* strip = findStrip(ref);
    if (strip == nullptr || !validSlot(uiSlot))
        return;
    InsertSlotState& slot = strip->slots[static_cast<size_t>(uiSlot)];
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
    for (int s = 0; s < kInsertSlotsPerStrip; ++s) {
        const InsertSlotState& slot = getSlot(ref, s);
        if (!slot.isEmpty() && std::strcmp(slot.effect->getName(), effectName) == 0)
            return slot.effect;
    }
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
    for (const TrackModel& t : tracks_)
        for (const Clip& c : t.clips)
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
    for (TrackModel& t : tracks_) {
        if (t.clips.empty())
            continue;
        for (Clip& c : t.clips) {
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
