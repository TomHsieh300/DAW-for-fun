#pragma once
// Meters: the components that *display* measurements (Steps 13, 15 and 20).
//
// The measurements themselves are made on the audio thread by the chapter-11 classes inside the
// engine (dsp::LevelMeter, dsp::LoudnessMeter per strip) and by AudioHost (dsp::SpectrumAnalyzer
// on the master output). They publish their results through atomics. The components below are
// passive: MainComponent's 30 Hz timer reads the atomics and calls setLevels() / setValues() /
// update(); the components only repaint when what they show has changed.
//
//   MeterBar         stereo peak bars + peak-hold lines, vertical (strips) or horizontal
//                    (transport bar). The dB scale is linear in dB between kMinDb and kMaxDb.
//   LoudnessDisplay  momentary / short-term / integrated LUFS (EBU R 128, chapter 11).
//   SpectrumView     the master spectrum on a logarithmic frequency axis, 20 Hz - 20 kHz.

#include <dawlab/dsp/SpectrumAnalyzer.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <vector>

namespace dawlab::app {

class MeterBar final : public juce::Component, public juce::SettableTooltipClient {
public:
    enum class Orientation { Vertical, Horizontal };
    static constexpr float kMinDb = -60.0f;
    static constexpr float kMaxDb = 6.0f;

    explicit MeterBar(Orientation orientation = Orientation::Vertical);

    // Peak and peak-hold per channel, in dBFS. Repaints only if a value moved by > 0.1 dB.
    void setLevels(float peakLeftDb, float peakRightDb, float holdLeftDb, float holdRightDb);

    // Position of `db` along the meter, 0 (kMinDb and below) .. 1 (kMaxDb).
    static float dbToProportion(float db) noexcept;

    float getPeakDb(int channel) const noexcept { return peak_[static_cast<size_t>(channel & 1)]; }

    void paint(juce::Graphics&) override;

private:
    Orientation orientation_;
    std::array<float, 2> peak_{-100.0f, -100.0f};
    std::array<float, 2> hold_{-100.0f, -100.0f};
};

class LoudnessDisplay final : public juce::Component, public juce::SettableTooltipClient {
public:
    LoudnessDisplay();
    void setValues(float momentaryLufs, float shortTermLufs, float integratedLufs);
    void paint(juce::Graphics&) override;

    static juce::String formatLufs(float lufs); // "-14.2" or "-inf"

private:
    float m_ = -100.0f, s_ = -100.0f, i_ = -100.0f;
};

class SpectrumView final : public juce::Component, public juce::SettableTooltipClient {
public:
    SpectrumView();

    // Pulls a new spectrum from the analyzer (message thread). Returns true if it changed.
    bool update(dsp::SpectrumAnalyzer& analyzer);

    // x position (0..1) of a frequency on the 20 Hz .. 20 kHz log axis, and back.
    static float frequencyToProportion(double hz) noexcept;
    static double proportionToFrequency(float proportion) noexcept;

    // The highest bin of what is displayed (after the ballistics): frequency in Hz and level in
    // dBFS. False before the first update(). Used by the smoke test.
    bool getPeak(double& frequencyHz, float& levelDb) const noexcept;

    void paint(juce::Graphics&) override;

private:
    void paintSpectrum(juce::Graphics& g, juce::Rectangle<float> area); // the filled trace

    std::vector<float> magnitudesDb_;
    std::vector<double> binFrequencies_;
    std::vector<float> smoothed_; // display ballistics: fast rise, slow fall
};

} // namespace dawlab::app
