#include "MainComponent.h"

#include "EffectMenu.h"
#include "VstInsert.h"

#include <dawlab/engine/DemoSong.h>

#include <algorithm>

namespace dawlab::app {

using engine::ChannelRef;

// A dark layer over the whole window while an export renders: it swallows mouse clicks (the
// project must not change during the render, see Exporter.h) and shows a busy bar.
class MainComponent::ExportOverlay final : public juce::Component {
public:
    explicit ExportOverlay(const juce::String& text) : text_(text) {
        addAndMakeVisible(progress_);
        progress_.setPercentageDisplay(false);
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colours::black.withAlpha(0.55f));
        g.setColour(DawColours::text());
        g.setFont(DawLookAndFeel::uiFont(18.0f, true));
        g.drawText(text_, getLocalBounds().withSizeKeepingCentre(600, 30).translated(0, -30), juce::Justification::centred);
    }
    void resized() override { progress_.setBounds(getLocalBounds().withSizeKeepingCentre(360, 18)); }

private:
    double progressValue_ = -1.0; // negative = indeterminate (spinning)
    juce::ProgressBar progress_{progressValue_};
    juce::String text_;
};

MainComponent::MainComponent(Mode mode) : mode_(mode) {
    setLookAndFeel(&lookAndFeel_);
    setOpaque(true);
    const juce::String deviceError = setUpAudio();
    setUpProject();

    // ---- commands, menu, keyboard ------------------------------------------------------------------
    commandManager_.registerAllCommandsForTarget(this);
    commandManager_.setFirstCommandTarget(this);
    addKeyListener(commandManager_.getKeyMappings());
    setWantsKeyboardFocus(true);
    setApplicationCommandManagerToWatch(&commandManager_);
    menuBar_.setModel(this);
    addAndMakeVisible(menuBar_);

    setUpGui(deviceError);
    setSize(kDefaultWidth, kDefaultHeight);
    if (mode_ == Mode::Live) {
        loadDemoSongAsync(); // the app starts with something to listen to
        startTimerHz(30);
    }
}

// The constructor's parts, in its order. The audio device first; it returns why no device could be opened
// (empty if one was, and in Mode::Offline, which asks for none).
juce::String MainComponent::setUpAudio() {
    audioHost_.onEnginePrepared = [this](double oldRate, double newRate) {
        // A different device sample rate: clip positions and clip audio are in samples of the old
        // rate. Until they are converted, playing them would be off in pitch and position, so stop
        // the transport now (an atomic: this may be the thread that restarts the device) and
        // convert on the loader thread (startClipConversion), then resume.
        if (oldRate > 0.0 && oldRate != newRate) {
            auto& tr = engine_.getTransport();
            if (tr.isPlaying())
                resumeAfterConversion_.store(true);
            tr.stop();
            juce::Component::SafePointer<MainComponent> safe(this);
            juce::MessageManager::callAsync([safe, oldRate, newRate] {
                if (safe != nullptr) {
                    safe->session_.scaleTransportPositions(oldRate, newRate); // cheap, at once
                    safe->startClipConversion();
                }
            });
        }
    };
    juce::String deviceError;
    if (mode_ == Mode::Live)
        deviceError = audioHost_.openDefaultDevice();
    if (mode_ == Mode::Offline || deviceError.isNotEmpty())
        audioHost_.prepareOffline(kOfflineSampleRate, kOfflineBlockSize);
    offlineScratch_.setSize(engine::kNumChannels, 4096);
    return deviceError;
}

// The project: what the session asks of this window, and what the arrangement's clicks and drops do.
void MainComponent::setUpProject() {
    session_.onInsertWillBeRemoved = [this](engine::InsertEffect* fx) {
        parameterWindows_.erase(fx);
        if (auto* vst = dynamic_cast<VstInsert*>(fx))
            vst->closeEditor();
    };
    session_.newProject();
    arrangement_.onFilesDropped = [this](int track, const juce::StringArray& files) { loadDroppedFiles(track, files); };
}

