#pragma once
// Mixer: a pool of kMaxTracks track slots, 2 FX buses, 1 master, and the signal flow between
// them (chapter 14, Steps 4, 6, 8; the pool is chapter 15, Step 6).
//
// The pool: every slot is a complete Track, allocated in prepare(). A slot plays only while it
// holds a track id (trackInSlot_). Only the audio thread changes that, through
// activateTrack() / deactivateTrack() (Engine commands AddTrack / RemoveTrack), and only the
// audio thread (or setup code with no audio thread running) reads it. The UI thread keeps its
// own copy of which id lives in which slot (Engine::slotOf) and never reads trackInSlot_.
//
//   track 1 --+-- post/pre-fader sends ---------> bus A (e.g. reverb) --+
//   track 2 --+                                   bus B (e.g. delay) ---+
//     ...     |                                                         |
//   track N --+--> track sum --> [PDC: maxBusLatency] --------------------+--> master --> out
//
// Solo logic (tracks only): if any track is soloed, every non-soloed track is silent.
// Mute wins over solo on the same track. Buses and master are "solo safe": they only have
// mute, so a soloed track still reaches the reverb bus and you hear it with its reverb.
//
// Plugin delay compensation (PDC): every path to the master is padded so that all paths have
// the same total delay.
//   L_t = insert latency of track t, L_b = insert latency of bus b, L_m = master inserts
//   track t compensator   : maxTrackLatency - L_t   (after its inserts, before its sends)
//   bus b compensator     : maxBusLatency - L_b
//   track-sum compensator : maxBusLatency           (the dry sum waits for the slowest bus)
//   total engine latency  : maxTrackLatency + maxBusLatency + L_m
// All tracks are aligned when they reach the sends and the sum; the sum then waits for the
// slowest bus. Changing a delay amount moves a read pointer, so it may click; that only
// happens when inserts are added, removed, bypassed or change their latency.
//
// The PDC limit: every compensator holds at most C = maxPdcSamples (prepare(), default
// kDefaultMaxPdcSamples = 16384) samples of delay. If maxTrackLatency or maxBusLatency is larger
// than C, a full alignment is impossible. The mixer then aligns on C instead:
//   targetTrack = min(maxTrackLatency, C),  targetBus = min(maxBusLatency, C)
// Every path that can be delayed to the target is aligned there; a path whose own latency is
// larger than the target arrives (L - target) samples late. The reported total latency is
// targetTrack + targetBus + L_m, i.e. the delay of the aligned paths, so a bounce never shifts
// the aligned audio out of the file. hasPdcOverflow() reports the situation to the UI.
//
// Recording (chapter 17, Step 6): each track has a Recorder and a Monitor (Recording.h), and neither
// knows about the other.
//
//   input --> recorder (dry, before everything)
//         \-> monitor --> track, before its inserts (ThroughInserts): all that the track does, and the
//                                                     mix's whole compensation
//                     \-> the output, after the master (Direct): nothing of the above, as early as can be
//
// Recorders run in every slot, free ones too: a track can be removed while it is recording, and only
// the audio thread can confirm that its recording is over. Without an InputBlock nothing changes.

#include <dawlab/AudioBuffer.h>

#include <algorithm>
#include <memory>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

#include "ChannelStrip.h"
#include "Commands.h"
#include "Constants.h"
#include "Recording.h"
#include "Track.h"
#include "Transport.h"

namespace dawlab::engine {

class Mixer {
public:
    Mixer() {
        for (int b = 0; b < kNumBuses; ++b) // "Bus A", "Bus B" (, "Bus C" in Exercise 12)
            buses_[static_cast<size_t>(b)].setName(std::string("Bus ") + static_cast<char>('A' + b));
        master_.setName("Master");
        for (auto& b : buses_) // stereo strips: centre = unity (see ChannelStrip.h)
            b.setPanMode(PanMode::Balance);
        master_.setPanMode(PanMode::Balance);
    }
    Mixer(const Mixer&) = delete;
    Mixer& operator=(const Mixer&) = delete;

