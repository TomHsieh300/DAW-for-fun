// Exercise 8 (solution): audio quantize.
//
// Build and run:  l20_ex8_solution   (ctest, label "solution"; needs -Juce)
// Your version:   exercises/starter/ex8_quantize_audio.cpp -> l20_ex8_starter

#include "../exercises/ex8_quantize_audio.h"

#include <cmath>

namespace ex8 {

using namespace dawlab;

namespace {

float magnitudeAt(const AudioBuffer& audio, std::int64_t i) noexcept {
    float m = 0.0f;
    for (int ch = 0; ch < audio.getNumChannels(); ++ch)
        m = std::max(m, std::fabs(audio.getSample(ch, static_cast<int>(i))));
    return m;
}

} // namespace

std::vector<std::int64_t> findOnsets(const AudioBuffer& audio, std::int64_t from, std::int64_t to, double threshold, std::int64_t quietSamples) {
    std::vector<std::int64_t> onsets;
    from = std::max<std::int64_t>(from, 0);
    to = std::min<std::int64_t>(to, audio.getNumSamples());
    if (from >= to)
        return onsets;
    float peak = 0.0f;
    for (std::int64_t i = from; i < to; ++i)
        peak = std::max(peak, magnitudeAt(audio, i));
    if (peak <= 0.0f)
        return onsets;
    const double level = threshold * peak;
    std::int64_t quiet = quietSamples; // the range's start counts as after a quiet stretch
    for (std::int64_t i = from; i < to; ++i) {
        if (magnitudeAt(audio, i) >= level) {
            if (quiet >= quietSamples)
                onsets.push_back(i);
            quiet = 0;
        } else {
            ++quiet;
        }
    }
    return onsets;
}

Quantized quantizeClip(app::Session& session, engine::TrackId track, int clipIndex, std::int64_t gridTicks, double strength, engine::FollowMode mode) {
    Quantized result;
    const std::vector<engine::Clip>& clips = session.getClips(track);
    if ((mode != engine::FollowMode::Varispeed && mode != engine::FollowMode::Stretch) || gridTicks < 1 || !(strength >= 0.0 && strength <= 1.0) ||
        clipIndex < 0 || clipIndex >= static_cast<int>(clips.size()) || clips[static_cast<size_t>(clipIndex)].source == nullptr)
        return result;
    engine::Clip clip = clips[static_cast<size_t>(clipIndex)];
    const engine::FollowClip now = engine::followClipOf(clip);
    const std::int64_t length = now.isWarped() ? clip.follow->sourceLength : clip.length;
    const double rate = session.getClipSampleRate();
    const engine::TempoMap& map = session.getTempoMap();
    engine::WarpMap warp;
    const bool warped = now.isWarped() && warp.set(now.markers);
    const auto tickHeard = [&](std::int64_t s) {
        return warped ? warp.tickAt(static_cast<double>(s)) : engine::tickOfSample(map, rate, clip.start + (s - clip.sourceOffset));
    };

    const std::int64_t first = clip.sourceOffset, end = clip.sourceOffset + length;
    const double endTick = tickHeard(end);
    std::vector<engine::WarpMarker> markers{{static_cast<double>(first), tickHeard(first)}};
    const std::vector<std::int64_t> onsets = findOnsets(*clip.source, first, end, 0.25, static_cast<std::int64_t>(std::llround(0.02 * rate)));
    result.onsets = static_cast<int>(onsets.size());
    for (const std::int64_t s : onsets) {
        if (s <= first)
            continue;
        const double heard = tickHeard(s);
        const double line = std::round(heard / static_cast<double>(gridTicks)) * static_cast<double>(gridTicks);
        const double tick = heard + strength * (line - heard);
        if (tick >= markers.back().tick + 1.0 && tick <= endTick - 1.0)
            markers.push_back({static_cast<double>(s), tick});
    }
    markers.push_back({static_cast<double>(end), endTick});

    auto follow = std::make_shared<engine::ClipFollow>();
    follow->mode = mode;
    follow->sourceLength = length;
    follow->markers = markers;
    clip.follow = std::move(follow);
    std::vector<engine::Clip> list = clips;
    list[static_cast<size_t>(clipIndex)] = clip;
    if (!session.perform(app::edit::SetClips{track, std::move(list)}, "Quantize audio"))
        return Quantized{};
    result.ok = true;
    result.markers = static_cast<int>(markers.size());
    return result;
}

} // namespace ex8

#include "../exercises/ex8_check.h"
