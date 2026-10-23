#pragma once
// Chapter 13: a reusable VST3 plugin hosting library (CMake target: dawlab_plugin_host).
//
// Three classes, from the outside in:
//
//   PluginHost    -- "find and create plugins": scans .vst3 files, maintains a KnownPluginList (cache), creates instances.
//   HostedPlugin  -- "use a plugin": wraps a juce::AudioPluginInstance and handles prepare / process /
//                    parameters (0..1 normalized) / saving and restoring state. The chapter 14 DAW puts one in every insert slot.
//   PluginWindow  -- "look at a plugin": a DocumentWindow that shows the plugin's own editor;
//                    if the plugin has no editor it falls back to juce::GenericAudioProcessorEditor (an auto-generated slider UI).
//
// -----------------------------------------------------------------------------
// Thread rules (very important; chapter 14 follows the same scheme)
// -----------------------------------------------------------------------------
//   message thread (= GUI thread, the thread that owns JUCE's MessageManager):
//     - Every PluginHost function (scan / loadFromFile / createInstance). The VST3 spec requires plugin
//       creation, initialization and editor open/close to happen on the UI thread; many plugins crash outright if created on another thread.
//     - HostedPlugin::prepare / releaseResources / getState / setState / creating a PluginWindow.
//       prepare / setState must run while the audio thread is NOT calling process() at the same time
//       (for example while the audio device is stopped, or after taking the plugin out of the audio callback).
//   audio thread (the audio device callback, or an offline render loop):
//     - Only calls HostedPlugin::process(). It does not allocate, does not copy audio, does not wait on a lock:
//       it wraps the dawlab::AudioBlock pointers directly with juce::AudioBuffer<float>'s referencing constructor;
//       the plugin's callback lock is a try-lock, and if it cannot be taken (the message thread is in suspendProcessing(true)) the input is passed through.
//       Note: the try-lock only takes effect during suspendProcessing; setState / prepare are not protected by it (JUCE's VST3 hosting
//       takes a MessageManagerLock when changing state), which is why the rule above, "the audio thread is not calling process() at the same time", is needed.
//       Also, JUCE's VST3PluginInstance::processBlock internally spin-waits on processMutex (a SpinLock), and
//       prepareToPlay / releaseResources / reset / isBusesLayoutSupported take the same lock on the message thread;
//       "does not wait on a lock" therefore holds only if the host obeys the rules above (those calls never overlap with process()).
//   any thread:
//     - getParameterValue / setParameterValue / getLatencySamples / getNumParameters / getName.
//       In JUCE's VST3 hosting, setValue() only writes the value into a lock-free cache and queues it for the dispatcher;
//       the next process() then delivers it to the plugin through IParameterChanges, so doing automation on the audio thread
//       (setting a parameter once per sub-block) is feasible.
//       Detail: after setParameterValue() it also calls sendValueChangedMessageToListeners() so an open editor follows along;
//       that briefly takes the parameter's listenerLock (a CriticalSection) and calls the listeners synchronously. It is normally uncontended and fast,
//       but strictly speaking it is not lock-free; the most demanding hosts can instead call
//       getInstance().getParameters()[i]->setValue(v) on the audio thread (which does not notify listeners) and update the UI from a message-thread timer.
//
// Lifecycle:
//   PluginHost::loadFromFile()  ->  HostedPlugin(std::move(instance))  ->  prepare()
//     ->  process() x N  ->  releaseResources()  -> (close the PluginWindow first) -> destroy the HostedPlugin
//
// Dependencies: JUCE (juce_audio_utils and above, including the VST3 hosting in juce_audio_processors) and dawlab::dsp.

#include <dawlab/AudioBuffer.h>

#include <juce_audio_utils/juce_audio_utils.h>

#include <functional>
#include <memory>
#include <vector>

namespace dawlab::host {

// ===========================================================================
// PluginHost: scanning and creating plugins (all calls on the message thread)
// ===========================================================================
class PluginHost {
public:
    // Creates a juce::AudioPluginFormatManager and registers the VST3 format (juce::VST3PluginFormat, including editor support).
    PluginHost();
    ~PluginHost();

    PluginHost(const PluginHost&) = delete;
    PluginHost& operator=(const PluginHost&) = delete;

    // Scans one .vst3 (a file or a bundle folder) or one folder (finding every .vst3 under it).
    // Every plugin found is added to the KnownPluginList (a cache: a file that has not been modified is not reloaded).
    // Note: scanning a VST3 really has to load the DLL and call its factory, so a misbehaving plugin can crash the whole program.
    // Professional DAWs scan in a separate process (out-of-process scanning); to keep it simple for teaching, this library scans in-process.
    juce::Array<juce::PluginDescription> scan(const juce::File& fileOrFolder, bool recursive = true);

    // Scans vst3File and creates an instance from the first plugin. On failure returns nullptr and writes the reason to error.
    std::unique_ptr<juce::AudioPluginInstance> loadFromFile(const juce::File& vst3File, double sampleRate,
                                                            int blockSize, juce::String& error);

    // Creates an instance from a PluginDescription (for example one read back from a KnownPluginList or from a saved file).
    std::unique_ptr<juce::AudioPluginInstance> createInstance(const juce::PluginDescription& desc, double sampleRate,
                                                              int blockSize, juce::String& error);

    juce::AudioPluginFormatManager& getFormatManager();

    // ---- Extras (beyond the contract) -------------------------------------------------
    // The system default VST3 folders (Windows: %LOCALAPPDATA%\Programs\Common\VST3 and
    // C:\Program Files\Common Files\VST3, plus the VST3_PATH environment variable).
    juce::FileSearchPath getDefaultSearchPaths();