    // Allocates every scratch buffer and all strip state. Non-audio thread.
    void prepare(double sampleRate, int maxBlockSize, int maxPdcSamples = kDefaultMaxPdcSamples) {
        sampleRate_ = sampleRate;
        maxBlockSize_ = std::max(1, maxBlockSize);
        for (auto& t : tracks_) {
            t.prepare(sampleRate, maxBlockSize_, maxPdcSamples);
            t.getMonitor().prepare(static_cast<int>(sampleRate));
            t.prepareInstrument(sampleRate, maxBlockSize_);
        }
        for (auto& b : buses_)
            b.prepare(sampleRate, maxBlockSize_, maxPdcSamples);
        master_.prepare(sampleRate, maxBlockSize_, maxPdcSamples);
        trackSumCompensator_.prepare(maxPdcSamples, kNumChannels);
        scratch_.setSize(kNumChannels, maxBlockSize_);
        trackSum_.setSize(kNumChannels, maxBlockSize_);
        direct_.setSize(kNumChannels, maxBlockSize_);
        masterBuffer_.setSize(kNumChannels, maxBlockSize_);
        laneGains_.assign(static_cast<size_t>(maxBlockSize_), 0.0f);
        lanePans_.assign(static_cast<size_t>(maxBlockSize_), 0.0f);
        for (auto& b : busBuffers_)
            b.setSize(kNumChannels, maxBlockSize_);
        prepared_ = true;
        reset();
    }

    // Audio thread (or setup code).
    void reset() noexcept {
        for (auto& t : tracks_) {
            t.reset();
            t.resetInstrument();
        }
        for (auto& b : buses_)
            b.reset();
        master_.reset();
        trackSumCompensator_.reset();
    }

    // ---- access --------------------------------------------------------------------------------
    // The track in a SLOT, whether or not the slot is in use. The UI thread uses it for the
    // values (atomics) of the slots it gave out (Engine::getTrack(id) does that lookup for you).
    Track& getTrack(int slot) noexcept { return tracks_[static_cast<size_t>(std::clamp(slot, 0, kMaxTracks - 1))]; }
    const Track& getTrack(int slot) const noexcept {
        return tracks_[static_cast<size_t>(std::clamp(slot, 0, kMaxTracks - 1))];
    }
    ChannelStrip& getBus(int i) noexcept { return buses_[static_cast<size_t>(std::clamp(i, 0, kNumBuses - 1))]; }
    const ChannelStrip& getBus(int i) const noexcept {
        return buses_[static_cast<size_t>(std::clamp(i, 0, kNumBuses - 1))];
    }
    ChannelStrip& getMaster() noexcept { return master_; }
    const ChannelStrip& getMaster() const noexcept { return master_; }

    // AUDIO THREAD (or setup code with no audio thread running). nullptr when the reference is
    // out of range - or, for a track, when that slot does not hold that id (any more).
    ChannelStrip* getStrip(ChannelRef ref) noexcept {
        return const_cast<ChannelStrip*>(static_cast<const Mixer*>(this)->getStrip(ref));
    }
    const ChannelStrip* getStrip(ChannelRef ref) const noexcept {
        switch (ref.kind) {
        case ChannelKind::Track:
            return holds(ref.index, ref.trackId) ? &tracks_[static_cast<size_t>(ref.index)] : nullptr;
        case ChannelKind::Bus:
            return (ref.index >= 0 && ref.index < kNumBuses) ? &buses_[static_cast<size_t>(ref.index)] : nullptr;
        case ChannelKind::Master: return &master_;
        }
        return nullptr;
    }

    double getSampleRate() const noexcept { return sampleRate_; }
    int getMaxBlockSize() const noexcept { return maxBlockSize_; }
    bool isPrepared() const noexcept { return prepared_; }
    // Where the lanes send what the hand does (Engine's queue). Setup code; nullptr: nowhere.
    void setCaptureQueue(CaptureQueue* queue) noexcept { capture_ = queue; }
    // While a bounce runs: the curves play in every mode, and no hand is heard or recorded. Any thread.
    void setLanesReadOnly(bool readOnly) noexcept { lanesReadOnly_.store(readOnly, std::memory_order_relaxed); }

