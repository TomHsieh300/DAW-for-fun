#pragma once
// DawLabCompressor: wraps the chapter 04 dawlab::dsp::Compressor and the chapter 01 dawlab::dsp::Gain as a VST3 plugin.
//
//   input --+---------------------------------------------+ dry (pre-allocated buffer)
//           |                                             |
//           +-> Compressor (threshold/ratio/knee/attack/release) -> Gain (makeup) -> wet
//                                                                                |
//                                         out = (1 - mix)*dry + mix*wet <--------+
//
// This class is a juce::AudioProcessor. JUCE's plugin wrapper (juce_audio_plugin_client) translates it
// into the VST3 IComponent / IAudioProcessor / IEditController; all we implement is the AudioProcessor
// lifecycle functions: prepareToPlay -> processBlock x N -> releaseResources.
//
// All parameters are managed by juce::AudioProcessorValueTreeState (APVTS):
//   - the host sees a normalized value in 0..1; the APVTS converts it to the real value (dB, ms, ...) via the NormalisableRange;
//   - the audio thread gets a std::atomic<float>* from getRawParameterValue() and reads it once per block (lock-free);
//   - get/setStateInformation saves the whole ValueTree as XML (wrapped in binary); presets and project files both rely on it.

#include <dawlab/dsp/Compressor.h>
#include <dawlab/dsp/Gain.h>
#include <dawlab/dsp/SmoothedValue.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace dawlab::plugin {

// Parameter IDs: stored in the state and in host project files, so never change them once released (old projects would no longer load)
namespace CompressorParamIds {
inline constexpr const char* threshold = "threshold";
inline constexpr const char* ratio = "ratio";
inline constexpr const char* knee = "knee";
inline constexpr const char* attack = "attack";
inline constexpr const char* release = "release";
inline constexpr const char* makeup = "makeup";
inline constexpr const char* mix = "mix";
} // namespace CompressorParamIds

class CompressorProcessor final : public juce::AudioProcessor {
public:
    CompressorProcessor();
    ~CompressorProcessor() override;

    // Parameter layout (name, range, default, display format). static: tests or documentation can query it directly.
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // ---- Lifecycle ---------------------------------------------------------------
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    using AudioProcessor::processBlock; // the double version keeps the default implementation

    // ---- editor ------------------------------------------------------------------
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // ---- Description -------------------------------------------------------------
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // programs (preset list): some hosts do not accept 0 programs, so report 1
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    // ---- state -------------------------------------------------------------------
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // ---- For the editor (read on the message thread, written on the audio thread; all atomic) ----
    float getGainReductionDb() const noexcept { return compressor_.getGainReductionDb(); }
    float getInputPeakDb() const noexcept { return inputPeakDb_.load(std::memory_order_relaxed); }
    float getOutputPeakDb() const noexcept { return outputPeakDb_.load(std::memory_order_relaxed); }
    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return state_; }

private:
    void updateDspParameters() noexcept;
    void processChunk(juce::AudioBuffer<float>& buffer, int start, int length, int numChannels) noexcept;

    juce::AudioProcessorValueTreeState state_;

    // Pointers to the APVTS-internal atomics (obtained once at construction; the audio thread only does load)
    std::atomic<float>* thresholdParam_ = nullptr;
    std::atomic<float>* ratioParam_ = nullptr;
    std::atomic<float>* kneeParam_ = nullptr;
    std::atomic<float>* attackParam_ = nullptr;
    std::atomic<float>* releaseParam_ = nullptr;
    std::atomic<float>* makeupParam_ = nullptr;
    std::atomic<float>* mixParam_ = nullptr;

    dawlab::dsp::Compressor compressor_;
    dawlab::dsp::Gain makeup_;
    dawlab::dsp::SmoothedValue mix_{1.0f};
    juce::AudioBuffer<float> dry_; // allocated in prepareToPlay; processBlock only copies into it
    int maxBlockSize_ = 0;

    std::atomic<float> inputPeakDb_{-100.0f};
    std::atomic<float> outputPeakDb_{-100.0f};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompressorProcessor)
};

} // namespace dawlab::plugin
