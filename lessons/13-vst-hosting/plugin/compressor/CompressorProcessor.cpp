// AudioProcessor implementation of DawLabCompressor. See the signal-flow diagram in CompressorProcessor.h.

#include "CompressorProcessor.h"
#include "CompressorEditor.h"

#include <dawlab/DspMath.h>

#include <algorithm>
#include <cmath>

namespace dawlab::plugin {

namespace {

// value -> display text (used by the host's automation panel and by GenericAudioProcessorEditor)
juce::AudioParameterFloatAttributes withUnit(const char* unit, int decimals) {
    const juce::String suffix = juce::String(" ") + unit;
    return juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction([suffix, decimals](float v, int) { return juce::String(v, decimals) + suffix; })
        .withValueFromStringFunction([](const juce::String& text) { return text.getFloatValue(); });
}

} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout CompressorProcessor::createParameterLayout() {
    using juce::AudioParameterFloat;
    using juce::NormalisableRange;
    using juce::ParameterID;
    namespace id = CompressorParamIds;

    // NormalisableRange(start, end, interval, skew):
    //   normalized = ((value - start) / (end - start))^skew
    //   skew < 1 gives the first half of the slider to small values (for parameters such as attack / release / ratio where "the small-value region matters more").
    NormalisableRange<float> attackRange(0.1f, 200.0f, 0.01f);
    attackRange.setSkewForCentre(10.0f); // slider center (normalized 0.5) = 10 ms
    NormalisableRange<float> releaseRange(5.0f, 2000.0f, 0.1f);
    releaseRange.setSkewForCentre(100.0f); // center = 100 ms

    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    // The second argument of ParameterID is a version hint (VST3 / AU use it to tell which version a parameter was added in)
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id::threshold, 1}, "Threshold",
                                                     NormalisableRange<float>(-60.0f, 0.0f, 0.1f), -20.0f,
                                                     withUnit("dB", 1)));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id::ratio, 1}, "Ratio",
                                                     NormalisableRange<float>(1.0f, 20.0f, 0.01f, 0.5f), 4.0f,
                                                     withUnit(": 1", 2)));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id::knee, 1}, "Knee",
                                                     NormalisableRange<float>(0.0f, 24.0f, 0.1f), 6.0f,
                                                     withUnit("dB", 1)));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id::attack, 1}, "Attack", attackRange, 10.0f,
                                                     withUnit("ms", 2)));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id::release, 1}, "Release", releaseRange, 100.0f,
                                                     withUnit("ms", 1)));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id::makeup, 1}, "Makeup",
                                                     NormalisableRange<float>(-12.0f, 24.0f, 0.1f), 0.0f,
                                                     withUnit("dB", 1)));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id::mix, 1}, "Mix",
                                                     NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f,
                                                     withUnit("%", 1)));
    return layout;
}

CompressorProcessor::CompressorProcessor()
    : juce::AudioProcessor(BusesProperties()
                               .withInput("Input", juce::AudioChannelSet::stereo(), true)
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state_(*this, nullptr, "DawLabCompressor", createParameterLayout()) {
    namespace id = CompressorParamIds;
    thresholdParam_ = state_.getRawParameterValue(id::threshold);
    ratioParam_ = state_.getRawParameterValue(id::ratio);
    kneeParam_ = state_.getRawParameterValue(id::knee);
    attackParam_ = state_.getRawParameterValue(id::attack);
    releaseParam_ = state_.getRawParameterValue(id::release);
    makeupParam_ = state_.getRawParameterValue(id::makeup);
    mixParam_ = state_.getRawParameterValue(id::mix);
}

CompressorProcessor::~CompressorProcessor() = default;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
void CompressorProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    // The only place that may allocate: the host guarantees that every later processBlock has at most samplesPerBlock samples
    // (we still split defensively inside processBlock, because not every host plays by the rules).
    maxBlockSize_ = std::max(1, samplesPerBlock);
    const int numChannels = std::max(getTotalNumInputChannels(), getTotalNumOutputChannels());

    compressor_.prepare(sampleRate, maxBlockSize_, numChannels);
    compressor_.setMakeupGainDb(0.0f); // makeup is left to the chapter 01 Gain (which has its own 50 ms smoothing)
    makeup_.prepare(sampleRate, maxBlockSize_, numChannels);
    mix_.reset(sampleRate, 0.05);
    dry_.setSize(std::max(1, numChannels), maxBlockSize_, false, true, false);

    // Parameters jump straight to their values: when playback starts, do not glide from the defaults to the current values
    updateDspParameters();
    makeup_.reset();
    mix_.setCurrentAndTargetValue(mixParam_->load() * 0.01f);
    compressor_.reset();

    setLatencySamples(0); // feedforward, no lookahead -> zero latency (the Limiter plugin is not, see LimiterProcessor)
}

