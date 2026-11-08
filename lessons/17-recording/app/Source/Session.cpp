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
    return addTrackWithId(TrackId{nextTrackId_}, format, name, index);
}

// A new id (== nextTrackId_) or one this session handed out before (undoing a delete, Step 9);
// never one that is in use. The counter only moves up.
TrackId Session::addTrackWithId(TrackId id, TrackFormat format, const juce::String& name, int index) {
    if (!id.isValid() || id.value > nextTrackId_ || hasTrack(id))
        return TrackId{};
    const int at = index >= 0 && index < getNumTracks() ? index : getNumTracks();
    // The default name counts the tracks the user sees ("Track 9" with eight), not the ids.
    const juce::String trackName = name.isNotEmpty() ? name : "Track " + juce::String(getNumTracks() + 1);
    if (engine_.addTrack(id, format, trackName.toStdString()).index < 0)
        return TrackId{}; // pool or queue full: nothing changed, the id is still free
    nextTrackId_ = std::max(nextTrackId_, id.value + 1);
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
    forgetLanesOfTrack(id); // the engine's curves went with the RemoveTrack command
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
    // By now only the fixed strips' own lanes are left; the rest went with their owners.
    while (!lanes_.empty()) {
        const LaneAddress address = lanes_.back().first;
        setLane(address, std::nullopt);
        if (lanes_.size() > 0 && lanes_.back().first == address)
            lanes_.pop_back();
    }
    recorder_ = engine::AutomationRecorder{};
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

void Session::clearProject() {
    // Deleting a track deletes its clips and inserts too; a new track starts with the engine's
    // defaults (Engine::addTrack): fader 0 dB, centre, no sends.
    removeAllTracks();
    removeBusAndMasterInserts();
    clipSampleRate_ = engine_.getSampleRate(); // no clips: nothing to convert
    resetBusAndMasterSettings();
    Transport& tr = engine_.getTransport();
    tr.stop();
    tr.setTempo(120.0);
    tr.setTimeSignature(4, 4);
    tr.setLoopRangeBars(1, 4);
    tr.setLoopEnabled(false);
    tr.setPosition(0);
    engine_.requestReset();
    projectName_ = "Untitled";
    history_.clear();
    sendChangeMessage();
}

void Session::newProject() {
    clearProject();
    for (int t = 0; t < kNewProjectTracks; ++t)
        addTrack(TrackFormat::Mono); // until a loaded file says otherwise
    // A safety limiter on the master, so that a new project can never clip the converter.
    if (InsertEffect* lim = setBuiltinInsert(ChannelRef::master(), kInsertSlotsPerStrip - 1, EffectType::Limiter))
        lim->setParameterByName("Ceiling", -1.0f);
}

void Session::setProjectName(const juce::String& name) {
    projectName_ = name;
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
    history_.clear();
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

Clip Session::makeAudioClip(std::shared_ptr<const AudioBuffer> audio, const juce::String& clipName, int64_t start) const {
    Clip c = Clip::fromBuffer(std::move(audio), std::max<int64_t>(0, start), clipName.toStdString());
    // 5 ms fades: an audio file rarely starts and ends exactly at a zero crossing.
    const int fade = static_cast<int>(std::lround(0.005 * getClipSampleRate()));
    c.fadeInSamples = static_cast<int>(std::min<int64_t>(fade, c.length / 2));
    c.fadeOutSamples = c.fadeInSamples;
    return c;
}

int Session::moveClip(TrackId track, int clipIndex, int64_t newStart) {
    const TrackModel* t = findTrack(track);
    if (t == nullptr)
        return -1;
    std::vector<Clip> list = t->clips;
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
    constexpr std::uint32_t kMoveClip = 0x4d4f5645; // "MOVE"
    if (!perform(edit::SetClips{track, std::move(list)}, "Move clip", mergeKeyFor(StripTarget::ofTrack(track), kMoveClip)))
        return -1;
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

InsertEffect* Session::setBuiltinInsert(ChannelRef ref, int uiSlot, EffectType type, EffectId id) {
    StripModel* strip = findStrip(ref);
    if (strip == nullptr || !validSlot(uiSlot))
        return nullptr;
    std::unique_ptr<InsertEffect> fx = createEffect(type);
    if (!fx)
        return nullptr;
    fx->setEffectId(id);
    InsertSlotState& slot = strip->slots[static_cast<size_t>(uiSlot)];
    const int chainIndex = chainIndexOf(ref, uiSlot);

    InsertEffect* observer = nullptr;
    if (slot.isEmpty()) {
        observer = engine_.addInsert(ref, chainIndex, std::move(fx));
    } else {
        if (onInsertWillBeRemoved)
            onInsertWillBeRemoved(slot.effect);
        const EffectId old = slot.effect->getEffectId();
        observer = engine_.replaceInsert(ref, chainIndex, std::move(fx));
        if (observer == nullptr) {
            // The old effect is still in the chain (the command was dropped); keep showing it.
            return nullptr;
        }
        forgetLanesOfEffect(old);
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
        const EffectId old = slot.effect->getEffectId();
        observer = engine_.replaceInsert(ref, chainIndex, std::move(plugin));
        if (observer != nullptr)
            forgetLanesOfEffect(old);
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
    forgetLanesOfEffect(slot.effect->getEffectId());
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

int Session::findInsertSlot(ChannelRef ref, const char* effectName) const noexcept {
    for (int s = 0; s < kInsertSlotsPerStrip; ++s) {
        const InsertSlotState& slot = getSlot(ref, s);
        if (!slot.isEmpty() && std::strcmp(slot.effect->getName(), effectName) == 0)
            return s;
    }
    return -1;
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
    // A curve's positions are samples too; values, order and jumps stay.
    for (auto& [address, content] : lanes_) {
        engine::AutomationCurve scaled(content.curve.defaultValue());
        for (engine::AutomationPoint point : content.curve.points()) {
            point.position = scale(point.position);
            scaled.add(point);
        }
        content.curve = std::move(scaled);
        pushLane(address);
    }
    clipSampleRate_ = newRate;
    history_.clear(); // its clip lists and positions are at the old rate
    sendChangeMessage();
}

// ---- undo and redo (chapter 15, Step 9) ------------------------------------------------------------

bool Session::perform(std::vector<Edit> edits, const juce::String& name, std::uint32_t mergeKey) {
    return history_.perform(*this, std::move(edits), name, mergeKey);
}

bool Session::perform(Edit edit, const juce::String& name, std::uint32_t mergeKey) {
    std::vector<Edit> edits;
    edits.push_back(std::move(edit));
    return perform(std::move(edits), name, mergeKey);
}

bool Session::undo() { return history_.undo(*this); }
bool Session::redo() { return history_.redo(*this); }

std::optional<Edit> Session::applyEdit(const Edit& e) {
    return std::visit([this](const auto& kind) { return apply(kind); }, e);
}

ChannelRef Session::refOf(const StripTarget& t) const noexcept {
    switch (t.kind) {
    case ChannelKind::Track: return trackRef(t.track);
    case ChannelKind::Bus: return ChannelRef::bus(t.bus);
    case ChannelKind::Master: return ChannelRef::master();
    }
    return ChannelRef::master();
}

edit::AddTrack Session::makeNewTrack(TrackFormat format, int index, const juce::String& name) const {
    edit::AddTrack a;
    a.track.id = TrackId{nextTrackId_};
    a.track.format = format;
    a.track.name = name.isNotEmpty() ? name : "Track " + juce::String(getNumTracks() + 1);
    a.index = index;
    return a;
}

InsertSnapshot Session::snapshotInsert(ChannelRef ref, int uiSlot) const {
    InsertSnapshot s;
    const InsertSlotState& st = getSlot(ref, uiSlot);
    if (st.isEmpty())
        return s;
    s.kind = st.kind;
    s.bypassed = st.effect->isBypassed();
    if (st.kind == SlotKind::Vst3 && st.vst != nullptr) {
        host::HostedPlugin& plugin = st.vst->getPlugin();
        s.vstFile = plugin.getInstance().getPluginDescription().fileOrIdentifier;
        s.vstState = plugin.getState();
    } else {
        s.type = st.type;
        for (int i = 0; i < st.effect->getNumParameters(); ++i)
            s.params.push_back(knobValue(*st.effect, i));
    }
    s.effectId = st.effect->getEffectId();
    s.lanes = ownedLanes(StripTarget::of(ref), s.effectId);
    return s;
}

TrackSnapshot Session::snapshotTrack(TrackId id) const {
    TrackSnapshot s;
    const ChannelRef ref = trackRef(id);
    const ChannelStrip* cs = engine_.getStrip(ref);
    if (cs == nullptr)
        return s;
    s.id = id;
    s.format = getTrackFormat(id);
    s.name = juce::String(cs->getName());
    s.gainDb = cs->getGainDb();
    s.pan = cs->getPan();
    s.mute = cs->isMuted();
    s.solo = cs->isSoloed();
    for (int b = 0; b < kNumBuses; ++b)
        s.sends[static_cast<size_t>(b)] = {cs->getSendLevelDb(b), cs->isSendPreFader(b)};
    for (int slot = 0; slot < kInsertSlotsPerStrip; ++slot)
        s.inserts[static_cast<size_t>(slot)] = snapshotInsert(ref, slot);
    s.clips = getClips(id);
    s.lanes = ownedLanes(StripTarget::ofTrack(id), EffectId{});
    return s;
}

bool Session::applyInsertSnapshot(ChannelRef ref, int uiSlot, const InsertSnapshot& insert) {
    InsertEffect* fx = nullptr;
    switch (insert.kind) {
    case SlotKind::Empty:
        clearInsert(ref, uiSlot);
        return getSlot(ref, uiSlot).isEmpty();
    case SlotKind::Builtin:
        fx = setBuiltinInsert(ref, uiSlot, insert.type, insert.effectId);
        if (fx != nullptr)
            for (int i = 0; i < std::min(fx->getNumParameters(), static_cast<int>(insert.params.size())); ++i)
                fx->setParameter(i, insert.params[static_cast<size_t>(i)]);
        break;
    case SlotKind::Vst3: {
        if (!createVst3)
            return false;
        juce::String error;
        std::unique_ptr<VstInsert> plugin = createVst3(juce::File(insert.vstFile), insert.vstState, error);
        if (plugin == nullptr)
            return false;
        plugin->setEffectId(insert.effectId);
        fx = setVstInsert(ref, uiSlot, std::move(plugin));
        break;
    }
    }
    if (fx == nullptr)
        return false;
    setInsertBypassed(ref, uiSlot, insert.bypassed);
    restoreLanes(StripTarget::of(ref), fx->getEffectId(), insert.lanes); // after its parameters: they are the knobs
    return true;
}

std::optional<Edit> Session::apply(const edit::AddTrack& e) {
    const TrackSnapshot& s = e.track;
    const TrackId id = addTrackWithId(s.id, s.format, s.name, e.index);
    if (!id.isValid())
        return std::nullopt;
    const ChannelRef ref = trackRef(id);
    ChannelStrip& cs = *engine_.getStrip(ref);
    cs.setGainDb(s.gainDb);
    cs.setPan(s.pan);
    cs.setMute(s.mute);
    cs.setSolo(s.solo);
    for (int b = 0; b < kNumBuses; ++b) {
        cs.setSendLevelDb(b, s.sends[static_cast<size_t>(b)].levelDb);
        cs.setSendPreFader(b, s.sends[static_cast<size_t>(b)].preFader);
    }
    for (int slot = 0; slot < kInsertSlotsPerStrip; ++slot)
        applyInsertSnapshot(ref, slot, s.inserts[static_cast<size_t>(slot)]); // a plugin that is gone stays out
    if (!s.clips.empty())
        setClips(id, s.clips);
    restoreLanes(StripTarget::ofTrack(id), EffectId{}, s.lanes);
    return edit::RemoveTrack{id};
}

std::optional<Edit> Session::apply(const edit::RemoveTrack& e) {
    const int index = indexOf(e.id);
    if (index < 0)
        return std::nullopt;
    edit::AddTrack inverse{snapshotTrack(e.id), index}; // before anything is gone
    if (!removeTrack(e.id))
        return std::nullopt;
    return inverse;
}

std::optional<Edit> Session::apply(const edit::SetValue& e) {
    ChannelStrip* cs = engine_.getStrip(refOf(e.strip));
    if (cs == nullptr || (e.field == edit::Field::SendLevel && (e.bus < 0 || e.bus >= kNumBuses)))
        return std::nullopt;
    edit::SetValue inverse = e;
    switch (e.field) {
    case edit::Field::Gain:
        inverse.value = cs->getGainDb();
        cs->setGainDb(e.value);
        break;
    case edit::Field::Pan:
        inverse.value = cs->getPan();
        cs->setPan(e.value);
        break;
    case edit::Field::Mute:
        inverse.value = cs->isMuted() ? 1.0f : 0.0f;
        cs->setMute(e.value >= 0.5f);
        break;
    case edit::Field::Solo:
        inverse.value = cs->isSoloed() ? 1.0f : 0.0f;
        cs->setSolo(e.value >= 0.5f);
        break;
    case edit::Field::SendLevel:
        inverse.value = cs->getSendLevelDb(e.bus);
        cs->setSendLevelDb(e.bus, e.value);
        break;
    }
    sendChangeMessage(); // the arrangement shows M / S; the strips follow the atomics by themselves
    return inverse;
}

std::optional<Edit> Session::apply(const edit::Rename& e) {
    const ChannelRef ref = refOf(e.strip);
    if (engine_.getStrip(ref) == nullptr)
        return std::nullopt;
    edit::Rename inverse{e.strip, getStripName(ref)};
    setStripName(ref, e.name);
    return inverse;
}

std::optional<Edit> Session::apply(const edit::SetFormat& e) {
    if (!hasTrack(e.track))
        return std::nullopt;
    edit::SetFormat inverse{e.track, getTrackFormat(e.track)};
    setTrackFormat(e.track, e.format);
    return inverse;
}

std::optional<Edit> Session::apply(const edit::SetClips& e) {
    if (!hasTrack(e.track))
        return std::nullopt;
    edit::SetClips inverse{e.track, getClips(e.track)};
    setClips(e.track, e.clips);
    return inverse;
}

std::optional<Edit> Session::apply(const edit::SetInsert& e) {
    const ChannelRef ref = refOf(e.strip);
    if (findStrip(ref) == nullptr || e.slot < 0 || e.slot >= kInsertSlotsPerStrip)
        return std::nullopt;
    edit::SetInsert inverse{e.strip, e.slot, snapshotInsert(ref, e.slot)};
    if (!applyInsertSnapshot(ref, e.slot, e.insert))
        return std::nullopt; // the old insert is still there (or the slot was empty and stays so)
    return inverse;
}

std::optional<Edit> Session::apply(const edit::SetParameter& e) {
    const InsertSlotState& slot = getSlot(refOf(e.strip), e.slot);
    if (slot.isEmpty() || e.index < 0 || e.index >= slot.effect->getNumParameters())
        return std::nullopt;
    edit::SetParameter inverse = e;
    inverse.value = knobValue(*slot.effect, e.index);
    slot.effect->setParameter(e.index, e.value); // an atomic; editor panels follow by their timer
    if (engine::AutomationLane* lane = slot.effect->getLane(e.index); lane != nullptr && lane->isActive())
        lane->setKnobValue(slot.effect->getParameterInfo(e.index).toNormalized(e.value));
    return inverse;
}

std::optional<Edit> Session::apply(const edit::SetBypass& e) {
    const ChannelRef ref = refOf(e.strip);
    const InsertSlotState& slot = getSlot(ref, e.slot);
    if (slot.isEmpty())
        return std::nullopt;
    edit::SetBypass inverse{e.strip, e.slot, slot.effect->isBypassed()};
    setInsertBypassed(ref, e.slot, e.bypassed);
    return inverse;
}

std::optional<Edit> Session::apply(const edit::SetTempo& e) {
    if (!(e.bpm > 0.0))
        return std::nullopt;
    Transport& tr = engine_.getTransport();
    edit::SetTempo inverse{tr.getTempo()};
    tr.setTempo(e.bpm);
    sendChangeMessage();
    return inverse;
}

// ---- automation (chapter 16, Step 6) -----------------------------------------------------------------

const LaneContent* Session::getLane(const LaneAddress& address) const noexcept {
    for (const auto& [a, content] : lanes_)
        if (a == address)
            return &content;
    return nullptr;
}

std::vector<std::pair<LaneAddress, LaneContent>> Session::getLanesOf(ChannelRef ref) const {
    const StripTarget strip = StripTarget::of(ref);
    std::vector<std::pair<LaneAddress, LaneContent>> found;
    for (const auto& lane : lanes_)
        if (lane.first.strip == strip)
            found.push_back(lane);
    return found;
}

int Session::slotOfEffect(ChannelRef ref, EffectId id) const noexcept {
    const StripModel* strip = findStrip(ref);
    if (strip == nullptr || !id.isValid())
        return -1;
    for (int s = 0; s < kInsertSlotsPerStrip; ++s) {
        const InsertSlotState& slot = strip->slots[static_cast<size_t>(s)];
        if (!slot.isEmpty() && slot.effect->getEffectId() == id)
            return s;
    }
    return -1;
}

InsertEffect* Session::effectOf(const LaneAddress& address) const noexcept {
    const ChannelRef ref = refOf(address.strip);
    const int slot = slotOfEffect(ref, address.effect);
    return slot >= 0 ? getSlot(ref, slot).effect : nullptr;
}

int Session::parameterIndexOf(const LaneAddress& address) const noexcept {
    if (!address.effect.isValid())
        return address.parameter == "Gain" ? engine::kLaneGain : (address.parameter == "Pan" ? engine::kLanePan : -1);
    const InsertEffect* effect = effectOf(address);
    return effect != nullptr ? effect->findParameter(address.parameter.toRawUTF8()) : -1;
}

// By the effect's own pointer, not through the engine's chain: an effect added a moment ago is not in
// the chain until the audio thread has taken it, but its lanes are already there.
engine::AutomationLane* Session::laneOf(const LaneAddress& address) const noexcept {
    const int parameter = parameterIndexOf(address);
    if (parameter < 0)
        return nullptr;
    if (address.effect.isValid()) {
        InsertEffect* effect = effectOf(address);
        return effect != nullptr ? effect->getLane(parameter) : nullptr;
    }
    ChannelStrip* strip = const_cast<Engine&>(engine_).getStrip(refOf(address.strip));
    return strip != nullptr ? strip->getOwnLane(parameter) : nullptr;
}

bool Session::isLaneLive(const LaneAddress& address) const noexcept { return laneOf(address) != nullptr; }

// Tells the engine what the model says about this lane.
void Session::pushLane(const LaneAddress& address) {
    engine::AutomationLane* lane = laneOf(address);
    if (lane == nullptr)
        return; // an unknown parameter: nothing plays it
    const int parameter = parameterIndexOf(address);
    if (InsertEffect* effect = effectOf(address); effect != nullptr && !lane->isActive())
        lane->setKnobValue(effect->getParameterNormalized(parameter)); // where it returns when the lane goes
    const engine::LaneTarget target{refOf(address.strip), address.effect, parameter};
    const LaneContent* content = getLane(address);
    lane->setMode(content != nullptr ? content->mode : engine::AutomationMode::Read);
    if (content != nullptr && content->curve.size() > 0)
        engine_.setLaneCurve(target, content->curve);
    else
        engine_.clearLaneCurve(target);
}

bool Session::setLane(const LaneAddress& address, std::optional<LaneContent> content) {
    if (engine_.getStrip(refOf(address.strip)) == nullptr || (address.effect.isValid() && effectOf(address) == nullptr))
        return false;
    const auto it = std::find_if(lanes_.begin(), lanes_.end(), [&address](const auto& lane) { return lane.first == address; });
    if (it != lanes_.end())
        lanes_.erase(it);
    if (content)
        lanes_.emplace_back(address, std::move(*content));
    pushLane(address);
    sendChangeMessage();
    return true;
}

void Session::forgetLanesOfEffect(EffectId id) {
    lanes_.erase(std::remove_if(lanes_.begin(), lanes_.end(), [id](const auto& lane) { return lane.first.effect == id; }), lanes_.end());
}

void Session::forgetLanesOfTrack(TrackId id) {
    const StripTarget strip = StripTarget::ofTrack(id);
    lanes_.erase(std::remove_if(lanes_.begin(), lanes_.end(), [&strip](const auto& lane) { return lane.first.strip == strip; }), lanes_.end());
}

std::vector<OwnedLane> Session::ownedLanes(const StripTarget& strip, EffectId effect) const {
    std::vector<OwnedLane> owned;
    for (const auto& [address, content] : lanes_)
        if (address.strip == strip && address.effect == effect)
            owned.push_back({address.parameter, content});
    return owned;
}

bool Session::restoreLanes(const StripTarget& strip, EffectId effect, const std::vector<OwnedLane>& lanes) {
    bool ok = true;
    for (const OwnedLane& lane : lanes)
        ok = setLane({strip, effect, lane.parameter}, lane.content) && ok;
    return ok;
}

std::optional<Edit> Session::apply(const edit::SetLane& e) {
    const LaneContent* old = getLane(e.address);
    edit::SetLane inverse{e.address, old != nullptr ? std::optional<LaneContent>(*old) : std::nullopt};
    if (!setLane(e.address, e.content))
        return std::nullopt;
    return inverse;
}

float Session::knobValue(InsertEffect& effect, int index) const noexcept {
    const engine::AutomationLane* lane = effect.getLane(index);
    return lane != nullptr && lane->isActive() ? effect.getParameterInfo(index).fromNormalized(lane->getKnobValue()) : effect.getParameter(index);
}

LaneContent Session::contentOrNew(const LaneAddress& address) const {
    const LaneContent* content = getLane(address);
    return content != nullptr ? *content : LaneContent{};
}

bool Session::editLane(const LaneAddress& address, LaneContent content, const juce::String& name, std::uint32_t mergeKey) {
    return perform(edit::SetLane{address, std::move(content)}, name + " " + address.parameter, mergeKey);
}

int Session::addPoint(const LaneAddress& address, std::int64_t position, float value, std::uint32_t mergeKey) {
    LaneContent content = contentOrNew(address);
    const int index = content.curve.add({std::max<std::int64_t>(0, position), value, engine::Shape::Linear});
    return editLane(address, std::move(content), "Add point", mergeKey) ? index : -1;
}

int Session::movePoint(const LaneAddress& address, int index, std::int64_t position, float value, std::uint32_t mergeKey) {
    const LaneContent* old = getLane(address);
    if (old == nullptr || index < 0 || index >= old->curve.size())
        return -1;
    LaneContent content = *old;
    engine::AutomationPoint point = content.curve.points()[static_cast<size_t>(index)];
    point.position = std::max<std::int64_t>(0, position);
    point.value = value;
    content.curve.remove(index);
    const int moved = content.curve.add(point);
    return editLane(address, std::move(content), "Move point", mergeKey) ? moved : -1;
}

bool Session::removePoint(const LaneAddress& address, int index) {
    const LaneContent* old = getLane(address);
    if (old == nullptr || index < 0 || index >= old->curve.size())
        return false;
    LaneContent content = *old;
    content.curve.remove(index);
    return editLane(address, std::move(content), "Remove point", 0);
}

bool Session::setLaneMode(const LaneAddress& address, engine::AutomationMode mode) {
    LaneContent content = contentOrNew(address);
    content.mode = mode;
    return editLane(address, std::move(content), "Automation mode", 0);
}

void Session::touchLane(const LaneAddress& address) noexcept {
    if (engine::AutomationLane* lane = laneOf(address))
        lane->touch();
}

void Session::releaseLane(const LaneAddress& address) noexcept {
    if (engine::AutomationLane* lane = laneOf(address))
        lane->release();
}

bool Session::handMoves(const LaneAddress& address, float value) noexcept {
    engine::AutomationLane* lane = laneOf(address);
    if (lane == nullptr || !lane->isActive())
        return false;
    if (InsertEffect* effect = effectOf(address)) {
        lane->setKnobValue(effect->getParameterInfo(parameterIndexOf(address)).toNormalized(value));
        return true;
    }
    ChannelStrip* strip = engine_.getStrip(refOf(address.strip));
    if (strip == nullptr)
        return false;
    if (parameterIndexOf(address) == engine::kLaneGain)
        strip->setGainDb(value); // the knob follows (ChannelStrip::setGainDb)
    else
        strip->setPan(value);
    return true;
}

std::vector<std::pair<juce::String, LaneAddress>> Session::getAutomatableParameters(ChannelRef ref) const {
    std::vector<std::pair<juce::String, LaneAddress>> list;
    const StripTarget strip = StripTarget::of(ref);
    list.push_back({"Gain", {strip, EffectId{}, "Gain"}});
    list.push_back({"Pan", {strip, EffectId{}, "Pan"}});
    for (int s = 0; s < kInsertSlotsPerStrip; ++s) {
        const InsertSlotState& slot = getSlot(ref, s);
        if (slot.isEmpty() || slot.kind != SlotKind::Builtin)
            continue;
        for (int i = 0; i < slot.effect->getNumParameters(); ++i) {
            const juce::String name = slot.effect->getParameterInfo(i).name;
            list.push_back({slot.name + ": " + name, {strip, slot.effect->getEffectId(), name}});
        }
    }
    return list;
}

int Session::collectAutomation() {
    return recorder_.collect(engine_.getCaptureQueue(), [this](const engine::LaneTarget& target, std::vector<engine::AutomationPoint> points) {
        // The name of the lane, now: a track or an effect deleted since then ends the recording here.
        const LaneAddress address = [&] {
            LaneAddress a{StripTarget::of(target.channel), target.effect, {}};
            if (!target.effect.isValid())
                a.parameter = target.parameter == engine::kLaneGain ? "Gain" : "Pan";
            else if (const InsertEffect* effect = effectOf(a); effect != nullptr && target.parameter < effect->getNumParameters())
                a.parameter = effect->getParameterInfo(target.parameter).name;
            return a;
        }();
        if (address.parameter.isEmpty() || !isLaneLive(address))
            return;
        // A fader behind latency hears the start of the song before position 0: that is position 0.
        for (engine::AutomationPoint& point : points)
            point.position = std::max<std::int64_t>(0, point.position);
        LaneContent content = contentOrNew(address);
        if (content.curve.size() == 0) { // the first recording IS the curve: there is nothing to return to
            engine::AutomationCurve first(content.curve.defaultValue());
            for (const engine::AutomationPoint& point : points)
                first.add(point);
            content.curve = std::move(first);
        } else {
            content.curve = engine::punch(content.curve, points);
        }
        editLane(address, std::move(content), "Record", 0);
    });
}

} // namespace dawlab::app
