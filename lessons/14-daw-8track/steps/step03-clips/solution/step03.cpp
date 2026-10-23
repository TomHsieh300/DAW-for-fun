// Step 3 (solution): clipFadeAt() and renderClip().
//
// Build and run:  l14_step03_solution   (ctest, label "solution")
// Your version:   starter/step03.cpp -> l14_step03_starter

#include "../minidaw.h"

namespace minidaw {

float clipFadeAt(const Clip& clip, int64_t i) noexcept {
    float g = 1.0f;
    if (clip.fadeInSamples > 0 && i < clip.fadeInSamples)
        g *= fadeGain(clip.fadeShape, static_cast<float>(i + 1) / static_cast<float>(clip.fadeInSamples + 1));
    const int64_t r = clip.length - 1 - i; // samples left after this one
    if (clip.fadeOutSamples > 0 && r < clip.fadeOutSamples)
        g *= fadeGain(clip.fadeShape, static_cast<float>(r + 1) / static_cast<float>(clip.fadeOutSamples + 1));
    return g;
}

void renderClip(const Clip& clip, int64_t timelineStart, AudioBlock dest) noexcept {
    if (clip.muted || !clip.source || clip.length <= 0 || clip.source->getNumChannels() <= 0)
        return;
    const AudioBuffer& src = *clip.source;
    const int64_t from = std::max(timelineStart, clip.start); // overlap of block and clip
    const int64_t to = std::min(timelineStart + dest.getNumSamples(), clip.end());
    if (to <= from)
        return;

    const float gain = dawlab::dbToGain(clip.gainDb);
    const int n = static_cast<int>(to - from);
    const int destOffset = static_cast<int>(from - timelineStart);
    const int64_t clipIndex0 = from - clip.start;             // index inside the clip
    const int64_t srcIndex0 = clip.sourceOffset + clipIndex0; // index inside the source
    const int64_t srcLen = src.getNumSamples();
    for (int ch = 0; ch < dest.getNumChannels(); ++ch) {
        const float* s = src.getReadPointer(std::min(ch, src.getNumChannels() - 1)); // mono -> all
        float* d = dest.getChannelPointer(ch) + destOffset;
        for (int i = 0; i < n; ++i) {
            const int64_t si = srcIndex0 + i;
            if (si < 0 || si >= srcLen)
                continue; // the clip is longer than its source: silence
            d[i] += s[si] * gain * clipFadeAt(clip, clipIndex0 + i);
        }
    }
}

} // namespace minidaw

#include "../check.h"