void CompressorProcessor::releaseResources() {
    // When playback stops we could free memory; our buffers are small, so keep them to make the next prepareToPlay faster
}

bool CompressorProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    // Only mono->mono or stereo->stereo are accepted (the host uses this function to negotiate the bus layout)
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void CompressorProcessor::updateDspParameters() noexcept {
    compressor_.setThresholdDb(thresholdParam_->load(std::memory_order_relaxed));
    compressor_.setRatio(ratioParam_->load(std::memory_order_relaxed));
    compressor_.setKneeDb(kneeParam_->load(std::memory_order_relaxed));
    compressor_.setAttackMs(attackParam_->load(std::memory_order_relaxed));
    compressor_.setReleaseMs(releaseParam_->load(std::memory_order_relaxed));
    makeup_.setGainDb(makeupParam_->load(std::memory_order_relaxed));
    mix_.setTargetValue(mixParam_->load(std::memory_order_relaxed) * 0.01f); // % -> 0..1
}

void CompressorProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ignoreUnused(midi);
    const juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    // Channels beyond the inputs in the output may contain garbage; clear them first (the JUCE template convention)
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear(ch, 0, numSamples);

    const int numChannels = std::min({numOut, buffer.getNumChannels(), dry_.getNumChannels()});
    if (numChannels <= 0 || maxBlockSize_ <= 0)
        return;

    // Read the parameters once per block (automation within a block is provided by the host by splitting into sub-blocks, see exercise 4)
    updateDspParameters();

    // Input peak (for the editor's meter)
    float inPeak = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        inPeak = std::max(inPeak, buffer.getMagnitude(ch, 0, numSamples));

    for (int start = 0; start < numSamples; start += maxBlockSize_)
        processChunk(buffer, start, std::min(maxBlockSize_, numSamples - start), numChannels);

    float outPeak = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        outPeak = std::max(outPeak, buffer.getMagnitude(ch, 0, numSamples));
    inputPeakDb_.store(dawlab::gainToDb(inPeak), std::memory_order_relaxed);
    outputPeakDb_.store(dawlab::gainToDb(outPeak), std::memory_order_relaxed);
}

void CompressorProcessor::processChunk(juce::AudioBuffer<float>& buffer, int start, int length,
                                       int numChannels) noexcept {
    // 1) Keep a dry copy (needed for parallel compression)
    for (int ch = 0; ch < numChannels; ++ch)
        dry_.copyFrom(ch, 0, buffer, ch, start, length);

    // 2) wet = makeup * compress(x). dawlab::AudioBlock wraps the JUCE buffer's pointers directly (zero copy),
    //    so the same DSP code works in offline tests, in the plugin and in the DAW.
    dawlab::AudioBlock block(buffer.getArrayOfWritePointers(), numChannels, length, start);
    compressor_.process(block);
    makeup_.process(block);

    // 3) out = (1 - m)*dry + m*wet. When mix = 100 % and it is not ramping, wet passes through unchanged (bit-exact).
    if (!mix_.isSmoothing() && mix_.getTargetValue() >= 1.0f)
        return;
    for (int i = 0; i < length; ++i) {
        const float m = mix_.getNextValue();
        for (int ch = 0; ch < numChannels; ++ch) {
            float* wet = block.getChannelPointer(ch);
            const float dry = dry_.getSample(ch, i);
            wet[i] = dry + m * (wet[i] - dry);
        }
    }
}

// ---------------------------------------------------------------------------
// editor
// ---------------------------------------------------------------------------
juce::AudioProcessorEditor* CompressorProcessor::createEditor() { return new CompressorEditor(*this); }

// ---------------------------------------------------------------------------
// state: the APVTS ValueTree -> XML -> binary (copyXmlToBinary adds a magic number and a length)
// ---------------------------------------------------------------------------
void CompressorProcessor::getStateInformation(juce::MemoryBlock& destData) {
    const auto tree = state_.copyState(); // copyState is thread-safe (it locks internally)
    if (auto xml = tree.createXml())
        copyXmlToBinary(*xml, destData);
}

void CompressorProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(state_.state.getType()))
            state_.replaceState(juce::ValueTree::fromXml(*xml)); // updates every parameter and notifies the host and the editor
}

} // namespace dawlab::plugin

// The JUCE plugin wrapper uses this factory function to create our processor (one per plugin)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new dawlab::plugin::CompressorProcessor(); }
