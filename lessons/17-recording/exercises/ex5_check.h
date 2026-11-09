#pragma once
// Exercise 5 - the self-check (main). Included at the end of the starter and of the solution.

#include "ex5_input_health.h"

#include <dawlab/Check.h>

#include <cmath>
#include <cstdio>
#include <random>

namespace ex5::check {

constexpr double kRate = 48000.0;
constexpr double kPi = 3.14159265358979323846;

inline bool has(const std::vector<std::string>& w, const char* part) {
    for (const std::string& s : w)
        if (s.find(part) != std::string::npos)
            return true;
    return false;
}

// One channel through add(), in blocks of random sizes (a run may cross the edge of a block).
inline InputHealth feed(const std::vector<float>& x, unsigned seed = 1) {
    InputHealth h;
    h.reset(kRate);
    std::mt19937 rng(seed);
    for (size_t at = 0; at < x.size();) {
        const int n = static_cast<int>(std::min<size_t>(1 + rng() % 700, x.size() - at));
        const float* ch[] = {x.data() + at};
        h.add(ch, 1, n);
        at += static_cast<size_t>(n);
    }
    return h;
}

// The clips of a whole signal, counted the plain way: runs of one sign at full scale, kClipRun or longer.
inline void countClips(const std::vector<float>& x, int& clips, int& longest) {
    clips = longest = 0;
    size_t i = 0;
    while (i < x.size()) {
        if (std::abs(x[i]) < InputHealth::kFullScale) {
            ++i;
            continue;
        }
        size_t j = i;
        while (j < x.size() && std::abs(x[j]) >= InputHealth::kFullScale && (x[j] > 0.0f) == (x[i] > 0.0f))
            ++j;
        const int length = static_cast<int>(j - i);
        if (length >= InputHealth::kClipRun) {
            ++clips;
            longest = std::max(longest, length);
        }
        i = j;
    }
}

inline std::vector<float> noise(size_t n, float sigma, unsigned seed) {
    std::vector<float> x(n);
    std::mt19937 rng(seed);
    std::normal_distribution<float> d(0.0f, sigma);
    for (float& s : x)
        s = d(rng);
    return x;
}

inline std::vector<float> sine(size_t n, double amplitude, double hz) {
    std::vector<float> x(n);
    for (size_t i = 0; i < n; ++i)
        x[i] = static_cast<float>(std::clamp(amplitude * std::sin(2.0 * kPi * hz * static_cast<double>(i) / kRate), -1.0, 1.0));
    return x;
}

inline void run() {
    // Step 7's first recording: 3 s of exact zeros.
    InputHealth h = feed(std::vector<float>(144000, 0.0f));
    std::vector<std::string> w = h.warnings();
    DAWLAB_CHECK(h.frames() == 144000 && h.zeroFrames() == 144000);
    DAWLAB_CHECK(w.size() == 1 && has(w, "exactly 0"));
    if (!w.empty())
        std::printf("  muted:   %s\n", w[0].c_str());

    // Step 7's second: a laptop's microphone, peaking at about -64 dBFS.
    h = feed(noise(144000, 0.00015f, 3));
    w = h.warnings();
    char peak[32];
    std::snprintf(peak, sizeof(peak), "%.1f dBFS", 20.0 * std::log10(h.peak()));
    DAWLAB_CHECK(w.size() == 1 && has(w, "quiet") && has(w, peak));
    if (!w.empty())
        std::printf("  quiet:   %s\n", w[0].c_str());

    // A healthy take.
    h = feed(sine(96000, 0.5, 440.0));
    DAWLAB_CHECK(h.warnings().empty() && std::abs(h.peak() - 0.5f) < 1e-3f);

    // Too hot: a 1 kHz sine at 1.5 - flat tops, two clips a period.
    const std::vector<float> hot = sine(48000, 1.5, 1000.0);
    int clips = 0, longest = 0;
    countClips(hot, clips, longest);
    bool same = true;
    for (unsigned seed = 1; seed <= 5; ++seed) {
        const InputHealth b = feed(hot, seed);
        same = same && b.clips() == clips && b.longestClip() == longest;
    }
    h = feed(hot);
    w = h.warnings();
    DAWLAB_CHECK(clips == 2000 && same);
    DAWLAB_CHECK(w.size() == 1 && has(w, "clipped") && has(w, std::to_string(clips).c_str()) && has(w, std::to_string(longest).c_str()));
    if (!w.empty())
        std::printf("  hot:     %s\n", w[0].c_str());

    // Full scale is not always a clip.
    std::vector<float> peaks(4800, 0.1f);
    peaks[100] = 1.0f;                       // one loud sample
    peaks[200] = peaks[201] = -1.0f;         // two
    for (int i = 300; i < 400; ++i)          // a full-scale square at Nyquist: one sample per sign
        peaks[static_cast<size_t>(i)] = i % 2 == 0 ? 1.0f : -1.0f;
    peaks[500] = peaks[501] = 1.0f;          // two of one sign, then
    peaks[502] = peaks[503] = -1.0f;         // two of the other
    h = feed(peaks);
    DAWLAB_CHECK(h.clips() == 0 && h.warnings().empty());
    peaks[600] = peaks[601] = peaks[602] = -0.9995f; // three: a clip
    peaks[700] = 1.0f;                                // one up, then three down: one clip
    peaks[701] = peaks[702] = peaks[703] = -1.0f;
    h = feed(peaks);
    DAWLAB_CHECK(h.clips() == 2 && h.longestClip() == 3);

    // Two channels: each has its own runs.
    {
        InputHealth two;
        two.reset(kRate);
        std::vector<float> left(1000, 0.2f), right(1000, 0.2f);
        for (int i = 10; i < 12; ++i)
            left[static_cast<size_t>(i)] = 1.0f;  // two on the left...
        for (int i = 12; i < 14; ++i)
            right[static_cast<size_t>(i)] = 1.0f; // ...and two on the right: no clip of three anywhere
        for (int i = 500; i < 520; ++i)
            right[static_cast<size_t>(i)] = -1.0f;
        for (int at = 0; at < 1000; at += 250) {
            const float* ch[] = {left.data() + at, right.data() + at};
            two.add(ch, 2, 250);
        }
        DAWLAB_CHECK(two.clips() == 1 && two.longestClip() == 20 && two.frames() == 1000);
    }

    // The input stops for 0.2 s in the middle; silence before the first sound and after the last is no gap.
    std::vector<float> gap = noise(48000, 0.05f, 5);
    gap.insert(gap.end(), 9600, 0.0f);
    const std::vector<float> more = noise(48000, 0.05f, 6);
    gap.insert(gap.end(), more.begin(), more.end());
    h = feed(gap);
    w = h.warnings();
    DAWLAB_CHECK(h.longestGap() == 9600 && w.size() == 1 && has(w, "stopped") && has(w, "0.200 s"));
    if (!w.empty())
        std::printf("  gap:     %s\n", w[0].c_str());
    std::vector<float> edges(24000, 0.0f);
    const std::vector<float> middle = noise(48000, 0.05f, 7);
    edges.insert(edges.end(), middle.begin(), middle.end());
    edges.insert(edges.end(), 24000, 0.0f);
    h = feed(edges);
    DAWLAB_CHECK(h.longestGap() == 0 && h.warnings().empty());
    std::vector<float> shortGap = noise(48000, 0.05f, 8);
    shortGap.insert(shortGap.end(), 4000, 0.0f); // 83 ms: a pause, not a failure
    shortGap.insert(shortGap.end(), more.begin(), more.end());
    h = feed(shortGap);
    DAWLAB_CHECK(h.longestGap() == 4000 && h.warnings().empty());

    // Muted AND nothing else: one sentence, not three.
    h = feed(std::vector<float>(1000, 0.0f));
    DAWLAB_CHECK(h.warnings().size() == 1);
    // reset() forgets the last take.
    h.reset(kRate);
    DAWLAB_CHECK(h.frames() == 0 && h.clips() == 0 && h.peak() == 0.0f && h.warnings().empty());
}

} // namespace ex5::check

int main() {
    std::printf("Exercise 5 - tell the user what is wrong with the input\n");
    ex5::check::run();
    return dawlab::test::summary("l17_ex5");
}
