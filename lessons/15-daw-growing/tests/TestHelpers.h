#pragma once
// Small helpers shared by the chapter-14 engine tests.

#include <dawlab/Common.h>
#include <dawlab/engine/All.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace l14test {

using namespace dawlab;
using namespace dawlab::engine;

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

// A mono buffer filled with a constant ("DC"). Handy because every gain stage then shows up
// directly as a sample value: out = dc * g.
inline std::shared_ptr<const AudioBuffer> makeConstant(float value, int numSamples) {
    auto b = std::make_shared<AudioBuffer>(1, numSamples);
    for (int i = 0; i < numSamples; ++i)
        b->setSample(0, i, value);
    return b;
}

// A mono buffer with a single impulse.
inline std::shared_ptr<const AudioBuffer> makeImpulseClip(int numSamples, int position, float amplitude = 1.0f) {
    return std::make_shared<AudioBuffer>(makeImpulse(numSamples, 1, position, amplitude));
}

// Index of the largest |sample| in a channel.
inline int argMaxAbs(const AudioBuffer& b, int ch) {
    int best = 0;
    float bestV = -1.0f;
    for (int i = 0; i < b.getNumSamples(); ++i) {
        const float v = std::abs(b.getSample(ch, i));
        if (v > bestV) {
            bestV = v;
            best = i;
        }
    }
    return best;
}

// Number of samples in a channel with |x| > threshold.
inline int countAbove(const AudioBuffer& b, int ch, float threshold) {
    int n = 0;
    for (int i = 0; i < b.getNumSamples(); ++i)
        if (std::abs(b.getSample(ch, i)) > threshold)
            ++n;
    return n;
}

inline float maxAbsDiff(const AudioBuffer& a, const AudioBuffer& b) {
    float d = 0.0f;
    const int nch = std::min(a.getNumChannels(), b.getNumChannels());
    const int n = std::min(a.getNumSamples(), b.getNumSamples());
    for (int ch = 0; ch < nch; ++ch)
        for (int i = 0; i < n; ++i)
            d = std::max(d, std::abs(a.getSample(ch, i) - b.getSample(ch, i)));
    return d;
}

// Renders `numSamples` from timeline 0 without latency compensation.
inline AudioBuffer render(Engine& engine, int64_t numSamples, int blockSize = kBlockSize, bool compensate = false) {
    RenderOptions o;
    o.numSamples = numSamples;
    o.blockSize = blockSize;
    o.compensateLatency = compensate;
    return renderOffline(engine, o);
}

} // namespace l14test