// The components and what they call; the status line says whether there is an audio device.
void MainComponent::setUpGui(const juce::String& deviceError) {
    transportBar_.onPlayStop = [this] { togglePlay(); };
    transportBar_.onReturnToStart = [this] { returnToStart(); };
    transportBar_.onToggleLoop = [this] { toggleLoop(); };
    addAndMakeVisible(transportBar_);
    addAndMakeVisible(arrangement_);
    mixer_.onLoadRequested = [this](int track) { chooseAudioFileForTrack(track); };
    mixer_.onSlotClicked = [this](ChannelRef ref, int slot, juce::Component& anchor, bool menu) {
        if (menu || session_.getSlot(ref, slot).isEmpty())
            showSlotMenu(ref, slot, anchor);
        else
            openInsertEditor(ref, slot, false);
    };
    addAndMakeVisible(mixer_);
    for (auto* l : {&status_, &deviceInfo_}) {
        l->setFont(DawLookAndFeel::uiFont(12.0f));
        l->setColour(juce::Label::textColourId, DawColours::textDim());
        addAndMakeVisible(*l);
    }
    deviceInfo_.setJustificationType(juce::Justification::centredRight);
    deviceInfo_.setText(audioHost_.getDeviceDescription(), juce::dontSendNotification);
    setStatus(deviceError.isNotEmpty() ? "No audio device (" + deviceError + "): running offline"
                                       : juce::String("Ready. Space = play/stop, F1 = keyboard shortcuts"));
}

MainComponent::~MainComponent() {
    stopTimer();
    exportJob_.reset();         // waits for a running export
    closeAllEditors();
    audioHost_.closeDevice();   // no audio callback after this line
    menuBar_.setModel(nullptr);
    setApplicationCommandManagerToWatch(nullptr);
    removeKeyListener(commandManager_.getKeyMappings());
    session_.onInsertWillBeRemoved = nullptr;
    setLookAndFeel(nullptr);
}

// ---- the UI tick ------------------------------------------------------------------------------------

void MainComponent::tick() {
    // Session's change messages are asynchronous; deliver pending ones now, so that a tick always
    // shows the current structure (this matters for the snapshot, which has no running message loop).
    session_.dispatchPendingMessages();
    // During an export the render thread collects the garbage (Exporter.h, rule 2).
    if (!isExporting())
        engine_.collectGarbage();
    transportBar_.update();
    arrangement_.update();
    mixer_.update();
    if (audioHost_.isAnalyzerReady())
        mixer_.updateSpectrum(audioHost_.getSpectrumAnalyzer());
}

void MainComponent::simulatePlayback(int64_t numSamples, int blockSize) {
    jassert(!audioHost_.isDeviceOpen() || !audioHost_.isEngineAttached());
    blockSize = std::clamp(blockSize, 1, offlineScratch_.getNumSamples());
    float* channels[engine::kNumChannels] = {offlineScratch_.getWritePointer(0), offlineScratch_.getWritePointer(1)};
    for (int64_t done = 0; done < numSamples; done += blockSize) {
        const int n = static_cast<int>(std::min<int64_t>(blockSize, numSamples - done));
        audioHost_.processOffline(channels, engine::kNumChannels, n);
    }
}

// ---- transport ----------------------------------------------------------------------------------------

void MainComponent::togglePlay() {
    auto& tr = engine_.getTransport();
    if (isConvertingSampleRate() && !tr.isPlaying()) {
        // The clips are still at the old device rate: start as soon as they are converted.
        const bool resume = !resumeAfterConversion_.load();
        resumeAfterConversion_.store(resume);
        setStatus(resume ? "Converting clips to the new sample rate: playback starts when done"
                         : "Converting clips to the new sample rate");
        return;
    }
    tr.setPlaying(!tr.isPlaying());
    transportBar_.update();
}

void MainComponent::returnToStart() {
    engine_.getTransport().setPosition(0); // a pending seek, applied by the audio thread at its next block
    transportBar_.update();
}

void MainComponent::toggleLoop() {
    auto& tr = engine_.getTransport();
    tr.setLoopEnabled(!tr.isLoopEnabled());
    arrangement_.repaint();
    transportBar_.update();
}

void MainComponent::seekBars(int deltaBars) {
    auto& tr = engine_.getTransport();
    auto p = tr.getPositionBarsBeats();
    p.bar = std::max(1, p.bar + deltaBars);
    p.beat = 1;
    p.tick = 0;
    tr.setPositionBarsBeats(p);
}

// ---- project ------------------------------------------------------------------------------------------

// True, and it says so on the status line, while an export prevents replacing the project.
bool MainComponent::refuseProjectChange(const juce::String& what) {
    if (!isExporting())
        return false;
    setStatus(what + " has to wait: the project is being exported");
    return true;
}

void MainComponent::newProject() {
    if (refuseProjectChange("New project"))
        return;
    closeAllEditors();
    resumeAfterConversion_.store(false); // a new project starts stopped, conversion or not
    session_.newProject();
    arrangement_.zoomToFit();
    setStatus("New project");
}

