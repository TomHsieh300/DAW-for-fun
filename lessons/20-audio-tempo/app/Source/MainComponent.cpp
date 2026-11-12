#include "MainComponent.h"

#include "MidiFileIO.h"
#include "PluginInstrument.h"

#include "EffectMenu.h"
#include "VstInsert.h"

#include <dawlab/engine/DemoSong.h>

#include <algorithm>
#include <filesystem>

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

    // ---- audio -------------------------------------------------------------------------------
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

    // ---- project --------------------------------------------------------------------------------
    // Undoing the removal of a VST3 insert builds the plugin again from its file and state.
    session_.createVst3 = [this](const juce::File& file, const juce::MemoryBlock& state, juce::String& error) {
        std::unique_ptr<VstInsert> vst;
        if (auto instance = pluginHost_.loadFromFile(file, engine_.getSampleRate(), engine_.getMaxBlockSize(), error)) {
            auto hosted = std::make_unique<host::HostedPlugin>(std::move(instance));
            if (state.getSize() > 0)
                hosted->setState(state);
            vst = std::make_unique<VstInsert>(std::move(hosted));
        }
        return vst;
    };
    // Chapter 19: a plugin instrument for a track, the same way - and only what can be an instrument.
    session_.createPluginInstrument = [this](const juce::File& file, const juce::MemoryBlock& state, juce::String& error) {
        std::unique_ptr<PluginInstrument> plugin;
        if (auto instance = pluginHost_.loadFromFile(file, engine_.getSampleRate(), engine_.getMaxBlockSize(), error)) {
            if (!PluginInstrument::isInstrument(*instance)) {
                error = instance->getName() + " is not an instrument: it takes no MIDI or makes no sound";
                return plugin;
            }
            plugin = std::make_unique<PluginInstrument>(std::move(instance));
            plugin->setState(state);
        }
        return plugin;
    };
    session_.onInsertWillBeRemoved = [this](engine::InsertEffect* fx) {
        parameterWindows_.erase(fx);
        if (auto* vst = dynamic_cast<VstInsert*>(fx))
            vst->closeEditor();
    };
    session_.newProject();
    session_.dispatchPendingMessages(); // the views show the new tracks before the first tick
    arrangement_.onFilesDropped = [this](engine::TrackId track, const juce::StringArray& files) {
        loadDroppedFiles(track, files);
    };
    arrangement_.selectLane(0);
    arrangement_.onArmClicked = [this](engine::TrackId track) {
        const engine::Track* t = engine_.getTrack(track);
        setArmed(track, t != nullptr && !t->isArmed());
    };
    arrangement_.onMonitorClicked = [this](engine::TrackId track) { cycleMonitor(track); };
    arrangement_.onRecordMenu = [this](engine::TrackId track) { showRecordMenu(track); };
    arrangement_.onClipMenu = [this](engine::TrackId track, int clip) { showClipMenu(track, clip); };
    arrangement_.onEditMidiClip = [this](engine::TrackId track, int clip) { openPianoRoll(track, clip); };
    arrangement_.onMidiClipMenu = [this](engine::TrackId track, int clip) { showMidiClipMenu(track, clip); };
    arrangement_.liveTakeOf = [this](engine::TrackId track, engine::LiveTake& live) { return takeRecorder_.getLive(track, live); };
    takeRecorder_.onMessage = [this](const juce::String& message, bool alarm) {
        setStatus(message);
        status_.setColour(juce::Label::textColourId, alarm ? DawColours::mute() : DawColours::textDim());
    };

    // ---- commands, menu, keyboard ------------------------------------------------------------------
    commandManager_.registerAllCommandsForTarget(this);
    commandManager_.setFirstCommandTarget(this);
    addKeyListener(commandManager_.getKeyMappings());
    setWantsKeyboardFocus(true);
    setApplicationCommandManagerToWatch(&commandManager_);
    menuBar_.setModel(this);
    addAndMakeVisible(menuBar_);

    // ---- GUI ------------------------------------------------------------------------------------------
    transportBar_.onPlayStop = [this] { togglePlay(); };
    transportBar_.onReturnToStart = [this] { returnToStart(); };
    transportBar_.onToggleLoop = [this] { toggleLoop(); };
    transportBar_.onRecord = [this] { toggleRecord(); };
    transportBar_.onTempoChange = [this](double bpm) {
        session_.perform(edit::SetTempo{bpm}, "Tempo", mergeKeyFor(StripTarget{}, 0x54454d50)); // "TEMP"
    };
    transportBar_.onTempoGesture = [this] { session_.getHistory().endMerge(); };
    addAndMakeVisible(transportBar_);
    addAndMakeVisible(arrangement_);
    addAndMakeVisible(tempoLane_);
    mixer_.onLoadRequested = [this](engine::TrackId track) { chooseAudioFileForTrack(track); };
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

    setSize(kDefaultWidth, kDefaultHeight);
    if (mode_ == Mode::Live) {
        juce::PropertiesFile::Options options;
        options.applicationName = "DawLab 20";
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        settings_ = std::make_unique<juce::PropertiesFile>(options);
        takeRecorder_.setTrimSamples(settings_->getIntValue("recordingTrim", 0));
        // A take that a crash left behind matters more than the demo song.
        if (std::filesystem::exists(std::filesystem::u8path(takeRecorder_.getFolder().getFullPathName().toStdString())) &&
            !engine::findUnfinishedTakes(std::filesystem::u8path(takeRecorder_.getFolder().getFullPathName().toStdString())).empty())
            recoverCrashedTakes();
        else
            loadDemoSongAsync(); // the app starts with something to listen to
        startTimerHz(30);
    }
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
    session_.createVst3 = nullptr;
    session_.createPluginInstrument = nullptr;
    synthPanel_.reset();
    setLookAndFeel(nullptr);
}

// ---- the UI tick ------------------------------------------------------------------------------------

// Chapter 20: what the status line says about clips whose render could not be made.
static juce::String failedRendersText(int clips) {
    const bool one = clips == 1;
    return juce::String(clips) + (one ? " clip that follows" : " clips that follow") +
           " the tempo could not be rendered (longer than the engine renders, or out of memory): " + (one ? "it plays at its" : "they play at their") +
           " old speed";
}

void MainComponent::tick() {
    // Session's change messages are asynchronous; deliver pending ones now, so that a tick always
    // shows the current structure (this matters for the snapshot, which has no running message loop).
    session_.dispatchPendingMessages();
    // During an export the render thread collects the garbage (Exporter.h, rule 2).
    if (!isExporting())
        engine_.collectGarbage();
    session_.collectAutomation();
    // Chapter 20: renders of clips that follow the tempo arrive from the worker thread.
    if (!isExporting() && session_.pollRenders() && session_.getNumFailedRenders() > 0)
        setStatus(failedRendersText(session_.getNumFailedRenders()));
    // Chapter 19: the synth panel shows the selected track.
    if (synthPanel_ != nullptr && synthPanel_->isVisible() && synthPanel_->getPanel().getTrack() != arrangement_.getSelectedTrack())
        synthPanel_->getPanel().showTrack(arrangement_.getSelectedTrack());
    // Chapter 19: the tempo lane shares the arrangement's time axis, and follows its scrolling and zooming.
    if (arrangement_.getViewStart() != laneViewStart_ || arrangement_.getSamplesPerPixel() != laneSamplesPerPixel_) {
        laneViewStart_ = arrangement_.getViewStart();
        laneSamplesPerPixel_ = arrangement_.getSamplesPerPixel();
        tempoLane_.repaint();
    }
    if (!isConvertingSampleRate())
        takeRecorder_.tick();
    // Chapter 18: the keyboard's keys into the takes; a transport that stopped ends a MIDI recording.
    if (midiRecording_.isRecording()) {
        midiRecording_.poll();
        if (!engine_.getTransport().isPlaying()) {
            const int notes = midiRecording_.stop();
            setStatus(notes > 0 ? "Recorded " + juce::String(notes) + (notes == 1 ? " note" : " notes") : juce::String("No notes recorded"));
        }
    }
    transportBar_.setRecording(takeRecorder_.getPhase() == TakeRecorder::Phase::Recording || midiRecording_.isRecording());
    transportBar_.update();
    // Chapter 15: while the CPU display is red (a dropout in the last 2 s), the status line says
    // in words what happened and what to do; this matters more than any other message.
    const juce::String dropouts = "Audio dropouts: the mix takes longer to compute than it lasts. Remove tracks or "
                                  "effects, or choose a larger buffer size in Audio settings";
    if (transportBar_.getCpuStatus().state == engine::CpuState::Overload && !isExporting() && getStatus() != dropouts)
        setStatus(dropouts);
    arrangement_.update();
    mixer_.update();
    if (audioHost_.isAnalyzerReady())
        mixer_.updateSpectrum(audioHost_.getSpectrumAnalyzer());
    if (const juce::String title = getWindowTitle(); title != lastTitle_) {
        lastTitle_ = title;
        if (onTitleChanged)
            onTitleChanged(title);
    }
}

