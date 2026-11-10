// Chapter 20, Step 4 (solution): a time-stretcher.
//
// Build and run:  l20_step04_solution   (ctest, label "solution")
// Your version:   starter/step04.cpp -> l20_step04_starter

#include "../minidaw.h"

namespace minidaw {

int bestOffset(const std::vector<float>& x, std::int64_t candidate, std::int64_t natural, const StretchSettings& settings) noexcept {
    if (candidate == natural || settings.search <= 0)
        return 0;
    const int half = settings.window / 2;
    const auto score = [&](int d) {
        double cross = 0.0, energy = 0.0;
        for (int i = 0; i < half; ++i) {
            const double a = inputAt(x, candidate + d + i), b = inputAt(x, natural + i);
            cross += a * b;
            energy += a * a;
        }
        return energy > 0.0 ? cross / std::sqrt(energy) : 0.0;
    };
    int best = 0;
    double bestScore = score(0);
    for (int distance = 1; distance <= settings.search; ++distance) // outwards: the nearest of equal scores stays
        for (const int d : {distance, -distance}) {
            const double s = score(d);
            if (s > bestScore) {
                bestScore = s;
                best = d;
            }
        }
    return best;
}

std::vector<float> stretch(const std::vector<float>& x, const std::vector<double>& positions, const StretchSettings& settings) {
    if (settings.window < 4 || settings.window % 2 != 0 || settings.search < 0)
        return {};
    const std::int64_t length = static_cast<std::int64_t>(positions.size());
    const int half = settings.window / 2;
    std::vector<double> sum(positions.size(), 0.0);

    bool first = true;
    std::int64_t before = 0; // where the piece before this one started in the input
    for (std::int64_t at = -half; at < length; at += half) {
        const std::int64_t candidate = std::llround(positionAt(positions, at + half)) - half;
        const std::int64_t start = first ? candidate : candidate + bestOffset(x, candidate, before + half, settings);
        for (int i = 0; i < settings.window; ++i)
            if (at + i >= 0 && at + i < length)
                sum[static_cast<size_t>(at + i)] += hann(i, settings.window) * inputAt(x, start + i);
        before = start;
        first = false;
    }
    std::vector<float> out(sum.size());
    for (size_t n = 0; n < sum.size(); ++n)
        out[n] = static_cast<float>(sum[n]);
    return out;
}

} // namespace minidaw

#include "../check.h"
