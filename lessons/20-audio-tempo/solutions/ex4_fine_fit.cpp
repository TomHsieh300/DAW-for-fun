// Exercise 4 (solution): a fit to a fraction of a sample.
//
// Build and run:  l20_ex4_solution   (ctest, label "solution")
// Your version:   exercises/starter/ex4_fine_fit.cpp -> l20_ex4_starter

#include "../exercises/ex4_fine_fit.h"

#include <cmath>

namespace ex4 {

double refine(double before, double at, double after) noexcept {
    const double bend = before - 2.0 * at + after;
    if (!(bend < 0.0))
        return 0.0; // not a peak: a straight line, a valley, or not numbers
    return std::clamp(0.5 * (before - after) / bend, -0.5, 0.5);
}

double readHermite(const std::vector<float>& x, double position) noexcept {
    const double whole = std::floor(position);
    const std::int64_t i = static_cast<std::int64_t>(whole);
    const double f = position - whole;
    const double y0 = inputAt(x, i - 1), y1 = inputAt(x, i), y2 = inputAt(x, i + 1), y3 = inputAt(x, i + 2);
    const double c1 = 0.5 * (y2 - y0);
    const double c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3;
    const double c3 = 1.5 * (y1 - y2) + 0.5 * (y3 - y0);
    return ((c3 * f + c2) * f + c1) * f + y1;
}

std::vector<float> stretchFine(const std::vector<float>& x, const std::vector<double>& positions, const StretchSettings& settings) {
    if (settings.window < 4 || settings.window % 2 != 0 || settings.search < 0)
        return {};
    const std::int64_t length = static_cast<std::int64_t>(positions.size());
    const int half = settings.window / 2;
    std::vector<double> sum(positions.size(), 0.0);
    std::vector<double> natural(static_cast<size_t>(half));
    const auto score = [&](std::int64_t start) { // against the natural continuation, read at its fraction
        double cross = 0.0, energy = 0.0;
        for (int i = 0; i < half; ++i) {
            const double a = inputAt(x, start + i);
            cross += a * natural[static_cast<size_t>(i)];
            energy += a * a;
        }
        return energy > 0.0 ? cross / std::sqrt(energy) : 0.0;
    };

    bool first = true;
    double before = 0.0; // where the piece before started in the input: a fraction now
    for (std::int64_t at = -half; at < length; at += half) {
        const std::int64_t candidate = std::llround(positionAt(positions, at + half)) - half;
        double start = static_cast<double>(candidate);
        const double next = before + half;
        if (!first && std::fabs(next - static_cast<double>(candidate)) > 1e-9) {
            for (int i = 0; i < half; ++i)
                natural[static_cast<size_t>(i)] = readHermite(x, next + i);
            int best = 0;
            double bestScore = score(candidate);
            for (int distance = 1; distance <= settings.search; ++distance)
                for (const int d : {distance, -distance}) {
                    const double s = score(candidate + d);
                    if (s > bestScore) {
                        bestScore = s;
                        best = d;
                    }
                }
            start = static_cast<double>(candidate + best) + refine(score(candidate + best - 1), bestScore, score(candidate + best + 1));
        } else if (!first) {
            start = next; // it IS the natural continuation
        }
        for (int i = 0; i < settings.window; ++i)
            if (at + i >= 0 && at + i < length)
                sum[static_cast<size_t>(at + i)] += hann(i, settings.window) * readHermite(x, start + i);
        before = start;
        first = false;
    }
    std::vector<float> out(sum.size());
    for (size_t n = 0; n < sum.size(); ++n)
        out[n] = static_cast<float>(sum[n]);
    return out;
}

} // namespace ex4

#include "../exercises/ex4_check.h"
