#include "VstInsert.h"

#include <algorithm>

namespace dawlab::app {

VstInsert::VstInsert(std::unique_ptr<host::HostedPlugin> plugin) : plugin_(std::move(plugin)) {
    jassert(plugin_ != nullptr);
    name_ = plugin_->getName().toStdString();
    if (name_.empty())
        name_ = "VST3";
    const int n = std::min(plugin_->getNumParameters(), kMaxExposedParameters);
    const auto& params = plugin_->getInstance().getParameters();
    for (int i = 0; i < n; ++i) {
        juce::String pname = plugin_->getParameterName(i);
        if (pname.isEmpty())
            pname = "Param " + juce::String(i + 1);
        paramNames_.push_back(pname.toStdString());
        const float def = (i < params.size() && params[i] != nullptr) ? params[i]->getDefaultValue() : 0.0f;
        paramDefaults_.push_back(def);
    }
}

VstInsert::~VstInsert() { closeEditor(); }

void VstInsert::prepare(double sampleRate, int maxBlockSize, int numChannels) {
    // Engine::addInsert() calls this on the message thread, before the plugin reaches the audio
    // thread: the right place for prepareToPlay(), which may allocate and may take locks.
    plugin_->prepare(sampleRate, maxBlockSize, numChannels);
    monoScratch_.setSize(std::max(2, numChannels), maxBlockSize);
}

void VstInsert::process(AudioBlock block) noexcept {
    const int n = block.getNumSamples();
    if (block.getNumChannels() >= 2 || n > monoScratch_.getNumSamples()) {
        plugin_->process(block);
        return;
    }
    // Mono track: run the stereo plugin on L = R and fold its output back to one channel.
    float* mono = block.getChannelPointer(0);
    AudioBlock stereo = AudioBlock(monoScratch_).getSubBlock(0, n).getChannelSubset(0, 2);
    std::copy_n(mono, n, stereo.getChannelPointer(0));
    std::copy_n(mono, n, stereo.getChannelPointer(1));
    plugin_->process(stereo);
    const float* l = stereo.getChannelPointer(0);
    const float* r = stereo.getChannelPointer(1);
    for (int i = 0; i < n; ++i)
        mono[i] = 0.5f * (l[i] + r[i]);
}

int VstInsert::getLatencySamples() const noexcept {
    // A plain int read inside JUCE (AudioProcessor::getLatencySamples is a getter).
    return std::max(0, plugin_->getInstance().getLatencySamples());
}

engine::ParameterInfo VstInsert::getParameterInfo(int index) const noexcept {
    engine::ParameterInfo info;
    if (!valid(index))
        return info;
    info.name = paramNames_[static_cast<size_t>(index)].c_str();
    info.unit = "";
    info.min = 0.0f;
    info.max = 1.0f;
    info.defaultValue = paramDefaults_[static_cast<size_t>(index)];
    info.kind = engine::ParameterKind::Float;
    return info;
}

float VstInsert::getParameter(int index) const noexcept {
    return valid(index) ? plugin_->getParameterValue(index) : 0.0f;
}

void VstInsert::setParameter(int index, float value) noexcept {
    if (valid(index))
        plugin_->setParameterValue(index, std::clamp(value == value ? value : 0.0f, 0.0f, 1.0f));
}

juce::String VstInsert::getParameterText(int index, float normalizedValue) const {
    if (!valid(index))
        return {};
    const juce::String text = plugin_->getParameterText(index, normalizedValue);
    return text.isNotEmpty() ? text : juce::String(normalizedValue, 3);
}

void VstInsert::openEditor() {
    if (window_ == nullptr) {
        window_ = std::make_unique<host::PluginWindow>(*plugin_);
        // Closing the window deletes it (asynchronously: we must not delete a window from inside
        // its own closeButtonPressed()). The plugin stays in the mixer.
        window_->onCloseRequested = [this] {
            juce::MessageManager::callAsync([safe = juce::Component::SafePointer<juce::Component>(window_.get()), this] {
                if (safe != nullptr)
                    closeEditor();
            });
        };
    }
    window_->setVisible(true);
    window_->toFront(true);
}

void VstInsert::closeEditor() { window_.reset(); }

} // namespace dawlab::app