void MainComponent::loadDemoSongAsync() {
    if (refuseProjectChange("Loading the demo song"))
        return;
    setStatus("Generating the demo song...");
    const double sr = engine_.getSampleRate();
    juce::Component::SafePointer<MainComponent> safe(this);
    loader_.loadDemoSong(sr, [safe](std::shared_ptr<const engine::DemoSong> song) {
        if (safe == nullptr)
            return;
        if (song->sampleRate != safe->engine_.getSampleRate()) { // the device changed meanwhile
            safe->loadDemoSongAsync();
            return;
        }
        if (safe->refuseProjectChange("Loading the demo song")) // a recording or an export started meanwhile
            return;
        safe->closeAllEditors();
        safe->resumeAfterConversion_.store(false);
        const int unknown = safe->session_.applyDemoSong(*song);
        safe->arrangement_.zoomToFit();
        safe->setStatus("Demo song loaded: 8 tracks, 19 inserts" +
                        (unknown ? ", " + juce::String(unknown) + " unknown parameters" : juce::String()));
    });
}

int MainComponent::loadDemoSongNow() {
    if (refuseProjectChange("Loading the demo song"))
        return -1;
    closeAllEditors();
    resumeAfterConversion_.store(false);
    const engine::DemoSong song = engine::makeDemoSong(engine_.getSampleRate());
    const int unknown = session_.applyDemoSong(song);
    arrangement_.zoomToFit();
    setStatus("Demo song loaded");
    return unknown;
}

void MainComponent::chooseAudioFileForTrack(int track) {
    fileChooser_ = std::make_unique<juce::FileChooser>("Load audio into track " + juce::String(track + 1),
                                                       juce::File::getSpecialLocation(juce::File::userMusicDirectory),
                                                       MediaLoader::getWildcard());
    juce::Component::SafePointer<MainComponent> safe(this);
    fileChooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe, track](const juce::FileChooser& chooser) {
                                  if (safe != nullptr && chooser.getResult() != juce::File())
                                      safe->loadAudioFile(track, chooser.getResult());
                              });
}

void MainComponent::loadDroppedFiles(int track, const juce::StringArray& files) {
    const int count = std::min(files.size(), engine::kNumTracks - track);
    for (int i = 0; i < count; ++i)
        loadAudioFile(track + i, juce::File(files[i]));
    if (files.size() > count)
        setStatus(juce::String(files.size() - count) + " file(s) not loaded: no tracks left below track " +
                  juce::String(track + 1));
}

void MainComponent::loadAudioFile(int track, const juce::File& file) {
    setStatus("Loading " + file.getFileName() + "...");
    juce::Component::SafePointer<MainComponent> safe(this);
    // New clip audio must be at the clip rate: normally the engine rate; the old rate while a
    // device change is being converted (finishClipConversion picks the new clip up then).
    loader_.loadFile(track, file, session_.getClipSampleRate(), [safe, file](const LoadedMedia& media) {
        if (safe == nullptr)
            return;
        if (media.audio == nullptr) {
            safe->setStatus("Load failed: " + media.error);
            return;
        }
        if (media.targetSampleRate != safe->session_.getClipSampleRate()) {
            // The device changed the sample rate while the file was being read: read it again
            // for the current rate instead of putting audio at the wrong rate into the project.
            safe->loadAudioFile(media.track, file);
            return;
        }
        // Like Cubase: a mono file makes a mono track (inserts in mono, pan law), anything with
        // two or more channels a stereo track (balance).
        safe->session_.setTrackFormat(media.track, media.fileNumChannels == 1 ? engine::TrackFormat::Mono
                                                                               : engine::TrackFormat::Stereo);
        safe->session_.setTrackAudio(media.track, media.audio, media.name);
        safe->arrangement_.setSelectedTrack(media.track);
        safe->arrangement_.zoomToFit();
        juce::String text = "Loaded " + media.name + " into track " + juce::String(media.track + 1);
        if (media.fileSampleRate != media.targetSampleRate)
            text << " (resampled " << juce::String(media.fileSampleRate, 0) << " -> " << juce::String(media.targetSampleRate, 0) << " Hz)";
        if (media.nonFiniteSamples > 0)
            text << " (" << MediaLoader::describeNonFinite(media.nonFiniteSamples) << ")";
        safe->setStatus(text);
    });
}

// ---- device sample-rate changes (Step 11) ---------------------------------------------------------------

void MainComponent::startClipConversion() {
    if (conversionRunning_)
        return; // finishClipConversion() checks the rates again and starts another round
    const double from = session_.getClipSampleRate();
    const double to = engine_.getSampleRate();
    if (from == to) {
        if (resumeAfterConversion_.exchange(false))
            engine_.getTransport().play();
        return;
    }
    conversionRunning_ = true;
    setStatus("Converting clips " + juce::String(from, 0) + " -> " + juce::String(to, 0) + " Hz...");
    juce::Component::SafePointer<MainComponent> safe(this);
    loader_.convertBuffers(session_.getClipSources(), from, to, [safe, from, to](const ConvertedBuffers& converted) {
        if (safe != nullptr)
            safe->finishClipConversion(from, to, converted);
    });
}

