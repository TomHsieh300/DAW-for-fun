// Exercise 9 (solution): a loop that fits.
//
// Build and run:  l20_ex9_solution   (ctest, label "solution"; needs -Juce)
// Your version:   exercises/starter/ex9_loop_fits.cpp -> l20_ex9_starter

#include "../exercises/ex9_loop_fits.h"

#include <cmath>

namespace ex9 {

using namespace dawlab;

int guessBeats(std::int64_t lengthSamples, double sampleRate, double bpm) noexcept {
    if (lengthSamples < 1 || !(sampleRate > 0.0) || !(bpm > 0.0))
        return 0;
    const double beats = static_cast<double>(lengthSamples) / sampleRate * bpm / 60.0;
    int best = 1;
    double bestDistance = std::fabs(std::log2(1.0 / beats));
    for (int guess = 2; guess <= 256; guess *= 2) {
        const double distance = std::fabs(std::log2(guess / beats));
        if (distance < bestDistance) {
            best = guess;
            bestDistance = distance;
        }
    }
    return best;
}

Dropped dropLoop(app::Session& session, engine::TrackId track, std::shared_ptr<const AudioBuffer> audio, int bar, engine::FollowMode mode) {
    Dropped result;
    if ((mode != engine::FollowMode::Varispeed && mode != engine::FollowMode::Stretch) || bar < 1 || audio == nullptr || audio->getNumSamples() < 1 ||
        !session.hasTrack(track))
        return result;
    const double rate = session.getClipSampleRate();
    const std::int64_t tick = static_cast<std::int64_t>(bar - 1) * 3840;
    const int beats = guessBeats(audio->getNumSamples(), rate, session.getTempoMap().bpmAt(static_cast<double>(tick)));

    engine::Clip clip = session.makeAudioClip(audio, "Loop", session.sampleOfTick(tick));
    clip.fadeInSamples = 0; // the downbeat is the loop's first sample
    auto follow = std::make_shared<engine::ClipFollow>();
    follow->mode = mode;
    follow->sourceLength = clip.length;
    follow->markers = engine::markersForLoop(0, clip.length, static_cast<double>(beats), static_cast<double>(tick));
    clip.follow = std::move(follow);
    engine::placeUnder(clip, session.getTempoMap(), rate); // where it is now: the clips under it are cut there

    if (!session.perform(app::edit::SetClips{track, session.withClipOnTop(session.getClips(track), clip)}, "Add loop"))
        return result;
    const std::vector<engine::Clip>& clips = session.getClips(track);
    for (size_t i = 0; i < clips.size(); ++i)
        if (clips[i].follow != nullptr && clips[i].source == audio && clips[i].start == clip.start)
            result.clipIndex = static_cast<int>(i);
    result.ok = true;
    result.beats = beats;
    return result;
}

} // namespace ex9

#include "../exercises/ex9_check.h"
