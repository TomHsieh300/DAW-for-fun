#pragma once
// Sidechain: the sample loop that Gate and Expander share.
//
// Both detect from a sidechain (or from the signal itself, when the sidechain is the main block)
// and multiply every main channel by the gain the detector asks for. What differs is only how the
// gain is computed, so that part comes in as a function: nextGain(float detector) -> float gain.

#include <dawlab/AudioBuffer.h>

#include <algorithm>
#include <cmath>

namespace dawlab::dsp {

// For each sample: the detector value is max|x| across the sidechain's channels (stereo-link),
// nextGain(detector) returns the gain, and every channel of main is multiplied by it. The
// sidechain may have a different number of channels from main; the number of samples is the
// smaller of the two.
template <class NextGain>
void applySidechainGain(AudioBlock main, const AudioBlock& sidechain, NextGain&& nextGain) noexcept {
    const int n = std::min(main.getNumSamples(), sidechain.getNumSamples());
    const int nMain = main.getNumChannels();
    const int nSc = sidechain.getNumChannels();
    for (int i = 0; i < n; ++i) {
        float det = 0.0f;
        for (int ch = 0; ch < nSc; ++ch)
            det = std::max(det, std::abs(sidechain.getChannelPointer(ch)[i]));
        const float g = nextGain(det);
        for (int ch = 0; ch < nMain; ++ch)
            main.getChannelPointer(ch)[i] *= g;
    }
}

} // namespace dawlab::dsp
