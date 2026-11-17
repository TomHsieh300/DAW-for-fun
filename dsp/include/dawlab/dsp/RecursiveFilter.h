#pragma once
// RecursiveFilter: what OnePole, Biquad and StateVariableFilter share.
//
// Each of them keeps one small state per channel and computes one sample at a time, so the block
// loop and the denormal flush of the state are the same in all three. A filter inherits this
// privately and keeps its own processSample():
//
//     class OnePole : private RecursiveFilter {
//         ...
//         void process(AudioBlock block) noexcept { processChannels(*this, block, numStates()); }
//     };

#include <dawlab/AudioBuffer.h>

#include <algorithm>
#include <cmath>

namespace dawlab::dsp {

class RecursiveFilter {
protected:
    // The state of a recursive structure approaches 0 forever without ever reaching it, and
    // eventually falls into the denormal range (the CPU becomes very slow). Anything below
    // 1e-30 (about -600 dBFS) is cleared to 0.
    static double flushDenormal(double v) noexcept { return std::abs(v) < 1.0e-30 ? 0.0 : v; }

    // Runs filter.processSample(ch, x) over the block, channel by channel, for the channels that
    // have a state (numStates); any further channels of the block pass through unchanged.
    template <class Filter>
    static void processChannels(Filter& filter, AudioBlock block, int numStates) noexcept {
        const int numChannels = std::min(block.getNumChannels(), numStates);
        for (int ch = 0; ch < numChannels; ++ch) {
            float* d = block.getChannelPointer(ch);
            for (int i = 0; i < block.getNumSamples(); ++i)
                d[i] = filter.processSample(ch, d[i]);
        }
    }
};

} // namespace dawlab::dsp