void MainComponent::simulatePlayback(int64_t numSamples, int blockSize, engine::InputBlock input) {
    jassert(!audioHost_.isDeviceOpen() || !audioHost_.isEngineAttached());
    blockSize = std::clamp(blockSize, 1, offlineScratch_.getNumSamples());
    float* channels[engine::kNumChannels] = {offlineScratch_.getWritePointer(0), offlineScratch_.getWritePointer(1)};
    for (int64_t done = 0; done < numSamples; done += blockSize) {
        const int n = static_cast<int>(std::min<int64_t>(blockSize, numSamples - done));
        // The input goes along with the frames it belongs to.
        const float* inputs[engine::kMaxInputChannels];
        const int numInputs = std::min(input.numChannels, engine::kMaxInputChannels);
        for (int ch = 0; ch < numInputs; ++ch)
            inputs[ch] = input.channels[ch] != nullptr ? input.channels[ch] + done : nullptr;
        audioHost_.processOffline(channels, engine::kNumChannels, n, engine::InputBlock{inputs, numInputs});
    }
}

// ---- recording (chapter 17, Step 7) ---------------------------------------------------------------------

bool MainComponent::refuseWhileRecording(const juce::String& what) {
    if (!takeRecorder_.isBusy())
        return false;
    setStatus(what + " has to wait: a recording is running. Stop it first");
    return true;
}

bool MainComponent::toggleRecord() {
    const bool recordingAudio = takeRecorder_.getPhase() == TakeRecorder::Phase::Recording;
    if (recordingAudio || midiRecording_.isRecording()) {
        if (recordingAudio)
            takeRecorder_.stop();
        if (midiRecording_.isRecording()) { // chapter 18: one undo step for all the takes
            const int notes = midiRecording_.stop();
            if (!recordingAudio)
                setStatus(notes > 0 ? "Recorded " + juce::String(notes) + (notes == 1 ? " note" : " notes")
                                    : juce::String("No notes recorded"));
        }
        return true;
    }
    // Chapter 18: armed audio tracks record audio, armed instrument tracks record the keyboard.
    bool audioArmed = false, midiArmed = false;
    for (int i = 0; i < session_.getNumTracks(); ++i) {
        const engine::TrackId id = session_.getTrackId(i);
        const engine::Track* t = engine_.getTrack(id);
        if (t != nullptr && t->isArmed())
            (session_.isInstrument(id) ? midiArmed : audioArmed) = true;
    }
    if (midiArmed && !audioArmed) {
        if (isExporting() || isConvertingSampleRate()) {
            setStatus("Not now: the project is being exported or converted");
            return false;
        }
        midiRecording_.start(midiMode_);
        status_.setColour(juce::Label::textColourId, DawColours::textDim());
        engine_.getTransport().play();
        setStatus(midiMode_ == MidiRecordMode::Overdub ? "Recording MIDI (overdub)" : "Recording MIDI (replace)");
        transportBar_.update();
        return true;
    }
    if (isExporting() || isConvertingSampleRate()) {
        setStatus("Not now: the project is being exported or converted");
        return false;
    }
    const juce::String error = takeRecorder_.start();
    if (error.isNotEmpty()) {
        setStatus(error);
        return false;
    }
    if (midiArmed)
        midiRecording_.start(midiMode_);
    status_.setColour(juce::Label::textColourId, DawColours::textDim());
    engine_.getTransport().play();
    setStatus("Recording");
    transportBar_.update();
    return true;
}

bool MainComponent::setArmed(engine::TrackId track, bool armed) {
    engine::Track* t = engine_.getTrack(track);
    if (t == nullptr)
        return false;
    if (armed && session_.isInstrument(track)) {
        // Chapter 18: an instrument track listens to the keyboard, not to the audio input.
        if (mode_ == Mode::Live) {
            const juce::StringArray keyboards = audioHost_.openMidiInputs();
            setStatus(keyboards.isEmpty() ? juce::String("No MIDI keyboard found: connect one and arm the track again")
                                          : "Playing " + session_.getStripName(session_.trackRef(track)) + " from " + keyboards.joinIntoString(", "));
        }
    } else if (armed) {
        if (const juce::String error = audioHost_.ensureInputsOpen(); error.isNotEmpty()) {
            setStatus("Cannot open the input: " + error);
            return false;
        }
        deviceInfo_.setText(audioHost_.getDeviceDescription(), juce::dontSendNotification);
    }
    t->setArmed(armed);
    if (!armed) // nothing would explain why an unarmed track goes on being heard
        t->getMonitor().setMode(engine::MonitorMode::Off);
    arrangement_.repaint();
    return true;
}

void MainComponent::cycleMonitor(engine::TrackId track) {
    engine::Track* t = engine_.getTrack(track);
    if (t == nullptr)
        return;
    using engine::MonitorMode;
    const MonitorMode now = t->getMonitor().mode();
    const MonitorMode next = now == MonitorMode::Off ? MonitorMode::Auto : (now == MonitorMode::Auto ? MonitorMode::Always : MonitorMode::Off);
    if (next != MonitorMode::Off && !t->isArmed() && !setArmed(track, true))
        return;
    t->getMonitor().resetFeedback(); // the user's hand on it: try again
    t->getMonitor().setMode(next);
    if (next != MonitorMode::Off) {
        const engine::Latencies latencies = audioHost_.getDeviceLatencies(0);
        engine::Latencies withMix = latencies;
        withMix.mix = engine_.getLatencySamples();
        const double ms = 1000.0 * static_cast<double>(engine::monitorLatency(withMix, t->getMonitorPath())) / engine_.getSampleRate();
        setStatus("Monitor " + juce::String(next == MonitorMode::Auto ? "Auto" : "Always") + ": you hear yourself about " +
                  juce::String(ms, 1) + " ms late. Use headphones: a microphone near a speaker howls");
    } else {
        setStatus("Monitor off");
    }
    arrangement_.repaint();
}

void MainComponent::showRecordMenu(engine::TrackId track) {
    engine::Track* t = engine_.getTrack(track);
    if (t == nullptr)
        return;
    juce::PopupMenu inputs;
    const juce::StringArray names = audioHost_.getInputNames();
    const int stereo = t->getNumProcessingChannels() == 2 ? 1 : 0;
    for (int i = 0; i + stereo < names.size(); ++i)
        inputs.addItem(100 + i, stereo != 0 ? names[i] + " + " + names[i + 1] : names[i], true, t->getInputChannel() == i);
    juce::PopupMenu menu;
    menu.addSectionHeader("Recording");
    menu.addItem(1, "Armed", true, t->isArmed());
    menu.addSubMenu("Input", inputs, !names.isEmpty());
    menu.addSeparator();
    menu.addSectionHeader("Monitor");
    menu.addItem(2, "Directly, dry: as early as it can be", true, t->getMonitorPath() == engine::MonitorPath::Direct);
    menu.addItem(3, "Through this track's effects: with the whole mix's latency", true, t->getMonitorPath() == engine::MonitorPath::ThroughInserts);
    if (t->getMonitor().feedbackDetected())
        menu.addItem(4, "Feedback was detected: listen again");
    juce::Component::SafePointer<MainComponent> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options{}, [safe, track](int result) {
        if (safe == nullptr)
            return;
        engine::Track* track_ = safe->engine_.getTrack(track);
        if (track_ == nullptr || result == 0)
            return;
        if (result == 1)
            safe->setArmed(track, !track_->isArmed());
        else if (result == 2)
            track_->setMonitorPath(engine::MonitorPath::Direct);
        else if (result == 3)
            track_->setMonitorPath(engine::MonitorPath::ThroughInserts);
        else if (result == 4)
            track_->getMonitor().resetFeedback();
        else if (result >= 100)
            track_->setInputChannel(result - 100);
        safe->arrangement_.repaint();
    });
}

