// HostedPlugin: wraps a juce::AudioPluginInstance.
// prepare / releaseResources / getState / setState: message thread (while audio is stopped).
// process: audio thread; does not allocate, does not copy audio, does not wait on a lock (provided the host obeys the thread rules in PluginHost.h).

#include <dawlab/host/PluginHost.h>

#include <algorithm>

namespace dawlab::host {

HostedPlugin::HostedPlugin(std::unique_ptr<juce::AudioPluginInstance> instance) : instance_(std::move(instance)) {
    jassert(instance_ != nullptr);
}

HostedPlugin::~HostedPlugin() {
    // Always release before destruction: VST3 requires the order setActive(false) -> terminate
    releaseResources();
}

// ---------------------------------------------------------------------------
// prepare: bus layout negotiation + prepareToPlay + pre-allocation
// ---------------------------------------------------------------------------
void HostedPlugin::prepare(double sampleRate, int maxBlockSize, int numChannels) {
    if (instance_ == nullptr)
        return;
    if (prepared_)
        releaseResources();

    sampleRate_ = sampleRate;
    maxBlockSize_ = std::max(1, maxBlockSize);
    auto& p = *instance_;

    // 1) Bus layout negotiation.
    //    The host proposes "numChannels channels on both the main input and the main output" and asks whether the plugin supports it
    //    (checkBusesLayoutSupported calls the plugin's isBusesLayoutSupported; for VST3 it is setBusArrangements).
    //    If not supported, the plugin's default layout is kept and process() handles the channel-count mismatch.
    //    Extra buses such as a sidechain are left as they are (mostly disabled by default).
    if (numChannels >= 1) {
        auto layout = p.getBusesLayout();
        const auto set = juce::AudioChannelSet::canonicalChannelSet(numChannels);
        if (!layout.inputBuses.isEmpty())
            layout.inputBuses.getReference(0) = set;
        if (!layout.outputBuses.isEmpty())
            layout.outputBuses.getReference(0) = set;
        if (layout != p.getBusesLayout() && p.checkBusesLayoutSupported(layout))
            p.setBusesLayout(layout);
    }

    // 2) Lifecycle: the plugin can only processBlock after prepareToPlay.
    //    For VST3 this step performs setupProcessing -> activateBus -> setActive(true), and reads out the latency.
    p.setRateAndBufferSizeDetails(sampleRate_, maxBlockSize_);
    p.prepareToPlay(sampleRate_, maxBlockSize_);

    // 3) Pre-allocate what process() needs (so the audio thread never has to allocate later)
    numPluginChannels_ = std::max(p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
    channelPtrs_.assign(static_cast<size_t>(numPluginChannels_), nullptr);
    scratch_.setSize(std::max(1, numPluginChannels_), maxBlockSize_, false, true, false);
    scratch_.clear();
    midi_.ensureSize(256); // effects usually need no MIDI; leave some room so a plugin that outputs MIDI does not allocate on the fly
    midi_.clear();
    prepared_ = true;
}

void HostedPlugin::releaseResources() {
    if (instance_ != nullptr && prepared_)
        instance_->releaseResources();
    prepared_ = false;
}

// ---------------------------------------------------------------------------
// process: audio thread
// ---------------------------------------------------------------------------
void HostedPlugin::process(dawlab::AudioBlock block) noexcept {
    if (!prepared_ || numPluginChannels_ <= 0)
        return;
    const int n = block.getNumSamples();
    // The host promised "at most maxBlockSize samples at a time" (the second argument of prepareToPlay),
    // so a longer block is split into segments; the promise must never be broken (many plugins size internal buffers from maxBlockSize).
    for (int start = 0; start < n; start += maxBlockSize_)
        processChunk(block, start, std::min(maxBlockSize_, n - start));
}

void HostedPlugin::processChunk(const dawlab::AudioBlock& block, int start, int length) noexcept {
    const int blockChannels = block.getNumChannels();
    const int mainInputs = instance_->getMainBusNumInputChannels();

    // Prepare the channel pointers: channels the block has point straight into the block (zero copy);
    // channels the block lacks use the memory of scratch_: the main-input ones get a copy of channel 0 (mono -> stereo),
    // the rest (e.g. sidechain) are cleared to 0.
    for (int ch = 0; ch < numPluginChannels_; ++ch) {
        if (ch < blockChannels) {
            channelPtrs_[static_cast<size_t>(ch)] = block.getChannelPointer(ch) + start;
        } else {
            float* s = scratch_.getWritePointer(ch);
            if (ch < mainInputs && blockChannels > 0)
                std::copy_n(block.getChannelPointer(0) + start, length, s);
            else
                std::fill_n(s, length, 0.0f);
            channelPtrs_[static_cast<size_t>(ch)] = s;
        }
    }

    // Referencing constructor: allocates no memory and copies no samples, it only remembers the pointers (up to 32 channels use a built-in array)
    juce::AudioBuffer<float> buffer(channelPtrs_.data(), numPluginChannels_, length);
    midi_.clear();

    // The plugin's callback lock: the message thread holds it only during suspendProcessing(true) (changing state / prepare do not).
    // The audio thread must never wait, so it uses a try-lock: if the lock cannot be taken, this segment keeps the original audio (an instantaneous bypass).
    // setState / prepare are not protected by this lock; the host guarantees they never run at the same time as process() (see PluginHost.h);
    // that also means the processMutex that JUCE's VST3 hosting spin-waits on inside processBlock is never contended.
    const juce::ScopedTryLock lock(instance_->getCallbackLock());
    if (!lock.isLocked() || instance_->isSuspended())
        return;

    const juce::ScopedNoDenormals noDenormals; // tiny values in recursive filters make the CPU very slow (denormals)
    instance_->processBlock(buffer, midi_);
}

int HostedPlugin::getLatencySamples() const { return instance_ != nullptr ? instance_->getLatencySamples() : 0; }

// ---------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------
juce::AudioProcessorParameter* HostedPlugin::getParam(int index) const {
    if (instance_ == nullptr)
        return nullptr;
    const auto& params = instance_->getParameters();
    return juce::isPositiveAndBelow(index, params.size()) ? params[index] : nullptr;
}

int HostedPlugin::getNumParameters() const { return instance_ != nullptr ? instance_->getParameters().size() : 0; }

juce::String HostedPlugin::getParameterName(int index) const {
    if (auto* p = getParam(index))
        return p->getName(128);
    return {};
}

juce::String HostedPlugin::getParameterId(int index) const {
    if (auto* hosted = dynamic_cast<juce::HostedAudioProcessorParameter*>(getParam(index)))
        return hosted->getParameterID();
    return juce::String(index);
}

int HostedPlugin::findParameter(const juce::String& name) const {
    const int n = getNumParameters();
    for (int i = 0; i < n; ++i)
        if (getParameterName(i).equalsIgnoreCase(name))
            return i;
    for (int i = 0; i < n; ++i) // if the name is not found, compare against the parameter ID
        if (getParameterId(i).equalsIgnoreCase(name))
            return i;
    return -1;
}

float HostedPlugin::getParameterValue(int index) const {
    if (auto* p = getParam(index))
        return p->getValue();
    return 0.0f;
}

void HostedPlugin::setParameterValue(int index, float normalizedValue) {
    if (auto* p = getParam(index)) {
        // setValue: hands the value to the plugin (VST3 hosting delivers it through IParameterChanges in the next processBlock,
        // and also updates the edit controller). Then notify listeners so an open editor (e.g. GenericAudioProcessorEditor) follows along.
        const float v = juce::jlimit(0.0f, 1.0f, normalizedValue);
        p->setValue(v);
        p->sendValueChangedMessageToListeners(v);
    }
}

juce::String HostedPlugin::getParameterText(int index) const {
    if (auto* p = getParam(index))
        return p->getCurrentValueAsText();
    return {};
}

juce::String HostedPlugin::getParameterText(int index, float normalizedValue) const {
    if (auto* p = getParam(index))
        return p->getText(juce::jlimit(0.0f, 1.0f, normalizedValue), 64);
    return {};
}

// ---------------------------------------------------------------------------
// state
// ---------------------------------------------------------------------------
juce::MemoryBlock HostedPlugin::getState() {
    juce::MemoryBlock state;
    if (instance_ != nullptr)
        instance_->getStateInformation(state); // VST3: component state + controller state packed together
    return state;
}

void HostedPlugin::setState(const juce::MemoryBlock& state) {
    if (instance_ != nullptr && state.getSize() > 0)
        instance_->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
}

// ---------------------------------------------------------------------------
// Extras
// ---------------------------------------------------------------------------
juce::String HostedPlugin::getName() const { return instance_ != nullptr ? instance_->getName() : juce::String(); }

bool HostedPlugin::hasEditor() const { return instance_ != nullptr && instance_->hasEditor(); }

juce::AudioPluginInstance& HostedPlugin::getInstance() { return *instance_; }

const juce::AudioPluginInstance& HostedPlugin::getInstance() const { return *instance_; }

juce::String HostedPlugin::describeBusLayout() const {
    if (instance_ == nullptr)
        return {};
    juce::String s;
    for (int i = 0; i < instance_->getBusCount(true); ++i)
        if (auto* bus = instance_->getBus(true, i))
            s << "in[" << i << "] " << bus->getName() << ": " << bus->getCurrentLayout().getDescription()
              << (bus->isEnabled() ? juce::String() : juce::String(" (disabled)")) << "  ";
    for (int i = 0; i < instance_->getBusCount(false); ++i)
        if (auto* bus = instance_->getBus(false, i))
            s << "out[" << i << "] " << bus->getName() << ": " << bus->getCurrentLayout().getDescription()
              << (bus->isEnabled() ? juce::String() : juce::String(" (disabled)")) << "  ";
    return s.trimEnd();
}

} // namespace dawlab::host
