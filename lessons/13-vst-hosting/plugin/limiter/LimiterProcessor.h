#pragma once
// DawLabLimiter: wraps the chapter 05 dawlab::dsp::Limiter as a VST3 plugin.
//
// The biggest difference from DawLabCompressor: a lookahead limiter has latency.
// The plugin must tell the host with setLatencySamples() that "my output is L samples later than my input",
// so the host can do plugin delay compensation (PDC): delay the signals that do not pass through this plugin by L as well, so everything lines up.
//
// A plugin with latency must also override processBlockBypassed(): the VST3 wrapper automatically adds a Bypass parameter,
// and when it is on, processBlockBypassed() is called instead. JUCE's default implementation is a plain pass-through (latency 0),
// but the latency the plugin reports is still L and the host's PDC still compensates L -> the bypassed signal arrives L samples earlier than the other tracks.
// So when bypassed we output "the original audio delayed by L", and switch between wet / dry with a 10 ms crossfade (same as exercise 7).
//
// Another deliberate difference: hasEditor() returns false. When the host opens it, it falls back to GenericAudioProcessorEditor
// (the PluginWindow fallback path), which is exactly what we want to demonstrate for "a plugin without a custom UI".

#include <dawlab/dsp/DelayLine.h>
#include <dawlab/dsp/Limiter.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace dawlab::plugin {

namespace LimiterParamIds {
inline constexpr const char* ceiling = "ceiling";
inline constexpr const char* inputGain = "inputGain";
inline constexpr const char* release = "release";
inline constexpr const char* lookahead = "lookahead";
} // namespace LimiterParamIds

class LimiterProcessor final : public juce::AudioProcessor {
public:
    LimiterProcessor();
    ~LimiterProcessor() override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    // When bypassed: output the original audio delayed by getLatencySamples(), so the time position is unchanged across bypass (PDC stays correct)
    void processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; } // the host will use GenericAudioProcessorEditor

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    float getGainReductionDb() const noexcept { return limiter_.getGainReductionDb(); }

private:
    void applyLookaheadIfChanged() noexcept;
    // Shared by processBlock and processBlockBypassed: bypassed decides the crossfade target (0 = wet, 1 = dry)
    void processInternal(juce::AudioBuffer<float>& buffer, bool bypassed) noexcept;

    juce::AudioProcessorValueTreeState state_;
    std::atomic<float>* ceilingParam_ = nullptr;
    std::atomic<float>* inputGainParam_ = nullptr;
    std::atomic<float>* releaseParam_ = nullptr;
    std::atomic<float>* lookaheadParam_ = nullptr;

    dawlab::dsp::Limiter limiter_;
    float appliedLookaheadMs_ = -1.0f;
    int maxBlockSize_ = 0;

    // For bypass: delay line of the original audio (capacity = lookahead upper bound + maxBlockSize, allocated in prepareToPlay)
    dawlab::dsp::DelayLine dryDelay_;
    float bypassMix_ = 0.0f;     // 0 = fully wet (limiter output), 1 = fully dry (original delayed by L)
    float bypassStep_ = 1.0f;    // change per sample of the crossfade = 1 / (number of samples in 10 ms)
    bool snapBypassMix_ = true;  // the first block after prepare takes the target value directly (there was no earlier output, so no fade is needed)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LimiterProcessor)
};

} // namespace dawlab::plugin
