// Step 4 (solution): strip gains, solo logic and the per-track signal flow.
//
// Build and run:  l14_step04_solution   (ctest, label "solution")
// Your version:   starter/step04.cpp -> l14_step04_starter

#include "../minidaw.h"

namespace minidaw {

void ChannelStrip::computeStripGains(float gainDb, float pan, dawlab::dsp::PanLaw law, PanMode mode, float& left,
                                     float& right) noexcept {
    float pl = 1.0f, pr = 1.0f;
    if (mode == PanMode::PanLaw) {
        dawlab::dsp::Panner::computeGains(pan, law, pl, pr); // chapter 01
    } else {
        const float p = std::clamp(pan, -1.0f, 1.0f); // balance: only attenuate the other side
        pl = std::min(1.0f, 1.0f - p);
        pr = std::min(1.0f, 1.0f + p);
    }
    const float g = gainDb > kMinusInfinityDb ? dawlab::dbToGain(gainDb) : 0.0f;
    left = g * pl;
    right = g * pr;
}

bool Mixer::isTrackAudible(int t) const noexcept {
    const Track& track = getTrack(t);
    return !track.isMuted() && (!isAnyTrackSoloed() || track.isSoloed());
}

void Mixer::processTrack(int t, AudioBlock tb, AudioBlock sum, const TimelineSegment* segments,
                         int numSegments) noexcept {
    Track& track = getTrack(t);
    track.renderClips(tb, segments, numSegments);
    track.beginBlock(isTrackAudible(t));
    track.applyMuteSolo(tb);
    track.applyFaderAndPan(tb);
    track.meter(tb);
    sum.addFrom(tb);
}

} // namespace minidaw

#include "../check.h"