void MainComponent::finishClipConversion(double from, double to, const ConvertedBuffers& converted) {
    conversionRunning_ = false;
    // The project may have moved on while the loader thread worked: another device change, a
    // new project, the demo song. Then this result is useless; start over from the current state.
    if (from != session_.getClipSampleRate() || to != engine_.getSampleRate()) {
        startClipConversion();
        return;
    }
    // A file that finished loading meanwhile (at the old clip rate) is not in `converted` yet:
    // convert it in one more round instead of resampling it here on the message thread.
    std::vector<std::shared_ptr<const AudioBuffer>> missing;
    for (const auto& src : session_.getClipSources())
        if (converted.count(src) == 0)
            missing.push_back(src);
    if (!missing.empty()) {
        conversionRunning_ = true;
        juce::Component::SafePointer<MainComponent> safe(this);
        loader_.convertBuffers(std::move(missing), from, to, [safe, from, to, converted](const ConvertedBuffers& more) {
            if (safe == nullptr)
                return;
            ConvertedBuffers all = converted;
            all.insert(more.begin(), more.end());
            safe->finishClipConversion(from, to, all);
        });
        return;
    }
    session_.applySampleRateConversion(to, converted); // swaps sources, scales positions, pushes clips
    setStatus("Sample rate changed to " + juce::String(to, 0) + " Hz: clips converted");
    if (resumeAfterConversion_.exchange(false))
        engine_.getTransport().play();
}

// ---- export -------------------------------------------------------------------------------------------

void MainComponent::exportMixdownInteractive() {
    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Export mixdown", juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("DawLab mixdown.wav"),
        "*.wav");
    juce::Component::SafePointer<MainComponent> safe(this);
    fileChooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                                  juce::FileBrowserComponent::warnAboutOverwriting,
                              [safe](const juce::FileChooser& chooser) {
                                  if (safe != nullptr && chooser.getResult() != juce::File())
                                      safe->exportMixdown(chooser.getResult().withFileExtension("wav"));
                              });
}

void MainComponent::exportMixdown(const juce::File& file) {
    if (isExporting())
        return;
    if (isConvertingSampleRate()) {
        setStatus("Export not started: the clips are still being converted to the new sample rate");
        return;
    }
    if (session_.getSongEndSample() <= 0) {
        setStatus("Nothing to export: the project has no clips");
        return;
    }
    // Rule 1 (Exporter.h): detach the engine from the device. After this call no audio callback
    // touches the engine; the transport state is restored by the renderer afterwards.
    audioHost_.setEngineAttached(false);
    closeAllEditors();
    exportOverlay_ = std::make_unique<ExportOverlay>("Exporting " + file.getFileName() + "...");
    addAndMakeVisible(*exportOverlay_);
    exportOverlay_->setBounds(getLocalBounds());
    setStatus("Exporting...");

    ExportOptions options;
    options.file = file;
    juce::Component::SafePointer<MainComponent> safe(this);
    exportJob_ = std::make_unique<ExportJob>(engine_, options, [safe](const ExportResult& result) {
        if (safe != nullptr)
            safe->finishExport(result);
    });
    exportJob_->start();
}

void MainComponent::finishExport(const ExportResult& result) {
    exportJob_.reset();
    exportOverlay_.reset();
    if (mode_ == Mode::Live && audioHost_.isDeviceOpen())
        audioHost_.setEngineAttached(true);
    if (result.ok)
        setStatus("Exported " + result.file.getFullPathName() + ": " + juce::String(result.seconds, 2) + " s, peak " +
                  juce::String(result.peakDb, 2) + " dBFS, " + juce::String(result.integratedLufs, 1) + " LUFS, rendered in " +
                  juce::String(result.renderSeconds, 2) + " s");
    else
        setStatus("Export failed: " + result.error);
    if (onExportFinished)
        onExportFinished(result);
}

// ---- dialogs -------------------------------------------------------------------------------------------

void MainComponent::showAudioSettings() {
    if (!audioHost_.isDeviceOpen()) {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Audio settings",
                                               "No audio device is open (offline mode).");
        return;
    }
    // JUCE's ready-made device selector. Changing the device or sample rate restarts the device:
    // audioDeviceStopped -> audioDeviceAboutToStart -> Engine::prepare (AudioHost.cpp).
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent>(audioHost_.getDeviceManager(), 0, 2, 2, 2,
                                                                         false, false, true, false);
    selector->setSize(520, 420);
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(selector.release());
    options.dialogTitle = "Audio settings";
    options.dialogBackgroundColour = DawColours::panel();
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.componentToCentreAround = this;
    options.launchAsync();
}

