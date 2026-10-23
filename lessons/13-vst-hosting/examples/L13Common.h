#pragma once
// 第 13 章範例、練習共用的小工具（只給本章的程式用，不是 dsp 元件）。
//
//   - defaultCompressorVst3() / defaultLimiterVst3()：本章 build 出來的 plugin 路徑（CMake 編進程式）
//   - u8()：把 UTF-8 字串字面值轉成 juce::String（juce::String(const char*) 只接受 ASCII）
//   - toNormalized() / fromNormalized()：JUCE NormalisableRange 的換算公式（README「normalized parameter」）
//   - mustLoad()：載入 plugin，失敗就印出原因
//   - setParam()：用「名稱 + 實際值」設定參數（自己換算成 0..1）
//   - renderOffline()：把整段 AudioBuffer 以固定 block size 送進 plugin（模擬 audio callback）

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

// JUCE NormalisableRange 的換算（skew = 1 時就是線性）：
//   normalized = ((value − start) / (end − start))^skew
//   value      = start + (end − start) · normalized^(1/skew)
inline float toNormalized(float value, float start, float end, float skew = 1.0f) {
    const double proportion = std::clamp((static_cast<double>(value) - start) / (static_cast<double>(end) - start), 0.0, 1.0);
    return static_cast<float>(std::pow(proportion, static_cast<double>(skew)));
}

inline float fromNormalized(float normalized, float start, float end, float skew = 1.0f) {
    const double n = std::clamp(static_cast<double>(normalized), 0.0, 1.0);
    return static_cast<float>(start + (end - start) * std::pow(n, 1.0 / skew));
}

// setSkewForCentre(centre) 算出來的 skew：讓 normalized 0.5 剛好對到 centre
inline float skewForCentre(float start, float end, float centre) {
    return static_cast<float>(std::log(0.5) / std::log((static_cast<double>(centre) - start) / (static_cast<double>(end) - start)));
}

inline std::unique_ptr<dawlab::host::HostedPlugin> mustLoad(dawlab::host::PluginHost& host, const juce::File& file,
                                                           double sampleRate, int blockSize) {
    juce::String error;
    auto instance = host.loadFromFile(file, sampleRate, blockSize, error);
    if (instance == nullptr) {
        std::printf("  載入失敗：%s\n  （先 build plugin：cmake --build <build> --target DawLabCompressor_VST3）\n",
                    error.toRawUTF8());
        return nullptr;
    }
    return std::make_unique<dawlab::host::HostedPlugin>(std::move(instance));
}

// 以名稱找參數並設定 normalized 值；找不到就印警告並回傳 false
inline bool setParam(dawlab::host::HostedPlugin& plugin, const char* name, float normalized) {
    const int index = plugin.findParameter(name);
    if (index < 0) {
        std::printf("  [警告] 找不到參數 %s\n", name);
        return false;
    }
    plugin.setParameterValue(index, normalized);
    return true;
}

inline float getParam(const dawlab::host::HostedPlugin& plugin, const char* name) {
    const int index = plugin.findParameter(name);
    return index >= 0 ? plugin.getParameterValue(index) : -1.0f;
}

// 把 input 複製一份、以 blockSize 一塊一塊送進 plugin（就像 audio callback），回傳處理後的結果
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
