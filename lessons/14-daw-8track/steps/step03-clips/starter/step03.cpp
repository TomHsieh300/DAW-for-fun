// Step 3 (starter): clipFadeAt() and renderClip().
//
// Build and run:  l14_step03_starter   (prints [FAIL] until both TODOs are done)
// Reference:      ../solution/step03.cpp, docs/step-03-clips.md ("Your turn")
//
// Clip, fadeGain(), Track and the engine are in ../minidaw.h.

#include "../minidaw.h"

namespace minidaw {

float clipFadeAt(const Clip& clip, int64_t i) noexcept {
    // TODO 1: start with g = 1.
    //   fade-in : if i < clip.fadeInSamples,  g *= fadeGain(shape, (i + 1) / (fadeInSamples + 1))
    //   fade-out: r = clip.length - 1 - i;  if r < clip.fadeOutSamples,
    //             g *= fadeGain(shape, (r + 1) / (fadeOutSamples + 1))
    (void)clip;
    (void)i;
    return 0.0f;
}

void renderClip(const Clip& clip, int64_t timelineStart, AudioBlock dest) noexcept {
    // TODO 2: return for muted clips and missing / empty sources.
    // TODO 3: the overlap: from = max(timelineStart, clip.start),
    //         to = min(timelineStart + dest.getNumSamples(), clip.end()); nothing if to <= from.
    // TODO 4: for every channel (a mono source feeds all channels) and every T in [from, to):
    //         dest[T - timelineStart] += src[clip.sourceOffset + T - clip.start]
    //                                    * dbToGain(clip.gainDb) * clipFadeAt(clip, T - clip.start)
    //         skipping source indices outside the source buffer.
    (void)clip;
    (void)timelineStart;
    (void)dest;
}

} // namespace minidaw

#include "../check.h"
