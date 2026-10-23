// Exercise 4 (starter code): fader taper -- a piecewise-linear mapping between fader position (0..1) and dB
//
// Breakpoints (position -> dB):
//   1.00 -> +6     0.75 -> 0     0.50 -> -12     0.25 -> -30     0.05 -> -60     0.00 -> -inf
// Every segment from 0.05 upward is "dB linear in position"; the bottom segment [0, 0.05] is "linear gain linear in position",
// running smoothly from -60 dB down to 0 (true silence).
// You are done when every self-check passes after you complete the TODOs. The starter code prints FAIL when it runs; that is expected.

#include <dawlab/Common.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace dawlab;

namespace {

struct Breakpoint {
    float position;
    float db;
};

constexpr float kBottomPosition = 0.05f;
constexpr float kBottomDb = -60.0f;
constexpr Breakpoint kPoints[] = {{0.05f, -60.0f}, {0.25f, -30.0f}, {0.5f, -12.0f}, {0.75f, 0.0f}, {1.0f, 6.0f}};
constexpr int kNumPoints = 5;

} // namespace

// Fader position -> linear gain
float faderToGain(float position) {
    const float p = std::clamp(position, 0.0f, 1.0f);
    // TODO: when p <= kBottomPosition: gain = dbToGain(kBottomDb) * (p / kBottomPosition)
    //       otherwise find which segment of kPoints p falls into, interpolate linearly in dB (dawlab::lerp), then apply dbToGain
    return p;
}

float faderToDb(float position) { return gainToDb(faderToGain(position)); }

// Inverse function: linear gain -> fader position
float gainToFader(float gain) {
    const float g = std::max(0.0f, gain);
    // TODO: the inverse of faderToGain (the bottom segment is inverted in gain; for the other segments convert gain to dB and invert in dB)
    return g;
}

float dbToFader(float db) { return gainToFader(dbToGain(db)); }

int main() {
    std::printf("%8s %10s %10s\n", "position", "dB", "gain");
    for (int i = 0; i <= 20; ++i) {
        const float p = 0.05f * static_cast<float>(i);
        std::printf("%8.2f %10.2f %10.5f\n", p, faderToDb(p), faderToGain(p));
    }

    DAWLAB_CHECK_NEAR(faderToDb(1.0f), 6.0, 1e-4);
    DAWLAB_CHECK_NEAR(faderToDb(0.75f), 0.0, 1e-4);
    DAWLAB_CHECK_NEAR(faderToDb(0.5f), -12.0, 1e-4);
    DAWLAB_CHECK_NEAR(faderToDb(0.25f), -30.0, 1e-4);
    DAWLAB_CHECK_NEAR(faderToDb(0.05f), -60.0, 1e-4);
    DAWLAB_CHECK(faderToGain(0.0f) == 0.0f);
    DAWLAB_CHECK_NEAR(faderToDb(0.875f), 3.0, 1e-4);
    DAWLAB_CHECK_NEAR(faderToDb(0.625f), -6.0, 1e-4);
    DAWLAB_CHECK_NEAR(faderToGain(0.025f), dbToGain(-60.0f) * 0.5, 1e-7);

    bool monotonic = true;
    float prev = -1.0f;
    for (int i = 0; i <= 1000; ++i) {
        const float g = faderToGain(0.001f * static_cast<float>(i));
        monotonic = monotonic && (g >= prev);
        prev = g;
    }
    DAWLAB_CHECK(monotonic);

    float maxErr = 0.0f;
    for (int i = 0; i <= 1000; ++i) {
        const float p = 0.001f * static_cast<float>(i);
        maxErr = std::max(maxErr, std::abs(gainToFader(faderToGain(p)) - p));
    }
    std::printf("round-trip max error: %.2e\n", maxErr);
    DAWLAB_CHECK(maxErr < 1e-4f);
    DAWLAB_CHECK_NEAR(dbToFader(0.0f), 0.75, 1e-5);
    DAWLAB_CHECK_NEAR(dbToFader(-12.0f), 0.5, 1e-5);
    DAWLAB_CHECK_NEAR(dbToFader(6.0f), 1.0, 1e-5);
    DAWLAB_CHECK_NEAR(dbToFader(-200.0f), 0.0, 1e-6);
    DAWLAB_CHECK_NEAR(dbToFader(20.0f), 1.0, 1e-6);

    return dawlab::test::summary("l01_ex4_starter");
}