    // ---- the pool: audio thread (or setup code with no audio thread running) -----------------
    // Starts using a free slot for track `id`. False if the slot is out of range or in use, or
    // the id is invalid. The slot's DSP state is clean: prepare() and deactivateTrack() reset it.
    bool activateTrack(int slot, TrackId id) noexcept {
        if (slot < 0 || slot >= kMaxTracks || !id.isValid() || trackInSlot_[static_cast<size_t>(slot)].isValid())
            return false;
        trackInSlot_[static_cast<size_t>(slot)] = id;
        return true;
    }
    // Stops the track in ref's slot if it is still ref.trackId. Everything the slot owns goes to
    // `discard` (called with InsertEffect* and const std::vector<Clip>*, never nullptr) - the
    // audio thread may not delete it - and its DSP state is cleared, so the next track in this
    // slot starts silent (its PDC delay line still holds the old track's audio). False for a
    // stale ref.
    template <typename Discard>
    bool deactivateTrack(ChannelRef ref, Discard&& discard) noexcept {
        if (ref.kind != ChannelKind::Track || !holds(ref.index, ref.trackId))
            return false;
        Track& t = tracks_[static_cast<size_t>(ref.index)];
        for (int n = t.getInserts().getNumInserts(); n > 0; --n)
            if (InsertEffect* fx = t.getInserts().removeAt(n - 1))
                discard(fx);
        if (const std::vector<Clip>* clips = t.exchangeClips(nullptr))
            discard(clips);
        if (const std::vector<MidiClip>* midi = t.exchangeMidiClips(nullptr))
            discard(midi);
        if (Instrument* instrument = t.exchangeInstrument(nullptr))
            discard(instrument);
        t.discardOwnCurves(discard);
        t.reset();
        // Whatever sounded stops at once: the track is gone. NOT setInstrument(false): that is the UI's value,
        // and the UI may already have given this slot to a new instrument track (Engine::addTrack resets it).
        t.resetInstrument();
        t.getMonitor().clearAudioState();
        trackInSlot_[static_cast<size_t>(ref.index)] = TrackId{};
        return true;
    }
    // The id playing in a slot (invalid = free).
    TrackId trackInSlot(int slot) const noexcept {
        return (slot >= 0 && slot < kMaxTracks) ? trackInSlot_[static_cast<size_t>(slot)] : TrackId{};
    }
    int getNumActiveTracks() const noexcept {
        return static_cast<int>(std::count_if(trackInSlot_.begin(), trackInSlot_.end(), [](TrackId id) { return id.isValid(); }));
    }

    // Audio thread: solo is a property of the tracks that exist (a free slot's old solo flag
    // must not silence everything).
    bool isAnyTrackSoloed() const noexcept {
        for (int t = 0; t < kMaxTracks; ++t)
            if (trackInSlot_[static_cast<size_t>(t)].isValid() && tracks_[static_cast<size_t>(t)].isSoloed())
                return true;
        return false;
    }

    // Is the track in this slot heard right now?  audible = in use && !mute && (!anySolo || solo)
    bool isTrackAudible(int slot) const noexcept {
        if (!trackInSlot(slot).isValid())
            return false;
        const Track& tr = getTrack(slot);
        return !tr.isMuted() && (!isAnyTrackSoloed() || tr.isSoloed());
    }

    // Timeline end of the last clip on any track (UI thread / offline; see Track::getClips).
    int64_t getSongEndSample() const noexcept {
        int64_t end = 0;
        for (const auto& t : tracks_)
            end = std::max(end, t.getEndSample());
        return end;
    }

    // ---- PDC (any thread reads, the audio thread writes) -------------------------------------
    void setLatencyCompensationEnabled(bool enabled) noexcept { pdcEnabled_.store(enabled, std::memory_order_relaxed); }
    // Chapter 19, Step 3: while it is on, an armed track skips the compensation - it waits for no other track, and
    // no track waits for it - so a musician playing it hears it as early as it can be. Any thread.
    void setLowLatencyMonitoring(bool on) noexcept { lowLatencyMonitoring_.store(on, std::memory_order_relaxed); }
    bool isLowLatencyMonitoring() const noexcept { return lowLatencyMonitoring_.load(std::memory_order_relaxed); }
    bool isLatencyCompensationEnabled() const noexcept { return pdcEnabled_.load(std::memory_order_relaxed); }
    int getTotalLatencySamples() const noexcept { return totalLatency_.load(std::memory_order_relaxed); }
    int getMaxTrackLatencySamples() const noexcept { return maxTrackLatency_.load(std::memory_order_relaxed); }
    int getMaxBusLatencySamples() const noexcept { return maxBusLatency_.load(std::memory_order_relaxed); }
    int getTrackSumCompensationSamples() const noexcept { return trackSumCompensator_.getDelaySamples(); }
    // The largest delay one compensator can apply (the C of the header comment).
    int getMaxPdcSamples() const noexcept { return trackSumCompensator_.getMaxDelaySamples(); }
    // True while some insert latency exceeds the PDC limit, so not every path could be aligned
    // (see the header comment). The getters above then report the targets actually used.
    bool hasPdcOverflow() const noexcept { return pdcOverflow_.load(std::memory_order_relaxed); }

