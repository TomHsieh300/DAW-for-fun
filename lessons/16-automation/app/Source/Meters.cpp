#include "Meters.h"

#include "DawLookAndFeel.h"

#include <algorithm>
#include <cmath>

namespace dawlab::app {

// ---- MeterBar -----------------------------------------------------------------------------------

MeterBar::MeterBar(Orientation orientation) : orientation_(orientation) {
    setOpaque(true);
}

float MeterBar::dbToProportion(float db) noexcept {
    if (!(db > kMinDb))
        return 0.0f;
    return std::min(1.0f, (db - kMinDb) / (kMaxDb - kMinDb));
}

void MeterBar::setLevels(float peakLeftDb, float peakRightDb, float holdLeftDb, float holdRightDb) {
    const std::array<float, 2> p{peakLeftDb, peakRightDb};
    const std::array<float, 2> h{holdLeftDb, holdRightDb};
    bool changed = false;
    for (size_t ch = 0; ch < 2; ++ch) {
        if (std::abs(dbToProportion(p[ch]) - dbToProportion(peak_[ch])) > 0.0015f ||
            std::abs(dbToProportion(h[ch]) - dbToProportion(hold_[ch])) > 0.0015f)
            changed = true;
        peak_[ch] = p[ch];
        hold_[ch] = h[ch];
    }
    if (changed)
        repaint();
}

void MeterBar::paint(juce::Graphics& g) {
    g.fillAll(DawColours::background());
    const auto area = getLocalBounds().toFloat().reduced(1.0f);
    const bool vertical = orientation_ == Orientation::Vertical;
    const float gap = 2.0f;

    // Colour zones: green below -18 dBFS, yellow up to -6, red above.
    const auto zoneColour = [](float db) {
        return db > -6.0f ? DawColours::meterHigh() : (db > -18.0f ? DawColours::meterMid() : DawColours::meterLow());
    };

    for (int ch = 0; ch < 2; ++ch) {
        juce::Rectangle<float> bar;
        if (vertical) {
            const float w = (area.getWidth() - gap) * 0.5f;
            bar = {area.getX() + static_cast<float>(ch) * (w + gap), area.getY(), w, area.getHeight()};
        } else {
            const float h = (area.getHeight() - gap) * 0.5f;
            bar = {area.getX(), area.getY() + static_cast<float>(ch) * (h + gap), area.getWidth(), h};
        }
        g.setColour(DawColours::panel());
        g.fillRect(bar);

        // Draw the level in three segments so each zone keeps its colour.
        const float level = dbToProportion(peak_[static_cast<size_t>(ch)]);
        const float zones[] = {dbToProportion(-18.0f), dbToProportion(-6.0f), 1.0f};
        float from = 0.0f;
        for (float to : zones) {
            const float a = from, b = std::min(level, to);
            if (b > a) {
                g.setColour(zoneColour(MeterBar::kMinDb + (a + b) * 0.5f * (MeterBar::kMaxDb - MeterBar::kMinDb)));
                if (vertical)
                    g.fillRect(bar.getX(), bar.getBottom() - b * bar.getHeight(), bar.getWidth(), (b - a) * bar.getHeight());
                else
                    g.fillRect(bar.getX() + a * bar.getWidth(), bar.getY(), (b - a) * bar.getWidth(), bar.getHeight());
            }
            from = to;
        }
        // Peak hold line
        const float hold = dbToProportion(hold_[static_cast<size_t>(ch)]);
        if (hold > 0.0f) {
            g.setColour(hold_[static_cast<size_t>(ch)] > 0.0f ? DawColours::meterHigh() : DawColours::text());
            if (vertical)
                g.fillRect(bar.getX(), bar.getBottom() - hold * bar.getHeight() - 1.0f, bar.getWidth(), 2.0f);
            else
                g.fillRect(bar.getX() + hold * bar.getWidth() - 1.0f, bar.getY(), 2.0f, bar.getHeight());
        }
    }
    // 0 dBFS tick
    g.setColour(DawColours::textDim());
    const float zero = dbToProportion(0.0f);
    if (vertical)
        g.fillRect(area.getX(), area.getBottom() - zero * area.getHeight(), area.getWidth(), 1.0f);
    else
        g.fillRect(area.getX() + zero * area.getWidth(), area.getY(), 1.0f, area.getHeight());
}

// ---- LoudnessDisplay ----------------------------------------------------------------------------

LoudnessDisplay::LoudnessDisplay() = default;

juce::String LoudnessDisplay::formatLufs(float lufs) {
    return lufs > -70.0f ? juce::String(lufs, 1) : juce::String("-inf");
}

void LoudnessDisplay::setValues(float momentaryLufs, float shortTermLufs, float integratedLufs) {
    if (std::abs(momentaryLufs - m_) < 0.05f && std::abs(shortTermLufs - s_) < 0.05f &&
        std::abs(integratedLufs - i_) < 0.05f)
        return;
    m_ = momentaryLufs;
    s_ = shortTermLufs;
    i_ = integratedLufs;
    repaint();
}

void LoudnessDisplay::paint(juce::Graphics& g) {
    auto area = getLocalBounds();
    g.setColour(DawColours::background());
    g.fillRoundedRectangle(area.toFloat(), 3.0f);
    area.reduce(4, 2);
    const int w = area.getWidth() / 3;
    const struct {
        const char* label;
        float value;
    } items[] = {{"M", m_}, {"S", s_}, {"I", i_}};
    for (const auto& item : items) {
        auto cell = area.removeFromLeft(w);
        g.setColour(DawColours::textDim());
        g.setFont(DawLookAndFeel::uiFont(11.0f));
        g.drawText(item.label, cell.removeFromLeft(12), juce::Justification::centredLeft);
        g.setColour(DawColours::text());
        g.setFont(DawLookAndFeel::uiFont(14.0f, true));
        g.drawText(formatLufs(item.value), cell, juce::Justification::centredLeft);
    }
}

// ---- SpectrumView -------------------------------------------------------------------------------

namespace {
constexpr double kMinHz = 20.0;
constexpr double kMaxHz = 20000.0;
constexpr float kTopDb = 0.0f;
constexpr float kBottomDb = -90.0f;
} // namespace

SpectrumView::SpectrumView() { setOpaque(true); }

float SpectrumView::frequencyToProportion(double hz) noexcept {
    // log axis: x = log(f / fmin) / log(fmax / fmin)
    hz = std::clamp(hz, kMinHz, kMaxHz);
    return static_cast<float>(std::log(hz / kMinHz) / std::log(kMaxHz / kMinHz));
}

double SpectrumView::proportionToFrequency(float proportion) noexcept {
    return kMinHz * std::pow(kMaxHz / kMinHz, static_cast<double>(std::clamp(proportion, 0.0f, 1.0f)));
}

bool SpectrumView::update(dsp::SpectrumAnalyzer& analyzer) {
    if (!analyzer.computeSpectrum(magnitudesDb_))
        return false;
    const size_t n = magnitudesDb_.size();
    if (binFrequencies_.size() != n) {
        binFrequencies_.resize(n);
        for (size_t i = 0; i < n; ++i)
            binFrequencies_[i] = analyzer.getBinFrequency(static_cast<int>(i));
        smoothed_.assign(n, kBottomDb);
    }
    // Ballistics on top of the analyzer's own smoothing: rise immediately, fall 1.5 dB per frame
    // (~45 dB/s at 30 Hz), so the display is calm but never late on transients.
    for (size_t i = 0; i < n; ++i)
        smoothed_[i] = std::max(magnitudesDb_[i], smoothed_[i] - 1.5f);
    repaint();
    return true;
}

bool SpectrumView::getPeak(double& frequencyHz, float& levelDb) const noexcept {
    if (smoothed_.size() < 2)
        return false;
    size_t best = 1; // skip DC
    for (size_t i = 2; i < smoothed_.size(); ++i)
        if (smoothed_[i] > smoothed_[best])
            best = i;
    frequencyHz = binFrequencies_[best];
    levelDb = smoothed_[best];
    return true;
}

void SpectrumView::paint(juce::Graphics& g) {
    const auto area = getLocalBounds().toFloat();
    g.fillAll(DawColours::background());

    // grid: decades and dB lines (the labels are drawn last, on top of the spectrum)
    static constexpr double kGridHz[] = {50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0};
    const auto dbToY = [&area](float db) { return area.getY() + (kTopDb - db) / (kTopDb - kBottomDb) * area.getHeight(); };
    g.setColour(DawColours::outline().withAlpha(0.6f));
    for (double f : kGridHz)
        g.drawVerticalLine(static_cast<int>(area.getX() + frequencyToProportion(f) * area.getWidth()), area.getY(),
                           area.getBottom());
    for (float db = -20.0f; db > kBottomDb; db -= 20.0f)
        g.drawHorizontalLine(static_cast<int>(dbToY(db)), area.getX(), area.getRight());

    if (smoothed_.size() >= 2)
        paintSpectrum(g, area);

    // Labels on a dark backing so that the trace can never hide them.
    g.setFont(DawLookAndFeel::uiFont(10.0f));
    const auto label = [&g](const juce::String& text, int x, int y) {
        const int w = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), text) + 4;
        g.setColour(DawColours::background().withAlpha(0.75f));
        g.fillRect(x, y, w, 11);
        g.setColour(DawColours::textDim());
        g.drawText(text, x + 2, y, w - 2, 11, juce::Justification::left);
    };
    for (double f : kGridHz) {
        const int x = static_cast<int>(area.getX() + frequencyToProportion(f) * area.getWidth());
        label(f >= 1000.0 ? juce::String(static_cast<int>(f / 1000.0)) + "k" : juce::String(static_cast<int>(f)), x + 1,
              static_cast<int>(area.getBottom()) - 12);
    }
    label("0 dB", 2, static_cast<int>(area.getY()) + 2); // the top edge of the plot
    for (float db = -20.0f; db > kBottomDb; db -= 20.0f)
        label(juce::String(static_cast<int>(db)) + " dB", 2, static_cast<int>(dbToY(db)) - 12);

    g.setColour(DawColours::outline());
    g.drawRect(getLocalBounds(), 1);
}

