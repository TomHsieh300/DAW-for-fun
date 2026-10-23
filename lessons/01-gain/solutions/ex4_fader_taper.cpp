// Exercise 4 solution: fader taper -- a piecewise-linear mapping between fader position (0..1) and dB
//
// Breakpoints (position -> dB):
//   1.00 -> +6     0.75 -> 0     0.50 -> -12     0.25 -> -30     0.05 -> -60     0.00 -> -inf
// Every segment from 0.05 upward is "dB linear in position" (so the upper half of the fader changes only a little dB per step, which suits fine adjustment),
// while the bottom [0, 0.05] is "linear gain linear in position", running smoothly from -60 dB down to 0 (true silence).
// If the bottom segment were also linear in dB, the fader could never reach -inf; if the whole fader were linear in gain, the upper half would be squeezed into a few dB
// and the lower half would be almost entirely "inaudible" -- which is why DAW faders are logarithmic / piecewise.

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

// From bottom to top; the bottom segment [0, kBottomPosition] is handled specially
constexpr float kBottomPosition = 0.05f;
constexpr float kBottomDb = -60.0f;
constexpr Breakpoint kPoints[] = {{0.05f, -60.0f}, {0.25f, -30.0f}, {0.5f, -12.0f}, {0.75f, 0.0f}, {1.0f, 6.0f}};
constexpr int kNumPoints = 5;

} // namespace

// Fader position -> linear gain
float faderToGain(float position) {
    const float p = std::clamp(position, 0.0f, 1.0f);
    if (p <= kBottomPosition) // bottom segment: gain is linear in position, exactly 0 at p = 0
        return dbToGain(kBottomDb) * (p / kBottomPosition);
    for (int i = 1; i < kNumPoints; ++i) {
        if (p <= kPoints[i].position) {
            const float t = (p - kPoints[i - 1].position) / (kPoints[i].position - kPoints[i - 1].position);
            return dbToGain(lerp(kPoints[i - 1].db, kPoints[i].db, t)); // dB is linear in position
        }
    }
    return dbToGain(kPoints[kNumPoints - 1].db);
}

float faderToDb(float position) { return gainToDb(faderToGain(position)); }

// Inverse function: linear gain -> fader position (used when the UI needs to draw the current gain back as a fader position)
float gainToFader(float gain) {
    const float g = std::max(0.0f, gain);
    const float bottomGain = dbToGain(kBottomDb);
    if (g <= bottomGain)
        return kBottomPosition * (g / bottomGain);
    const float db = gainToDb(g);
    for (int i = 1; i < kNumPoints; ++i) {
        if (db <= kPoints[i].db) {
            const float t = (db - kPoints[i - 1].db) / (kPoints[i].db - kPoints[i - 1].db);
            return lerp(kPoints[i - 1].position, kPoints[i].position, t);
        }
    }
    return 1.0f;
}

float dbToFader(float db) { return gainToFader(dbToGain(db)); }

int main() {
    std::printf("%8s %10s %10s\n", "position", "dB", "gain");
    for (int i = 0; i <= 20; ++i) {
        const float p = 0.05f * static_cast<float>(i);
        std::printf("%8.2f %10.2f %10.5f\n", p, faderToDb(p), faderToGain(p));
    }

    // The breakpoints must be exact
    DAWLAB_CHECK_NEAR(faderToDb(1.0f), 6.0, 1e-4);
    DAWLAB_CHECK_NEAR(faderToDb(0.75f), 0.0, 1e-4);
    DAWLAB_CHECK_NEAR(faderToDb(0.5f), -12.0, 1e-4);
    DAWLAB_CHECK_NEAR(faderToDb(0.25f), -30.0, 1e-4);
    DAWLAB_CHECK_NEAR(faderToDb(0.05f), -60.0, 1e-4);
    DAWLAB_CHECK(faderToGain(0.0f) == 0.0f);          // fader at the bottom = true silence
    DAWLAB_CHECK_NEAR(faderToDb(0.875f), 3.0, 1e-4);  // dB is linear within a segment: the midpoint of 0.75..1.0 = +3 dB
    DAWLAB_CHECK_NEAR(faderToDb(0.625f), -6.0, 1e-4); // the midpoint of 0.5..0.75 = -6 dB
    DAWLAB_CHECK_NEAR(faderToGain(0.025f), dbToGain(-60.0f) * 0.5, 1e-7); // the bottom segment is linear in gain

    // Monotonically increasing (moving the fader up can only make the sound louder)
    bool monotonic = true;
    float prev = -1.0f;
    for (int i = 0; i <= 1000; ++i) {
        const float g = faderToGain(0.001f * static_cast<float>(i));
        monotonic = monotonic && (g >= prev);
        prev = g;
    }
    DAWLAB_CHECK(monotonic);

    // Round trip through the inverse function
    float maxErr = 0.0f;
    for (int i = 0; i <= 1000; ++i) {
        const float p = 0.001f * static_cast<float>(i);
        maxErr = std::max(maxErr, std::abs(gainToFader(faderToGain(p)) - p));
    }
    std::printf("round-trip max error of gainToFader(faderToGain(p)): %.2e\n", maxErr);
    DAWLAB_CHECK(maxErr < 1e-4f);
    DAWLAB_CHECK_NEAR(dbToFader(0.0f), 0.75, 1e-5);
    DAWLAB_CHECK_NEAR(dbToFader(-12.0f), 0.5, 1e-5);
    DAWLAB_CHECK_NEAR(dbToFader(6.0f), 1.0, 1e-5);
    DAWLAB_CHECK_NEAR(dbToFader(-200.0f), 0.0, 1e-6);  // below -100 dB counts as -inf
    DAWLAB_CHECK_NEAR(dbToFader(20.0f), 1.0, 1e-6);    // above the upper limit is clamped to the top

    return dawlab::test::summary("l01_ex4");
}
