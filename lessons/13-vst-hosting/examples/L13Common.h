#pragma once
// Small utilities shared by this chapter's examples and exercises (used only by this chapter's programs; not a dsp component).
//
//   - defaultCompressorVst3() / defaultLimiterVst3(): paths of the plugins built by this chapter (compiled in by CMake)
//   - u8(): converts a UTF-8 string literal to juce::String (juce::String(const char*) only accepts ASCII)
//   - toNormalized() / fromNormalized(): the JUCE NormalisableRange conversion formulas (README "Normalized parameters")
//   - mustLoad(): loads a plugin and prints the reason on failure
//   - setParam(): sets a parameter by "name + real value" (does the conversion to 0..1 itself)
//   - renderOffline(): feeds a whole AudioBuffer into the plugin at a fixed block size (simulating the audio callback)

#include <dawlab/Common.h>
#include <dawlab/host/PluginHost.h>

#include <cmath>
#include <cstdio>
#include <memory>

#ifndef DAWLAB_L13_COMPRESSOR_VST3
#define DAWLAB_L13_COMPRESSOR_VST3 ""
#endif
#ifndef DAWLAB_L13_LIMITER_VST3
#define DAWLAB_L13_LIMITER_VST3 ""
#endif

namespace l13 {

inline juce::String u8(const char* text) { return juce::String::fromUTF8(text); }

inline juce::File defaultCompressorVst3() { return juce::File(u8(DAWLAB_L13_COMPRESSOR_VST3)); }
inline juce::File defaultLimiterVst3() { return juce::File(u8(DAWLAB_L13_LIMITER_VST3)); }

// JUCE NormalisableRange conversion (with skew = 1 it is linear):
//   normalized = ((value - start) / (end - start))^skew
//   value      = start + (end - start) * normalized^(1/skew)
inline float toNormalized(float value, float start, float end, float skew = 1.0f) {
    const double proportion = std::clamp((static_cast<double>(value) - start) / (static_cast<double>(end) - start), 0.0, 1.0);
    return static_cast<float>(std::pow(proportion, static_cast<double>(skew)));
}

inline float fromNormalized(float normalized, float start, float end, float skew = 1.0f) {
    const double n = std::clamp(static_cast<double>(normalized), 0.0, 1.0);
    return static_cast<float>(start + (end - start) * std::pow(n, 1.0 / skew));
}

// The skew computed by setSkewForCentre(centre): makes normalized 0.5 land exactly on centre
inline float skewForCentre(float start, float end, float centre) {
    return static_cast<float>(std::log(0.5) / std::log((static_cast<double>(centre) - start) / (static_cast<double>(end) - start)));
}

inline std::unique_ptr<dawlab::host::HostedPlugin> mustLoad(dawlab::host::PluginHost& host, const juce::File& file,
                                                           double sampleRate, int blockSize) {
    juce::String error;
    auto instance = host.loadFromFile(file, sampleRate, blockSize, error);
    if (instance == nullptr) {
        std::printf("  Load failed: %s\n  (build the plugin first: cmake --build <build> --target DawLabCompressor_VST3)\n",
                    error.toRawUTF8());
        return nullptr;
    }
    return std::make_unique<dawlab::host::HostedPlugin>(std::move(instance));
}

// Finds a parameter by name and sets its normalized value; prints a warning and returns false if it is not found
inline bool setParam(dawlab::host::HostedPlugin& plugin, const char* name, float normalized) {
    const int index = plugin.findParameter(name);
    if (index < 0) {
        std::printf("  [warning] parameter not found: %s\n", name);
        return false;
    }
    plugin.setParameterValue(index, normalized);
    return true;
}

inline float getParam(const dawlab::host::HostedPlugin& plugin, const char* name) {
    const int index = plugin.findParameter(name);
    return index >= 0 ? plugin.getParameterValue(index) : -1.0f;
}

// Copies the input and feeds it into the plugin block by block of blockSize (like the audio callback); returns the processed result
inline dawlab::AudioBuffer renderOffline(dawlab::host::HostedPlugin& plugin, const dawlab::AudioBuffer& input,
                                         int blockSize) {
    dawlab::AudioBuffer out = input;
    dawlab::AudioBlock all(out);
    for (int start = 0; start < out.getNumSamples(); start += blockSize)
        plugin.process(all.getSubBlock(start, std::min(blockSize, out.getNumSamples() - start)));
    return out;
}

inline float maxAbsDifference(const dawlab::AudioBuffer& a, const dawlab::AudioBuffer& b, int startSample = 0) {
    float d = 0.0f;
    const int nch = std::min(a.getNumChannels(), b.getNumChannels());
    const int n = std::min(a.getNumSamples(), b.getNumSamples());
    for (int ch = 0; ch < nch; ++ch)
        for (int i = startSample; i < n; ++i)
            d = std::max(d, std::abs(a.getSample(ch, i) - b.getSample(ch, i)));
    return d;
}

inline float peakDb(const dawlab::AudioBuffer& b) { return dawlab::gainToDb(dawlab::peakAbs(b)); }

inline float rmsDb(const dawlab::AudioBuffer& b) {
    double sum = 0.0;
    for (int ch = 0; ch < b.getNumChannels(); ++ch) {
        const float r = dawlab::rms(b, ch);
        sum += static_cast<double>(r) * r;
    }
    return dawlab::gainToDb(static_cast<float>(std::sqrt(sum / std::max(1, b.getNumChannels()))));
}

} // namespace l13
