#include "LimiterProcessor.h"

#include <algorithm>

namespace dawlab::plugin {

namespace {
juce::AudioParameterFloatAttributes withUnit(const char* unit, int decimals) {
    const juce::String suffix = juce::String(" ") + unit;
    return juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction([suffix, decimals](float v, int) { return juce::String(v, decimals) + suffix; })
        .withValueFromStringFunction([](const juce::String& text) { return text.getFloatValue(); });
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout LimiterProcessor::createParameterLayout() {
    using juce::AudioParameterFloat;
    using juce::NormalisableRange;
    using juce::ParameterID;
    namespace id = LimiterParamIds;

    NormalisableRange<float> releaseRange(1.0f, 1000.0f, 0.1f);
    releaseRange.setSkewForCentre(100.0f);

    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id::ceiling, 1}, "Ceiling",
                                                     NormalisableRange<float>(-24.0f, 0.0f, 0.1f), -1.0f,
                                                     withUnit("dB", 1)));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id::inputGain, 1}, "Input Gain",
                                                     NormalisableRange<float>(0.0f, 24.0f, 0.1f), 0.0f,
                                                     withUnit("dB", 1)));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id::release, 1}, "Release", releaseRange, 100.0f,
                                                     withUnit("ms", 1)));
    // The 20 ms upper bound of lookahead = dsp::Limiter's default setMaxLookaheadMs(20)
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id::lookahead, 1}, "Lookahead",
                                                     NormalisableRange<float>(0.0f, 20.0f, 0.1f), 5.0f,
                                                     withUnit("ms", 1)));
    return layout;
}

LimiterProcessor::LimiterProcessor()
    : juce::AudioProcessor(BusesProperties()
                               .withInput("Input", juce::AudioChannelSet::stereo(), true)
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state_(*this, nullptr, "DawLabLimiter", createParameterLayout()) {
    namespace id = LimiterParamIds;
    ceilingParam_ = state_.getRawParameterValue(id::ceiling);
    inputGainParam_ = state_.getRawParameterValue(id::inputGain);
    releaseParam_ = state_.getRawParameterValue(id::release);
    lookaheadParam_ = state_.getRawParameterValue(id::lookahead);
}

LimiterProcessor::~LimiterProcessor() = default;

void LimiterProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    maxBlockSize_ = std::max(1, samplesPerBlock);
    const int numChannels = std::max(getTotalNumInputChannels(), getTotalNumOutputChannels());
    // The lookahead upper bound uses dsp::Limiter's default (20 ms, allocated in prepare), consistent with the parameter range 0..20 ms
    limiter_.setLookaheadMs(lookaheadParam_->load());
    limiter_.prepare(sampleRate, maxBlockSize_, numChannels);
    limiter_.setCeilingDb(ceilingParam_->load());
    limiter_.setInputGainDb(inputGainParam_->load());
    limiter_.setReleaseMs(releaseParam_->load());
    limiter_.reset();
    appliedLookaheadMs_ = lookaheadParam_->load();

    // Delay line for bypass: processInternal handles at most maxBlockSize_ samples at a time; the whole segment is pushed in first,
    // then the value from "d = (n - 1 - i) + L" back is read, so the capacity must be >= (maxBlockSize - 1) + the lookahead upper bound.
    const int maxLookaheadSamples = std::max(0, dawlab::msToSamples(limiter_.getMaxLookaheadMs(), sampleRate));
    dryDelay_.prepare(maxLookaheadSamples + maxBlockSize_, numChannels);
    bypassStep_ = 1.0f / static_cast<float>(std::max(1, dawlab::msToSamples(10.0, sampleRate)));
    snapBypassMix_ = true;

    // Report the latency in prepareToPlay: the host (and JUCE's VST3 hosting) reads this value once prepare has finished
    setLatencySamples(limiter_.getLatencySamples());
}

bool LimiterProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

double LimiterProcessor::getTailLengthSeconds() const {
    // After the input stops, the output keeps "flushing out" a signal as long as the lookahead
    const double sr = getSampleRate();
    return sr > 0.0 ? limiter_.getLatencySamples() / sr : 0.0;
}