void MainComponent::showClipMenu(engine::TrackId track, int clipIndex) {
    const auto& clips = session_.getClips(track);
    if (clipIndex < 0 || clipIndex >= static_cast<int>(clips.size()))
        return;
    const engine::Clip& clip = clips[static_cast<size_t>(clipIndex)];
    const std::vector<engine::Placement>* takes = session_.getTakesOf(clip);
    const engine::FollowMode mode = engine::followModeOf(clip);
    juce::PopupMenu menu;
    // Chapter 20: how the clip follows the tempo.
    constexpr int kFollowBase = 1000;
    menu.addSectionHeader("When the tempo changes");
    menu.addItem(kFollowBase + 0, "Stay where it is", true, mode == engine::FollowMode::Off);
    menu.addItem(kFollowBase + 1, "Keep its start on the beat", true, mode == engine::FollowMode::Start);
    menu.addItem(kFollowBase + 2, "Follow, pitch changes (varispeed)", true, mode == engine::FollowMode::Varispeed);
    menu.addItem(kFollowBase + 3, "Follow, pitch stays (stretch)", true, mode == engine::FollowMode::Stretch);
    // A take is chosen before the clip follows (Session::chooseTake).
    if (takes != nullptr && takes->size() >= 2 && session_.getCurrentTake(clip) >= 0 && mode == engine::FollowMode::Off) {
        menu.addSectionHeader("Takes");
        for (int i = 0; i < static_cast<int>(takes->size()); ++i)
            menu.addItem(i + 1, "Take " + juce::String(i + 1), true, session_.getCurrentTake(clip) == i);
    }
    juce::Component::SafePointer<MainComponent> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options{}, [safe, track, clipIndex](int result) {
        if (safe == nullptr || result <= 0)
            return;
        if (result >= kFollowBase)
            safe->setClipFollow(track, clipIndex, static_cast<engine::FollowMode>(result - kFollowBase));
        else
            safe->session_.chooseTake(track, clipIndex, result - 1);
    });
}

bool MainComponent::setClipFollow(engine::TrackId track, int clipIndex, engine::FollowMode mode) {
    if (refuseWhileRecording("Changing how a clip follows the tempo"))
        return false;
    if (session_.getClipFollow(track, clipIndex) == mode)
        return true;
    if (!session_.setClipFollow(track, clipIndex, mode)) {
        setStatus("This clip cannot follow the tempo");
        return false;
    }
    switch (session_.getClipFollow(track, clipIndex)) {
    case engine::FollowMode::Off: setStatus("The clip stays where it is when the tempo changes"); break;
    case engine::FollowMode::Start: setStatus("The clip's start stays on its beat when the tempo changes"); break;
    case engine::FollowMode::Varispeed: setStatus("The clip follows the tempo: faster is higher (varispeed)"); break;
    case engine::FollowMode::Stretch: setStatus("The clip follows the tempo and keeps its pitch (stretch)"); break;
    }
    return session_.getClipFollow(track, clipIndex) == mode;
}

void MainComponent::askForTrim() {
    auto* window = new juce::AlertWindow("Recording latency trim",
                                         "How many samples later the real round trip is than the driver reports (negative: earlier).\n"
                                         "Record a click through a cable: the distance from the click to its recording is this number.",
                                         juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor("trim", juce::String(takeRecorder_.getTrimSamples()), "Samples:");
    window->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    juce::Component::SafePointer<MainComponent> safe(this);
    window->enterModalState(true, juce::ModalCallbackFunction::create([safe, window](int result) {
        if (safe != nullptr && result == 1) {
            const int trim = window->getTextEditorContents("trim").getIntValue();
            safe->takeRecorder_.setTrimSamples(trim);
            if (safe->settings_ != nullptr)
                safe->settings_->setValue("recordingTrim", trim);
            safe->setStatus("Recording trim: " + juce::String(trim) + " samples");
        }
    }), true);
}

void MainComponent::recoverCrashedTakes() {
    const int found = takeRecorder_.recover();
    if (found == 0)
        return;
    arrangement_.zoomToFit();
    const juce::String message = juce::String(found) + (found == 1 ? " take was" : " takes were") +
                                 " still being recorded when the program ended. They are on new tracks, where they were played. "
                                 "Save the project to keep them with it. The files are in " + takeRecorder_.getFolder().getFullPathName();
    setStatus(message);
    if (mode_ == Mode::Live)
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Recovered takes", message);
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

void MainComponent::newProject() {
    if (refuseWhileRecording("New project"))
        return;
    closeAllEditors();
    resumeAfterConversion_.store(false); // a new project starts stopped, conversion or not
    session_.newProject();
    projectFile_ = juce::File();
    arrangement_.zoomToFit();
    setStatus("New project");
}

// ---- project files (chapter 15, Step 8) -----------------------------------------------------------

ProjectIO::OpenResult MainComponent::openProject(const juce::File& file) {
    if (refuseWhileRecording("Opening a project")) {
        ProjectIO::OpenResult refused;
        refused.error = "a recording is running";
        return refused;
    }
    if (isExporting()) {
        ProjectIO::OpenResult busy;
        busy.error = "an export is running";
        return busy;
    }
    closeAllEditors();
    resumeAfterConversion_.store(false); // an opened project starts stopped
    const ProjectIO::OpenResult r = ProjectIO::open(session_, &pluginHost_, file);
    if (!r.ok) {
        setStatus("Not opened: " + r.error); // the current project is unchanged
        if (mode_ == Mode::Live)
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Open project", r.error);
        return r;
    }
    projectFile_ = file;
    session_.dispatchPendingMessages(); // the views show the opened tracks now
    arrangement_.selectLane(0);
    arrangement_.zoomToFit();
    juce::String text = "Opened " + file.getFileName() + ": " + juce::String(session_.getNumTracks()) + " tracks, " +
                        juce::String(r.audioFiles) + " audio files";
    if (!r.warnings.isEmpty()) {
        text << ", " << r.warnings.size() << " warning(s): " << r.warnings[0];
        if (mode_ == Mode::Live)
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Opened with warnings",
                                                   r.warnings.joinIntoString("\n"));
    }
    setStatus(text);
    return r;
}

bool MainComponent::saveProject(const juce::File& target) {
    if (isExporting())
        return false;
    const juce::File file = target.hasFileExtension(ProjectIO::kFileExtension)
                                ? target
                                : target.withFileExtension(ProjectIO::kFileExtension);
    juce::String error;
    if (!ProjectIO::save(session_, file, error)) {
        setStatus("Not saved: " + error);
        if (mode_ == Mode::Live)
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Save project", error);
        return false;
    }
    projectFile_ = file;
    session_.setProjectName(file.getFileNameWithoutExtension());
    session_.getHistory().markSaved();
    setStatus("Saved " + file.getFullPathName());
    return true;
}

void MainComponent::openProjectInteractive() {
    const juce::File folder = projectFile_ != juce::File() ? projectFile_.getParentDirectory()
                                                          : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    fileChooser_ = std::make_unique<juce::FileChooser>("Open project", folder, juce::String("*") + ProjectIO::kFileExtension);
    juce::Component::SafePointer<MainComponent> safe(this);
    fileChooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe](const juce::FileChooser& chooser) {
                                  if (safe != nullptr && chooser.getResult() != juce::File())
                                      safe->openProject(chooser.getResult());
                              });
}

void MainComponent::saveProjectInteractive(bool saveAs, std::function<void()> afterSave) {
    if (!saveAs && projectFile_ != juce::File()) {
        if (saveProject(projectFile_) && afterSave)
            afterSave();
        return;
    }
    const juce::File suggestion =
        projectFile_ != juce::File()
            ? projectFile_
            : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                  .getChildFile(session_.getProjectName() + ProjectIO::kFileExtension);
    fileChooser_ = std::make_unique<juce::FileChooser>("Save project as", suggestion, juce::String("*") + ProjectIO::kFileExtension);
    juce::Component::SafePointer<MainComponent> safe(this);
    fileChooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                                  juce::FileBrowserComponent::warnAboutOverwriting,
                              [safe, afterSave](const juce::FileChooser& chooser) {
                                  if (safe != nullptr && chooser.getResult() != juce::File() &&
                                      safe->saveProject(chooser.getResult()) && afterSave)
                                      afterSave();
                              });
}

void MainComponent::askToSaveChanges(std::function<void()> then) {
    if (!session_.getHistory().isModified() || mode_ != Mode::Live) {
        then();
        return;
    }
    const auto options = juce::MessageBoxOptions::makeOptionsYesNoCancel(
        juce::MessageBoxIconType::QuestionIcon, "Save changes?",
        "\"" + session_.getProjectName() + "\" has changes that are not saved.", "Save", "Don't save", "Cancel", this);
    juce::Component::SafePointer<MainComponent> safe(this);
    juce::AlertWindow::showAsync(options, [safe, then](int result) {
        if (safe == nullptr)
            return;
        if (result == 1) // Save (asks for a file if there is none), then go on
            safe->saveProjectInteractive(false, then);
        else if (result == 2) // Don't save
            then();
    }); // 0: Cancel - nothing happens
}

bool MainComponent::undo() {
    const juce::String name = session_.getHistory().getUndoName();
    if (!session_.undo()) {
        setStatus(name.isEmpty() ? juce::String("Nothing to undo") : "Could not undo " + name + " (dropped)");
        return false;
    }
    session_.dispatchPendingMessages(); // lanes and strips follow now (a track may be back)
    setStatus("Undo: " + name);
    return true;
}