void MainComponent::showKeyboardShortcuts() {
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::NoIcon, "Keyboard shortcuts", keyboardShortcutsText());
}

juce::String MainComponent::keyboardShortcutsText() {
    juce::String text;
    juce::Array<juce::CommandID> ids;
    getAllCommands(ids);
    for (auto id : ids) {
        const auto* info = commandManager_.getCommandForID(id);
        if (info == nullptr)
            continue;
        const auto keys = commandManager_.getKeyMappings()->getKeyPressesAssignedToCommand(id);
        if (keys.isEmpty())
            continue;
        text << info->shortName << ":  " << keys.getReference(0).getTextDescriptionWithIcons() << "\n";
    }
    return text;
}

// ---- inserts ----------------------------------------------------------------------------------------------

namespace {
// What a menu shown with showMenuAsync() calls with the item chosen (0: none): `handle(owner, item)`, if `owner`
// is still there by then. A menu's own functions build it and handle the item: the smoke test calls those.
template <typename Owner, typename Handle>
std::function<void(int)> menuCallback(Owner* owner, Handle handle) {
    return [safe = juce::Component::SafePointer<Owner>(owner), handle](int item) {
        if (safe != nullptr)
            handle(*safe.getComponent(), item);
    };
}
} // namespace

void MainComponent::showSlotMenu(ChannelRef ref, int slot, juce::Component& anchor) {
    if (isExporting())
        return;
    juce::PopupMenu menu = EffectMenu::build(session_.getSlot(ref, slot), EffectMenu::chapter13CompressorFile(),
                                             EffectMenu::chapter13LimiterFile());
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&anchor),
                       menuCallback(this, [ref, slot](MainComponent& m, int result) { m.handleSlotMenuResult(ref, slot, result); }));
}

void MainComponent::handleSlotMenuResult(ChannelRef ref, int slot, int result) {
    switch (result) {
    case EffectMenu::kRemove:
        removeInsert(ref, slot);
        break;
    case EffectMenu::kVst3File:
        chooseVst3For(ref, slot);
        break;
    case EffectMenu::kChapter13Compressor:
    case EffectMenu::kChapter13Limiter:
        loadChapter13Plugin(ref, slot, EffectMenu::chapter13File(result));
        break;
    case EffectMenu::kToggleBypass:
        toggleInsertBypass(ref, slot);
        break;
    case EffectMenu::kOpenEditor:
        openInsertEditor(ref, slot, false);
        break;
    case EffectMenu::kOpenGenericPanel:
        openInsertEditor(ref, slot, true);
        break;
    default: // a built-in effect, or no item (0)
        insertBuiltin(ref, slot, result - EffectMenu::kFirstBuiltin);
        break;
    }
}

void MainComponent::insertBuiltin(ChannelRef ref, int slot, int entry) {
    const auto& entries = EffectMenu::builtinEntries();
    if (entry < 0 || entry >= static_cast<int>(entries.size()))
        return;
    const engine::EffectType type = entries[static_cast<size_t>(entry)].type;
    if (session_.setBuiltinInsert(ref, slot, type) != nullptr)
        openInsertEditor(ref, slot, false);
    else
        setStatus("Could not insert the effect (command queue full?)");
}

void MainComponent::removeInsert(ChannelRef ref, int slot) {
    session_.clearInsert(ref, slot);
}

void MainComponent::toggleInsertBypass(ChannelRef ref, int slot) {
    const auto& s = session_.getSlot(ref, slot);
    if (!s.isEmpty())
        session_.setInsertBypassed(ref, slot, !s.effect->isBypassed());
}

void MainComponent::loadChapter13Plugin(ChannelRef ref, int slot, const juce::File& file) {
    juce::String error;
    if (!loadVst3Into(ref, slot, file, error))
        setStatus("VST3 load failed: " + error);
}

void MainComponent::chooseVst3For(ChannelRef ref, int slot) {
    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Choose a VST3 plugin", juce::File("C:\\Program Files\\Common Files\\VST3"), "*.vst3");
    juce::Component::SafePointer<MainComponent> safe(this);
    // A .vst3 "bundle" is a folder on Windows (older plugins: a single file), so allow both.
    fileChooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles |
                                  juce::FileBrowserComponent::canSelectDirectories,
                              [safe, ref, slot](const juce::FileChooser& chooser) {
                                  if (safe == nullptr || chooser.getResult() == juce::File())
                                      return;
                                  juce::String error;
                                  if (!safe->loadVst3Into(ref, slot, chooser.getResult(), error))
                                      safe->setStatus("VST3 load failed: " + error);
                              });
}

