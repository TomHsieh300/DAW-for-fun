// Chapter 19, Step 3 (starter): instrument latency.
//
// Fill in the TODOs. Until then the self-check prints FAIL (expected). The solution is
// solution/step03.cpp. What the functions must do is in ../minidaw.h.

#include "../minidaw.h"

namespace minidaw {

Plan planLatency(const std::vector<LatencyTrack>& tracks, bool lowLatencyMonitoring) {
    // TODO: total = the largest instrument + insert latency (leaving out armed tracks with low-latency monitoring);
    //       each track's compensation = total - its latency (0 for the ones left out).
    (void)lowLatencyMonitoring;
    Plan plan;
    plan.compensation.assign(tracks.size(), 0);
    return plan;
}

int heardLive(const std::vector<LatencyTrack>& tracks, const Plan& plan, int track, int scheduledDelay, int outputLatency) {
    // TODO: the scheduled delay + the track's latency + its compensation + the output latency.
    (void)tracks;
    (void)plan;
    (void)track;
    return scheduledDelay + outputLatency;
}

std::int64_t placeRecorded(std::int64_t renderedSample, const Plan& plan, int outputLatency) {
    // TODO: what the musician heard was rendered total + output latency earlier.
    (void)plan;
    (void)outputLatency;
    return renderedSample;
}

void Mix::setTracks(const std::vector<LatencyTrack>& tracks, bool lowLatencyMonitoring) {
    // TODO: the plan; a strip per track with its instrument, its insert delay and its compensation delay.
    plan_ = planLatency(tracks, lowLatencyMonitoring);
    strips_.clear();
    for (const LatencyTrack& t : tracks) {
        Strip s{LatentInstrument(t.instrumentLatency), {}, {}};
        strips_.push_back(std::move(s));
    }
}

void Mix::render(const std::vector<std::vector<BlockEvent>>& events, float* out, int numSamples) {
    // TODO: per track the instrument, then its insert delay and its compensation, summed into out.
    std::fill(out, out + numSamples, 0.0f);
    for (size_t t = 0; t < strips_.size() && t < events.size(); ++t)
        strips_[t].instrument.render(events[t], out, numSamples);
}

} // namespace minidaw

#include "../check.h"
