// Exercise 2 (solution): automation by the numbers.
//
// (a) a fade read once per block: values a second, the step per block in normalized units and in dB
// (b) Step 2's slew limit: how long a jump takes
// (c) thinning a recorded pass by hand (Ramer-Douglas-Peucker, Step 3)
// (d) what a 30 s fader ride costs before and after thinning
//
// The derivations are in solutions/README.md; this program only proves the numbers. Its thin() and
// slew() are written the way Steps 2 and 3 describe them.

#include <dawlab/Check.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

constexpr double kRate = 48000.0;
constexpr double kDbSpan = 60.0; // the exercise's gain: normalized 0..1 = -60..0 dB, linear in dB

struct Point {
    double x = 0.0, v = 0.0;
};

// Step 3: keep the points without which the line would be off by more than `tolerance` (in value).
void thinInto(const std::vector<Point>& p, size_t a, size_t b, double tolerance, std::vector<bool>& keep) {
    if (b <= a + 1)
        return;
    double worst = -1.0;
    size_t at = a;
    for (size_t i = a + 1; i < b; ++i) {
        const double line = p[a].v + (p[b].v - p[a].v) * (p[i].x - p[a].x) / (p[b].x - p[a].x);
        const double off = std::abs(p[i].v - line);
        if (off > worst) {
            worst = off;
            at = i;
        }
    }
    if (worst > tolerance) {
        keep[at] = true;
        thinInto(p, a, at, tolerance, keep);
        thinInto(p, at, b, tolerance, keep);
    }
}

std::vector<size_t> thin(const std::vector<Point>& p, double tolerance) {
    std::vector<bool> keep(p.size(), false);
    keep.front() = keep.back() = true;
    thinInto(p, 0, p.size() - 1, tolerance, keep);
    std::vector<size_t> kept;
    for (size_t i = 0; i < p.size(); ++i)
        if (keep[i])
            kept.push_back(i);
    return kept;
}

// Step 2's limit: at most 1/declick per sample. Returns the samples until `to` is reached.
int slewSamples(double from, double to, int declick) {
    const double step = 1.0 / declick;
    double value = from;
    int n = 0;
    while (value != to) {
        value = to - value > step ? value + step : (value - to > step ? value - step : to);
        ++n;
    }
    return n;
}

} // namespace

int main() {
    std::printf("Exercise 2 - automation by the numbers\n");

    // (a) a fade 0 -> 1 over 2 s, read once per 512-sample block.
    const double perSecond = kRate / 512;
    const double step = 1.0 / (2.0 * perSecond);
    std::printf("(a) %.2f values a second; a 2 s fade moves %.6f a block = %.3f dB; per sample %.6f dB\n", perSecond, step,
                step * kDbSpan, kDbSpan / (2.0 * kRate));
    DAWLAB_CHECK_NEAR(perSecond, 93.75, 1e-12);
    DAWLAB_CHECK_NEAR(step, 0.0053333, 1e-7);
    DAWLAB_CHECK_NEAR(step * kDbSpan, 0.32, 1e-9);
    DAWLAB_CHECK_NEAR(kDbSpan / (2.0 * kRate), 0.000625, 1e-12);

    // (b) a jump from 0.2 to 0.8 with declick 64; and a full jump.
    const int jump = slewSamples(0.2, 0.8, 64), full = slewSamples(0.0, 1.0, 64);
    std::printf("(b) 0.2 -> 0.8: %d samples = %.4f ms; 0 -> 1: %d samples = %.4f ms\n", jump, 1e3 * jump / kRate, full, 1e3 * full / kRate);
    DAWLAB_CHECK(jump == 39);
    DAWLAB_CHECK(full == 64);

    // (c) a pass, one point every 4800 samples, tolerance 0.05.
    const std::vector<Point> pass = {{0, 0.00}, {4800, 0.10}, {9600, 0.22}, {14400, 0.30}, {19200, 0.42}, {24000, 0.90}, {28800, 1.00}};
    const std::vector<size_t> kept = thin(pass, 0.05);
    std::printf("(c) kept:");
    for (const size_t i : kept)
        std::printf(" %zu", i);
    std::printf("\n");
    DAWLAB_CHECK((kept == std::vector<size_t>{0, 4, 5, 6}));
    DAWLAB_CHECK((thin(pass, 0.25) == std::vector<size_t>{0, 6}));      // 0.2467 is the largest distance
    DAWLAB_CHECK((thin(pass, 0.0) == std::vector<size_t>{0, 1, 2, 3, 4, 5, 6}));
    std::printf("    tolerance 0.002 on a 60 dB fader = %.3f dB\n", 0.002 * kDbSpan);
    DAWLAB_CHECK_NEAR(0.002 * kDbSpan, 0.12, 1e-12);

    // (d) 30 s of a fader ride: one event per block, 16 bytes a point.
    const auto events = static_cast<long long>(std::ceil(30.0 * perSecond));
    std::printf("(d) 30 s: %lld points = %lld bytes before thinning\n", events, events * 16);
    DAWLAB_CHECK(events == 2813);
    // A slow, straight fade thins to its two ends; a wobbly ride keeps what the ear could hear.
    std::vector<Point> fade, wobble;
    for (long long i = 0; i < events; ++i) {
        const double x = static_cast<double>(i) * 512.0;
        fade.push_back({x, static_cast<double>(i) / static_cast<double>(events - 1)});
        wobble.push_back({x, 0.5 + 0.2 * std::sin(2.0 * 3.14159265358979 * 0.5 * x / kRate)}); // 0.5 Hz
    }
    const size_t fadeKept = thin(fade, 0.002).size(), wobbleKept = thin(wobble, 0.002).size();
    std::printf("    after thinning (0.002): a straight fade %zu points, a 0.5 Hz wobble %zu points\n", fadeKept, wobbleKept);
    DAWLAB_CHECK(fadeKept == 2);
    DAWLAB_CHECK(wobbleKept > 30 && wobbleKept < 400);
    return dawlab::test::summary("l16_ex2");
}