bool MainComponent::loadVst3Into(ChannelRef ref, int slot, const juce::File& file, juce::String& error) {
    // Chapter 13: create the instance on the message thread, wrap it, prepare it, hand it over.
    auto instance = pluginHost_.loadFromFile(file, engine_.getSampleRate(), engine_.getMaxBlockSize(), error);
    if (instance == nullptr)
        return false;
    auto vst = std::make_unique<VstInsert>(std::make_unique<host::HostedPlugin>(std::move(instance)));
    if (session_.setVstInsert(ref, slot, std::move(vst)) == nullptr) {
        error = "the engine refused the insert";
        return false;
    }
    setStatus("Loaded VST3 " + session_.getSlot(ref, slot).name + " (latency " +
              juce::String(session_.getSlot(ref, slot).effect->getReportedLatencySamples()) + " samples, compensated)");
    openInsertEditor(ref, slot, false);
    return true;
}

void MainComponent::openInsertEditor(ChannelRef ref, int slot, bool generic) {
    const InsertSlotState& s = session_.getSlot(ref, slot);
    if (s.isEmpty())
        return;
    if (s.kind == SlotKind::Vst3 && !generic) {
        s.vst->openEditor();
        return;
    }
    auto it = parameterWindows_.find(s.effect);
    if (it == parameterWindows_.end()) {
        const juce::String title = session_.getStripName(ref) + " - slot " + juce::String(slot + 1) + ": " +
                                   (s.kind == SlotKind::Builtin ? EffectMenu::labelFor(s.type) : s.name);
        auto window = std::make_unique<ParameterWindow>(*s.effect, title, &lookAndFeel_);
        engine::InsertEffect* key = s.effect;
        window->onClose = [this, key] {
            // Deleting a window inside its own callback is not allowed: do it asynchronously.
            juce::Component::SafePointer<MainComponent> safe(this);
            juce::MessageManager::callAsync([safe, key] {
                if (safe != nullptr)
                    safe->parameterWindows_.erase(key);
            });
        };
        const auto size = window->getPanel().getIdealSize();
        window->centreAroundComponent(this, size.x, size.y + 30);
        it = parameterWindows_.emplace(key, std::move(window)).first;
    }
    it->second->setVisible(true);
    it->second->toFront(true);
}

ParameterWindow* MainComponent::getParameterWindow(engine::InsertEffect* effect) const {
    auto it = parameterWindows_.find(effect);
    return it != parameterWindows_.end() ? it->second.get() : nullptr;
}

void MainComponent::closeAllEditors() {
    parameterWindows_.clear();
    for (int s = 0; s < kNumStrips; ++s)
        for (int slot = 0; slot < kInsertSlotsPerStrip; ++slot)
            if (auto* vst = session_.getSlot(stripRefAt(s), slot).vst)
                vst->closeEditor();
}

// ---- layout ---------------------------------------------------------------------------------------------

void MainComponent::setStatus(const juce::String& text) {
    status_.setText(text, juce::dontSendNotification);
    deviceInfo_.setText(audioHost_.getDeviceDescription(), juce::dontSendNotification);
}

void MainComponent::paint(juce::Graphics& g) { g.fillAll(DawColours::background()); }

void MainComponent::resized() {
    auto area = getLocalBounds();
    menuBar_.setBounds(area.removeFromTop(24));
    transportBar_.setBounds(area.removeFromTop(58));
    auto statusRow = area.removeFromBottom(22).reduced(8, 2);
    deviceInfo_.setBounds(statusRow.removeFromRight(420));
    status_.setBounds(statusRow);
    const int mixerHeight = std::max(430, area.getHeight() * 55 / 100);
    mixer_.setBounds(area.removeFromBottom(mixerHeight));
    arrangement_.setBounds(area);
    if (exportOverlay_ != nullptr)
        exportOverlay_->setBounds(getLocalBounds());
}

// ---- commands -------------------------------------------------------------------------------------------

void MainComponent::getAllCommands(juce::Array<juce::CommandID>& c) {
    const juce::CommandID ids[] = {CommandIDs::newProject, CommandIDs::loadDemoSong, CommandIDs::loadAudioIntoTrack, CommandIDs::exportMixdown,
                CommandIDs::showAudioSettings, juce::StandardApplicationCommandIDs::quit, CommandIDs::playStop,
                CommandIDs::returnToStart, CommandIDs::toggleLoop, CommandIDs::seekBackOneBar, CommandIDs::seekForwardOneBar,
                CommandIDs::selectNextTrack, CommandIDs::selectPreviousTrack, CommandIDs::toggleMuteSelected,
                CommandIDs::toggleSoloSelected, CommandIDs::zoomIn, CommandIDs::zoomOut, CommandIDs::showKeyboardShortcuts};
    c.addArray(ids, static_cast<int>(sizeof(ids) / sizeof(ids[0])));
}

