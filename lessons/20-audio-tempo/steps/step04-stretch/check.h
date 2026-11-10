#pragma once
// Chapter 20, Step 4 - the self-check (main). Included at the end of the starter and of the solution.

#include "minidaw.h"

#include <dawlab/Check.h>

#include <cstdio>
#include <random>

namespace step04_check {

using namespace minidaw;

constexpr double kRate = 48000.0;

inline std::vector<float> sine(double hz, int samples, double amplitude) {
    std::vector<float> s(static_cast<size_t>(samples));
    for (int i = 0; i < samples; ++i)
        s[static_cast<size_t>(i)] = static_cast<float>(amplitude * std::sin(dawlab::kTwoPi * hz * i / kRate));
    return s;
}

// Output sample n sounds like input sample n x speed.
inline std::vector<double> steady(double speed, int outputSamples) {
    std::vector<double> p(static_cast<size_t>(outputSamples));
    for (int n = 0; n < outputSamples; ++n)
        p[static_cast<size_t>(n)] = n * speed;
    return p;
}

// The part of y from `from` to `to`.
inline std::vector<float> part(const std::vector<float>& y, size_t from, size_t to) {
    to = std::min(to, y.size());
    from = std::min(from, to);
    return std::vector<float>(y.begin() + static_cast<std::ptrdiff_t>(from), y.begin() + static_cast<std::ptrdiff_t>(to));
}

// A tone, measured in stretches of 4096 samples: in each the best sine of `hz` (its size and phase free), and what
// is left when it is taken out. A stretcher may move the phase a little from piece to piece - that is a tiny error
// of pitch, not noise - so the phase is free in every stretch, and its walk says how far the pitch is off.
struct Tone {
    double noiseDb = -200.0; // what is not the tone, in dB below it (the higher the better)
    double cents = 0.0;      // the pitch against `hz`, in hundredths of a semitone
};

inline Tone measureTone(const std::vector<float>& y, double hz) {
    constexpr size_t kStretch = 4096;
    const double w = dawlab::kTwoPi * hz / kRate;
    double signal = 0.0, noise = 0.0, turned = 0.0, before = 0.0;
    size_t stretches = 0;
    for (size_t from = 0; from + kStretch <= y.size(); from += kStretch) {
        double ss = 0.0, cc = 0.0, sc = 0.0, ys = 0.0, yc = 0.0;
        for (size_t i = from; i < from + kStretch; ++i) {
            const double sn = std::sin(w * static_cast<double>(i)), cs = std::cos(w * static_cast<double>(i));
            ss += sn * sn, cc += cs * cs, sc += sn * cs, ys += y[i] * sn, yc += y[i] * cs;
        }
        const double det = ss * cc - sc * sc;
        if (std::fabs(det) < 1e-12)
            continue;
        const double a = (ys * cc - yc * sc) / det, b = (yc * ss - ys * sc) / det;
        for (size_t i = from; i < from + kStretch; ++i) {
            const double fit = a * std::sin(w * static_cast<double>(i)) + b * std::cos(w * static_cast<double>(i));
            signal += fit * fit;
            noise += (y[i] - fit) * (y[i] - fit);
        }
        const double phase = std::atan2(b, a);
        if (stretches > 0) {
            double step = phase - before;
            while (step > dawlab::kPi)
                step -= dawlab::kTwoPi;
            while (step < -dawlab::kPi)
                step += dawlab::kTwoPi;
            turned += step;
        }
        before = phase;
        ++stretches;
    }
    Tone tone;
    if (signal > 0.0 && stretches > 1) {
        tone.noiseDb = 10.0 * std::log10(signal / std::max(noise, 1e-300));
        const double cyclesPerSample = turned / (dawlab::kTwoPi * static_cast<double>((stretches - 1) * kStretch));
        tone.cents = 1200.0 * std::log2(1.0 + cyclesPerSample * kRate / hz);
    }
    return tone;
}

inline double rmsDb(const std::vector<float>& y) {
    double sum = 0.0;
    for (const float v : y)
        sum += static_cast<double>(v) * v;
    return 10.0 * std::log10(std::max(sum / static_cast<double>(std::max<size_t>(y.size(), 1)), 1e-30));
}

inline void checkOffset() {
    // A wave that repeats every 100 samples. A piece fits where it is a whole number of periods from the
    // natural continuation.
    const std::vector<float> wave = sine(480.0, 20000, 0.5);
    const StretchSettings s; // window 1024, search 256
    DAWLAB_CHECK(bestOffset(wave, 5000, 5030, s) == 30);   // 30 on, not 70 back
    DAWLAB_CHECK(bestOffset(wave, 5000, 5070, s) == -30);
    DAWLAB_CHECK(bestOffset(wave, 5000, 5050, s) == 50);   // 50 either way: the positive one
    DAWLAB_CHECK(bestOffset(wave, 5000, 5000, s) == 0 && bestOffset(wave, 5000, 5300, s) == 0); // three periods on: it fits as it is
    DAWLAB_CHECK((bestOffset(wave, 5000, 5030, {1024, 0}) == 0));  // no search: plain OLA
    DAWLAB_CHECK((bestOffset(wave, 5000, 5030, {1024, 20}) == 20)); // not allowed far enough: as near as it gets
    DAWLAB_CHECK(bestOffset(std::vector<float>(20000, 0.0f), 5000, 5030, s) == 0); // silence fits anywhere: stay
    // The score is about the SHAPE. Pieces of 200 samples; the natural continuation is one period at 9000. It is
    // found again 30 samples on. A copy four times as loud but 10 samples out of step, 150 back, must not win for
    // being loud; and a copy a hundred times quieter and 3 samples out of step, 100 back, must not win for being
    // quiet.
    const StretchSettings small{200, 256};
    const auto period = [](int i, int shift) { return static_cast<float>(0.5 * std::sin(dawlab::kTwoPi * (i + shift) / 100.0)); };
    std::vector<float> loud(12000, 0.0f), quiet(12000, 0.0f);
    for (int k = 0; k < 100; ++k) {
        loud[static_cast<size_t>(9000 + k)] = quiet[static_cast<size_t>(9000 + k)] = period(k, 0);
        loud[static_cast<size_t>(5030 + k)] = quiet[static_cast<size_t>(5030 + k)] = period(k, 0);
        loud[static_cast<size_t>(4850 + k)] = 4.0f * period(k, 10);
        quiet[static_cast<size_t>(4900 + k)] = 0.01f * period(k, 3);
    }
    DAWLAB_CHECK(bestOffset(loud, 5000, 9000, small) == 30);
    DAWLAB_CHECK(bestOffset(quiet, 5000, 9000, small) == 30);
    // At the edge of the input a piece hangs over into silence: no crash, an offset in range.
    const int edge = bestOffset(wave, 19990, 19500, s);
    DAWLAB_CHECK(edge >= -256 && edge <= 256);
}

inline void checkUnchanged() {
    std::mt19937 rng(50);
    std::vector<float> noise(30000);
    for (float& v : noise)
        v = std::uniform_real_distribution<float>(-1.0f, 1.0f)(rng);
    // Speed 1: the pieces are each other's natural continuation, and two Hann halves add up to 1.
    std::vector<double> positions(20000);
    for (size_t n = 0; n < positions.size(); ++n)
        positions[n] = 3000.0 + static_cast<double>(n);
    const std::vector<float> out = stretch(noise, positions, {});
    DAWLAB_CHECK(out.size() == positions.size());
    float worst = 0.0f;
    for (size_t n = 0; n < out.size(); ++n)
        worst = std::max(worst, std::fabs(out[n] - noise[n + 3000]));
    std::printf("  noise at speed 1: at most %.1g from the input\n", static_cast<double>(worst));
    DAWLAB_CHECK(!out.empty() && worst < 1e-5f);

    DAWLAB_CHECK(stretch(noise, {}, {}).empty());
    DAWLAB_CHECK((stretch(noise, positions, {1023, 256}).empty() && stretch(noise, positions, {2, 0}).empty() && stretch(noise, positions, {1024, -1}).empty()));
    // Silence in, silence out; and an input that is too short is silence beyond its end.
    const std::vector<float> quiet = stretch(std::vector<float>(5000, 0.0f), steady(0.5, 8000), {});
    DAWLAB_CHECK(quiet.size() == 8000 && rmsDb(quiet) < -200.0);
    const std::vector<float> beyond = stretch(sine(440.0, 4800, 0.5), steady(1.0, 24000), {});
    DAWLAB_CHECK(beyond.size() == 24000 && rmsDb(part(beyond, 8000, 24000)) < -200.0 && rmsDb(part(beyond, 1000, 4000)) > -12.0);
}

inline void checkTone() {
    // 440 Hz, slower and faster: still 440 Hz (within 2 hundredths of a semitone), and as long as the speed says.
    // What is not the tone is the damage: a piece can only slide by whole samples, so it fits to half a sample.
    const std::vector<float> tone = sine(440.0, 4 * 48000, 0.5);
    bool fine = true;
    for (const double speed : {0.5, 0.8, 1.25, 2.0}) {
        const int length = static_cast<int>(2.0 * 48000 / speed); // two seconds of the tone
        const std::vector<float> wsola = stretch(tone, steady(speed, length), {});
        const std::vector<float> ola = stretch(tone, steady(speed, length), {1024, 0});
        if (static_cast<int>(wsola.size()) != length || static_cast<int>(ola.size()) != length) {
            fine = false;
            continue;
        }
        const Tone good = measureTone(part(wsola, 2048, wsola.size() - 2048), 440.0);
        const Tone plain = measureTone(part(ola, 2048, ola.size() - 2048), 440.0);
        const double level = rmsDb(part(wsola, 2048, wsola.size() - 2048)) - rmsDb(tone);
        const double levelPlain = rmsDb(part(ola, 2048, ola.size() - 2048)) - rmsDb(tone);
        std::printf("  440 Hz at speed %.2f: %.1f s long, %+.2f cents, what is not the tone %.1f dB below it, the level %+.2f dB (plain OLA: %5.1f dB, %+.2f dB)\n",
                    speed, length / kRate, good.cents, good.noiseDb, level, plain.noiseDb, levelPlain);
        fine = fine && good.noiseDb > 28.0 && good.noiseDb > plain.noiseDb + 15.0 && std::fabs(level) < 0.5 && std::fabs(good.cents) < 2.0;
    }
    DAWLAB_CHECK(fine);
}

inline void checkLevel() {
    // Something a piece cannot fit: noise, and a chord of three tones that never line up again. The level stays.
    std::mt19937 rng(51);
    std::vector<float> noise(3 * 48000), chord(3 * 48000);
    for (size_t i = 0; i < noise.size(); ++i) {
        noise[i] = std::uniform_real_distribution<float>(-0.5f, 0.5f)(rng);
        chord[i] = static_cast<float>(0.2 * (std::sin(dawlab::kTwoPi * 220.0 * static_cast<double>(i) / kRate) + std::sin(dawlab::kTwoPi * 277.18 * static_cast<double>(i) / kRate) +
                                             std::sin(dawlab::kTwoPi * 329.63 * static_cast<double>(i) / kRate)));
    }
    bool fine = true;
    for (const double speed : {0.6, 1.5}) {
        const int length = static_cast<int>(2.0 * 48000 / speed);
        const std::vector<float> n = stretch(noise, steady(speed, length), {}), c = stretch(chord, steady(speed, length), {});
        const std::vector<float> cPlain = stretch(chord, steady(speed, length), {1024, 0});
        const double dn = rmsDb(n) - rmsDb(noise), dc = rmsDb(c) - rmsDb(chord), dp = rmsDb(cPlain) - rmsDb(chord);
        std::printf("  at speed %.1f the level of noise changes by %+.2f dB, of a chord by %+.2f dB (plain OLA: %+.2f dB)\n", speed, dn, dc, dp);
        fine = fine && std::fabs(dn) < 2.0 && std::fabs(dc) < 1.0 && !n.empty();
    }
    DAWLAB_CHECK(fine);
}

inline void checkFollows() {
    // A tempo change under the clip: the first second of the output at speed 0.75, then at 1.5.
    const std::vector<float> tone = sine(440.0, 4 * 48000, 0.5);
    std::vector<double> positions(96000);
    for (size_t n = 0; n < positions.size(); ++n)
        positions[n] = n < 48000 ? 0.75 * static_cast<double>(n) : 36000.0 + 1.5 * static_cast<double>(n - 48000);
    const std::vector<float> out = stretch(tone, positions, {});
    DAWLAB_CHECK(out.size() == 96000);
    if (out.size() != 96000)
        return;
    const Tone slow = measureTone(part(out, 2048, 46000), 440.0), fast = measureTone(part(out, 50000, 94000), 440.0);
    std::printf("  speed 0.75 for a second, then 1.5: 440 Hz in both (%+.2f and %+.2f cents, %.1f and %.1f dB)\n", slow.cents, fast.cents, slow.noiseDb,
                fast.noiseDb);
    DAWLAB_CHECK(slow.noiseDb > 28.0 && fast.noiseDb > 28.0 && std::fabs(slow.cents) < 2.0 && std::fabs(fast.cents) < 2.0);
}

struct Clicks {
    int heard = 0;      // clicks with a copy of at least half their size
    int twice = 0;      // clicks with a second copy of at least a quarter
    int worstOff = 0;   // the loudest copy's distance from where the click belongs, samples
};

inline Clicks clicksAt(double speed, const StretchSettings& settings) {
    // A click every half second, 8 of them (the first at 0.25 s).
    std::vector<float> input(4 * 48000 + 24000, 0.0f);
    for (int k = 0; k < 8; ++k)
        input[static_cast<size_t>(12000 + k * 24000)] = 1.0f;
    const int length = static_cast<int>(4.0 * 48000 / speed);
    const std::vector<float> out = stretch(input, steady(speed, length), settings);
    Clicks result;
    if (static_cast<int>(out.size()) != length)
        return result;
    for (int k = 0; k < 8; ++k) {
        const int want = static_cast<int>(std::lround((12000 + k * 24000) / speed));
        int loudest = -1;
        float peak = 0.0f;
        for (int s = std::max(want - 2048, 0); s < std::min(want + 2048, length); ++s)
            if (std::fabs(out[static_cast<size_t>(s)]) > peak) {
                peak = std::fabs(out[static_cast<size_t>(s)]);
                loudest = s;
            }
        if (loudest < 0 || peak < 0.5f)
            continue;
        ++result.heard;
        result.worstOff = std::max(result.worstOff, std::abs(loudest - want));
        for (int s = std::max(want - 2048, 0); s < std::min(want + 2048, length); ++s)
            if (s != loudest && std::fabs(out[static_cast<size_t>(s)]) >= 0.25f) {
                ++result.twice;
                break;
            }
    }
    return result;
}

inline void checkClicks() {
    // What it costs: a sound that happens once.
    const StretchSettings s;
    const Clicks slow = clicksAt(0.5, s), near1 = clicksAt(0.9, s), fast = clicksAt(2.0, s);
    std::printf("  8 clicks at speed 0.5: %d heard, %d of them twice, the loudest copy at most %d samples from its place\n", slow.heard, slow.twice, slow.worstOff);
    std::printf("  8 clicks at speed 0.9: %d heard, %d of them twice, at most %d samples off\n", near1.heard, near1.twice, near1.worstOff);
    std::printf("  8 clicks at speed 2.0: %d heard, %d of them twice, at most %d samples off\n", fast.heard, fast.twice, fast.worstOff);
    // Wherever a click is heard, it is within half a window and the search of its place.
    DAWLAB_CHECK(slow.worstOff <= 512 + 256 && near1.worstOff <= 512 + 256 && fast.worstOff <= 512 + 256);
    // Stretched to twice the length every click is there, and close to speed 1 none is doubled.
    DAWLAB_CHECK(slow.heard == 8 && near1.heard == 8 && near1.twice == 0);
}

} // namespace step04_check

int main() {
    using namespace step04_check;
    std::printf("Step 4: a time-stretcher\n");
    checkOffset();
    checkUnchanged();
    checkTone();
    checkLevel();
    checkFollows();
    checkClicks();
    return dawlab::test::summary("l20_step04");
}