    // Audio thread, once per block (cheap: a few integer sums). Also safe from setup code.
    // First lets every insert apply pending parameter changes, so a latency that depends on a
    // parameter (limiter lookahead) is current before we compensate for it.
    void updateLatencies() noexcept {
        for (auto& t : tracks_)
            t.getInserts().applyPendingParameters();
        for (auto& b : buses_)
            b.getInserts().applyPendingParameters();
        master_.getInserts().applyPendingParameters();

        const bool enabled = pdcEnabled_.load(std::memory_order_relaxed);
        const bool lowLatency = lowLatencyMonitoring_.load(std::memory_order_relaxed);
        auto skipsThePlan = [lowLatency](const Track& t) { return lowLatency && t.isArmed(); };
        int maxTrack = 0;
        for (const auto& t : tracks_)
            if (!skipsThePlan(t))
                maxTrack = std::max(maxTrack, t.getLatencySamples());
        int maxBus = 0;
        for (const auto& b : buses_)
            maxBus = std::max(maxBus, b.getInsertLatencySamples());
        // A compensator cannot delay by more than C samples: align on min(max, C) and remember
        // that some path could not be aligned (header comment, "The PDC limit").
        const int limit = getMaxPdcSamples();
        const int targetTrack = std::min(maxTrack, limit);
        const int targetBus = std::min(maxBus, limit);
        pdcOverflow_.store(enabled && (maxTrack > targetTrack || maxBus > targetBus), std::memory_order_relaxed);
        // setDelaySamples() clamps a negative amount (a path slower than the target) to 0.
        for (auto& t : tracks_)
            t.getCompensator().setDelaySamples(enabled && !skipsThePlan(t) ? targetTrack - t.getLatencySamples() : 0);
        for (auto& b : buses_)
            b.getCompensator().setDelaySamples(enabled ? targetBus - b.getInsertLatencySamples() : 0);
        trackSumCompensator_.setDelaySamples(enabled ? targetBus : 0);
        maxTrackLatency_.store(targetTrack, std::memory_order_relaxed);
        maxBusLatency_.store(targetBus, std::memory_order_relaxed);
        // With PDC off the paths have different delays and there is no single "mix latency";
        // we report 0 so that a bounce does not shift anything.
        totalLatency_.store(enabled ? targetTrack + targetBus + master_.getInsertLatencySamples() : 0,
                            std::memory_order_relaxed);
    }