// Track > Mute / Solo selected track.
void MainComponent::toggleMuteSelected() {
    auto& strip = engine_.getMixer().getTrack(arrangement_.getSelectedTrack());
    strip.setMute(!strip.isMuted());
    arrangement_.repaint();
}

void MainComponent::toggleSoloSelected() {
    auto& strip = engine_.getMixer().getTrack(arrangement_.getSelectedTrack());
    strip.setSolo(!strip.isSoloed());
    arrangement_.repaint();
}

// View > Zoom in / out: around the middle of the lanes.
void MainComponent::zoomArrangement(double factor) {
    arrangement_.zoom(factor, ArrangementView::kHeaderWidth + 0.5f * (arrangement_.getWidth() - ArrangementView::kHeaderWidth));
}

// A command of this target as the menus, the keyboard and the list of shortcuts show it, what it does and
// when it can run. findCommand() has them all in one table.
struct MainComponent::Command {
    struct Key {
        int code = 0; // 0: no key
        int modifiers = 0;
    };
    juce::CommandID id;
    const char* name;
    const char* description;
    const char* category;
    Key keys[2];                  // its default key presses
    void (*run)(MainComponent&);  // what it does
    bool idleOnly = false;        // not while an export renders
    Shows shows = Shows::nothing; // what its tick mark in the menu shows
};

// The table of all commands; nullptr for an id that is none of them.
const MainComponent::Command* MainComponent::findCommand(juce::CommandID id) {
    using juce::KeyPress;
    namespace category = CommandCategories;
    constexpr int cmd = juce::ModifierKeys::commandModifier; // Ctrl on Windows, Cmd on macOS
    constexpr int none = juce::ModifierKeys::noModifiers;
    constexpr bool idle = true, anytime = false; // idle: not while an export renders
    static const Command commands[] = {
        {CommandIDs::newProject, "New project", "Start an empty project", category::file, {{'n', cmd}},
         [](MainComponent& m) { m.newProject(); }, idle},
        {CommandIDs::loadDemoSong, "Load demo song", "Generate the 8-track demo song", category::file, {{'d', cmd}},
         [](MainComponent& m) { m.loadDemoSongAsync(); }, idle},
        {CommandIDs::loadAudioIntoTrack, "Load audio into selected track...", "Load a WAV / AIFF file", category::file,
         {{'o', cmd}}, [](MainComponent& m) { m.chooseAudioFileForTrack(m.arrangement_.getSelectedTrack()); }, idle},
        {CommandIDs::exportMixdown, "Export mixdown...", "Bounce the song to a WAV file", category::file, {{'e', cmd}},
         [](MainComponent& m) { m.exportMixdownInteractive(); }, idle},
        {CommandIDs::showAudioSettings, "Audio settings...", "Choose the audio device", category::file, {{',', cmd}},
         [](MainComponent& m) { m.showAudioSettings(); }, idle},
        {juce::StandardApplicationCommandIDs::quit, "Quit", "Quit DawLab 8", category::file, {{'q', cmd}},
         [](MainComponent&) { if (auto* app = juce::JUCEApplicationBase::getInstance()) app->systemRequestedQuit(); }, anytime},
        {CommandIDs::playStop, "Play / Stop", "Start or stop playback", category::transport, {{KeyPress::spaceKey, none}},
         [](MainComponent& m) { m.togglePlay(); }, idle, Shows::playing},
        {CommandIDs::returnToStart, "Return to start", "Move the playhead to bar 1", category::transport,
         {{KeyPress::homeKey, none}}, [](MainComponent& m) { m.returnToStart(); }, idle},
        {CommandIDs::toggleLoop, "Loop", "Loop on / off", category::transport, {{'l', none}},
         [](MainComponent& m) { m.toggleLoop(); }, idle, Shows::loop},
        {CommandIDs::seekBackOneBar, "Back one bar", "Move the playhead one bar back", category::transport,
         {{KeyPress::leftKey, none}}, [](MainComponent& m) { m.seekBars(-1); }, idle},
        {CommandIDs::seekForwardOneBar, "Forward one bar", "Move the playhead one bar forward", category::transport,
         {{KeyPress::rightKey, none}}, [](MainComponent& m) { m.seekBars(+1); }, idle},
        {CommandIDs::selectNextTrack, "Select next track", "", category::track, {{KeyPress::downKey, none}},
         [](MainComponent& m) { m.arrangement_.setSelectedTrack(m.arrangement_.getSelectedTrack() + 1); }, anytime},
        {CommandIDs::selectPreviousTrack, "Select previous track", "", category::track, {{KeyPress::upKey, none}},
         [](MainComponent& m) { m.arrangement_.setSelectedTrack(m.arrangement_.getSelectedTrack() - 1); }, anytime},
        {CommandIDs::toggleMuteSelected, "Mute selected track", "", category::track, {{'m', none}},
         [](MainComponent& m) { m.toggleMuteSelected(); }, idle},
        {CommandIDs::toggleSoloSelected, "Solo selected track", "", category::track, {{'s', none}},
         [](MainComponent& m) { m.toggleSoloSelected(); }, idle},
        {CommandIDs::zoomIn, "Zoom in", "", category::view, {{'=', none}, {'+', none}},
         [](MainComponent& m) { m.zoomArrangement(1.5); }, anytime},
        {CommandIDs::zoomOut, "Zoom out", "", category::view, {{'-', none}},
         [](MainComponent& m) { m.zoomArrangement(1.0 / 1.5); }, anytime},
        {CommandIDs::showKeyboardShortcuts, "Keyboard shortcuts...", "List all shortcuts", category::view,
         {{KeyPress::F1Key, none}}, [](MainComponent& m) { m.showKeyboardShortcuts(); }, anytime},
    };
    for (const Command& command : commands)
        if (command.id == id)
            return &command;
    return nullptr;
}

