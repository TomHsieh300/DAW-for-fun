// HostedPlugin：包住一個 juce::AudioPluginInstance。
// prepare / releaseResources / getState / setState：message thread（audio 停止時）。
// process：audio thread，不 allocate、不複製 audio、不等待 lock（前提：host 遵守 PluginHost.h 的 thread 規則）。

#include <dawlab/host/PluginHost.h>

#include <algorithm>

namespace dawlab::host {

HostedPlugin::HostedPlugin(std::unique_ptr<juce::AudioPluginInstance> instance) : instance_(std::move(instance)) {
    jassert(instance_ != nullptr);
}

HostedPlugin::~HostedPlugin() {
    // 解構前一定要 release：VST3 規定 setActive(false) → terminate 的順序
    releaseResources();
}

// ---------------------------------------------------------------------------
// prepare：bus layout 協商 + prepareToPlay + 預先配置
// ---------------------------------------------------------------------------
void HostedPlugin::prepare(double sampleRate, int maxBlockSize, int numChannels) {
    if (instance_ == nullptr)
        return;
    if (prepared_)
        releaseResources();

    sampleRate_ = sampleRate;
    maxBlockSize_ = std::max(1, maxBlockSize);
    auto& p = *instance_;

    // 1) Bus layout 協商（negotiation）。
    //    host 提出「主輸入 / 主輸出各 numChannels 個 channel」，問 plugin 支不支援
    //    （checkBusesLayoutSupported 會呼叫 plugin 的 isBusesLayoutSupported；VST3 則是 setBusArrangements）。
    //    不支援就保留 plugin 的預設 layout，process() 再處理 channel 數不一致的情況。
    //    sidechain 之類的額外 bus 維持原狀（預設多半是關閉的）。
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

    // 2) 生命週期：prepareToPlay 之後 plugin 才能 processBlock。
    //    對 VST3 而言這一步會做 setupProcessing → activateBus → setActive(true)，並讀出 latency。
    p.setRateAndBufferSizeDetails(sampleRate_, maxBlockSize_);
    p.prepareToPlay(sampleRate_, maxBlockSize_);

    // 3) 預先配置 process() 要用的東西（之後 audio thread 上就不必 allocate）
    numPluginChannels_ = std::max(p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
    channelPtrs_.assign(static_cast<size_t>(numPluginChannels_), nullptr);
    scratch_.setSize(std::max(1, numPluginChannels_), maxBlockSize_, false, true, false);
    scratch_.clear();
    midi_.ensureSize(256); // 效果器通常不需要 MIDI；留一點空間避免 plugin 輸出 MIDI 時臨時 allocate
    midi_.clear();
    prepared_ = true;
}

void HostedPlugin::releaseResources() {
    if (instance_ != nullptr && prepared_)
        instance_->releaseResources();
    prepared_ = false;
}

// ---------------------------------------------------------------------------
// process：audio thread
// ---------------------------------------------------------------------------
void HostedPlugin::process(dawlab::AudioBlock block) noexcept {
    if (!prepared_ || numPluginChannels_ <= 0)
        return;
    const int n = block.getNumSamples();
    // host 承諾過「每次最多 maxBlockSize 個 sample」（prepareToPlay 的第二個參數），
    // 遇到更長的 block 就切段，絕不能違約（很多 plugin 依 maxBlockSize 配置內部 buffer）。
    for (int start = 0; start < n; start += maxBlockSize_)
        processChunk(block, start, std::min(maxBlockSize_, n - start));
}

void HostedPlugin::processChunk(const dawlab::AudioBlock& block, int start, int length) noexcept {
    const int blockChannels = block.getNumChannels();
    const int mainInputs = instance_->getMainBusNumInputChannels();

    // 準備 channel 指標：block 有的 channel 直接指進 block（零複製）；
    // block 沒有的 channel 用 scratch_ 的記憶體：主輸入的部分放 channel 0 的複本（mono → stereo），
    // 其他（例如 sidechain）清成 0。
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

    // referencing constructor：不配置記憶體、不複製 sample，只記住指標（32 個 channel 以內用內建陣列）
    juce::AudioBuffer<float> buffer(channelPtrs_.data(), numPluginChannels_, length);
    midi_.clear();

    // plugin 的 callback lock：message thread 只在 suspendProcessing(true) 期間拿著它（換 state / prepare 不會）。
    // audio thread 絕不能等待，所以用 try-lock：拿不到就讓這一段維持原音（等同瞬間 bypass）。
    // setState / prepare 不受這個 lock 保護，由 host 保證它們不與 process() 同時進行（見 PluginHost.h）；
    // 這也讓 JUCE VST3 hosting 在 processBlock 內部 spin 等待的 processMutex 不會有競爭。
    const juce::ScopedTryLock lock(instance_->getCallbackLock());
    if (!lock.isLocked() || instance_->isSuspended())
        return;

    const juce::ScopedNoDenormals noDenormals; // 遞迴 filter 的極小值會讓 CPU 變很慢（denormal）
    instance_->processBlock(buffer, midi_);
}

int HostedPlugin::getLatencySamples() const { return instance_ != nullptr ? instance_->getLatencySamples() : 0; }

// ---------------------------------------------------------------------------
// 參數
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
    for (int i = 0; i < n; ++i) // 名稱找不到再比對 parameter ID
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
        // setValue：把值交給 plugin（VST3 hosting 會在下一個 processBlock 透過 IParameterChanges 送進去，
        // 同時更新 edit controller）。再通知 listener，讓開著的 editor（例如 GenericAudioProcessorEditor）跟著動。
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
        instance_->getStateInformation(state); // VST3：component state + controller state 一起打包
    return state;
}

void HostedPlugin::setState(const juce::MemoryBlock& state) {
    if (instance_ != nullptr && state.getSize() > 0)
        instance_->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
}

// ---------------------------------------------------------------------------
// 其他
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
              << (bus->isEnabled() ? juce::String() : juce::String::fromUTF8("（關閉）")) << "  ";
    for (int i = 0; i < instance_->getBusCount(false); ++i)
        if (auto* bus = instance_->getBus(false, i))
            s << "out[" << i << "] " << bus->getName() << ": " << bus->getCurrentLayout().getDescription()
              << (bus->isEnabled() ? juce::String() : juce::String::fromUTF8("（關閉）")) << "  ";
    return s.trimEnd();
}

} // namespace dawlab::host