void SpectrumView::paintSpectrum(juce::Graphics& g, juce::Rectangle<float> area) {
    // One point per pixel column: the maximum of all bins that fall into it (high frequencies
    // have many bins per pixel on a log axis, low frequencies less than one).
    juce::Path path;
    const int width = static_cast<int>(area.getWidth());
    size_t bin = 1;
    bool started = false;
    for (int px = 0; px < width; ++px) {
        const double fHi = proportionToFrequency(static_cast<float>(px + 1) / static_cast<float>(width));
        float peak = kBottomDb;
        bool any = false;
        while (bin < smoothed_.size() && binFrequencies_[bin] < fHi) {
            peak = std::max(peak, smoothed_[bin]);
            any = true;
            ++bin;
        }
        if (!any) // less than one bin per pixel (low frequencies): reuse the next bin above
        {
            const size_t b = std::min(bin, smoothed_.size() - 1);
            peak = smoothed_[b];
        }
        const float y = area.getY() + std::clamp((kTopDb - peak) / (kTopDb - kBottomDb), 0.0f, 1.0f) * area.getHeight();
        const float x = area.getX() + static_cast<float>(px);
        if (!started) {
            path.startNewSubPath(x, area.getBottom());
            started = true;
        }
        path.lineTo(x, y);
    }
    path.lineTo(area.getRight(), area.getBottom());
    path.closeSubPath();
    g.setColour(DawColours::accent().withAlpha(0.25f));
    g.fillPath(path);
    g.setColour(DawColours::accent());
    g.strokePath(path, juce::PathStrokeType(1.2f));
}

} // namespace dawlab::app
