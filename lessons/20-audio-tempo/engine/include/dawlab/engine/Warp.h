#pragma once
// Warp: audio clips that follow the tempo (chapter 20, Step 5; Steps 1 to 4 in the engine).
//
// The audio thread plays plain clips, as it has since chapter 14: a buffer, a start and a length in samples
// (Clip.h). A clip that follows the tempo is turned INTO such a clip ahead of time, on the UI thread or on a
// worker (FollowRenderer.h), and handed over with Engine::setTrackClips() like any other. Nothing in this file
// runs on the audio thread, so it may allocate.
//
//   FollowClip        a clip as the user means it: a Clip, and how it follows.
//     Off             it stays at its sample (DawLab 19).
//     Start           its start is a tick; it plays at its own speed (Step 2).
//     Varispeed       everything in it follows, read faster or slower through a windowed sinc; the pitch goes
//                     with the speed (Step 3).
//     Stretch         everything follows and the pitch stays: WSOLA (Step 4).
//   markers           Varispeed and Stretch need to know where the audio belongs: warp markers, each a position
//                     in the SOURCE buffer and a tick (Step 1). markersForLoop() and markersForTake() make them.
//   renderFollowClip  the plain clip that plays for a FollowClip under a tempo map: for Varispeed and Stretch a
//                     new buffer, covering exactly the song samples whose position is in the clip's range of
//                     the source, [sourceOffset, sourceOffset + length).
//   placeholderFor    what plays while a new render is not ready after the map changed: the last render, moved
//                     to where the clip now starts (or, with no render yet, the clip itself there). On the beat
//                     at its start, at the old speed after it - Step 2's behaviour, for the moment it takes.
//
// Offline the position of EVERY song sample is computed exactly (Step 3's lines between knots are for a player
// that has to be fast); a second of audio costs a few milliseconds.

