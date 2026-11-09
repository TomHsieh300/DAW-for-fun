#pragma once
// Track: a ChannelStrip that reads audio from clips on the timeline (Step 3).
//
// The clip list is an immutable std::vector<Clip> owned by the track through a raw pointer.
// The UI never edits it in place: it builds a *new* vector, hands it to the engine with a
// ReplaceClips command, and the audio thread swaps the pointer (exchangeClips). The old
// vector goes into the garbage queue and is deleted by the UI thread. Readers (the audio
// thread) therefore never see a half-edited list and never block.

#include <dawlab/AudioBuffer.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "ChannelStrip.h"
#include "Clip.h"
#include "Midi.h"
#include "Recording.h"
#include "Transport.h"

namespace dawlab::engine {

// Channel format of a track, like the mono and stereo audio tracks of Cubase (Step 4):
//
//   Mono    clips -> (L+R)/2 -> inserts on ONE channel -> PDC -> copy to L and R -> pan law
//           A mono source is positioned in the stereo field: centre = -3 dB per side
//           (constant power), hard left = L only.
//   Stereo  clips -> inserts on TWO channels -> PDC -> balance
//           The image already exists; the pan control only turns one side down:
//           centre = unity on both sides, right = left side attenuated.
//
// The mixer after the panner is always stereo (sends, buses, master).
enum class TrackFormat { Mono, Stereo };

inline const char* trackFormatName(TrackFormat f) noexcept { return f == TrackFormat::Mono ? "Mono" : "Stereo"; }

class Track : public ChannelStrip {
public:
    Track() = default;
    ~Track() {
        delete clips_.load(std::memory_order_relaxed);
        delete midiClips_.load(std::memory_order_relaxed);
    }

    // Any thread (an atomic, read once per block by the audio thread). Also selects the pan
    // mode that belongs to the format: Mono -> PanMode::PanLaw, Stereo -> PanMode::Balance.
    // setPanMode() can still override it afterwards (exercises do).
    void setFormat(TrackFormat format) noexcept {
        format_.store(static_cast<int>(format), std::memory_order_relaxed);
        setPanMode(format == TrackFormat::Mono ? PanMode::PanLaw : PanMode::Balance);
    }
    TrackFormat getFormat() const noexcept { return static_cast<TrackFormat>(format_.load(std::memory_order_relaxed)); }
    int getNumProcessingChannels() const noexcept { return getFormat() == TrackFormat::Mono ? 1 : 2; }

    // Audio thread (via Engine command) or setup code. Returns the previous list, which the
    // caller must delete on a non-audio thread (the Engine pushes it to the garbage queue).
    const std::vector<Clip>* exchangeClips(const std::vector<Clip>* newClips) noexcept {
        return clips_.exchange(newClips, std::memory_order_acq_rel);
    }

    // Any thread may *look* (e.g. the UI drawing the arrangement), but the pointer is only
    // guaranteed to stay alive until the next ReplaceClips command is collected.
    const std::vector<Clip>* getClips() const noexcept { return clips_.load(std::memory_order_acquire); }
    int getNumClips() const noexcept {
        const auto* c = getClips();
        return c ? static_cast<int>(c->size()) : 0;
    }

    // Timeline end of the last clip (0 when empty). Useful for "render whole song".
    int64_t getEndSample() const noexcept {
        int64_t end = 0;
        if (const auto* c = getClips())
            for (const Clip& clip : *c)
                end = std::max(end, clip.end());
        return end;
    }

    // Audio thread: adds every clip that overlaps each timeline segment into `dest`.
    // Sample-accurate: a clip starting at timeline sample S lands exactly at S.
    void renderClips(AudioBlock dest, const TimelineSegment* segments, int numSegments) noexcept {
        const auto* clips = getClips();
        if (clips == nullptr || clips->empty())
            return;
        for (int s = 0; s < numSegments; ++s) {
            const TimelineSegment& seg = segments[s];
            const AudioBlock sub = dest.getSubBlock(seg.blockOffset, seg.numSamples);
            for (const Clip& clip : *clips)
                renderClip(clip, seg.timelineStart, sub);
        }
    }

    // ---- recording (chapter 17, Step 6; every slot has these, used or not) -------------------------
    // Armed: the next Engine::startRecording() records this track. Any thread.
    void setArmed(bool armed) noexcept { armed_.store(armed, std::memory_order_relaxed); }
    bool isArmed() const noexcept { return armed_.load(std::memory_order_relaxed); }
    // The first channel of the device's input for this track (a stereo track also takes the next one);
    // -1: none. Any thread.
    void setInputChannel(int firstChannel) noexcept { inputChannel_.store(firstChannel, std::memory_order_relaxed); }
    int getInputChannel() const noexcept { return inputChannel_.load(std::memory_order_relaxed); }
    // What the musician hears, and by which way. Any thread.
    void setMonitorPath(MonitorPath path) noexcept { monitorPath_.store(path, std::memory_order_relaxed); }
    MonitorPath getMonitorPath() const noexcept { return monitorPath_.load(std::memory_order_relaxed); }
    Monitor& getMonitor() noexcept { return monitor_; }
    const Monitor& getMonitor() const noexcept { return monitor_; }
    Recorder& getRecorder() noexcept { return recorder_; }
    const Recorder& getRecorder() const noexcept { return recorder_; }
    // UI thread, for a slot that starts over.
    void resetRecordingValues() noexcept {
        setArmed(false);
        setInputChannel(0);
        setMonitorPath(MonitorPath::Direct);
        monitor_.setMode(MonitorMode::Off);
        monitor_.setLevel(1.0f);
        monitor_.setRange(RecordRange{});
    }