bool MainComponent::redo() {
    const juce::String name = session_.getHistory().getRedoName();
    if (!session_.redo()) {
        setStatus(name.isEmpty() ? juce::String("Nothing to redo") : "Could not redo " + name + " (dropped)");
        return false;
    }
    session_.dispatchPendingMessages();
    setStatus("Redo: " + name);
    return true;
}

juce::String MainComponent::getWindowTitle() const {
    return "DawLab 20 - " + session_.getProjectName() + (session_.getHistory().isModified() ? " *" : "");
}

void MainComponent::loadDemoSongAsync() {
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
        safe->closeAllEditors();
        safe->resumeAfterConversion_.store(false);
        const int unknown = safe->session_.applyDemoSong(*song);
        safe->projectFile_ = juce::File();
        safe->arrangement_.zoomToFit();
        safe->setStatus("Demo song loaded: 8 tracks, 19 inserts" +
                        (unknown ? ", " + juce::String(unknown) + " unknown parameters" : juce::String()));
    });
}

int MainComponent::loadDemoSongNow() {
    closeAllEditors();
    resumeAfterConversion_.store(false);
    const engine::DemoSong song = engine::makeDemoSong(engine_.getSampleRate());
    const int unknown = session_.applyDemoSong(song);
    projectFile_ = juce::File();
    arrangement_.zoomToFit();
    setStatus("Demo song loaded");
    return unknown;
}

void MainComponent::chooseAudioFileForTrack(engine::TrackId track) {
    if (!session_.hasTrack(track))
        return;
    fileChooser_ = std::make_unique<juce::FileChooser>("Load audio into " + session_.getStripName(session_.trackRef(track)),
                                                       juce::File::getSpecialLocation(juce::File::userMusicDirectory),
                                                       MediaLoader::getWildcard());
    juce::Component::SafePointer<MainComponent> safe(this);
    fileChooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe, track](const juce::FileChooser& chooser) {
                                  if (safe != nullptr && chooser.getResult() != juce::File())
                                      safe->loadAudioFile(track, chooser.getResult());
                              });
}

void MainComponent::loadDroppedFiles(engine::TrackId track, const juce::StringArray& files) {
    const int first = session_.indexOf(track);
    if (first < 0)
        return;
    for (int i = 0; i < files.size(); ++i) {
        engine::TrackId target = session_.getTrackId(first + i);
        if (!target.isValid()) { // no track left below: add one at the end
            const edit::AddTrack add = session_.makeNewTrack(engine::TrackFormat::Mono);
            if (session_.perform(add, "Add track"))
                target = add.track.id;
        }
        if (!target.isValid()) {
            setStatus(juce::String(files.size() - i) + " file(s) not loaded: " + juce::String(engine::kMaxTracks) +
                      " tracks is the limit");
            return;
        }
        loadAudioFile(target, juce::File(files[i]));
    }
}

engine::TrackId MainComponent::addTrack(engine::TrackFormat format, bool instrument) {
    // Below the selected track, like most DAWs; at the end in a project without tracks.
    const int selectedLane = session_.indexOf(arrangement_.getSelectedTrack());
    const edit::AddTrack add = session_.makeNewTrack(format, selectedLane >= 0 ? selectedLane + 1 : -1,
                                                     instrument ? "Synth " + juce::String(session_.getNumTracks() + 1) : juce::String(),
                                                     instrument);
    const engine::TrackId id = session_.perform(add, "Add track " + add.track.name) ? add.track.id : engine::TrackId{};
    if (!id.isValid()) {
        setStatus(session_.getNumTracks() >= engine::kMaxTracks
                      ? "No track added: " + juce::String(engine::kMaxTracks) + " tracks is the limit"
                      : juce::String("No track added: the engine's command queue is full, try again"));
        return id;
    }
    session_.dispatchPendingMessages(); // the arrangement and the mixer add the lane and the strip now
    arrangement_.setSelectedTrack(id);
    mixer_.scrollToTrack(id);
    setStatus("Added " + session_.getStripName(session_.trackRef(id)));
    return id;
}

bool MainComponent::deleteSelectedTrack() {
    if (refuseWhileRecording("Deleting a track"))
        return false;
    const engine::TrackId id = arrangement_.getSelectedTrack();
    const juce::String name = session_.getStripName(session_.trackRef(id));
    // Session::removeTrack() calls onInsertWillBeRemoved for every insert: open editors close.
    // The history keeps the whole track, so undo brings it back with the same id.
    if (!session_.perform(edit::RemoveTrack{id}, "Delete track " + name)) {
        setStatus(session_.hasTrack(id) ? juce::String("Not deleted: the engine's command queue is full, try again")
                                        : juce::String("No track selected"));
        return false;
    }
    session_.dispatchPendingMessages(); // the arrangement selects the track that took its place
    setStatus("Deleted " + name);
    return true;
}

void MainComponent::loadAudioFile(engine::TrackId track, const juce::File& file) {
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
        if (!safe->session_.hasTrack(media.track)) { // deleted while the file was being read
            safe->setStatus("Not loaded: the track for " + media.name + " was deleted");
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
        // One undoable step: the format, the clip and the name.
        Session& session = safe->session_;
        std::vector<Edit> edits;
        edits.push_back(edit::SetFormat{media.track, media.fileNumChannels == 1 ? engine::TrackFormat::Mono
                                                                                : engine::TrackFormat::Stereo});
        edits.push_back(edit::SetClips{media.track, {session.makeAudioClip(media.audio, media.name)}});
        edits.push_back(edit::Rename{StripTarget::ofTrack(media.track), media.name.upToFirstOccurrenceOf(".", false, false)});
        session.perform(std::move(edits), "Load " + media.name);
        safe->arrangement_.setSelectedTrack(media.track);
        safe->arrangement_.zoomToFit();
        juce::String text = "Loaded " + media.name + " into track " + juce::String(safe->session_.indexOf(media.track) + 1);
        if (media.fileSampleRate != media.targetSampleRate)
            text << " (resampled " << juce::String(media.fileSampleRate, 0) << " -> " << juce::String(media.targetSampleRate, 0) << " Hz)";
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

SynthPanelWindow* MainComponent::showSynthPanel() {
    if (mode_ == Mode::Offline)
        return nullptr;
    if (synthPanel_ == nullptr) {
        synthPanel_ = std::make_unique<SynthPanelWindow>(session_, &lookAndFeel_);
        synthPanel_->centreWithSize(synthPanel_->getWidth(), synthPanel_->getHeight());
    }
    synthPanel_->getPanel().showTrack(arrangement_.getSelectedTrack());
    synthPanel_->setVisible(true);
    synthPanel_->toFront(true);
    return synthPanel_.get();
}

bool MainComponent::loadPluginInstrument(engine::TrackId track, const juce::File& file) {
    if (!session_.isInstrument(track)) {
        setStatus("A plugin instrument needs an instrument track (Track > Add instrument track)");
        return false;
    }
    juce::String why;
    if (!session_.setPluginInstrument(track, file, &why)) {
        setStatus("Plugin instrument not loaded: " + (why.isNotEmpty() ? why : juce::String("refused")));
        return false;
    }
    const PluginInstrument* plugin = session_.getPluginInstrument(track);
    setStatus(session_.getStripName(session_.trackRef(track)) + " plays " + (plugin != nullptr ? plugin->getName() : file.getFileName()) +
              (plugin != nullptr && plugin->getLatencySamples() > 0 ? " (latency " + juce::String(plugin->getLatencySamples()) + " samples, compensated)" : ""));
    return true;
}

bool MainComponent::useBuiltinSynth(engine::TrackId track) {
    if (session_.getPluginInstrument(track) == nullptr)
        return false;
    const bool done = session_.setPluginInstrument(track, juce::File());
    if (done)
        setStatus(session_.getStripName(session_.trackRef(track)) + " plays the built-in synth again");
    return done;
}

void MainComponent::loadPluginInstrumentInteractive() {
    const engine::TrackId track = arrangement_.getSelectedTrack();
    fileChooser_ = std::make_unique<juce::FileChooser>("Load a VST3 instrument", juce::File(), "*.vst3");
    juce::Component::SafePointer<MainComponent> safe(this);
    fileChooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles |
                                  juce::FileBrowserComponent::canSelectDirectories, // a .vst3 is a folder on some systems
                              [safe, track](const juce::FileChooser& chooser) {
                                  if (safe != nullptr && chooser.getResult() != juce::File())
                                      safe->loadPluginInstrument(track, chooser.getResult());
                              });
}

bool MainComponent::importMidi(const juce::File& file) {
    if (refuseWhileRecording("The import"))
        return false;
    const MidiImport r = importMidiFile(session_, file);
    if (!r.ok) {
        setStatus("MIDI import failed: " + r.error);
        return false;
    }
    juce::String text = "Imported " + file.getFileName() + ": " + juce::String(static_cast<int>(r.tracks.size())) +
                        (r.tracks.size() == 1 ? " track, " : " tracks, ") + juce::String(r.notes) + " notes, " + juce::String(r.tempos) +
                        (r.tempos == 1 ? " tempo" : " tempos");
    if (r.thinned)
        text << " (the file had more than the tempo map holds: no beat moved by more than " << juce::String(1000.0 * r.tolerance, 1) << " ms)";
    if (r.dropped > 0)
        text << "; " << r.dropped << " other events (pitch bend, program changes) left out";
    setStatus(text);
    return true;
}

bool MainComponent::exportMidi(const juce::File& file) {
    const MidiExport e = songOf(session_);
    if (e.song.tracks.empty()) {
        setStatus("Nothing to export: no instrument track has notes");
        return false;
    }
    if (!exportMidiFile(session_, file)) {
        setStatus("Cannot write " + file.getFullPathName());
        return false;
    }
    setStatus("Exported " + juce::String(static_cast<int>(e.song.tracks.size())) + (e.song.tracks.size() == 1 ? " track, " : " tracks, ") +
              juce::String(e.notes) + " notes to " + file.getFileName());
    return true;
}

void MainComponent::importMidiInteractive() {
    fileChooser_ = std::make_unique<juce::FileChooser>("Import MIDI file", juce::File::getSpecialLocation(juce::File::userMusicDirectory),
                                                       "*.mid;*.midi");
    juce::Component::SafePointer<MainComponent> safe(this);
    fileChooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe](const juce::FileChooser& chooser) {
                                  if (safe != nullptr && chooser.getResult() != juce::File())
                                      safe->importMidi(chooser.getResult());
                              });
}

