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
#include <vector>

#include "ChannelStrip.h"
#include "Clip.h"
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
    ~Track() { delete clips_.load(std::memory_order_relaxed); }

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

private:
    std::atomic<const std::vector<Clip>*> clips_{nullptr};
    std::atomic<int> format_{static_cast<int>(TrackFormat::Mono)}; // matches ChannelStrip's default PanLaw
};

} // namespace dawlab::engine
