#pragma once
// VstInsert: a hosted VST3 plugin (chapter 13's HostedPlugin) behind the engine's InsertEffect
// interface (Step 17).
//
// The engine does not know JUCE. It only knows InsertEffect: prepare / process / latency /
// a self-describing parameter list. This adapter translates:
//
//   InsertEffect                      chapter 13 (dawlab::host::HostedPlugin)
//   ------------                      ---------------------------------------
//   prepare(sr, block, ch)       -->  HostedPlugin::prepare()    (message thread; allocates)
//   process(AudioBlock)          -->  HostedPlugin::process()    (audio thread; wraps the
//                                     block's channel pointers in a juce::AudioBuffer, no copy)
//   getLatencySamples()          -->  AudioProcessor::getLatencySamples()  -> engine PDC
//   parameter i, 0..1            -->  HostedPlugin::get/setParameterValue (normalized)
//   reset()                      -->  nothing (see below)
//
// The engine reads getLatencySamples() every block, so when a plugin changes its latency (for
// example a lookahead knob), plugin delay compensation follows within one block.
//
// Parameters are exposed as Float 0..1 ("normalized", the VST3 convention), at most
// kMaxExposedParameters of them; getParameterText() gives the plugin's own display text
// ("-18.0 dB") for the auto-generated panel. The names are copied into std::strings at
// construction, because ParameterInfo carries `const char*`.
//
// Mono tracks (Step 4): their inserts get a one-channel block, but the plugin was prepared with
// a stereo bus layout, and handing it a one-channel buffer would let it read a channel that
// does not exist. process() therefore copies the mono block to both sides of a stereo scratch
// buffer (allocated in prepare()), runs the plugin, and folds the result back to mono with
// (L + R) / 2. A stereo-widening plugin collapses to mono on a mono track this way - the same
// trade-off a mono track makes for every insert; put such a plugin on a stereo track to keep
// its width.
//
// reset(): the engine calls reset() on the audio thread (Engine::requestReset). A VST3
// plugin's reset goes through setActive(false/true), which may allocate, so it is not
// real-time safe; we leave plugin state alone. Its tail simply rings out.
//
// Lifetime: the engine owns the VstInsert (Engine::addInsert) and the UI thread deletes it in
// Engine::collectGarbage(). The editor window must disappear before the plugin, so the window
// is a member declared *after* the plugin (members are destroyed in reverse order).

#include <dawlab/engine/InsertEffect.h>
#include <dawlab/host/PluginHost.h>

#include <memory>
#include <string>
#include <vector>

namespace dawlab::app {

class VstInsert final : public engine::InsertEffect {
public:
    static constexpr int kMaxExposedParameters = 64;

    explicit VstInsert(std::unique_ptr<host::HostedPlugin> plugin);
    ~VstInsert() override;

    // ---- InsertEffect -----------------------------------------------------------------------
    const char* getName() const noexcept override { return name_.c_str(); }
    void prepare(double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() noexcept override {}
    void process(AudioBlock block) noexcept override;
    int getLatencySamples() const noexcept override;

    int getNumParameters() const noexcept override { return static_cast<int>(paramNames_.size()); }
    engine::ParameterInfo getParameterInfo(int index) const noexcept override;
    float getParameter(int index) const noexcept override;
    void setParameter(int index, float value) noexcept override;

    // ---- extras for the GUI (message thread) ------------------------------------------------
    juce::String getParameterText(int index, float normalizedValue) const;
    host::HostedPlugin& getPlugin() noexcept { return *plugin_; }

    // Opens the plugin's own editor (or JUCE's generic editor) in a PluginWindow.
    void openEditor();
    void closeEditor();
    bool isEditorOpen() const noexcept { return window_ != nullptr; }

private:
    bool valid(int index) const noexcept { return index >= 0 && index < getNumParameters(); }

    std::unique_ptr<host::HostedPlugin> plugin_;   // destroyed last
    AudioBuffer monoScratch_;                      // stereo copy of a mono block (mono tracks)
    std::string name_;
    std::vector<std::string> paramNames_;
    std::vector<float> paramDefaults_;
    std::unique_ptr<host::PluginWindow> window_;   // destroyed first
};

} // namespace dawlab::app