    // Only lists the .vst3 paths under a folder without loading any DLL (fast, cannot crash).
    juce::StringArray findPluginFiles(const juce::File& folder, bool recursive = true);

    // Cache of the scan results. A DAW reads it back at startup so it need not reload every plugin each time.
    juce::KnownPluginList& getKnownPluginList();
    bool saveKnownPluginList(const juce::File& xmlFile) const;
    bool loadKnownPluginList(const juce::File& xmlFile);

    // Files from the last scan() that are .vst3 but yielded no plugin when loaded.
    const juce::StringArray& getLastScanFailures() const;

private:
    juce::AudioPluginFormat* findVst3Format() const;

    juce::AudioPluginFormatManager formatManager_;
    juce::KnownPluginList knownPlugins_;
    juce::StringArray lastFailures_;
};

// ===========================================================================
// HostedPlugin: wraps one plugin instance
// ===========================================================================
class HostedPlugin {
public:
    explicit HostedPlugin(std::unique_ptr<juce::AudioPluginInstance> instance);
    ~HostedPlugin();

    HostedPlugin(const HostedPlugin&) = delete;
    HostedPlugin& operator=(const HostedPlugin&) = delete;

    // Message thread (or while audio is stopped). The only place that allocates:
    //   1. Negotiate the bus layout with the plugin: try to set both the main input and main output to numChannels (1 = mono, 2 = stereo);
    //      if the plugin does not accept it, keep its default layout;
    //   2. call the plugin's prepareToPlay(sampleRate, maxBlockSize);
    //   3. pre-allocate what process() needs: the channel pointer array, a scratch buffer for extra channels, and a MidiBuffer.
    // Can be called repeatedly (e.g. to change the sample rate); it calls releaseResources() first.
    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void releaseResources();
    bool isPrepared() const noexcept { return prepared_; }

    // Audio thread: processes a block in place. Does not allocate or copy audio.
    //   - block longer than maxBlockSize: automatically split into several segments processed in order;
    //   - block has fewer channels than the plugin (e.g. a mono block into a stereo plugin): the extra input channels
    //     are fed with a copy of channel 0, and the corresponding outputs are discarded;
    //   - block has more channels than the plugin: the extra channels are left untouched (the plugin never sees them).
    //   - not yet prepared, or the plugin is suspended: the block is left untouched (equivalent to bypass).
    void process(dawlab::AudioBlock block) noexcept;

    // The latency the plugin reports (in samples); the host uses it for plugin delay compensation (PDC).
    int getLatencySamples() const;

    // ---- Parameters: always a normalized value in 0..1 (as the VST3 spec requires) ----
    int getNumParameters() const;
    juce::String getParameterName(int index) const;
    // Finds a parameter by name (case-insensitive) or parameter ID; returns -1 if not found.
    int findParameter(const juce::String& name) const;
    float getParameterValue(int index) const;
    void setParameterValue(int index, float normalizedValue);
    // Extra: converts the current value, or any normalized value, to the plugin's own display text (e.g. "-20.0 dB").
    juce::String getParameterText(int index) const;
    juce::String getParameterText(int index, float normalizedValue) const;
    // Extra: the parameter ID (under VST3 hosting, the plugin's internal ParamID as a numeric string).
    juce::String getParameterId(int index) const;

    // ---- State: a plugin-defined binary blob; the host only stores it and never interprets it ----
    juce::MemoryBlock getState();
    void setState(const juce::MemoryBlock& state);

    juce::String getName() const;
    bool hasEditor() const;
    juce::AudioPluginInstance& getInstance();
    const juce::AudioPluginInstance& getInstance() const;

    // Extra: the settings actually in effect after prepare
    double getSampleRate() const noexcept { return sampleRate_; }
    int getMaxBlockSize() const noexcept { return maxBlockSize_; }
    int getNumPluginChannels() const noexcept { return numPluginChannels_; }
    juce::String describeBusLayout() const;

private:
    juce::AudioProcessorParameter* getParam(int index) const;
    void processChunk(const dawlab::AudioBlock& block, int start, int length) noexcept;

    std::unique_ptr<juce::AudioPluginInstance> instance_;
    double sampleRate_ = 0.0;
    int maxBlockSize_ = 0;
    int numPluginChannels_ = 0; // max(total input channels, total output channels)
    bool prepared_ = false;

    std::vector<float*> channelPtrs_;         // pre-allocated channel pointer array (process only fills in values)
    juce::AudioBuffer<float> scratch_;        // memory for channels the block does not have
    juce::MidiBuffer midi_;                   // effects do not take MIDI, but the processBlock interface needs one
};

// ===========================================================================
// PluginWindow: shows the plugin's editor (message thread)
// ===========================================================================
class PluginWindow : public juce::DocumentWindow {
public:
    // Creates and shows the window. If the plugin has an editor, uses createEditorAndMakeActive(); otherwise a GenericAudioProcessorEditor.
    // The window does not own the plugin: you must delete this window BEFORE the HostedPlugin is destroyed (the editor must go away before the plugin).
    explicit PluginWindow(HostedPlugin& plugin);
    ~PluginWindow() override;

    // When the close button is pressed: if onCloseRequested is set, hands off to it (usually the owner deletes this window);
    // if not set, it only hides the window. Neither case deletes the plugin.
    std::function<void()> onCloseRequested;

    bool isUsingGenericEditor() const noexcept { return usingGenericEditor_; }

    void closeButtonPressed() override;

private:
    HostedPlugin& plugin_;
    bool usingGenericEditor_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginWindow)
};

} // namespace dawlab::host