#include "Clip.h"
#include "TempoMap.h"

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace dawlab::engine {

// ---- Step 1: markers ------------------------------------------------------------------------------------------

struct WarpMarker {
    double source = 0.0; // a position in the source buffer, in samples
    double tick = 0.0;   // where it belongs in the song

    friend bool operator==(const WarpMarker& a, const WarpMarker& b) noexcept { return a.source == b.source && a.tick == b.tick; }
    friend bool operator!=(const WarpMarker& a, const WarpMarker& b) noexcept { return !(a == b); }
};

// True for a list a warp map can be made of: at least two markers, finite, sources and ticks strictly increasing.
inline bool areWarpMarkers(const std::vector<WarpMarker>& markers) noexcept {
    if (markers.size() < 2)
        return false;
    for (size_t i = 0; i < markers.size(); ++i)
        if (!std::isfinite(markers[i].source) || !std::isfinite(markers[i].tick) ||
            (i > 0 && (markers[i].source <= markers[i - 1].source || markers[i].tick <= markers[i - 1].tick)))
            return false;
    return true;
}

class WarpMap {
public:
    // False (the map stays as it was) unless areWarpMarkers().
    bool set(std::vector<WarpMarker> markers) {
        if (!areWarpMarkers(markers))
            return false;
        markers_ = std::move(markers);
        return true;
    }
    const std::vector<WarpMarker>& markers() const noexcept { return markers_; }

    // The line of the pair of markers around the tick (the first or the last pair beyond them).
    double sourceAt(double tick) const noexcept {
        size_t lo = 0, hi = markers_.size() - 1;
        while (hi - lo > 1) {
            const size_t mid = (lo + hi) / 2;
            (markers_[mid].tick <= tick ? lo : hi) = mid;
        }
        const WarpMarker &a = markers_[lo], &b = markers_[lo + 1];
        return a.source + (tick - a.tick) * (b.source - a.source) / (b.tick - a.tick);
    }
    double tickAt(double source) const noexcept {
        size_t lo = 0, hi = markers_.size() - 1;
        while (hi - lo > 1) {
            const size_t mid = (lo + hi) / 2;
            (markers_[mid].source <= source ? lo : hi) = mid;
        }
        const WarpMarker &a = markers_[lo], &b = markers_[lo + 1];
        return a.tick + (source - a.source) * (b.tick - a.tick) / (b.source - a.source);
    }

private:
    std::vector<WarpMarker> markers_{{0.0, 0.0}, {1.0, 1.0}};
};

// A loop: the source samples [sourceFrom, sourceFrom + lengthSamples) are `beats` quarter notes long, the first
// of them at `startTick`. Empty unless the length and the beats are above 0.
inline std::vector<WarpMarker> markersForLoop(std::int64_t sourceFrom, std::int64_t lengthSamples, double beats, double startTick) {
    if (lengthSamples <= 0 || !(beats > 0.0))
        return {};
    return {{static_cast<double>(sourceFrom), startTick},
            {static_cast<double>(sourceFrom + lengthSamples), startTick + beats * TempoMap::kTicksPerBeat}};
}

// A take: the source samples [sourceFrom, sourceFrom + lengthSamples) were recorded over `map`, the first of them
// at song sample `startSample` (>= 0). Over the same map no sample then plays more than `toleranceSeconds` from
// where it was recorded: a marker at the take's start, at every tempo point under it and at its end, and on a
// ramp as many equal pieces as the chord bound h^2/8 max|f''| asks for (Step 1).
// Empty unless the rate, the length and the tolerance are above 0 and the start is not negative.
inline std::vector<WarpMarker> markersForTake(const TempoMap& map, double sampleRate, std::int64_t startSample, std::int64_t sourceFrom,
                                              std::int64_t lengthSamples, double toleranceSeconds = 0.0005) {
    if (!(sampleRate > 0.0) || startSample < 0 || lengthSamples <= 0 || !(toleranceSeconds > 0.0))
        return {};
    const double startTick = map.tickAt(static_cast<double>(startSample) / sampleRate);
    const double endTick = map.tickAt(static_cast<double>(startSample + lengthSamples) / sampleRate);
    std::vector<double> edges{startTick};
    for (int i = 0; i < map.size(); ++i) {
        const double tick = static_cast<double>(map.point(i).tick);
        if (tick > startTick && tick < endTick)
            edges.push_back(tick);
    }
    edges.push_back(endTick);

    const double secondsPerTickAt1Bpm = 60.0 / TempoMap::kTicksPerBeat;
    const double toSource = static_cast<double>(sourceFrom - startSample);
    std::vector<WarpMarker> markers{{static_cast<double>(sourceFrom), startTick}};
    for (size_t e = 0; e + 1 < edges.size(); ++e) {
        const double a = edges[e], b = edges[e + 1];
        // The tempo at both ends of this stretch, each read just inside it: a ramp if they differ.
        const double tempoA = map.bpmAt(a + 1e-9 * (b - a)), tempoB = map.bpmAt(b - 1e-9 * (b - a));
        int pieces = 1;
        if (tempoA != tempoB) {
            const double k = (tempoB - tempoA) / (b - a), slow = std::min(tempoA, tempoB);
            pieces = std::max(1, static_cast<int>(std::ceil((b - a) * std::sqrt(secondsPerTickAt1Bpm * std::fabs(k) / (slow * slow) / (8.0 * toleranceSeconds)))));
        }
        for (int j = 1; j <= pieces; ++j) {
            const double tick = j == pieces ? b : a + (b - a) * j / pieces;
            markers.push_back({map.secondsAt(tick) * sampleRate + toSource, tick});
        }
    }
    markers.back().source = static_cast<double>(sourceFrom + lengthSamples); // the sample after the last, not a rounding of it
    return markers;
}

// ---- the clip ------------------------------------------------------------------------------------------------

enum class FollowMode : std::uint8_t { Off, Start, Varispeed, Stretch };

struct FollowClip {
    Clip clip;                       // as chapter 14's: what plays when it does not follow
    FollowMode mode = FollowMode::Off;
    double startTick = 0.0;          // Start: where the clip belongs (a fraction; unused in the other modes)
    std::vector<WarpMarker> markers; // Varispeed, Stretch: where the audio of the source belongs

    // True if the clip is played from a buffer that renderFollowClip() has to make.
    bool isWarped() const noexcept { return (mode == FollowMode::Varispeed || mode == FollowMode::Stretch) && areWarpMarkers(markers); }
};

// The tick of a timeline sample (a fraction), and the sample of a tick.
inline double tickOfSample(const TempoMap& map, double sampleRate, std::int64_t sample) noexcept {
    return map.tickAt(static_cast<double>(sample) / sampleRate);
}
inline std::int64_t sampleOfTick(const TempoMap& map, double sampleRate, double tick) noexcept {
    return std::llround(map.secondsAt(tick) * sampleRate);
}

// Step 2. Turns Start on without moving the clip: startTick becomes the exact tick of its start.
inline void anchorStart(FollowClip& clip, const TempoMap& map, double sampleRate) noexcept {
    clip.startTick = tickOfSample(map, sampleRate, clip.clip.start);
}

// The position in the source for a song sample (a fraction): Step 1's two maps.
inline double sourcePositionAt(const WarpMap& warp, const TempoMap& map, double sampleRate, double songSample) noexcept {
    return warp.sourceAt(map.tickAt(songSample / sampleRate));
}

// The first song sample whose position is at or after `source`.
inline std::int64_t firstSongSampleAtOrAfter(const WarpMap& warp, const TempoMap& map, double sampleRate, double source) noexcept {
    std::int64_t s = static_cast<std::int64_t>(std::ceil(map.secondsAt(warp.tickAt(source)) * sampleRate));
    while (sourcePositionAt(warp, map, sampleRate, static_cast<double>(s)) < source)
        ++s;
    while (sourcePositionAt(warp, map, sampleRate, static_cast<double>(s - 1)) >= source)
        --s;
    return s;
}

// The song samples a clip covers under a map: [first, second). For Off its own; for Start from its tick, as long
// as it is; for a warped clip those whose position is in [sourceOffset, sourceOffset + length).
inline std::pair<std::int64_t, std::int64_t> songRangeOf(const FollowClip& clip, const TempoMap& map, double sampleRate) {
    if (clip.isWarped()) {
        WarpMap warp;
        warp.set(clip.markers);
        const double from = static_cast<double>(clip.clip.sourceOffset);
        const std::int64_t first = firstSongSampleAtOrAfter(warp, map, sampleRate, from);
        return {first, std::max(first, firstSongSampleAtOrAfter(warp, map, sampleRate, from + static_cast<double>(clip.clip.length)))};
    }
    const std::int64_t first = clip.mode == FollowMode::Start ? sampleOfTick(map, sampleRate, clip.startTick) : clip.clip.start;
    return {first, first + clip.clip.length};
}

// ---- Step 3: reading between samples --------------------------------------------------------------------------

constexpr int kSincZeroCrossings = 16; // to each side of the position, at speed 1

// A channel at a position between its samples: a sinc under a Blackman window, cut at Nyquist - or at
// Nyquist / speed when reading faster than 1, so that nothing comes back as an alias. 0 outside the channel.
inline float readSinc(const float* samples, std::int64_t numSamples, double position, double speed) noexcept {
    const double cutoff = speed > 1.0 ? 1.0 / speed : 1.0;
    const double reach = kSincZeroCrossings / cutoff;
    const std::int64_t first = std::max<std::int64_t>(static_cast<std::int64_t>(std::ceil(position - reach)), 0);
    const std::int64_t last = std::min<std::int64_t>(static_cast<std::int64_t>(std::floor(position + reach)), numSamples - 1);
    double sum = 0.0, weights = 0.0;
    // The weights of the samples outside the channel count too: they are silence, not missing.
    for (std::int64_t k = static_cast<std::int64_t>(std::ceil(position - reach)); k <= static_cast<std::int64_t>(std::floor(position + reach)); ++k) {
        const double x = static_cast<double>(k) - position;
        const double t = x / reach;
        const double window = t <= -1.0 || t >= 1.0 ? 0.0 : 0.42 + 0.5 * std::cos(kPi * t) + 0.08 * std::cos(kTwoPi * t);
        const double arg = cutoff * x;
        const double w = (std::fabs(arg) < 1e-9 ? 1.0 : std::sin(kPi * arg) / (kPi * arg)) * window;
        weights += w;
        if (k >= first && k <= last)
            sum += w * samples[k];
    }
    return static_cast<float>(weights != 0.0 ? sum / weights : 0.0);
}

// ---- Step 4: WSOLA -------------------------------------------------------------------------------------------

struct StretchSettings {
    int window = 1024; // samples of a piece (even); a new piece every window / 2 output samples
    int search = 256;  // how far a piece may slide either way
};

// How far the piece that wants to start at `candidate` slides to fit the natural continuation at `natural`:
// the best of sum(a b) / sqrt(sum(a a)) over the first half window, the nearest of equal scores.
inline int bestStretchOffset(const std::vector<float>& x, std::int64_t candidate, std::int64_t natural, const StretchSettings& settings) noexcept {
    if (candidate == natural || settings.search <= 0)
        return 0;
    const int half = settings.window / 2;
    const std::int64_t size = static_cast<std::int64_t>(x.size());
    const auto at = [&](std::int64_t i) { return i >= 0 && i < size ? static_cast<double>(x[static_cast<size_t>(i)]) : 0.0; };
    const auto score = [&](int d) {
        double cross = 0.0, energy = 0.0;
        for (int i = 0; i < half; ++i) {
            const double a = at(candidate + d + i), b = at(natural + i);
            cross += a * b;
            energy += a * a;
        }
        return energy > 0.0 ? cross / std::sqrt(energy) : 0.0;
    };
    int best = 0;
    double bestScore = score(0);
    for (int distance = 1; distance <= settings.search; ++distance)
        for (const int d : {distance, -distance}) {
            const double s = score(d);
            if (s > bestScore) {
                bestScore = s;
                best = d;
            }
        }
    return best;
}

// The source stretched along `positions`: output sample n sounds like the source around positions[n], at the
// source's own pitch. All channels slide together, by what fits their mean - or the stereo image would wobble.
// `cancel` (may be null) is looked at once a piece; when it is set the result is an empty buffer.
inline AudioBuffer stretchAlong(const AudioBuffer& source, const std::vector<double>& positions, const StretchSettings& settings = {},
                                const std::atomic<bool>* cancel = nullptr) {
    const int channels = source.getNumChannels();
    const std::int64_t length = static_cast<std::int64_t>(positions.size());
    if (channels <= 0 || length <= 0 || length > INT32_MAX || settings.window < 4 || settings.window % 2 != 0 || settings.search < 0)
        return {};
    const std::int64_t sourceLength = source.getNumSamples();
    std::vector<float> mean(static_cast<size_t>(sourceLength), 0.0f);
    for (int ch = 0; ch < channels; ++ch) {
        const float* s = source.getReadPointer(ch);
        for (std::int64_t i = 0; i < sourceLength; ++i)
            mean[static_cast<size_t>(i)] += s[i] / static_cast<float>(channels);
    }
    const auto positionAt = [&](std::int64_t n) { // beyond the ends the line of the last two values goes on
        if (length == 1)
            return positions[0] + static_cast<double>(n);
        if (n < 0)
            return positions[0] + static_cast<double>(n) * (positions[1] - positions[0]);
        if (n >= length)
            return positions[static_cast<size_t>(length - 1)] +
                   static_cast<double>(n - (length - 1)) * (positions[static_cast<size_t>(length - 1)] - positions[static_cast<size_t>(length - 2)]);
        return positions[static_cast<size_t>(n)];
    };

    AudioBuffer out(channels, static_cast<int>(length));
    const int half = settings.window / 2;
    std::vector<float> hann(static_cast<size_t>(settings.window));
    for (int i = 0; i < settings.window; ++i)
        hann[static_cast<size_t>(i)] = static_cast<float>(0.5 - 0.5 * std::cos(kTwoPi * i / settings.window));

    bool first = true;
    std::int64_t before = 0;
    for (std::int64_t at = -half; at < length; at += half) {
        if (cancel != nullptr && cancel->load(std::memory_order_relaxed))
            return {};
        const std::int64_t candidate = std::llround(positionAt(at + half)) - half;
        const std::int64_t start = first ? candidate : candidate + bestStretchOffset(mean, candidate, before + half, settings);
        const int i0 = static_cast<int>(std::max<std::int64_t>({0, -at, -start}));
        const int i1 = static_cast<int>(std::min<std::int64_t>({settings.window, length - at, sourceLength - start}));
        for (int ch = 0; ch < channels; ++ch) {
            const float* s = source.getReadPointer(ch);
            float* d = out.getWritePointer(ch);
            for (int i = i0; i < i1; ++i)
                d[at + i] += hann[static_cast<size_t>(i)] * s[start + i];
        }
        before = start;
        first = false;
    }
    return out;
}

// ---- the render -----------------------------------------------------------------------------------------------

constexpr std::int64_t kMaxRenderSamples = std::int64_t{1} << 28; // 93 minutes at 48 kHz: more is refused

// The plain clip that plays for `clip` under `map`. Off and Start: the clip itself, at its place. Varispeed and
// Stretch: a new buffer. False - and `out` is not touched - if the render was cancelled or would be longer than
// kMaxRenderSamples. A warped mode without usable markers or without a source plays as Off.
inline bool renderFollowClip(const FollowClip& clip, const TempoMap& map, double sampleRate, Clip& out, const std::atomic<bool>* cancel = nullptr) {
    const auto [first, end] = songRangeOf(clip, map, sampleRate);
    Clip plain = clip.clip;
    plain.start = first;
    if (!clip.isWarped() || !clip.clip.source || clip.clip.source->getNumChannels() <= 0) {
        if (clip.mode != FollowMode::Start)
            plain.start = clip.clip.start;
        out = std::move(plain);
        return true;
    }
    const std::int64_t length = end - first;
    if (length > kMaxRenderSamples)
        return false;

    WarpMap warp;
    warp.set(clip.markers);
    const AudioBuffer& source = *clip.clip.source;
    // The exact position of every song sample, and of one more: the speed of the last one.
    std::vector<double> positions(static_cast<size_t>(length) + 1);
    for (std::int64_t n = 0; n <= length; ++n)
        positions[static_cast<size_t>(n)] = sourcePositionAt(warp, map, sampleRate, static_cast<double>(first + n));

    auto rendered = std::make_shared<AudioBuffer>();
    if (clip.mode == FollowMode::Stretch) {
        positions.pop_back();
        *rendered = stretchAlong(source, positions, {}, cancel);
        if (length > 0 && rendered->getNumSamples() != length)
            return false; // cancelled
    } else {
        rendered->setSize(source.getNumChannels(), static_cast<int>(length));
        for (std::int64_t n = 0; n < length; ++n) {
            if ((n & 4095) == 0 && cancel != nullptr && cancel->load(std::memory_order_relaxed))
                return false;
            const double speed = positions[static_cast<size_t>(n) + 1] - positions[static_cast<size_t>(n)];
            for (int ch = 0; ch < source.getNumChannels(); ++ch)
                rendered->getWritePointer(ch)[n] = readSinc(source.getReadPointer(ch), source.getNumSamples(), positions[static_cast<size_t>(n)], speed);
        }
    }
    plain.source = std::move(rendered);
    plain.sourceOffset = 0;
    plain.length = length;
    out = std::move(plain);
    return true;
}

// What plays for a warped clip until its render under a new map is ready: `lastRender` (a clip that
// renderFollowClip() made under another map) moved to where the clip now starts, or - if there is none yet,
// lastRender == nullptr - the clip's own audio from there.
inline Clip placeholderFor(const FollowClip& clip, const Clip* lastRender, const TempoMap& map, double sampleRate) {
    Clip plain = lastRender != nullptr ? *lastRender : clip.clip;
    plain.start = songRangeOf(clip, map, sampleRate).first;
    return plain;
}

} // namespace dawlab::engine
