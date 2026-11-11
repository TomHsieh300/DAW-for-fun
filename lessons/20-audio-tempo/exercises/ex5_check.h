#pragma once
// Exercise 5 - the self-check (main). Included at the end of the starter and of the solution.

#include "ex5_declick.h"

#include "../steps/warp_map.h"

#include <dawlab/Check.h>

#include <cstdio>
#include <random>

namespace ex5::check {

inline std::vector<float> sine(double hz, int samples, double amplitude) {
    std::vector<float> s(static_cast<size_t>(samples));
    for (int i = 0; i < samples; ++i)
        s[static_cast<size_t>(i)] = static_cast<float>(amplitude * std::sin(2.0 * kPi * hz * i / 48000.0));
    return s;
}

// The largest step from one output sample to the next: a click is a large one.
inline double largestStep(const std::vector<float>& y) {
    double worst = 0.0;
    for (size_t i = 1; i < y.size(); ++i)
        worst = std::max(worst, std::fabs(static_cast<double>(y[i]) - y[i - 1]));
    return worst;
}

// Reads `blocks` (a list of position lists) through a Declicker into one output.
inline std::vector<float> play(Declicker& d, const std::vector<float>& source, const std::vector<std::vector<double>>& blocks) {
    std::vector<float> out;
    for (const std::vector<double>& b : blocks) {
        std::vector<float> part(b.size(), 0.0f);
        d.read(source, b, part.data());
        out.insert(out.end(), part.begin(), part.end());
    }
    return out;
}

inline std::vector<double> line(double from, double speed, int n) {
    std::vector<double> p(static_cast<size_t>(n));
    for (int k = 0; k < n; ++k)
        p[static_cast<size_t>(k)] = from + k * speed;
    return p;
}

inline void checkNoJump() {
    // Reading that goes on - at one speed, and with a speed that changes at a block's edge - is read as it is.
    const std::vector<float> tone = sine(440.0, 100000, 0.5);
    std::mt19937 rng(50);
    std::vector<std::vector<double>> blocks;
    double at = 100.0, speed = 1.3;
    for (int b = 0; b < 60; ++b) {
        const int n = std::uniform_int_distribution<int>(1, 700)(rng);
        blocks.push_back(line(at, speed, n));
        at += n * speed;
        if (b % 7 == 3)
            speed = std::uniform_real_distribution<double>(0.5, 2.0)(rng);
    }
    Declicker d(480, 2.0);
    const std::vector<float> heard = play(d, tone, blocks);
    bool same = true;
    size_t k = 0;
    for (const auto& b : blocks)
        for (const double p : b)
            same = same && heard[k++] == static_cast<float>(readHermite(tone, p));
    DAWLAB_CHECK(same && !d.isFading());
    // Reading in blocks of one sample: the speed of a call of one sample is 1.
    Declicker one(480, 0.5);
    std::vector<float> o(1, 0.0f);
    one.read(tone, {10.0}, o.data());
    one.read(tone, {11.0}, o.data());
    one.read(tone, {12.0}, o.data());
    DAWLAB_CHECK(!one.isFading());
    one.read(tone, {14.0}, o.data()); // 2 samples on, the threshold is 0.5: a jump
    DAWLAB_CHECK(one.isFading());
    // After a block at speed 2, a call of one sample has speed 1: the next one goes on from there.
    Declicker two(480, 0.5);
    std::vector<float> ten(10, 0.0f);
    two.read(tone, line(0.0, 2.0, 10), ten.data()); // goes on at 20
    two.read(tone, {20.0}, o.data());                // then at 21
    two.read(tone, {21.0}, o.data());
    DAWLAB_CHECK(!two.isFading());
    // The threshold: a start 1.5 samples off is no jump, 2.5 samples off is one.
    Declicker near(480, 2.0), far(480, 2.0);
    std::vector<float> fifty(50, 0.0f);
    near.read(tone, line(100.0, 1.0, 50), fifty.data());
    near.read(tone, line(151.5, 1.0, 5), fifty.data());
    far.read(tone, line(100.0, 1.0, 50), fifty.data());
    far.read(tone, line(152.5, 1.0, 5), fifty.data());
    DAWLAB_CHECK(!near.isFading() && far.isFading());
}

inline void checkFadeCurve() {
    // Source: 0.25 up to sample 20000, -0.25 after. A jump from reading 0.25 to reading -0.25: the output is the
    // two equal-power curves, sample by sample.
    std::vector<float> two(40000, 0.25f);
    for (size_t i = 20000; i < two.size(); ++i)
        two[i] = -0.25f;
    Declicker d(9, 1.0);
    std::vector<float> a(100, 0.0f), b(20, 0.0f);
    d.read(two, line(1000.0, 1.0, 100), a.data());
    d.read(two, line(30000.0, 1.0, 20), b.data());
    bool exact = true;
    for (int k = 0; k < 20; ++k) {
        const double t = k < 9 ? (k + 1) / 10.0 : 1.0;
        const double want = k < 9 ? -0.25 * std::sin(t * kPi / 2.0) + 0.25 * std::cos(t * kPi / 2.0) : -0.25;
        exact = exact && std::fabs(b[static_cast<size_t>(k)] - want) < 1e-6;
    }
    DAWLAB_CHECK(exact && !d.isFading());
    // The output is ADDED to what is there.
    std::vector<float> c(5, 1.0f);
    Declicker fresh(9, 1.0);
    fresh.read(two, line(1000.0, 1.0, 5), c.data());
    DAWLAB_CHECK(std::fabs(c[0] - 1.25f) < 1e-6f && std::fabs(c[4] - 1.25f) < 1e-6f);
}

inline void checkOldPath() {
    // Source: a ramp, x[i] = i / 100000. The old path goes on at the old speed; the new path reads past the end,
    // where there is silence - so the output is old x cos, and says where the old path read.
    std::vector<float> ramp(60000);
    for (size_t i = 0; i < ramp.size(); ++i)
        ramp[i] = static_cast<float>(static_cast<double>(i) / 100000.0);
    Declicker d(50, 1.0);
    std::vector<float> a(200, 0.0f), b(30, 0.0f), c(40, 0.0f);
    d.read(ramp, line(5000.0, 1.25, 200), a.data()); // goes on at 5250, speed 1.25
    d.read(ramp, line(90000.0, 0.8, 30), b.data());  // a jump: a fade of 50 begins
    DAWLAB_CHECK(d.isFading());
    d.read(ramp, line(90024.0, 0.8, 40), c.data());  // the fade goes on into this call (and ends in it)
    DAWLAB_CHECK(!d.isFading());
    bool along = true;
    for (int k = 0; k < 50; ++k) {
        const double t = (k + 1) / 51.0;
        const float y = k < 30 ? b[static_cast<size_t>(k)] : c[static_cast<size_t>(k - 30)];
        const double want = (5250.0 + 1.25 * k) / 100000.0 * std::cos(t * kPi / 2.0);
        along = along && std::fabs(y - want) < 1e-6;
    }
    DAWLAB_CHECK(along);
    // After the fade only the new path, which is silence here.
    DAWLAB_CHECK(c[20] == 0.0f && c[39] == 0.0f);
}

inline void checkTempoChange() {
    // A clip of a 440 Hz tone whose start follows the tempo, on bar 2; between two blocks, 20 blocks into the clip,
    // the tempo becomes 90 BPM and the playhead keeps its musical place. The clip reads by the sample: it jumps.
    using namespace minidaw;
    const std::vector<float> tone = sine(440.0, 8 * 48000, 0.5);
    TempoMap at120, at90;
    at120.set(48000.0, {{0, 120.0, false}});
    at90.set(48000.0, {{0, 90.0, false}});
    const auto positions = [&](const TempoMap& map, std::int64_t from, int n) {
        const double clipStart = map.secondsAt(3840.0) * 48000.0; // bar 2 under this map
        return line(static_cast<double>(from) - clipStart, 1.0, n);
    };
    // A clip that follows by varispeed would read by the tick, which the playhead keeps: no jump there.
    WarpMap warp;
    warp.set(markersForLoop(8 * 48000, 16.0, 3840.0));
    const double tickThen = at120.tickAt((96000.0 + 40 * 512) / 48000.0);
    const double readBefore = warp.sourceAt(tickThen), readAfter = warp.sourceAt(at90.tickAt(std::llround(at90.secondsAt(tickThen) * 48000.0) / 48000.0));
    DAWLAB_CHECK(std::fabs(readBefore - readAfter) < 1.0);
    std::vector<std::vector<double>> blocks;
    std::int64_t song = 96000;
    for (int b = 0; b < 40; ++b, song += 512)
        blocks.push_back(positions(at120, song, 512));
    const double tick = at120.tickAt(static_cast<double>(song) / 48000.0);
    song = std::llround(at90.secondsAt(tick) * 48000.0); // the same musical place under the new map
    for (int b = 0; b < 40; ++b, song += 512)
        blocks.push_back(positions(at90, song, 512));

    Declicker raw(480, 1e9), smooth(480, 4.0);
    const std::vector<float> clicks = play(raw, tone, blocks), soft = play(smooth, tone, blocks);
    const double natural = 0.5 * 2.0 * kPi * 440.0 / 48000.0; // the largest step of the tone itself
    std::printf("  120 to 90 BPM while a 440 Hz clip whose start follows plays: the largest step from one sample to the next %.3f without a fade, %.3f with one\n"
                "    (the tone's own: %.3f)\n",
                largestStep(clicks), largestStep(soft), natural);
    DAWLAB_CHECK(largestStep(clicks) > 10.0 * natural && largestStep(soft) < 1.6 * natural);
    // After the fade it is the plain reading again, sample for sample.
    bool after = true;
    for (size_t k = 40 * 512 + 480; k < soft.size(); ++k)
        after = after && soft[k] == clicks[k];
    DAWLAB_CHECK(after);

    // A second jump while the first fade is going on: no click either, and the fade starts again.
    std::vector<std::vector<double>> twice(blocks.begin(), blocks.begin() + 41);
    twice.push_back(line(200000.0, 0.75, 200));
    twice.push_back(line(200150.0, 0.75, 600));
    Declicker again(480, 4.0);
    const std::vector<float> both = play(again, tone, twice);
    DAWLAB_CHECK(largestStep(both) < 1.6 * natural);
    // reset(): the next call is not a jump, wherever it reads.
    again.reset();
    std::vector<float> o(100, 0.0f);
    again.read(tone, line(5.0, 1.0, 100), o.data());
    DAWLAB_CHECK(!again.isFading() && o[50] == static_cast<float>(readHermite(tone, 55.0)));
}

} // namespace ex5::check

int main() {
    using namespace ex5::check;
    std::printf("Exercise 5 - a fade over a jump\n");
    checkNoJump();
    checkFadeCurve();
    checkOldPath();
    checkTempoChange();
    return dawlab::test::summary("l20_ex5");
}
