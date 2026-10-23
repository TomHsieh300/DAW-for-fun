#pragma once
// Mixer: 8 tracks, 2 FX buses, 1 master, and the signal flow between them (Steps 4, 6, 8).
//
//   track 1 --+-- post/pre-fader sends ---------> bus A (e.g. reverb) --+
//   track 2 --+                                   bus B (e.g. delay) ---+
//     ...     |                                                         |
//   track 8 --+--> track sum --> [PDC: maxBusLatency] --------------------+--> master --> out
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

#include <dawlab/AudioBuffer.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <string>

#include "ChannelStrip.h"
#include "Commands.h"
#include "Constants.h"
#include "Track.h"
#include "Transport.h"

namespace dawlab::engine {

class Mixer {
public:
    Mixer() {
        for (int t = 0; t < kNumTracks; ++t)
            tracks_[static_cast<size_t>(t)].setName("Track " + std::to_string(t + 1));
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
        for (auto& t : tracks_)
            t.prepare(sampleRate, maxBlockSize_, maxPdcSamples);
        for (auto& b : buses_)
            b.prepare(sampleRate, maxBlockSize_, maxPdcSamples);
        master_.prepare(sampleRate, maxBlockSize_, maxPdcSamples);
        trackSumCompensator_.prepare(maxPdcSamples, kNumChannels);
        scratch_.setSize(kNumChannels, maxBlockSize_);
        trackSum_.setSize(kNumChannels, maxBlockSize_);
        masterBuffer_.setSize(kNumChannels, maxBlockSize_);
        for (auto& b : busBuffers_)
            b.setSize(kNumChannels, maxBlockSize_);
        prepared_ = true;
        reset();
    }

    // Audio thread (or setup code).
    void reset() noexcept {
        for (auto& t : tracks_)
            t.reset();
        for (auto& b : buses_)
            b.reset();
        master_.reset();
        trackSumCompensator_.reset();
    }

    // ---- access --------------------------------------------------------------------------------
    Track& getTrack(int i) noexcept { return tracks_[static_cast<size_t>(std::clamp(i, 0, kNumTracks - 1))]; }
    const Track& getTrack(int i) const noexcept {
        return tracks_[static_cast<size_t>(std::clamp(i, 0, kNumTracks - 1))];
    }
    ChannelStrip& getBus(int i) noexcept { return buses_[static_cast<size_t>(std::clamp(i, 0, kNumBuses - 1))]; }
    const ChannelStrip& getBus(int i) const noexcept {
        return buses_[static_cast<size_t>(std::clamp(i, 0, kNumBuses - 1))];
    }
    ChannelStrip& getMaster() noexcept { return master_; }
    const ChannelStrip& getMaster() const noexcept { return master_; }

    // nullptr when the reference is out of range.
    ChannelStrip* getStrip(ChannelRef ref) noexcept {
        return const_cast<ChannelStrip*>(static_cast<const Mixer*>(this)->getStrip(ref));
    }
    const ChannelStrip* getStrip(ChannelRef ref) const noexcept {
        switch (ref.kind) {
        case ChannelKind::Track:
            return (ref.index >= 0 && ref.index < kNumTracks) ? &tracks_[static_cast<size_t>(ref.index)] : nullptr;
        case ChannelKind::Bus:
            return (ref.index >= 0 && ref.index < kNumBuses) ? &buses_[static_cast<size_t>(ref.index)] : nullptr;
        case ChannelKind::Master: return &master_;
        }
        return nullptr;
    }

    double getSampleRate() const noexcept { return sampleRate_; }
    int getMaxBlockSize() const noexcept { return maxBlockSize_; }
    bool isPrepared() const noexcept { return prepared_; }

    bool isAnyTrackSoloed() const noexcept {
        for (const auto& t : tracks_)
            if (t.isSoloed())
                return true;
        return false;
    }

    // Is track t heard right now?  audible = !mute && (!anySolo || solo)
    bool isTrackAudible(int t) const noexcept {
        const Track& tr = getTrack(t);
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
        int maxTrack = 0;
        for (const auto& t : tracks_)
            maxTrack = std::max(maxTrack, t.getInsertLatencySamples());
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
            t.getCompensator().setDelaySamples(enabled ? targetTrack - t.getInsertLatencySamples() : 0);
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
    void process(AudioBlock out, const TimelineSegment* segments, int numSegments, double bpm = 120.0) noexcept {
        const int n = out.getNumSamples();
        assert(prepared_ && n <= maxBlockSize_);
        const bool anySolo = isAnyTrackSoloed();
        PlayHead playHead;
        playHead.segments = segments;
        playHead.numSegments = numSegments;
        playHead.bpm = bpm;

        AudioBlock sum = trackSum_.toBlock().getSubBlock(0, n);
        sum.clear();
        std::array<AudioBlock, kNumBuses> bus;
        for (int b = 0; b < kNumBuses; ++b) {
            bus[static_cast<size_t>(b)] = busBuffers_[static_cast<size_t>(b)].toBlock().getSubBlock(0, n);
            bus[static_cast<size_t>(b)].clear();
        }

        // ---- tracks ----
        AudioBlock tb = scratch_.toBlock().getSubBlock(0, n);
        for (auto& track : tracks_) {
            tb.clear();
            track.renderClips(tb, segments, numSegments);
            track.beginBlock(!track.isMuted() && (!anySolo || track.isSoloed()));
            track.getInserts().setPlayHead(playHead);
            track.processInserts(tb); // inserts + PDC delay
            track.applyMuteSolo(tb);
            for (int b = 0; b < kNumBuses; ++b)
                if (track.isSendPreFader(b))
                    track.addSend(b, bus[static_cast<size_t>(b)], tb);
            track.applyFaderAndPan(tb);
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
            strip.processInserts(bb);
            strip.applyMuteSolo(bb);
            strip.applyFaderAndPan(bb);
            strip.meter(bb);
            mb.addFrom(bb);
        }
        master_.beginBlock(!master_.isMuted());
        master_.getInserts().setPlayHead(playHead);
        master_.processInserts(mb);
        master_.applyMuteSolo(mb);
        master_.applyFaderAndPan(mb);
        master_.meter(mb);

        out.clear();
        out.copyFrom(mb);
    }

private:
    double sampleRate_ = 0.0;
    int maxBlockSize_ = 0;
    bool prepared_ = false;

    std::array<Track, kNumTracks> tracks_;
    std::array<ChannelStrip, kNumBuses> buses_;
    ChannelStrip master_;
    LatencyCompensator trackSumCompensator_;

    AudioBuffer scratch_, trackSum_, masterBuffer_;
    std::array<AudioBuffer, kNumBuses> busBuffers_;

    std::atomic<bool> pdcEnabled_{true};
    std::atomic<int> totalLatency_{0};
    std::atomic<int> maxTrackLatency_{0};
    std::atomic<int> maxBusLatency_{0};
    std::atomic<bool> pdcOverflow_{false};
};

} // namespace dawlab::engine