    // ---- instruments (chapter 18, Step 7; every slot has these, used or not) -----------------------------
    // An instrument track plays its MIDI clips, and when armed the live MIDI of processBlock(), through its
    // synth; the synth's sound is added to the track like a clip's, before the inserts. Its audio clips still
    // play. Any thread (an atomic, read once per block). Turning it off silences the synth at once.
    void setInstrument(bool instrument) noexcept { instrument_.store(instrument, std::memory_order_relaxed); }
    bool isInstrument() const noexcept { return instrument_.load(std::memory_order_relaxed); }

    // As exchangeClips(), for the MIDI clips (Engine command ReplaceMidiClips).
    const std::vector<MidiClip>* exchangeMidiClips(const std::vector<MidiClip>* newClips) noexcept {
        return midiClips_.exchange(newClips, std::memory_order_acq_rel);
    }
    const std::vector<MidiClip>* getMidiClips() const noexcept { return midiClips_.load(std::memory_order_acquire); }

    // Setup (Mixer::prepare): the synth at this rate, room for a block's events and the synth's sound. The
    // instrument lives on the heap: 8 KB a track, 128 tracks, and an Engine on the stack would not fit.
    void prepareInstrument(double sampleRate, int maxBlockSize) {
        if (state_ == nullptr)
            state_ = std::make_unique<InstrumentState>();
        state_->synth.prepare(sampleRate, VoiceSettings{});
        state_->player.reset();
        state_->events.reserve(kMaxBlockEvents);
        state_->merged.reserve(kMaxBlockEvents);
        state_->out.assign(static_cast<size_t>(std::max(1, maxBlockSize)), 0.0f);
        state_->sampleRate = sampleRate;
    }
    // Audio thread: silent at once, nothing remembered (the track is gone, or the engine was reset).
    void resetInstrument() noexcept {
        if (state_ == nullptr)
            return;
        state_->synth.reset();
        state_->player.reset();
        state_->wasOn = false;
    }

    // Audio thread: the clips' events and (when armed) the live ones, merged in order of offset, through the
    // synth, ADDED to both channels of dest.
    void renderInstrument(AudioBlock dest, const TimelineSegment* segments, int numSegments, double bpm, MidiBlock live) noexcept {
        if (state_ == nullptr)
            return; // not prepared
        InstrumentState& in = *state_;
        if (!isInstrument()) {
            if (in.wasOn)
                resetInstrument();
            return;
        }
        in.wasOn = true;
        const int n = dest.getNumSamples();
        in.events.clear();
        in.player.process(getMidiClips(), segments, numSegments, in.sampleRate, bpm, in.events);
        // The clips' events are in order within each segment, and the segments follow each other; the live
        // ones are in order. Merge: at one offset the clips' first.
        in.merged.clear();
        const int numLive = isArmed() ? live.numEvents : 0;
        size_t a = 0;
        int b = 0;
        while ((a < in.events.size() || b < numLive) && in.merged.size() < in.merged.capacity()) {
            if (b >= numLive || (a < in.events.size() && in.events[a].offset <= live.events[b].offset))
                in.merged.push_back(in.events[a++]);
            else
                in.merged.push_back(live.events[b++]);
        }
        float* mono = in.out.data();
        std::fill(mono, mono + n, 0.0f);
        in.synth.render(in.merged.data(), static_cast<int>(in.merged.size()), mono, n);
        for (int ch = 0; ch < dest.getNumChannels(); ++ch) {
            float* d = dest.getChannelPointer(ch);
            for (int i = 0; i < n; ++i)
                d[i] += mono[i];
        }
    }
    // For tests and meters; nullptr before prepare().
    const Synth* getSynth() const noexcept { return state_ != nullptr ? &state_->synth : nullptr; }
    const ClipPlayer* getClipPlayer() const noexcept { return state_ != nullptr ? &state_->player : nullptr; }

private:
    static constexpr int kMaxBlockEvents = 4096;
    struct InstrumentState {
        ClipPlayer player;
        Synth synth;
        std::vector<BlockEvent> events, merged;
        std::vector<float> out;
        double sampleRate = 48000.0;
        bool wasOn = false; // audio thread only
    };
    std::atomic<bool> instrument_{false};
    std::atomic<const std::vector<MidiClip>*> midiClips_{nullptr};
    std::unique_ptr<InstrumentState> state_;

    std::atomic<bool> armed_{false};
    std::atomic<int> inputChannel_{0};
    std::atomic<MonitorPath> monitorPath_{MonitorPath::Direct};
    Monitor monitor_;
    Recorder recorder_;
    std::atomic<const std::vector<Clip>*> clips_{nullptr};
    std::atomic<int> format_{static_cast<int>(TrackFormat::Mono)}; // matches ChannelStrip's default PanLaw
};

} // namespace dawlab::engine