void LimiterProcessor::applyLookaheadIfChanged() noexcept {
    const float la = lookaheadParam_->load(std::memory_order_relaxed);
    if (la == appliedLookaheadMs_)
        return;
    appliedLookaheadMs_ = la;
    limiter_.setLookaheadMs(la);
    // Changing latency during playback: JUCE's wrapper notifies the host (VST3: restartComponent(kLatencyChanged)),
    // and the host mostly recomputes PDC at the next prepare (or on stop / restart of playback).
    setLatencySamples(limiter_.getLatencySamples());
}

void LimiterProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ignoreUnused(midi);
    processInternal(buffer, false);
}

// JUCE's default processBlockBypassed() does nothing (output = input, latency 0). But the latency we report is L,
// and the host's PDC delays the other tracks by L to match us -> when bypassed this track would be L samples early (PDC misalignment).
// So when bypassed we output "the original audio delayed by L", keeping the time position exactly the same as before bypass.
void LimiterProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ignoreUnused(midi);
    processInternal(buffer, true);
}

void LimiterProcessor::processInternal(juce::AudioBuffer<float>& buffer, bool bypassed) noexcept {
    const juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear(ch, 0, numSamples);

    limiter_.setCeilingDb(ceilingParam_->load(std::memory_order_relaxed));
    limiter_.setInputGainDb(inputGainParam_->load(std::memory_order_relaxed));
    limiter_.setReleaseMs(releaseParam_->load(std::memory_order_relaxed));
    applyLookaheadIfChanged();

    const int numChannels = std::min({numOut, buffer.getNumChannels(), dryDelay_.getNumChannels()});
    if (numChannels <= 0 || maxBlockSize_ <= 0)
        return;

    const float target = bypassed ? 1.0f : 0.0f;
    if (snapBypassMix_) {
        bypassMix_ = target;
        snapBypassMix_ = false;
    }

    float* const* data = buffer.getArrayOfWritePointers();
    // Even if the host breaks its promise and passes a block larger than maxBlockSize, process it in segments (the delay-line capacity only guarantees maxBlockSize)
    for (int start = 0; start < numSamples; start += maxBlockSize_) {
        const int n = std::min(maxBlockSize_, numSamples - start);

        // 1) Push the original audio into the delay line first (the limiter works in place, so the original is gone after processing)
        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < n; ++i)
                dryDelay_.push(ch, data[ch][start + i]);

        // 2) The limiter runs as usual, also while bypassed, so its envelope / lookahead state stays current and no stale sound appears when switching back
        dawlab::AudioBlock block(data, numChannels, numSamples);
        limiter_.process(block.getSubBlock(start, n));

        // 3) No bypass at all and no fade in progress: the output is simply the limiter result (no sample is touched)
        if (bypassMix_ == 0.0f && target == 0.0f)
            continue;

        // 4) Crossfade: y = (1 - m)*wet + m*dry[n - L]; m moves toward the target by bypassStep_ per sample (done in 10 ms)
        //    The original audio for the i-th sample is the value from (n - 1 - i) pushes ago, then L more samples back
        const int latency = limiter_.getLatencySamples();
        for (int i = 0; i < n; ++i) {
            if (bypassMix_ < target)
                bypassMix_ = std::min(target, bypassMix_ + bypassStep_);
            else if (bypassMix_ > target)
                bypassMix_ = std::max(target, bypassMix_ - bypassStep_);
            const float m = bypassMix_;
            const int d = (n - 1 - i) + latency;
            for (int ch = 0; ch < numChannels; ++ch) {
                float& y = data[ch][start + i];
                y = (1.0f - m) * y + m * dryDelay_.read(ch, d);
            }
        }
    }
}

void LimiterProcessor::getStateInformation(juce::MemoryBlock& destData) {
    if (auto xml = state_.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void LimiterProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(state_.state.getType()))
            state_.replaceState(juce::ValueTree::fromXml(*xml));
}

} // namespace dawlab::plugin

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new dawlab::plugin::LimiterProcessor(); }