void MainComponent::exportMidiInteractive() {
    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Export MIDI file", juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile(session_.getProjectName() + ".mid"),
        "*.mid");
    juce::Component::SafePointer<MainComponent> safe(this);
    fileChooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                                  juce::FileBrowserComponent::warnAboutOverwriting,
                              [safe](const juce::FileChooser& chooser) {
                                  if (safe != nullptr && chooser.getResult() != juce::File())
                                      safe->exportMidi(chooser.getResult().withFileExtension("mid"));
                              });
}

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
    if (isExporting() || refuseWhileRecording("The export"))
        return;
    if (isConvertingSampleRate()) {
        setStatus("Export not started: the clips are still being converted to the new sample rate");
        return;
    }
    if (session_.getSongEndSample() <= 0) {
        setStatus("Nothing to export: the project has no clips");
        return;
    }
    // Chapter 20: an export must not go out with placeholders. Wait for the renders (it blocks, for as long as
    // the slowest stretch takes).
    session_.finishRenders();
    if (session_.getNumFailedRenders() > 0) { // the file would have their placeholders in it
        setStatus("Export not started: " + failedRendersText(session_.getNumFailedRenders()));
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
    transportBar_.forgetCpuHistory(); // the render's blocks were not played in real time
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
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::NoIcon, "Keyboard shortcuts", text);
}

// ---- inserts ----------------------------------------------------------------------------------------------

void MainComponent::showSlotMenu(ChannelRef ref, int slot, juce::Component& anchor) {
    if (isExporting())
        return;
    juce::PopupMenu menu = EffectMenu::build(session_.getSlot(ref, slot), EffectMenu::chapter13CompressorFile(),
                                             EffectMenu::chapter13LimiterFile());
    juce::Component::SafePointer<MainComponent> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&anchor), [safe, ref, slot](int result) {
        if (safe != nullptr && result != 0)
            safe->handleSlotMenuResult(ref, slot, result);
    });
}

