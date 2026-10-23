// Step 4 (starter): strip gains, solo logic and the per-track signal flow.
//
// Build and run:  l14_step04_starter   (prints [FAIL] until the three TODOs are done)
// Reference:      ../solution/step04.cpp, docs/step-04-mixer.md ("Your turn")
//
// ChannelStrip's stages (beginBlock, applyMuteSolo, applyFaderAndPan, meter), Track and
// Mixer::process() are in ../minidaw.h.

#include "../minidaw.h"

namespace minidaw {

void ChannelStrip::computeStripGains(float gainDb, float pan, dawlab::dsp::PanLaw law, PanMode mode, float& left,
                                     float& right) noexcept {
    // TODO 1: pan gains pl / pr - PanMode::PanLaw: dawlab::dsp::Panner::computeGains(pan, law, pl, pr)
    //         (chapter 01); PanMode::Balance: pl = min(1, 1 - pan), pr = min(1, 1 + pan).
    //         g = dbToGain(gainDb), but 0 when gainDb <= kMinusInfinityDb.  left = g*pl, right = g*pr.
    (void)gainDb;
    (void)pan;
    (void)law;
    (void)mode;
    left = 0.0f;
    right = 0.0f;
}

bool Mixer::isTrackAudible(int t) const noexcept {
    // TODO 2: audible = !mute && (!anySolo || solo)   (isAnyTrackSoloed(), getTrack(t))
    (void)t;
    return false;
}

void Mixer::processTrack(int t, AudioBlock tb, AudioBlock sum, const TimelineSegment* segments,
                         int numSegments) noexcept {
    // TODO 3: track.renderClips(tb, ...); track.beginBlock(isTrackAudible(t));
    //         track.applyMuteSolo(tb); track.applyFaderAndPan(tb); track.meter(tb); sum.addFrom(tb).
    (void)t;
    (void)tb;
    (void)sum;
    (void)segments;
    (void)numSegments;
}

} // namespace minidaw

#include "../check.h"
