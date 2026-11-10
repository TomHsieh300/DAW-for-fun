#pragma once
// Clip: a region of audio placed on the timeline, and the sample-accurate renderer for it
// (Step 3).
//
//   timeline:   0 ........ start ......................... start+length ........
//   source:               [sourceOffset ............. sourceOffset+length)
//
// A clip does not own its audio; it points at a shared, immutable AudioBuffer. Several clips
// may reference the same buffer (for example one drum loop repeated eight times). The
// shared_ptr is only copied on the UI thread (while building a clip list); the audio thread
// only reads through it, which costs nothing and never touches the reference count.
//
// Gain of clip sample i (0-based inside the clip):
//
//   g(i) = clipGain * fadeIn(i) * fadeOut(i)
//   fadeIn(i)  = shape((i + 1) / (F_in + 1))        for i < F_in, else 1
//   fadeOut(i) = shape((r + 1) / (F_out + 1))       r = length - 1 - i, for r < F_out, else 1
//
//   shape(t) = t                    (Linear)
//   shape(t) = sin(t * pi / 2)      (EqualPower)
//
// With that definition a fade-out and a fade-in of the same length F that overlap exactly
// complement each other: at overlap index k the fade-out sees t_out = (F - k) / (F + 1) and
// the fade-in sees t_in = (k + 1) / (F + 1), so t_in + t_out = 1. Linear crossfades therefore
// sum to exactly 1 (right for correlated material), EqualPower crossfades satisfy
// sin^2 + cos^2 = 1 (constant power, right for uncorrelated material). Exercise 5 uses this.

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>

namespace dawlab::engine {

enum class FadeShape { Linear, EqualPower };

struct Clip {
    std::shared_ptr<const AudioBuffer> source; // immutable audio data (mono or stereo)
    int64_t start = 0;                         // timeline position of the first clip sample
    int64_t length = 0;                        // number of timeline samples the clip covers
    int64_t sourceOffset = 0;                  // first source sample used
    float gainDb = 0.0f;                       // clip gain
    int fadeInSamples = 0;
    int fadeOutSamples = 0;
    FadeShape fadeShape = FadeShape::Linear;
    bool muted = false;
    std::string name; // UI only; the audio thread never reads it

    int64_t end() const noexcept { return start + length; }

    // A clip that plays the whole buffer, starting at timeline sample `startSample`.
    static Clip fromBuffer(std::shared_ptr<const AudioBuffer> buffer, int64_t startSample, std::string clipName = {}) {
        Clip c;
        c.length = buffer ? buffer->getNumSamples() : 0;
        c.source = std::move(buffer);
        c.start = startSample;
        c.name = std::move(clipName);
        return c;
    }
};

inline float fadeGain(FadeShape shape, float t) noexcept { // t in [0, 1]
    return shape == FadeShape::EqualPower ? std::sin(t * static_cast<float>(kPi) * 0.5f) : t;
}

// Fade gain of clip sample i (0-based inside the clip). Clip gain is not included.
inline float clipFadeAt(const Clip& clip, int64_t i) noexcept {
    float g = 1.0f;
    if (clip.fadeInSamples > 0 && i < clip.fadeInSamples)
        g *= fadeGain(clip.fadeShape, static_cast<float>(i + 1) / static_cast<float>(clip.fadeInSamples + 1));
    const int64_t r = clip.length - 1 - i;
    if (clip.fadeOutSamples > 0 && r < clip.fadeOutSamples)
        g *= fadeGain(clip.fadeShape, static_cast<float>(r + 1) / static_cast<float>(clip.fadeOutSamples + 1));
    return g;
}

// Adds the part of `clip` that overlaps the timeline range
// [timelineStart, timelineStart + dest.getNumSamples()) into `dest`.
// Mono sources feed every destination channel. Real-time safe: no allocation, no locks.
//
// Sample accuracy: timeline sample T lands at dest index (T - timelineStart), exactly. There
// is no rounding anywhere, because clip positions and the transport are both integers.
inline void renderClip(const Clip& clip, int64_t timelineStart, AudioBlock dest) noexcept {
    if (clip.muted || !clip.source || clip.length <= 0 || clip.source->getNumChannels() <= 0)
        return;
    const AudioBuffer& src = *clip.source;
    const int64_t blockEnd = timelineStart + dest.getNumSamples();
    const int64_t from = std::max(timelineStart, clip.start); // overlap of block and clip
    const int64_t to = std::min(blockEnd, clip.end());
    if (to <= from)
        return;

    const float gain = dbToGain(clip.gainDb);
    const int n = static_cast<int>(to - from);
    const int destOffset = static_cast<int>(from - timelineStart);
    const int64_t clipIndex0 = from - clip.start;             // index inside the clip
    const int64_t srcIndex0 = clip.sourceOffset + clipIndex0; // index inside the source buffer
    const bool hasFades = clip.fadeInSamples > 0 || clip.fadeOutSamples > 0;
    const int64_t srcLen = src.getNumSamples();

    for (int ch = 0; ch < dest.getNumChannels(); ++ch) {
        const int srcCh = std::min(ch, src.getNumChannels() - 1);
        const float* s = src.getReadPointer(srcCh);
        float* d = dest.getChannelPointer(ch) + destOffset;
        for (int i = 0; i < n; ++i) {
            const int64_t si = srcIndex0 + i;
            if (si < 0 || si >= srcLen)
                continue; // the clip is longer than its source: silence
            const float g = hasFades ? gain * clipFadeAt(clip, clipIndex0 + i) : gain;
            d[i] += s[si] * g;
        }
    }
}

} // namespace dawlab::engine
