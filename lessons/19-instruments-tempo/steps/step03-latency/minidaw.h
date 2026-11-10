#pragma once
// Chapter 19, Step 3: instrument latency.
//
// Chapter 18's synth answers a note in the same sample. Many instruments do not: a sampler that streams from the
// disk reads ahead, a physical model or a synth with an oversampled filter works on blocks of its own, and they
// report a LATENCY - their sound for a note comes that many samples after the note. Chapter 14 compensated the
// latency of effects (plugin delay compensation, PDC): every track is delayed so that all of them reach the mix
// together. An instrument's latency is the same kind of delay, and it joins the same plan:
//
//   a track's latency    L_t = its instrument's latency + its inserts' latency
//   the plan             total = the largest L_t; each track waits compensation_t = total - L_t more,
//                        so a note at sample S on any track is heard at S + total - all of them together.
//
// A song's notes come out right that way: the whole mix is late by `total`, the same for every track, and chapter
// 14's playhead shows where the sound is. What the plan cannot make right is a note played NOW on a keyboard:
//
//   heard live           key -> the scheduled delay of chapter 18's Step 5 (blocks and the output latency) ->
//                        the track's own L_t -> its compensation -> the output. Every sample of that is felt.
//   low-latency          a DAW lets an armed track skip the plan while the musician plays: its compensation is
//   monitoring           0, and it does not count for `total` - so a sampler that reads 4096 samples ahead does not
//                        delay the whole song by 85 ms. The armed track is then early by its compensation against
//                        the others, which nobody hears while playing along; when the take plays back, the plan is
//                        whole again.
//   recording            a recorded note belongs where the musician heard the SONG when they pressed the key
//                        (chapter 18, Step 6): the song comes out `total` + the output latency after it was rendered.
//                        The instrument's own latency does not move the note - it only makes the note feel late.
//
// No JUCE, no threads. Header-only: the starter and the solution define the functions marked YOUR TURN.

#include <algorithm>
#include <cstdint>
#include <vector>

namespace minidaw {

struct BlockEvent {
    int offset = 0;  // in the block
    bool noteOn = true;
};

struct LatencyTrack {
    int instrumentLatency = 0; // samples its instrument reports
    int insertLatency = 0;     // samples its effects report (chapter 14)
    bool armed = false;        // a musician plays it now
};

struct Plan {
    std::vector<int> compensation; // per track: how much more it waits
    int total = 0;                 // when a song's note at S is heard: S + total (+ the output latency)
};

// ---- provided -------------------------------------------------------------------------------------------------
// An instrument with a latency: for every note on, an impulse of 1.0 `latency` samples later (in this block or a
// later one), nothing else. Easy to find in a mix.
class LatentInstrument {
public:
    explicit LatentInstrument(int latency = 0) : latency_(latency) {}
    int latency() const noexcept { return latency_; }
    // ADDS the block's sound to out.
    void render(const std::vector<BlockEvent>& events, float* out, int numSamples) {
        for (const BlockEvent& e : events)
            if (e.noteOn)
                due_.push_back(rendered_ + e.offset + latency_);
        for (auto it = due_.begin(); it != due_.end();) {
            if (*it < rendered_ + numSamples) {
                out[*it - rendered_] += 1.0f;
                it = due_.erase(it);
            } else {
                ++it;
            }
        }
        rendered_ += numSamples;
    }

private:
    int latency_ = 0;
    std::int64_t rendered_ = 0;
    std::vector<std::int64_t> due_;
};

// A delay of a whole number of samples (an insert's latency, or a track's compensation), in place.
class DelayLine {
public:
    void prepare(int maxDelay) { buffer_.assign(static_cast<size_t>(std::max(maxDelay, 0)) + 1, 0.0f); }
    void setDelay(int samples) noexcept { delay_ = std::clamp(samples, 0, static_cast<int>(buffer_.size()) - 1); }
    int delay() const noexcept { return delay_; }
    void process(float* x, int numSamples) noexcept {
        const int size = static_cast<int>(buffer_.size());
        for (int i = 0; i < numSamples; ++i) {
            buffer_[static_cast<size_t>(write_)] = x[i];
            x[i] = buffer_[static_cast<size_t>((write_ - delay_ + size) % size)];
            write_ = (write_ + 1) % size;
        }
    }

private:
    std::vector<float> buffer_{0.0f};
    int delay_ = 0;
    int write_ = 0;
};

// ---- YOUR TURN (starter/step03.cpp) ----------------------------------------------------------------------------
// The plan. With low-latency monitoring, an armed track gets compensation 0 and does not count for `total`; every
// other track is compensated as usual. Without it, armed or not, every track counts.
Plan planLatency(const std::vector<LatencyTrack>& tracks, bool lowLatencyMonitoring);
// Samples from a key going down to the musician hearing it on `track`: the keyboard's scheduled delay, the track's
// latency, its compensation, and the output latency.
int heardLive(const std::vector<LatencyTrack>& tracks, const Plan& plan, int track, int scheduledDelay, int outputLatency);
// Where a note belongs whose key went down while the engine was rendering timeline sample `renderedSample`: what the
// musician heard of the song at that moment had been rendered total + output latency samples earlier.
std::int64_t placeRecorded(std::int64_t renderedSample, const Plan& plan, int outputLatency);

class Mix {
public:
    static constexpr int kMaxDelay = 1 << 16;

    // ---- YOUR TURN --------------------------------------------------------------------------------------------
    // The tracks (a LatentInstrument with each one's latency, an insert delay of its insertLatency) and their
    // compensation from planLatency(). Allocates: not on the audio thread.
    void setTracks(const std::vector<LatencyTrack>& tracks, bool lowLatencyMonitoring);
    // One block: every track's instrument plays its events, its inserts delay it, its compensation delays it more,
    // and the tracks are summed into out (cleared first). events[t] are track t's.
    void render(const std::vector<std::vector<BlockEvent>>& events, float* out, int numSamples);

    // ---- provided ---------------------------------------------------------------------------------------------
    const Plan& plan() const noexcept { return plan_; }

private:
    struct Strip {
        LatentInstrument instrument;
        DelayLine inserts, compensation;
    };
    std::vector<Strip> strips_;
    std::vector<float> scratch_;
    Plan plan_;
};

} // namespace minidaw