// What a command's tick mark in the menu shows now.
bool MainComponent::isTicked(Shows shows) {
    switch (shows) {
    case Shows::nothing: return false;
    case Shows::playing: return engine_.getTransport().isPlaying();
    case Shows::loop: return engine_.getTransport().isLoopEnabled();
    }
    return false;
}

void MainComponent::getCommandInfo(juce::CommandID id, juce::ApplicationCommandInfo& info) {
    const Command* command = findCommand(id);
    if (command == nullptr)
        return;
    info.setInfo(command->name, command->description, command->category, 0);
    for (const Command::Key& key : command->keys)
        if (key.code != 0)
            info.addDefaultKeypress(key.code, key.modifiers);
    info.setActive(!command->idleOnly || !isExporting());
    info.setTicked(isTicked(command->shows));
}

bool MainComponent::perform(const InvocationInfo& info) {
    if (isExporting() && info.commandID != juce::StandardApplicationCommandIDs::quit)
        return true; // swallow everything while rendering
    const Command* command = findCommand(info.commandID);
    if (command == nullptr)
        return false;
    command->run(*this);
    return true;
}

// ---- menu bar --------------------------------------------------------------------------------------------

juce::StringArray MainComponent::getMenuBarNames() { return {"File", "Transport", "Track", "View"}; }

juce::PopupMenu MainComponent::getMenuForIndex(int index, const juce::String&) {
    juce::PopupMenu m;
    auto* cm = &commandManager_;
    switch (index) {
    case 0:
        m.addCommandItem(cm, CommandIDs::newProject);
        m.addCommandItem(cm, CommandIDs::loadDemoSong);
        m.addCommandItem(cm, CommandIDs::loadAudioIntoTrack);
        m.addSeparator();
        m.addCommandItem(cm, CommandIDs::exportMixdown);
        m.addSeparator();
        m.addCommandItem(cm, CommandIDs::showAudioSettings);
        m.addSeparator();
        m.addCommandItem(cm, juce::StandardApplicationCommandIDs::quit);
        break;
    case 1:
        m.addCommandItem(cm, CommandIDs::playStop);
        m.addCommandItem(cm, CommandIDs::returnToStart);
        m.addCommandItem(cm, CommandIDs::toggleLoop);
        m.addSeparator();
        m.addCommandItem(cm, CommandIDs::seekBackOneBar);
        m.addCommandItem(cm, CommandIDs::seekForwardOneBar);
        break;
    case 2:
        m.addCommandItem(cm, CommandIDs::selectPreviousTrack);
        m.addCommandItem(cm, CommandIDs::selectNextTrack);
        m.addSeparator();
        m.addCommandItem(cm, CommandIDs::toggleMuteSelected);
        m.addCommandItem(cm, CommandIDs::toggleSoloSelected);
        break;
    case 3:
        m.addCommandItem(cm, CommandIDs::zoomIn);
        m.addCommandItem(cm, CommandIDs::zoomOut);
        m.addSeparator();
        m.addCommandItem(cm, CommandIDs::showKeyboardShortcuts);
        break;
    default: break;
    }
    if (onExtendMenu)
        onExtendMenu(index, m);
    return m;
}

void MainComponent::setExtraCommandTarget(juce::ApplicationCommandTarget* target) {
    extraCommandTarget_ = target;
    if (target != nullptr)
        commandManager_.registerAllCommandsForTarget(target);
    commandManager_.commandStatusChanged();
}

void MainComponent::menuItemSelected(int, int) {} // command items are dispatched by the command manager

} // namespace dawlab::app