    // ---- audio thread -------------------------------------------------------------------------
    // Renders one block (n <= maxBlockSize). `segments` come from Transport::advance() and
    // cover the whole block; zero segments = stopped (tracks read no clips, but every insert
    // keeps running so reverb and delay tails ring out).
    // `playhead`: the transport's position, what the lanes read while stopped.
    //
    // Automation (chapter 16, Step 5): a fader comes after the inserts and the PDC delay, so the audio
    // reaching it is older than the transport's position, by the delay of its own path. Its lanes read
    // the curve that much earlier (LaneBlock::shift), so that a bounce with the latency compensated
    // has the jump exactly on the sample the curve says. Limit: at a loop wrap the last `shift` samples
    // before it are still on their way, but the lane has already wrapped.
    //
    // `input` (chapter 17, Step 6): the device's input for these same frames.
    //
    // `midi` (chapter 18, Step 7): this block's live MIDI, for the armed instrument tracks.
    // `map` (chapter 19): the tempo the segments' ticks were made with; nullptr: one tempo, `bpm`.
    void process(AudioBlock out, const TimelineSegment* segments, int numSegments, double bpm = 120.0,
                 int64_t playhead = 0, InputBlock input = {}, MidiBlock midi = {}, const TempoMap* map = nullptr) noexcept {
        const int n = out.getNumSamples();
        assert(prepared_ && n <= maxBlockSize_);
        const bool anySolo = isAnyTrackSoloed();
        PlayHead playHead;
        playHead.segments = segments;
        playHead.numSegments = numSegments;
        if (map == nullptr) {
            constantMap_->setConstant(bpm);
            map = constantMap_.get();
        }
        const TempoMap& tempo = *map;
        playHead.bpm = numSegments > 0 ? tempo.bpmAt(static_cast<double>(segments[0].tickFrom)) : bpm;

        LaneBlock lanes;
        lanes.segments = segments;
        lanes.numSegments = numSegments;
        lanes.playhead = playhead;
        lanes.capture = capture_;
        lanes.readOnly = lanesReadOnly_.load(std::memory_order_relaxed);
        float* laneGains = laneGains_.data();
        float* lanePans = lanePans_.data();

        AudioBlock sum = trackSum_.toBlock().getSubBlock(0, n);
        sum.clear();
        std::array<AudioBlock, kNumBuses> bus;
        for (int b = 0; b < kNumBuses; ++b) {
            bus[static_cast<size_t>(b)] = busBuffers_[static_cast<size_t>(b)].toBlock().getSubBlock(0, n);
            bus[static_cast<size_t>(b)].clear();
        }

        // What the monitors of this block send past the mix (Direct).
        AudioBlock direct = direct_.toBlock().getSubBlock(0, n);
        bool anyDirect = false;
        const int mixLatency = getTotalLatencySamples();

        // ---- tracks ----
        AudioBlock tb = scratch_.toBlock().getSubBlock(0, n);
        for (int slot = 0; slot < kMaxTracks; ++slot) {
            Track& track = tracks_[static_cast<size_t>(slot)];

            // This track's channels of the input. A channel the device does not have is silence.
            const int firstInput = track.getInputChannel();
            const int numTrackInputs = firstInput >= 0 ? track.getNumProcessingChannels() : 0;
            const float* trackInput[2] = {nullptr, nullptr};
            for (int ch = 0; ch < numTrackInputs; ++ch)
                if (firstInput + ch < input.numChannels)
                    trackInput[ch] = input.channels[firstInput + ch];
            track.getRecorder().process(trackInput, numTrackInputs, segments, numSegments, mixLatency);

            if (!trackInSlot_[static_cast<size_t>(slot)].isValid())
                continue; // a free slot costs little more than this
            tb.clear();
            track.renderClips(tb, segments, numSegments);
            track.renderInstrument(tb, segments, numSegments, tempo, midi); // chapter 18: nothing unless an instrument

            Monitor& monitor = track.getMonitor();
            if (!monitor.isSilent() && numTrackInputs > 0) {
                const bool toDirect = track.getMonitorPath() == MonitorPath::Direct;
                if (toDirect && !anyDirect) {
                    direct.clear();
                    anyDirect = true;
                }
                const AudioBlock& target = toDirect ? direct : tb;
                float* to[kNumChannels] = {target.getChannelPointer(0), target.getChannelPointer(1)};
                float* clips[kNumChannels] = {tb.getChannelPointer(0), tb.getChannelPointer(1)};
                monitor.process(trackInput, numTrackInputs, to, kNumChannels, clips, kNumChannels, segments, numSegments, n);
            }
            track.beginBlock(!track.isMuted() && (!anySolo || track.isSoloed()));
            track.getInserts().setPlayHead(playHead);
            const ChannelRef self = ChannelRef::track(slot, trackInSlot_[static_cast<size_t>(slot)]);
            lanes.shift = 0;
            track.getInserts().applyLanes(lanes, self, laneGains, n);
            if (track.getFormat() == TrackFormat::Mono) {
                // Mono track: fold the clips to one channel ((L+R)/2: exact for a mono file,
                // which the loader stores as L = R), run the inserts and the PDC delay on that
                // one channel, then copy it to the right side. From here on the strip is
                // stereo and the pan law places the mono source.
                float* l = tb.getChannelPointer(0);
                const float* r = tb.getChannelPointer(1);
                for (int i = 0; i < n; ++i)
                    l[i] = 0.5f * (l[i] + r[i]);
                track.processInserts(tb.getChannelSubset(0, 1));
                std::copy_n(l, n, tb.getChannelPointer(1));
            } else {
                track.processInserts(tb); // inserts + PDC delay on both channels
            }
            track.applyMuteSolo(tb);
            for (int b = 0; b < kNumBuses; ++b)
                if (track.isSendPreFader(b))
                    track.addSend(b, bus[static_cast<size_t>(b)], tb);
            lanes.shift = track.getLatencySamples() + track.getCompensator().getDelaySamples();
            track.applyFaderAndPan(tb, lanes, self, laneGains, lanePans);
            for (int b = 0; b < kNumBuses; ++b)
                if (!track.isSendPreFader(b))
                    track.addSend(b, bus[static_cast<size_t>(b)], tb);
            track.meter(tb);
            sum.addFrom(tb);
        }
        trackSumCompensator_.process(sum);

        // ---- buses and master ----
        AudioBlock mb = masterBuffer_.toBlock().getSubBlock(0, n);
        mb.copyFrom(sum);
        for (int b = 0; b < kNumBuses; ++b) {
            ChannelStrip& strip = buses_[static_cast<size_t>(b)];
            AudioBlock bb = bus[static_cast<size_t>(b)];
            strip.beginBlock(!strip.isMuted());
            strip.getInserts().setPlayHead(playHead);
            lanes.shift = 0;
            strip.getInserts().applyLanes(lanes, ChannelRef::bus(b), laneGains, n);
            strip.processInserts(bb);
            strip.applyMuteSolo(bb);
            lanes.shift = getMaxTrackLatencySamples() + strip.getInsertLatencySamples() + strip.getCompensator().getDelaySamples();
            strip.applyFaderAndPan(bb, lanes, ChannelRef::bus(b), laneGains, lanePans);
            strip.meter(bb);
            mb.addFrom(bb);
        }
        master_.beginBlock(!master_.isMuted());
        master_.getInserts().setPlayHead(playHead);
        lanes.shift = 0;
        master_.getInserts().applyLanes(lanes, ChannelRef::master(), laneGains, n);
        master_.processInserts(mb);
        master_.applyMuteSolo(mb);
        lanes.shift = getMaxTrackLatencySamples() + getMaxBusLatencySamples() + master_.getInsertLatencySamples();
        master_.applyFaderAndPan(mb, lanes, ChannelRef::master(), laneGains, lanePans);
        master_.meter(mb);

        out.clear();
        out.copyFrom(mb);
        if (anyDirect)
            out.addFrom(direct);
    }

private:
    bool holds(int slot, TrackId id) const noexcept {
        return slot >= 0 && slot < kMaxTracks && id.isValid() && trackInSlot_[static_cast<size_t>(slot)] == id;
    }

    double sampleRate_ = 0.0;
    int maxBlockSize_ = 0;
    bool prepared_ = false;

    std::array<Track, kMaxTracks> tracks_;
    std::array<TrackId, kMaxTracks> trackInSlot_{}; // audio thread only (see the header comment)
    std::array<ChannelStrip, kNumBuses> buses_;
    ChannelStrip master_;
    LatencyCompensator trackSumCompensator_;

    AudioBuffer scratch_, trackSum_, masterBuffer_, direct_;
    std::vector<float> laneGains_, lanePans_; // one block of a strip's lanes; used one strip at a time
    CaptureQueue* capture_ = nullptr;
    std::atomic<bool> lanesReadOnly_{false};
    std::array<AudioBuffer, kNumBuses> busBuffers_;

    std::atomic<bool> pdcEnabled_{true};
    std::atomic<int> totalLatency_{0};
    std::atomic<bool> lowLatencyMonitoring_{false};
    std::unique_ptr<TempoMap> constantMap_ = std::make_unique<TempoMap>(); // audio thread: the map of process() given a bpm only
    std::atomic<int> maxTrackLatency_{0};
    std::atomic<int> maxBusLatency_{0};
    std::atomic<bool> pdcOverflow_{false};
};

} // namespace dawlab::engine
