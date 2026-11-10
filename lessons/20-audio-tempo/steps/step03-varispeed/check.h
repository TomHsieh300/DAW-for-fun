#pragma once
// Chapter 20, Step 3 - the self-check (main). Included at the end of the starter and of the solution.

#include "minidaw.h"

#include <dawlab/Check.h>

#include <cstdio>
#include <random>

namespace step03_check {

using namespace minidaw;

constexpr double kRate = 48000.0;

inline TempoMap mapOf(std::vector<TempoPoint> points) {
    TempoMap map;
    map.set(kRate, std::move(points));
    return map;
}

inline TempoMap randomMap(std::mt19937& rng, bool ramps) {
    auto pick = [&rng](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
    std::vector<TempoPoint> points;
    std::int64_t tick = 0;
    const int n = pick(1, 12);
    for (int i = 0; i < n; ++i) {
        points.push_back({tick, static_cast<double>(pick(500, 2400)) / 10.0, ramps && pick(0, 2) != 0});
        tick += pick(200, 12000);
    }
    return mapOf(points);
}

inline std::vector<float> sine(double hz, int samples, double amplitude = 1.0) {
    std::vector<float> s(static_cast<size_t>(samples));
    for (int i = 0; i < samples; ++i)
        s[static_cast<size_t>(i)] = static_cast<float>(amplitude * std::sin(dawlab::kTwoPi * hz * i / kRate));
    return s;
}

// How far `y` is from the best sine of `cyclesPerSample` through it, in dB below that sine (the higher the better).
inline double sineToNoiseDb(const std::vector<float>& y, double cyclesPerSample) {
    double ss = 0.0, cc = 0.0, sc = 0.0, ys = 0.0, yc = 0.0;
    for (size_t i = 0; i < y.size(); ++i) {
        const double s = std::sin(dawlab::kTwoPi * cyclesPerSample * static_cast<double>(i)), c = std::cos(dawlab::kTwoPi * cyclesPerSample * static_cast<double>(i));
        ss += s * s, cc += c * c, sc += s * c, ys += y[i] * s, yc += y[i] * c;
    }
    const double det = ss * cc - sc * sc;
    if (std::fabs(det) < 1e-12)
        return -200.0;
    const double a = (ys * cc - yc * sc) / det, b = (yc * ss - ys * sc) / det;
    double signal = 0.0, noise = 0.0;
    for (size_t i = 0; i < y.size(); ++i) {
        const double fit = a * std::sin(dawlab::kTwoPi * cyclesPerSample * static_cast<double>(i)) + b * std::cos(dawlab::kTwoPi * cyclesPerSample * static_cast<double>(i));
        signal += fit * fit;
        noise += (y[i] - fit) * (y[i] - fit);
    }
    if (signal <= 0.0)
        return -200.0;
    return 10.0 * std::log10(signal / std::max(noise, 1e-300));
}

inline double rmsDb(const std::vector<float>& y) {
    double sum = 0.0;
    for (const float v : y)
        sum += static_cast<double>(v) * v;
    return 10.0 * std::log10(std::max(sum / static_cast<double>(std::max<size_t>(y.size(), 1)), 1e-30));
}

inline const char* nameOf(Interpolation how) {
    return how == Interpolation::Linear ? "linear" : how == Interpolation::Hermite ? "Hermite" : "sinc";
}

inline void checkPitch() {
    DAWLAB_CHECK(std::fabs(semitonesOf(2.0) - 12.0) < 1e-9 && std::fabs(semitonesOf(0.5) + 12.0) < 1e-9 && std::fabs(semitonesOf(1.0)) < 1e-12);
    // 120 BPM audio at 126: not quite a semitone up - a singer is already out of tune.
    DAWLAB_CHECK(std::fabs(semitonesOf(126.0 / 120.0) - 0.8447) < 1e-3);
    std::printf("  a loop made at 120 BPM played at 126: %.2f semitones up; at 60: %.0f\n", semitonesOf(126.0 / 120.0), semitonesOf(0.5));
}

inline void checkReading() {
    std::mt19937 rng(40);
    std::vector<float> noise(4000);
    for (float& v : noise)
        v = std::uniform_real_distribution<float>(-1.0f, 1.0f)(rng);
    const std::vector<float> flat(4000, 0.25f);

    // On a sample the answer is the sample; a constant stays the constant between samples, at any speed.
    int offSample = 0, offConstant = 0;
    for (const Interpolation how : {Interpolation::Linear, Interpolation::Hermite, Interpolation::Sinc})
        for (int k = 0; k < 300; ++k) {
            const int i = std::uniform_int_distribution<int>(100, 3900)(rng);
            const double speed = std::uniform_real_distribution<double>(0.25, 4.0)(rng);
            offSample += std::fabs(readAt(noise, static_cast<double>(i), 1.0, how) - noise[static_cast<size_t>(i)]) < 1e-6f ? 0 : 1;
            const double between = 200.0 + std::uniform_real_distribution<double>(0.0, 3600.0)(rng);
            offConstant += std::fabs(readAt(flat, between, speed, how) - 0.25f) < 1e-6f ? 0 : 1;
        }
    DAWLAB_CHECK(offSample == 0);
    DAWLAB_CHECK(offConstant == 0);
    // Half way between two samples: linear is their mean; Hermite on a parabola is the parabola.
    DAWLAB_CHECK((std::fabs(readAt({0.0f, 1.0f, 3.0f, 0.0f}, 1.5, 1.0, Interpolation::Linear) - 2.0f) < 1e-6f));
    DAWLAB_CHECK((std::fabs(readAt({0.0f, 1.0f, 4.0f, 9.0f, 16.0f}, 1.5, 1.0, Interpolation::Hermite) - 2.25f) < 1e-6f));
    DAWLAB_CHECK((std::fabs(readAt({0.0f, 1.0f, 4.0f, 9.0f, 16.0f}, 2.25, 1.0, Interpolation::Hermite) - 5.0625f) < 1e-5f));
    // Outside the source there is silence.
    DAWLAB_CHECK(readAt(flat, -500.0, 1.0, Interpolation::Sinc) == 0.0f && readAt(flat, 9000.5, 1.0, Interpolation::Hermite) == 0.0f &&
                 readAt(flat, -3.5, 1.0, Interpolation::Linear) == 0.0f);

    // A sine of 5 kHz read at another speed is a sine of another pitch - and what is not that sine is the error.
    const std::vector<float> tone = sine(5000.0, 60000);
    double db[3][2] = {};
    int h = 0;
    for (const Interpolation how : {Interpolation::Linear, Interpolation::Hermite, Interpolation::Sinc}) {
        int v = 0;
        for (const double speed : {0.7, 1.3}) {
            std::vector<float> out(20000);
            for (size_t k = 0; k < out.size(); ++k)
                out[k] = readAt(tone, 1000.0 + static_cast<double>(k) * speed, speed, how);
            db[h][v++] = sineToNoiseDb(out, 5000.0 * speed / kRate);
        }
        std::printf("  5 kHz read at speed 0.7 and 1.3, %-7s: the error is %5.1f and %5.1f dB below the tone\n", nameOf(how), db[h][0], db[h][1]);
        ++h;
    }
    DAWLAB_CHECK(db[0][0] > 20.0 && db[0][1] > 20.0);   // linear: usable, audibly dull
    DAWLAB_CHECK(db[1][0] > 40.0 && db[1][1] > 40.0);   // Hermite
    DAWLAB_CHECK(db[2][0] > 90.0 && db[2][1] > 90.0);   // sinc: below what 16 bits can hold
    DAWLAB_CHECK(db[1][0] > db[0][0] + 10.0 && db[2][0] > db[1][0] + 20.0);

    // Twice as fast, a sine of 15 kHz would be one of 30 kHz - above Nyquist, so it comes back as 18 kHz. The
    // sinc that cuts at Nyquist / speed removes it; Hermite cannot.
    const std::vector<float> high = sine(15000.0, 60000);
    std::vector<float> aliased(20000), clean(20000);
    for (size_t k = 0; k < aliased.size(); ++k) {
        aliased[k] = readAt(high, 1000.0 + 2.0 * static_cast<double>(k), 2.0, Interpolation::Hermite);
        clean[k] = readAt(high, 1000.0 + 2.0 * static_cast<double>(k), 2.0, Interpolation::Sinc);
    }
    std::printf("  15 kHz read twice as fast: Hermite leaves an alias at %.1f dB, the sinc %.1f dB\n", rmsDb(aliased), rmsDb(clean));
    DAWLAB_CHECK(rmsDb(aliased) > -10.0 && rmsDb(clean) < -60.0);
}

inline WarpedClip takeClip(const TempoMap& recordedOver, std::int64_t start, std::int64_t length) {
    WarpedClip clip;
    clip.warp.set(markersForTake(recordedOver, start, length, 0.0005));
    clip.sourceFrom = 0.0;
    clip.sourceTo = static_cast<double>(length);
    return clip;
}

inline void checkPositions() {
    // A loop of one bar made at 120 BPM, on bar 1: at 60 BPM every song sample is half a file sample.
    WarpedClip loop;
    loop.warp.set(markersForLoop(96000, 4.0, 0.0));
    loop.sourceTo = 96000.0;
    std::vector<double> p;
    positionsFor(loop, mapOf({{0, 60.0, false}}), 1000, 4, p);
    DAWLAB_CHECK(p.size() == 4);
    if (p.size() == 4)
        DAWLAB_CHECK(std::fabs(p[0] - 500.0) < 1e-6 && std::fabs(p[1] - 500.5) < 1e-6 && std::fabs(p[3] - 501.5) < 1e-6);
    positionsFor(loop, mapOf({{0, 60.0, false}}), 0, 0, p);
    DAWLAB_CHECK(p.empty());

    // Takes recorded over one map and played over another: every block against the exact position of every sample.
    std::mt19937 rng(41);
    for (const bool ramps : {false, true}) {
        int sizes = 0, firstOff = 0;
        double worst = 0.0;
        constexpr int kBlocks = 600;
        for (int c = 0; c < kBlocks; ++c) {
            const TempoMap old = randomMap(rng, ramps), now = randomMap(rng, ramps);
            const WarpedClip clip = takeClip(old, std::uniform_int_distribution<std::int64_t>(0, 200000)(rng), 2000000);
            const std::int64_t from = std::uniform_int_distribution<std::int64_t>(0, 3000000)(rng);
            const int n = std::uniform_int_distribution<int>(1, 512)(rng);
            positionsFor(clip, now, from, n, p);
            if (static_cast<int>(p.size()) != n) {
                ++sizes;
                continue;
            }
            firstOff += std::fabs(p[0] - exactPosition(clip, now, static_cast<double>(from))) < 1e-6 ? 0 : 1;
            for (int i = 0; i < n; ++i)
                worst = std::max(worst, std::fabs(p[static_cast<size_t>(i)] - exactPosition(clip, now, static_cast<double>(from + i))));
        }
        std::printf("  %d blocks of up to 512 samples over maps %s: the worst position %.2g samples off, %d blocks not exact at their start\n", kBlocks,
                    ramps ? "with ramps" : "of jumps  ", worst, firstOff);
        DAWLAB_CHECK(sizes == 0 && firstOff == 0);
        DAWLAB_CHECK(worst < (ramps ? 0.05 : 1e-5));
    }
}

inline void checkNoDrift() {
    // Ten minutes of a song whose tempo climbs from 90 to 150 in every bar and falls back, under a file made at
    // 120 BPM. The position from the block's own samples - and the position counted up from the speed.
    std::vector<TempoPoint> points;
    for (int bar = 0; bar < 400; ++bar)
        points.push_back({bar * 3840LL, 90.0, true}), points.push_back({bar * 3840LL + 3800, 150.0, false});
    const TempoMap map = mapOf(points);
    WarpedClip file;
    file.warp.set(markersForLoop(static_cast<std::int64_t>(1200 * kRate), 2400.0, 0.0)); // 20 minutes at 120 BPM
    file.sourceTo = 1200 * kRate;

    std::mt19937 rng(42);
    std::vector<double> p;
    double worst = 0.0, counted = 0.0;
    std::int64_t from = 0;
    while (from < static_cast<std::int64_t>(600 * kRate)) {
        const int n = std::uniform_int_distribution<int>(64, 512)(rng);
        positionsFor(file, map, from, n, p);
        if (static_cast<int>(p.size()) != n)
            break;
        for (int i = 0; i < n; i += 37)
            worst = std::max(worst, std::fabs(p[static_cast<size_t>(i)] - exactPosition(file, map, static_cast<double>(from + i))));
        // "The last position plus the speed": the speed at the block's start, for the whole block.
        counted += n * (exactPosition(file, map, static_cast<double>(from + 1)) - exactPosition(file, map, static_cast<double>(from)));
        from += n;
    }
    const double drift = std::fabs(counted - exactPosition(file, map, static_cast<double>(from)));
    std::printf("  ten minutes of ramps in blocks: the worst position %.3f samples off; counted up from the speed instead: %.0f samples (%.0f ms)\n", worst,
                drift, 1000.0 * drift / kRate);
    DAWLAB_CHECK(from >= static_cast<std::int64_t>(600 * kRate) && worst < 0.05);
}

inline void checkClipRange() {
    // A loop of one bar made at 120 BPM, on bar 2.
    WarpedClip loop;
    loop.warp.set(markersForLoop(96000, 4.0, 3840.0));
    loop.sourceTo = 96000.0;
    const TempoMap at120 = mapOf({{0, 120.0, false}}), at60 = mapOf({{0, 60.0, false}});
    DAWLAB_CHECK(firstSample(loop, at120) == 96000 && endSample(loop, at120) == 192000);
    DAWLAB_CHECK(firstSample(loop, at60) == 192000 && endSample(loop, at60) == 384000); // still exactly bar 2
    WarpedClip part = loop; // its second and third beat
    part.sourceFrom = 24000.0;
    part.sourceTo = 72000.0;
    DAWLAB_CHECK(firstSample(part, at60) == 240000 && endSample(part, at60) == 336000);

    // Exactly the samples whose position is in the range - on any maps.
    std::mt19937 rng(43);
    int wrong = 0;
    constexpr int kClips = 300;
    for (int c = 0; c < kClips; ++c) {
        const TempoMap old = randomMap(rng, true), now = randomMap(rng, true);
        WarpedClip clip = takeClip(old, std::uniform_int_distribution<std::int64_t>(0, 200000)(rng), 1000000);
        clip.sourceFrom = std::uniform_real_distribution<double>(0.0, 400000.0)(rng);
        clip.sourceTo = clip.sourceFrom + std::uniform_real_distribution<double>(1.0, 500000.0)(rng);
        const std::int64_t a = firstSample(clip, now), b = endSample(clip, now);
        const bool first = exactPosition(clip, now, static_cast<double>(a)) >= clip.sourceFrom && exactPosition(clip, now, static_cast<double>(a - 1)) < clip.sourceFrom;
        const bool end = exactPosition(clip, now, static_cast<double>(b)) >= clip.sourceTo && exactPosition(clip, now, static_cast<double>(b - 1)) < clip.sourceTo;
        wrong += first && end && b >= a ? 0 : 1;
    }
    std::printf("  %d clips over random maps: %d with a first or a last sample that is not the range's\n", kClips, wrong);
    DAWLAB_CHECK(wrong == 0);
}

inline std::vector<float> renderInBlocks(const std::vector<WarpedClip>& clips, const std::vector<std::vector<float>>& sources, const TempoMap& map,
                                         std::int64_t length, Interpolation how, unsigned seed) {
    std::vector<float> out(static_cast<size_t>(length), 0.0f);
    std::mt19937 rng(seed);
    for (std::int64_t from = 0; from < length;) {
        const int n = static_cast<int>(std::min<std::int64_t>(std::uniform_int_distribution<int>(1, 512)(rng), length - from));
        for (const WarpedClip& clip : clips)
            renderWarped(clip, sources, map, from, n, out.data() + from, how);
        from += n;
    }
    return out;
}

inline void checkSeam() {
    // Step 2's take, cut in two at bar 3: 8 s of 440 Hz recorded at 120 BPM. Now both halves follow with
    // everything in them, and the song plays at 60 BPM.
    const TempoMap at120 = mapOf({{0, 120.0, false}}), at60 = mapOf({{0, 60.0, false}});
    const std::vector<std::vector<float>> sources{sine(440.0, 384000, 0.5)};
    const WarpedClip whole = takeClip(at120, 0, 384000);
    WarpedClip a = whole, b = whole;
    a.sourceTo = 192000.0;
    b.sourceFrom = 192000.0;
    DAWLAB_CHECK(firstSample(a, at60) == 0 && endSample(a, at60) == 384000 && firstSample(b, at60) == 384000 && endSample(b, at60) == 768000);

    const std::vector<float> cut = renderInBlocks({a, b}, sources, at60, 768000, Interpolation::Hermite, 1);
    const std::vector<float> uncut = renderInBlocks({whole}, sources, at60, 768000, Interpolation::Hermite, 1);
    float worst = 0.0f;
    for (size_t i = 0; i < cut.size(); ++i)
        worst = std::max(worst, std::fabs(cut[i] - uncut[i]));
    const double db = sineToNoiseDb(cut, 220.0 / kRate);
    std::printf("  a take cut in two at bar 3, at 60 BPM: no gap (the halves meet at %.1f s), the cut is %.1g away from the uncut take,\n"
                "    and what plays is 220 Hz, an octave down (everything else %.0f dB below)\n",
                static_cast<double>(endSample(a, at60)) / kRate, static_cast<double>(worst), db);
    DAWLAB_CHECK(worst == 0.0f && db > 60.0);

    // On any map the halves meet: the end of one is the first sample of the other.
    std::mt19937 rng(44);
    int apart = 0;
    for (int c = 0; c < 200; ++c) {
        const TempoMap now = randomMap(rng, true);
        WarpedClip x = whole, y = whole;
        x.sourceTo = y.sourceFrom = std::uniform_real_distribution<double>(1.0, 383999.0)(rng);
        apart += endSample(x, now) == firstSample(y, now) && firstSample(x, now) <= endSample(x, now) ? 0 : 1;
    }
    DAWLAB_CHECK(apart == 0);
}

inline void checkRender() {
    // The clip is ADDED to what is there; a clip without a source adds nothing.
    const TempoMap at120 = mapOf({{0, 120.0, false}}), at240 = mapOf({{0, 240.0, false}});
    const std::vector<std::vector<float>> sources{std::vector<float>(96000, 0.25f), sine(15000.0, 96000)};
    WarpedClip flat;
    flat.warp.set(markersForLoop(96000, 4.0, 0.0));
    flat.sourceFrom = 1000.0;
    flat.sourceTo = 95000.0;
    std::vector<float> out(2000, 1.0f);
    renderWarped(flat, sources, at120, 40000, 2000, out.data(), Interpolation::Hermite);
    renderWarped(flat, sources, at120, 40000, 2000, out.data(), Interpolation::Linear);
    bool added = true;
    for (const float v : out)
        added = added && std::fabs(v - 1.5f) < 1e-6f;
    DAWLAB_CHECK(added);
    WarpedClip none = flat;
    none.source = 7;
    renderWarped(none, sources, at120, 40000, 2000, out.data(), Interpolation::Sinc);
    none.source = -1;
    renderWarped(none, sources, at120, 40000, 2000, out.data(), Interpolation::Sinc);
    DAWLAB_CHECK(std::fabs(out[0] - 1.5f) < 1e-6f && std::fabs(out[1999] - 1.5f) < 1e-6f);

    // The sinc gets the speed of the place it reads: 15 kHz made at 120 BPM, played at 240, is not an alias.
    WarpedClip high = flat;
    high.source = 1;
    std::vector<float> fast(20000, 0.0f), fastHermite(20000, 0.0f);
    renderWarped(high, sources, at240, 2000, 20000, fast.data(), Interpolation::Sinc);
    renderWarped(high, sources, at240, 2000, 20000, fastHermite.data(), Interpolation::Hermite);
    DAWLAB_CHECK(rmsDb(fast) < -60.0 && rmsDb(fastHermite) > -10.0);
}

inline void checkBeats() {
    // A take with a click on every beat, recorded at 120 BPM. Under a map of other tempos the clicks are on its beats.
    const TempoMap at120 = mapOf({{0, 120.0, false}});
    std::vector<float> clicks(16 * 24000, 0.0f);
    for (int beat = 0; beat < 16; ++beat)
        clicks[static_cast<size_t>(beat) * 24000] = 1.0f;
    const std::vector<std::vector<float>> sources{clicks};
    const WarpedClip take = takeClip(at120, 0, 16 * 24000);
    const TempoMap now = mapOf({{0, 100.0, false}, {3840, 150.0, false}, {7680, 70.0, true}, {11520, 130.0, false}});
    const std::int64_t length = endSample(take, now);
    if (length <= 0 || length > 4000000) {
        DAWLAB_CHECK(false);
        return;
    }
    const std::vector<float> out = renderInBlocks({take}, sources, now, length, Interpolation::Linear, 2);
    int onBeat = 0;
    for (int beat = 0; beat < 16; ++beat) {
        const std::int64_t want = std::llround(now.secondsAt(beat * 960.0) * kRate);
        std::int64_t loudest = -1;
        float peak = 0.2f;
        for (std::int64_t s = std::max<std::int64_t>(want - 200, 0); s < std::min(want + 200, length); ++s)
            if (std::fabs(out[static_cast<size_t>(s)]) > peak) {
                peak = std::fabs(out[static_cast<size_t>(s)]);
                loudest = s;
            }
        onBeat += loudest >= 0 && std::llabs(loudest - want) <= 1 ? 1 : 0;
    }
    std::printf("  16 clicks recorded at 120 BPM, played over 100, 150 and a ramp from 70 to 130: %d on their beats (the last at %.3f s, was %.3f s)\n", onBeat,
                now.secondsAt(15 * 960.0), 7.5);
    DAWLAB_CHECK(onBeat == 16);
}

} // namespace step03_check

int main() {
    using namespace step03_check;
    std::printf("Step 3: varispeed\n");
    checkPitch();
    checkReading();
    checkPositions();
    checkNoDrift();
    checkClipRange();
    checkSeam();
    checkRender();
    checkBeats();
    return dawlab::test::summary("l20_step03");
}
