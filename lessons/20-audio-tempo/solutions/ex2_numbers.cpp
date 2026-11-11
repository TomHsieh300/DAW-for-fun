// Exercise 2 (solution): audio and tempo by the numbers.
//
// (a) a loop at another tempo: the speed, and the pitch by varispeed
// (b) the markers of a take of a ramp: how many, and what one piece is off
// (c) what the sinc costs when it reads faster than 1
// (d) what WSOLA's search costs
// (e) what a render costs in memory
// (f) a tempo change while a clip plays: how far the audio inside it jumps
//
// The derivations are in solutions/README.md; this program only proves the numbers, with the steps' models.

#include "../steps/warp_map.h"

#include <dawlab/Check.h>

#include <cmath>
#include <cstdio>

using namespace minidaw;

int main() {
    std::printf("Exercise 2 - audio and tempo by the numbers\n");

    // (a) A drum loop made at 128 BPM, in a song at 120 and at 140.
    WarpMap loop;
    loop.set(markersForLoop(static_cast<std::int64_t>(std::llround(8 * 60.0 / 128.0 * 48000.0)), 8.0, 0.0)); // two bars
    TempoMap at120, at140;
    at120.set(48000.0, {{0, 120.0, false}});
    at140.set(48000.0, {{0, 140.0, false}});
    const auto speedAt = [&loop](const TempoMap& map) {
        const double samplesPerTick = (loop.sourceAt(960.0) - loop.sourceAt(0.0)) / 960.0;
        return samplesPerTick * map.bpmAt(0.0) * kTicksPerQuarter / (60.0 * map.sampleRate());
    };
    const double s120 = speedAt(at120), s140 = speedAt(at140);
    std::printf("(a) at 120 BPM: speed %.4f, %+.3f semitones; at 140 BPM: speed %.4f, %+.3f semitones\n", s120, 12.0 * std::log2(s120), s140,
                12.0 * std::log2(s140));
    DAWLAB_CHECK_NEAR(s120, 120.0 / 128.0, 1e-4);
    DAWLAB_CHECK_NEAR(s140, 140.0 / 128.0, 1e-4);
    DAWLAB_CHECK_NEAR(12.0 * std::log2(120.0 / 128.0), -1.1173, 1e-4);
    DAWLAB_CHECK_NEAR(12.0 * std::log2(140.0 / 128.0), 1.5513, 1e-4);

    // (b) A take of a ramp from 60 to 120 BPM over four bars, within 1 ms.
    TempoMap ramp;
    ramp.set(48000.0, {{0, 60.0, true}, {15360, 120.0, false}});
    const double k = 60.0 / 15360.0;                                // BPM a tick
    const double curve = 60.0 / kTicksPerQuarter * k / (60.0 * 60.0); // max |f''|, at the slow end (s per tick^2)
    const int pieces = static_cast<int>(std::ceil(15360.0 * std::sqrt(curve / (8.0 * 0.001))));
    const double h = 15360.0 / pieces;
    const std::int64_t length = static_cast<std::int64_t>(ramp.secondsAt(15360.0) * 48000.0);
    const std::vector<WarpMarker> markers = markersForTake(ramp, 0, length, 0.001);
    std::printf("(b) max|f''| = %.4g s/tick^2: %d pieces of %.1f ticks, %zu markers; one piece is off by at most %.4f ms\n", curve, pieces, h,
                markers.size(), 1000.0 * h * h / 8.0 * curve);
    DAWLAB_CHECK(pieces == 45 && markers.size() == 46);
    DAWLAB_CHECK(h * h / 8.0 * curve <= 0.001);
    // The real error of the first piece, where the ramp is slowest: the chord against the curve, at its middle.
    const double a = 0.0, b = h, mid = h / 2.0;
    const double chord = ramp.secondsAt(a) + (ramp.secondsAt(b) - ramp.secondsAt(a)) / 2.0;
    std::printf("    the first piece at its middle: %.4f ms off\n", 1000.0 * std::fabs(chord - ramp.secondsAt(mid)));
    DAWLAB_CHECK(std::fabs(chord - ramp.secondsAt(mid)) <= 0.001);

    // (c) The sinc at speed 1 and 2.
    const int reach1 = 16, reach2 = static_cast<int>(16 / 0.5);
    std::printf("(c) the sinc reaches %d samples to each side at speed 1 (%d in all), %d at speed 2 (%d in all)\n", reach1, 2 * reach1 + 1, reach2,
                2 * reach2 + 1);
    DAWLAB_CHECK(2 * reach2 + 1 == 65);

    // (d) WSOLA: pieces of 1024, a new one every 512 output samples, 513 offsets, two products per sample.
    const double perSecond = 48000.0 / 512.0 * 513.0 * 512.0 * 2.0;
    std::printf("(d) %.2f pieces a second, %.1f million multiplications a second of output\n", 48000.0 / 512.0, perSecond / 1e6);
    DAWLAB_CHECK_NEAR(perSecond, 49.248e6, 1e3);

    // (e) A 4-minute stereo clip stretched to speed 0.8, at 48 kHz.
    const double seconds = 240.0 / 0.8;
    const double audio = seconds * 48000.0 * 2.0 * sizeof(float), positions = seconds * 48000.0 * sizeof(double);
    std::printf("(e) %.0f s of output: %.1f MB of audio, and %.1f MB of positions while it is rendered\n", seconds, audio / 1e6, positions / 1e6);
    DAWLAB_CHECK_NEAR(audio, 115.2e6, 1.0);
    DAWLAB_CHECK_NEAR(positions, 115.2e6, 1.0);

    // (f) A clip whose start follows, on bar 2. The song plays at 120 BPM and is one beat and a half into the clip when
    //     the tempo becomes 90. How far does the audio inside the clip jump?
    TempoMap at90;
    at90.set(48000.0, {{0, 90.0, false}});
    const double into120 = at120.secondsAt(3840.0 + 1440.0) - at120.secondsAt(3840.0), into90 = at90.secondsAt(3840.0 + 1440.0) - at90.secondsAt(3840.0);
    std::printf("(f) %.3f s into the clip before, %.3f s after: the audio jumps %.3f s (%.0f samples) ahead\n", into120, into90, into90 - into120,
                (into90 - into120) * 48000.0);
    DAWLAB_CHECK_NEAR(into90 - into120, 0.25, 1e-12);

    return dawlab::test::summary("l20_ex2");
}
