// Step 6 (solution): the send and the two send taps of a track.
//
// Build and run:  l14_step06_solution   (ctest, label "solution")
// Your version:   starter/step06.cpp -> l14_step06_starter

#include "../minidaw.h"

namespace minidaw {

void ChannelStrip::addSend(int bus, AudioBlock dest, const AudioBlock& src) noexcept {
    if (!validBus(bus))
        return;
    dawlab::dsp::SmoothedValue& gain = sendGain_[static_cast<size_t>(bus)];
    const int n = std::min(dest.getNumSamples(), src.getNumSamples());
    const int nch = std::min(dest.getNumChannels(), src.getNumChannels());
    for (int i = 0; i < n; ++i) { // advance the ramp every sample, even at gain 0
        const float g = gain.getNextValue();
        for (int ch = 0; ch < nch; ++ch)
            dest.getChannelPointer(ch)[i] += src.getChannelPointer(ch)[i] * g;
    }
}

void Mixer::processTrack(int t, AudioBlock tb, AudioBlock sum, const std::array<AudioBlock, kNumBuses>& bus,
                         const TimelineSegment* segments, int numSegments) noexcept {
    Track& track = getTrack(t);
    track.renderClips(tb, segments, numSegments);
    track.beginBlock(isTrackAudible(t));
    track.processInserts(tb);
    track.applyMuteSolo(tb); // before both taps: a muted track sends nothing
    for (int b = 0; b < kNumBuses; ++b)
        if (track.isSendPreFader(b))
            track.addSend(b, bus[static_cast<size_t>(b)], tb); // pre-fader tap
    track.applyFaderAndPan(tb);
    for (int b = 0; b < kNumBuses; ++b)
        if (!track.isSendPreFader(b))
            track.addSend(b, bus[static_cast<size_t>(b)], tb); // post-fader tap
    track.meter(tb);
    sum.addFrom(tb);
}

} // namespace minidaw

#include "../check.h"
