// Chapter 19, Step 3 (solution): instrument latency.
//
// Build and run:  l19_step03_solution   (ctest, label "solution")
// Your version:   starter/step03.cpp -> l19_step03_starter

#include "../minidaw.h"

namespace minidaw {

namespace {

int latencyOf(const LatencyTrack& t) noexcept {
    return t.instrumentLatency + t.insertLatency;
}

bool skipsThePlan(const LatencyTrack& t, bool lowLatencyMonitoring) noexcept {
    return lowLatencyMonitoring && t.armed;
}

} // namespace

Plan planLatency(const std::vector<LatencyTrack>& tracks, bool lowLatencyMonitoring) {
    Plan plan;
    for (const LatencyTrack& t : tracks)
        if (!skipsThePlan(t, lowLatencyMonitoring))
            plan.total = std::max(plan.total, latencyOf(t));
    for (const LatencyTrack& t : tracks)
        plan.compensation.push_back(skipsThePlan(t, lowLatencyMonitoring) ? 0 : plan.total - latencyOf(t));
    return plan;
}

int heardLive(const std::vector<LatencyTrack>& tracks, const Plan& plan, int track, int scheduledDelay, int outputLatency) {
    const size_t t = static_cast<size_t>(track);
    return scheduledDelay + latencyOf(tracks[t]) + plan.compensation[t] + outputLatency;
}

std::int64_t placeRecorded(std::int64_t renderedSample, const Plan& plan, int outputLatency) {
    return renderedSample - plan.total - outputLatency;
}

void Mix::setTracks(const std::vector<LatencyTrack>& tracks, bool lowLatencyMonitoring) {
    plan_ = planLatency(tracks, lowLatencyMonitoring);
    strips_.clear();
    for (size_t t = 0; t < tracks.size(); ++t) {
        Strip s{LatentInstrument(tracks[t].instrumentLatency), {}, {}};
        s.inserts.prepare(kMaxDelay);
        s.inserts.setDelay(tracks[t].insertLatency);
        s.compensation.prepare(kMaxDelay);
        s.compensation.setDelay(plan_.compensation[t]);
        strips_.push_back(std::move(s));
    }
}

void Mix::render(const std::vector<std::vector<BlockEvent>>& events, float* out, int numSamples) {
    std::fill(out, out + numSamples, 0.0f);
    scratch_.resize(static_cast<size_t>(numSamples));
    for (size_t t = 0; t < strips_.size(); ++t) {
        float* x = scratch_.data();
        std::fill(x, x + numSamples, 0.0f);
        strips_[t].instrument.render(t < events.size() ? events[t] : std::vector<BlockEvent>{}, x, numSamples);
        strips_[t].inserts.process(x, numSamples);
        strips_[t].compensation.process(x, numSamples);
        for (int i = 0; i < numSamples; ++i)
            out[i] += x[i];
    }
}

} // namespace minidaw

#include "../check.h"