void MainComponent::handleSlotMenuResult(ChannelRef ref, int slot, int result) {
    const auto& entries = EffectMenu::builtinEntries();
    const StripTarget target = StripTarget::of(ref);
    if (result == EffectMenu::kRemove) {
        session_.perform(edit::SetInsert{target, slot, InsertSnapshot{}}, "Remove " + session_.getSlot(ref, slot).name);
    } else if (result >= EffectMenu::kFirstBuiltin && result < EffectMenu::kFirstBuiltin + static_cast<int>(entries.size())) {
        const auto& entry = entries[static_cast<size_t>(result - EffectMenu::kFirstBuiltin)];
        InsertSnapshot insert; // a new effect: no parameters given, so it keeps its defaults
        insert.kind = SlotKind::Builtin;
        insert.type = entry.type;
        if (session_.perform(edit::SetInsert{target, slot, insert}, "Insert " + EffectMenu::labelFor(entry.type)))
            openInsertEditor(ref, slot, false);
        else
            setStatus("Could not insert the effect (command queue full?)");
    } else if (result == EffectMenu::kVst3File) {
        chooseVst3For(ref, slot);
    } else if (result == EffectMenu::kChapter13Compressor) {
        juce::String error;
        if (!loadVst3Into(ref, slot, EffectMenu::chapter13CompressorFile(), error))
            setStatus("VST3 load failed: " + error);
    } else if (result == EffectMenu::kChapter13Limiter) {
        juce::String error;
        if (!loadVst3Into(ref, slot, EffectMenu::chapter13LimiterFile(), error))
            setStatus("VST3 load failed: " + error);
    } else if (result == EffectMenu::kToggleBypass) {
        const auto& s = session_.getSlot(ref, slot);
        if (!s.isEmpty())
            session_.perform(edit::SetBypass{target, slot, !s.effect->isBypassed()}, "Bypass " + s.name);
    } else if (result == EffectMenu::kOpenEditor) {
        openInsertEditor(ref, slot, false);
    } else if (result == EffectMenu::kOpenGenericPanel) {
        openInsertEditor(ref, slot, true);
    }
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
    // Chapter 13 creates the instance on the message thread (Session::createVst3, set up in the
    // constructor); as an edit, so that the insert can be undone like a built-in one (Step 9).
    if (!file.exists()) {
        error = file.getFullPathName() + " not found";
        return false;
    }
    InsertSnapshot insert;
    insert.kind = SlotKind::Vst3;
    insert.vstFile = file.getFullPathName();
    if (!session_.perform(edit::SetInsert{StripTarget::of(ref), slot, insert}, "Insert " + file.getFileNameWithoutExtension())) {
        error = "the plugin did not load, or the engine refused it";
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
        // The panel's knobs make undoable edits (Step 9). The target is the track's id and the
        // slot, both fixed for as long as this effect - and so this window - exists.
        ParameterPanel& panel = window->getPanel();
        const StripTarget target = StripTarget::of(ref);
        const auto laneOfParameter = [target, key](int index) {
            return LaneAddress{target, key->getEffectId(), key->getParameterInfo(index).name};
        };
        panel.onTouch = [this, laneOfParameter](int index, bool down) {
            if (down)
                session_.touchLane(laneOfParameter(index));
            else
                session_.releaseLane(laneOfParameter(index));
        };
        panel.onParameterChange = [this, target, slot, key, laneOfParameter](int index, float value) {
            if (session_.handMoves(laneOfParameter(index), value))
                return; // on an automated control the hand is a recording, not an undo step of its own
            session_.perform(edit::SetParameter{target, slot, index, value},
                             juce::String(key->getName()) + " " + key->getParameterInfo(index).name,
                             mergeKeyFor(target, 0x50000000u + static_cast<std::uint32_t>(slot) * 256u +
                                                     static_cast<std::uint32_t>(index))); // "P", slot, parameter
        };
        panel.onBypassChange = [this, target, slot, key](bool bypassed) {
            session_.perform(edit::SetBypass{target, slot, bypassed}, juce::String("Bypass ") + key->getName());
        };
        panel.onGesture = [this] { session_.getHistory().endMerge(); };
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

// ---- MIDI clips (chapter 18) ------------------------------------------------------------------------------

PianoRollWindow* MainComponent::openPianoRoll(engine::TrackId track, int clipIndex) {
    const auto& clips = session_.getMidiClips(track);
    if (clipIndex < 0 || clipIndex >= static_cast<int>(clips.size()) || mode_ == Mode::Offline)
        return nullptr;
    const MidiClipModel& clip = clips[static_cast<size_t>(clipIndex)];
    auto& window = pianoRolls_[clip.id];
    if (window == nullptr) {
        const juce::String title = session_.getStripName(session_.trackRef(track)) + " - " + clip.name;
        window = std::make_unique<PianoRollWindow>(session_, track, clip.id, title, &lookAndFeel_);
        const int id = clip.id;
        juce::Component::SafePointer<MainComponent> safe(this);
        window->onClose = [safe, id] {
            if (safe != nullptr)
                juce::MessageManager::callAsync([safe, id] {
                    if (safe != nullptr)
                        safe->pianoRolls_.erase(id);
                });
        };
        window->centreWithSize(900, 480);
        window->getRoll().setView(static_cast<double>(clip.offset), 4.0);
    }
    window->setVisible(true);
    window->toFront(true);
    return window.get();
}

bool MainComponent::quantizeMidiClip(engine::TrackId track, int clipIndex, QuantizeSettings settings) {
    const auto& clips = session_.getMidiClips(track);
    if (clipIndex < 0 || clipIndex >= static_cast<int>(clips.size()))
        return false;
    MidiClipModel clip = clips[static_cast<size_t>(clipIndex)];
    clip.quantize = settings;
    return session_.replaceMidiClip(track, clipIndex, std::move(clip), settings.strength > 0.0 ? "Quantize" : "Quantize off") >= 0;
}

bool MainComponent::setMidiClipMuted(engine::TrackId track, int clipIndex, bool muted) {
    const auto& clips = session_.getMidiClips(track);
    if (clipIndex < 0 || clipIndex >= static_cast<int>(clips.size()))
        return false;
    MidiClipModel clip = clips[static_cast<size_t>(clipIndex)];
    clip.muted = muted;
    return session_.replaceMidiClip(track, clipIndex, std::move(clip), muted ? "Mute MIDI clip" : "Unmute MIDI clip") >= 0;
}

void MainComponent::showMidiClipMenu(engine::TrackId track, int clipIndex) {
    const auto& clips = session_.getMidiClips(track);
    if (clipIndex < 0 || clipIndex >= static_cast<int>(clips.size()))
        return;
    const MidiClipModel& clip = clips[static_cast<size_t>(clipIndex)];
    const int id = clip.id;
    juce::Component::SafePointer<MainComponent> safe(this);
    // The clip is found again by its id when an item runs: the list may have changed meanwhile.
    auto withClip = [safe, track, id](std::function<void(MainComponent&, int index)> f) {
        return [safe, track, id, f] {
            if (safe == nullptr)
                return;
            const auto& list = safe->session_.getMidiClips(track);
            for (size_t i = 0; i < list.size(); ++i)
                if (list[i].id == id)
                    f(*safe, static_cast<int>(i));
        };
    };
    juce::PopupMenu quantize;
    struct Choice {
        const char* name;
        QuantizeSettings settings;
    };
    const Choice choices[] = {{"Off (as played)", {240, 0.0, 0.0}},  {"1/4", {960, 1.0, 0.0}},          {"1/8", {480, 1.0, 0.0}},
                              {"1/16", {240, 1.0, 0.0}},             {"1/16, 50 %", {240, 0.5, 0.0}},  {"1/8 swing", {480, 1.0, 1.0 / 3.0}},
                              {"1/16 swing", {240, 1.0, 1.0 / 3.0}}};
    for (const Choice& c : choices) {
        const QuantizeSettings s = c.settings;
        const bool current = clip.quantize == s || (s.strength == 0.0 && clip.quantize.strength == 0.0);
        quantize.addItem(juce::PopupMenu::Item(c.name).setTicked(current).setAction(
            withClip([track, s](MainComponent& m, int index) { m.quantizeMidiClip(track, index, s); })));
    }
    juce::PopupMenu menu;
    menu.addSectionHeader(clip.name);
    menu.addItem(juce::PopupMenu::Item("Edit Notes...").setAction(withClip([track](MainComponent& m, int index) { m.openPianoRoll(track, index); })));
    menu.addSubMenu("Quantize", quantize);
    menu.addItem(juce::PopupMenu::Item("Mute").setTicked(clip.muted).setAction(
        withClip([track, muted = clip.muted](MainComponent& m, int index) { m.setMidiClipMuted(track, index, !muted); })));
    menu.addSeparator();
    menu.addItem(juce::PopupMenu::Item("Delete").setAction(withClip([track](MainComponent& m, int index) {
        m.session_.removeMidiClip(track, index);
    })));
    menu.showMenuAsync(juce::PopupMenu::Options{});
}

void MainComponent::closeAllEditors() {
    parameterWindows_.clear();
    pianoRolls_.clear();
    for (int s = 0; s < session_.getNumStrips(); ++s)
        for (int slot = 0; slot < kInsertSlotsPerStrip; ++slot)
            if (auto* vst = session_.getSlot(session_.stripRefAt(s), slot).vst)
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
    tempoLane_.setBounds(area.removeFromTop(TempoLane::kHeight));
    arrangement_.setBounds(area);
    if (exportOverlay_ != nullptr)
        exportOverlay_->setBounds(getLocalBounds());
}

// ---- commands -------------------------------------------------------------------------------------------

void MainComponent::getAllCommands(juce::Array<juce::CommandID>& c) {
    const juce::CommandID ids[] = {CommandIDs::newProject, CommandIDs::openProject, CommandIDs::saveProject,
                CommandIDs::saveProjectAs, CommandIDs::undo, CommandIDs::redo, CommandIDs::loadDemoSong, CommandIDs::loadAudioIntoTrack, CommandIDs::exportMixdown,
                CommandIDs::importMidi, CommandIDs::exportMidi,
                CommandIDs::showAudioSettings, juce::StandardApplicationCommandIDs::quit, CommandIDs::playStop,
                CommandIDs::returnToStart, CommandIDs::toggleLoop, CommandIDs::seekBackOneBar, CommandIDs::seekForwardOneBar,
                CommandIDs::toggleRecord, CommandIDs::togglePunch, CommandIDs::setRecordingTrim, CommandIDs::toggleMidiReplace, CommandIDs::toggleArmSelected, CommandIDs::cycleMonitorSelected,
                CommandIDs::selectNextTrack, CommandIDs::selectPreviousTrack, CommandIDs::toggleMuteSelected,
                CommandIDs::toggleSoloSelected, CommandIDs::addMonoTrack, CommandIDs::addStereoTrack, CommandIDs::addInstrumentTrack,
                CommandIDs::showSynthPanel, CommandIDs::loadPluginInstrument, CommandIDs::useBuiltinSynth,
                CommandIDs::deleteSelectedTrack, CommandIDs::toggleAutomationLane, CommandIDs::zoomIn, CommandIDs::zoomOut, CommandIDs::showKeyboardShortcuts};
    c.addArray(ids, static_cast<int>(sizeof(ids) / sizeof(ids[0])));
}

void MainComponent::getCommandInfo(juce::CommandID id, juce::ApplicationCommandInfo& info) {
    using juce::KeyPress;
    using juce::ModifierKeys;
    const auto cmd = ModifierKeys::commandModifier; // Ctrl on Windows, Cmd on macOS
    const bool idle = !isExporting();
    switch (id) {
    case CommandIDs::newProject:
        info.setInfo("New project", "Start an empty project", CommandCategories::file, 0);
        info.addDefaultKeypress('n', cmd);
        info.setActive(idle);
        break;
    case CommandIDs::loadDemoSong:
        info.setInfo("Load demo song", "Generate the 8-track demo song", CommandCategories::file, 0);
        info.addDefaultKeypress('d', cmd);
        info.setActive(idle);
        break;
    case CommandIDs::loadAudioIntoTrack:
        info.setInfo("Load audio into selected track...", "Load a WAV / AIFF file", CommandCategories::file, 0);
        info.addDefaultKeypress('i', cmd); // chapter 15: Ctrl+O opens a project now
        info.setActive(idle);
        break;
    case CommandIDs::undo: {
        const History& h = session_.getHistory();
        info.setInfo(h.canUndo() ? "Undo " + h.getUndoName() : juce::String("Undo"), "Take back the last change",
                     CommandCategories::edit, 0);
        info.addDefaultKeypress('z', cmd);
        info.setActive(idle && h.canUndo());
        break;
    }
    case CommandIDs::redo: {
        const History& h = session_.getHistory();
        info.setInfo(h.canRedo() ? "Redo " + h.getRedoName() : juce::String("Redo"), "Do the undone change again",
                     CommandCategories::edit, 0);
        info.addDefaultKeypress('z', cmd | ModifierKeys::shiftModifier);
        info.addDefaultKeypress('y', cmd);
        info.setActive(idle && h.canRedo());
        break;
    }
    case CommandIDs::openProject:
        info.setInfo("Open project...", "Open a DawLab project file", CommandCategories::file, 0);
        info.addDefaultKeypress('o', cmd);
        info.setActive(idle);
        break;
    case CommandIDs::saveProject:
        info.setInfo("Save project", "Save the project to its file", CommandCategories::file, 0);
        info.addDefaultKeypress('s', cmd);
        info.setActive(idle);
        break;
    case CommandIDs::saveProjectAs:
        info.setInfo("Save project as...", "Save the project to a new file", CommandCategories::file, 0);
        info.addDefaultKeypress('s', cmd | ModifierKeys::shiftModifier);
        info.setActive(idle);
        break;
    case CommandIDs::exportMixdown:
        info.setInfo("Export mixdown...", "Bounce the song to a WAV file", CommandCategories::file, 0);
        info.addDefaultKeypress('e', cmd);
        info.setActive(idle);
        break;
    case CommandIDs::importMidi:
        info.setInfo("Import MIDI file...", "A Standard MIDI File as new instrument tracks, with its tempos", CommandCategories::file, 0);
        info.setActive(idle);
        break;
    case CommandIDs::exportMidi:
        info.setInfo("Export MIDI file...", "The instrument tracks' notes and the tempo map as a Standard MIDI File", CommandCategories::file, 0);
        info.setActive(idle);
        break;
    case CommandIDs::showAudioSettings:
        info.setInfo("Audio settings...", "Choose the audio device", CommandCategories::file, 0);
        info.addDefaultKeypress(',', cmd);
        info.setActive(idle);
        break;
    case juce::StandardApplicationCommandIDs::quit:
        info.setInfo("Quit", "Quit DawLab 20", CommandCategories::file, 0);
        info.addDefaultKeypress('q', cmd);
        break;
    case CommandIDs::playStop:
        info.setInfo("Play / Stop", "Start or stop playback", CommandCategories::transport, 0);
        info.addDefaultKeypress(KeyPress::spaceKey, ModifierKeys::noModifiers);
        info.setTicked(engine_.getTransport().isPlaying());
        info.setActive(idle);
        break;
    case CommandIDs::returnToStart:
        info.setInfo("Return to start", "Move the playhead to bar 1", CommandCategories::transport, 0);
        info.addDefaultKeypress(KeyPress::homeKey, ModifierKeys::noModifiers);
        info.setActive(idle);
        break;
    case CommandIDs::toggleRecord:
        info.setInfo("Record", "Record the armed tracks", CommandCategories::transport, 0);
        info.addDefaultKeypress('r', ModifierKeys::noModifiers);
        info.setTicked(takeRecorder_.getPhase() == TakeRecorder::Phase::Recording);
        break;
    case CommandIDs::toggleMidiReplace:
        info.setInfo("MIDI Loop Replaces", "Each pass of a loop replaces what the last one played (otherwise: overdub)",
                     CommandCategories::transport, 0);
        info.setTicked(midiMode_ == MidiRecordMode::Replace);
        info.setActive(!midiRecording_.isRecording());
        break;
    case CommandIDs::togglePunch:
        info.setInfo("Punch", "Record only between the loop's locators", CommandCategories::transport, 0);
        info.addDefaultKeypress('p', ModifierKeys::noModifiers);
        info.setTicked(takeRecorder_.isPunchEnabled());
        info.setActive(!takeRecorder_.isBusy());
        break;
    case CommandIDs::setRecordingTrim:
        info.setInfo("Recording Latency Trim...", "Corrects what the driver reports about its latency", CommandCategories::transport, 0);
        info.setActive(!takeRecorder_.isBusy());
        break;
    case CommandIDs::toggleArmSelected:
        info.setInfo("Arm Track", "Arm the selected track for recording", CommandCategories::track, 0);
        info.addDefaultKeypress('r', ModifierKeys::shiftModifier);
        if (const engine::Track* armedTrack = engine_.getTrack(arrangement_.getSelectedTrack()))
            info.setTicked(armedTrack->isArmed());
        info.setActive(!takeRecorder_.isBusy());
        break;
    case CommandIDs::cycleMonitorSelected:
        info.setInfo("Monitor", "The selected track's monitor: off, auto, always", CommandCategories::track, 0);
        info.addDefaultKeypress('i', ModifierKeys::noModifiers);
        break;
    case CommandIDs::toggleLoop:
        info.setInfo("Loop", "Loop on / off", CommandCategories::transport, 0);
        info.addDefaultKeypress('l', ModifierKeys::noModifiers);
        info.setTicked(engine_.getTransport().isLoopEnabled());
        info.setActive(idle);
        break;
    case CommandIDs::seekBackOneBar:
        info.setInfo("Back one bar", "Move the playhead one bar back", CommandCategories::transport, 0);
        info.addDefaultKeypress(KeyPress::leftKey, ModifierKeys::noModifiers);
        info.setActive(idle);
        break;
    case CommandIDs::seekForwardOneBar:
        info.setInfo("Forward one bar", "Move the playhead one bar forward", CommandCategories::transport, 0);
        info.addDefaultKeypress(KeyPress::rightKey, ModifierKeys::noModifiers);
        info.setActive(idle);
        break;
    case CommandIDs::selectNextTrack:
        info.setInfo("Select next track", "", CommandCategories::track, 0);
        info.addDefaultKeypress(KeyPress::downKey, ModifierKeys::noModifiers);
        break;
    case CommandIDs::selectPreviousTrack:
        info.setInfo("Select previous track", "", CommandCategories::track, 0);
        info.addDefaultKeypress(KeyPress::upKey, ModifierKeys::noModifiers);
        break;
    case CommandIDs::toggleMuteSelected:
        info.setInfo("Mute selected track", "", CommandCategories::track, 0);
        info.addDefaultKeypress('m', ModifierKeys::noModifiers);
        info.setActive(idle);
        break;
    case CommandIDs::toggleSoloSelected:
        info.setInfo("Solo selected track", "", CommandCategories::track, 0);
        info.addDefaultKeypress('s', ModifierKeys::noModifiers);
        info.setActive(idle);
        break;
    case CommandIDs::toggleAutomationLane:
        info.setInfo("Show automation", "Show or hide the automation lane of the selected track", CommandCategories::track, 0);
        info.addDefaultKeypress('a', 0);
        info.setActive(session_.hasTrack(arrangement_.getSelectedTrack()));
        break;
    case CommandIDs::addMonoTrack:
        info.setInfo("Add mono track", "Add a mono track below the selected one", CommandCategories::track, 0);
        info.addDefaultKeypress('t', cmd);
        info.setActive(idle && session_.getNumTracks() < engine::kMaxTracks);
        break;
    case CommandIDs::showSynthPanel:
        info.setInfo("Synth settings...", "The built-in synth's sound for the selected track", CommandCategories::track, 0);
        info.setActive(session_.isInstrument(arrangement_.getSelectedTrack()));
        break;
    case CommandIDs::loadPluginInstrument:
        info.setInfo("Load VST3 instrument...", "A plugin instrument in the synth's place on the selected track", CommandCategories::track, 0);
        info.setActive(idle && session_.isInstrument(arrangement_.getSelectedTrack()));
        break;
    case CommandIDs::useBuiltinSynth:
        info.setInfo("Use the built-in synth", "Take the plugin instrument off the selected track", CommandCategories::track, 0);
        info.setActive(idle && session_.getPluginInstrument(arrangement_.getSelectedTrack()) != nullptr);
        break;
    case CommandIDs::addInstrumentTrack:
        info.setInfo("Add instrument track", "Add a synth track for MIDI below the selected one", CommandCategories::track, 0);
        info.addDefaultKeypress('t', cmd | ModifierKeys::altModifier);
        info.setActive(idle && session_.getNumTracks() < engine::kMaxTracks);
        break;
    case CommandIDs::addStereoTrack:
        info.setInfo("Add stereo track", "Add a stereo track below the selected one", CommandCategories::track, 0);
        info.addDefaultKeypress('t', cmd | ModifierKeys::shiftModifier);
        info.setActive(idle && session_.getNumTracks() < engine::kMaxTracks);
        break;
    case CommandIDs::deleteSelectedTrack:
        info.setInfo("Delete selected track", "Delete the track with its clips and inserts", CommandCategories::track, 0);
        info.addDefaultKeypress(KeyPress::deleteKey, cmd);
        info.setActive(idle && session_.hasTrack(arrangement_.getSelectedTrack()));
        break;
    case CommandIDs::zoomIn:
        info.setInfo("Zoom in", "", CommandCategories::view, 0);
        info.addDefaultKeypress('=', ModifierKeys::noModifiers);
        info.addDefaultKeypress('+', ModifierKeys::noModifiers);
        break;
    case CommandIDs::zoomOut:
        info.setInfo("Zoom out", "", CommandCategories::view, 0);
        info.addDefaultKeypress('-', ModifierKeys::noModifiers);
        break;
    case CommandIDs::showKeyboardShortcuts:
        info.setInfo("Keyboard shortcuts...", "List all shortcuts", CommandCategories::view, 0);
        info.addDefaultKeypress(KeyPress::F1Key, ModifierKeys::noModifiers);
        break;
    default: break;
    }
}

bool MainComponent::perform(const InvocationInfo& info) {
    if (isExporting() && info.commandID != juce::StandardApplicationCommandIDs::quit)
        return true; // swallow everything while rendering
    const engine::TrackId selected = arrangement_.getSelectedTrack();
    const int selectedLane = session_.indexOf(selected);
    engine::Track* selectedStrip = engine_.getTrack(selected); // nullptr in a project without tracks
    switch (info.commandID) {
    case CommandIDs::newProject: askToSaveChanges([this] { newProject(); }); break;
    case CommandIDs::loadDemoSong: askToSaveChanges([this] { loadDemoSongAsync(); }); break;
    case CommandIDs::loadAudioIntoTrack: chooseAudioFileForTrack(selected); break;
    case CommandIDs::openProject: askToSaveChanges([this] { openProjectInteractive(); }); break;
    case CommandIDs::undo: undo(); break;
    case CommandIDs::redo: redo(); break;
    case CommandIDs::saveProject: saveProjectInteractive(false); break;
    case CommandIDs::saveProjectAs: saveProjectInteractive(true); break;
    case CommandIDs::exportMixdown: exportMixdownInteractive(); break;
    case CommandIDs::importMidi: importMidiInteractive(); break;
    case CommandIDs::exportMidi: exportMidiInteractive(); break;
    case CommandIDs::showAudioSettings: showAudioSettings(); break;
    case juce::StandardApplicationCommandIDs::quit:
        if (auto* app = juce::JUCEApplicationBase::getInstance())
            app->systemRequestedQuit();
        break;
    case CommandIDs::playStop: togglePlay(); break;
    case CommandIDs::returnToStart: returnToStart(); break;
    case CommandIDs::toggleLoop: toggleLoop(); break;
    case CommandIDs::toggleRecord: toggleRecord(); break;
    case CommandIDs::togglePunch:
        takeRecorder_.setPunchEnabled(!takeRecorder_.isPunchEnabled());
        setStatus(takeRecorder_.isPunchEnabled() ? "Punch on: recording only between the loop's locators" : "Punch off");
        break;
    case CommandIDs::setRecordingTrim: askForTrim(); break;
    case CommandIDs::toggleMidiReplace:
        midiMode_ = midiMode_ == MidiRecordMode::Replace ? MidiRecordMode::Overdub : MidiRecordMode::Replace;
        setStatus(midiMode_ == MidiRecordMode::Replace ? "MIDI loop recording: each pass replaces the last"
                                                       : "MIDI loop recording: overdub, every pass is kept");
        break;
    case CommandIDs::toggleArmSelected:
        if (const engine::Track* armedTrack = engine_.getTrack(arrangement_.getSelectedTrack()))
            setArmed(arrangement_.getSelectedTrack(), !armedTrack->isArmed());
        break;
    case CommandIDs::cycleMonitorSelected: cycleMonitor(arrangement_.getSelectedTrack()); break;
    case CommandIDs::seekBackOneBar: seekBars(-1); break;
    case CommandIDs::seekForwardOneBar: seekBars(+1); break;
    case CommandIDs::selectNextTrack: arrangement_.selectLane(selectedLane + 1); break;
    case CommandIDs::selectPreviousTrack: arrangement_.selectLane(selectedLane - 1); break;
    case CommandIDs::toggleMuteSelected:
        if (selectedStrip != nullptr)
            session_.perform(edit::SetValue{StripTarget::ofTrack(selected), edit::Field::Mute, 0, selectedStrip->isMuted() ? 0.0f : 1.0f},
                             "Mute " + selectedStrip->getName());
        break;
    case CommandIDs::toggleSoloSelected:
        if (selectedStrip != nullptr)
            session_.perform(edit::SetValue{StripTarget::ofTrack(selected), edit::Field::Solo, 0, selectedStrip->isSoloed() ? 0.0f : 1.0f},
                             "Solo " + selectedStrip->getName());
        break;
    case CommandIDs::addMonoTrack: addTrack(engine::TrackFormat::Mono); break;
    case CommandIDs::addStereoTrack: addTrack(engine::TrackFormat::Stereo); break;
    case CommandIDs::addInstrumentTrack: addTrack(engine::TrackFormat::Mono, true); break;
    case CommandIDs::showSynthPanel: showSynthPanel(); break;
    case CommandIDs::loadPluginInstrument: loadPluginInstrumentInteractive(); break;
    case CommandIDs::useBuiltinSynth: useBuiltinSynth(arrangement_.getSelectedTrack()); break;
    case CommandIDs::deleteSelectedTrack: deleteSelectedTrack(); break;
    case CommandIDs::toggleAutomationLane: arrangement_.toggleAutomation(arrangement_.getSelectedTrack()); break;
    case CommandIDs::zoomIn: arrangement_.zoom(1.5, ArrangementView::kHeaderWidth + 0.5f * (arrangement_.getWidth() - ArrangementView::kHeaderWidth)); break;
    case CommandIDs::zoomOut: arrangement_.zoom(1.0 / 1.5, ArrangementView::kHeaderWidth + 0.5f * (arrangement_.getWidth() - ArrangementView::kHeaderWidth)); break;
    case CommandIDs::showKeyboardShortcuts: showKeyboardShortcuts(); break;
    default: return false;
    }
    return true;
}

// ---- menu bar --------------------------------------------------------------------------------------------

juce::StringArray MainComponent::getMenuBarNames() { return {"File", "Edit", "Transport", "Track", "View"}; }

juce::PopupMenu MainComponent::getMenuForIndex(int index, const juce::String&) {
    juce::PopupMenu m;
    auto* cm = &commandManager_;
    switch (index) {
    case 0:
        m.addCommandItem(cm, CommandIDs::newProject);
        m.addCommandItem(cm, CommandIDs::openProject);
        m.addCommandItem(cm, CommandIDs::saveProject);
        m.addCommandItem(cm, CommandIDs::saveProjectAs);
        m.addSeparator();
        m.addCommandItem(cm, CommandIDs::loadDemoSong);
        m.addCommandItem(cm, CommandIDs::loadAudioIntoTrack);
        m.addSeparator();
        m.addCommandItem(cm, CommandIDs::exportMixdown);
        m.addSeparator();
        m.addCommandItem(cm, CommandIDs::importMidi);
        m.addCommandItem(cm, CommandIDs::exportMidi);
        m.addSeparator();
        m.addCommandItem(cm, CommandIDs::showAudioSettings);
        m.addSeparator();
        m.addCommandItem(cm, juce::StandardApplicationCommandIDs::quit);
        break;
    case 1:
        m.addCommandItem(cm, CommandIDs::undo);
        m.addCommandItem(cm, CommandIDs::redo);
        break;
    case 2:
        m.addCommandItem(cm, CommandIDs::playStop);
        m.addCommandItem(cm, CommandIDs::returnToStart);
        m.addCommandItem(cm, CommandIDs::toggleLoop);
        m.addSeparator();
        m.addCommandItem(cm, CommandIDs::toggleRecord);
        m.addCommandItem(cm, CommandIDs::togglePunch);
        m.addCommandItem(cm, CommandIDs::toggleMidiReplace);
        m.addCommandItem(cm, CommandIDs::setRecordingTrim);
        m.addSeparator();
        m.addCommandItem(cm, CommandIDs::seekBackOneBar);
        m.addCommandItem(cm, CommandIDs::seekForwardOneBar);
        break;
    case 3:
        m.addCommandItem(cm, CommandIDs::selectPreviousTrack);
        m.addCommandItem(cm, CommandIDs::selectNextTrack);
        m.addSeparator();
        m.addCommandItem(cm, CommandIDs::toggleMuteSelected);
        m.addCommandItem(cm, CommandIDs::toggleSoloSelected);
        m.addSeparator();
        m.addCommandItem(cm, CommandIDs::addMonoTrack);
        m.addCommandItem(cm, CommandIDs::addStereoTrack);
        m.addCommandItem(cm, CommandIDs::addInstrumentTrack);
        m.addCommandItem(cm, CommandIDs::deleteSelectedTrack);
        m.addSeparator();
        m.addCommandItem(cm, CommandIDs::showSynthPanel);
        m.addCommandItem(cm, CommandIDs::loadPluginInstrument);
        m.addCommandItem(cm, CommandIDs::useBuiltinSynth);
        m.addSeparator();
        m.addCommandItem(cm, CommandIDs::toggleAutomationLane);
        m.addSeparator();
        m.addCommandItem(cm, CommandIDs::toggleArmSelected);
        m.addCommandItem(cm, CommandIDs::cycleMonitorSelected);
        break;
    case 4:
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
