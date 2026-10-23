// l13_gui_host: a mini plugin host with a window
//
// Demonstrates:
//   - choosing a .vst3 (or directly loading this chapter's DawLabCompressor / DawLabLimiter);
//   - using PluginWindow to open the plugin's editor (DawLabLimiter has no editor -> falls back to GenericAudioProcessorEditor);
//   - playing a drum loop through the plugin in real time via juce::AudioDeviceManager + juce::AudioProcessorPlayer;
//   - the division of labor between the message thread and the audio thread: when switching plugins, first take the processor off the player, then delete the old one and create the new one.
//
// Run: l13_gui_host                 opens the window (uses the system default audio output, mind the volume)
//       l13_gui_host --smoke-test    opens no audio device: automatically loads both plugins, opens and closes the editors, renders a few blocks offline,
//                                    then exits; the result is written to dawlab_output/l13_gui_host_smoke.txt (for automated checking)
// What to listen for: after Play, a 120 BPM drum loop; turn Threshold down and the drum heads get squashed while the GR meter grows downward;
//             press Bypass to compare with the original (a direct switch, with a slight click -- exercise 7 fixes this with a crossfade).

#include "L13Common.h"

#include <atomic>

namespace {

// ---------------------------------------------------------------------------
// LoopPlayer: an AudioProcessor inside the host that generates the drum loop and feeds it into the HostedPlugin.
// AudioProcessorPlayer calls its processBlock on the audio thread.
// ---------------------------------------------------------------------------
class LoopPlayer final : public juce::AudioProcessor {
public:
    LoopPlayer() : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)) {}

    // May only be called while it is "not connected to the AudioProcessorPlayer" (message thread)
    void setPlugin(dawlab::host::HostedPlugin* plugin) { plugin_ = plugin; }

    std::atomic<bool> playing{false};
    std::atomic<bool> bypassed{false};
    std::atomic<float> outputPeak{0.0f};

    const juce::String getName() const override { return "LoopPlayer"; }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        // Allocated in the prepare phase: the loop is regenerated for the current device's sample rate
        loop_ = dawlab::makeDrumLoop(sampleRate, 2, 120.0, 0.5f);
        position_ = 0;
        if (plugin_ != nullptr)
            plugin_->prepare(sampleRate, samplesPerBlock, 2);
    }

    void releaseResources() override {
        if (plugin_ != nullptr)
            plugin_->releaseResources();
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        buffer.clear();
        const int n = buffer.getNumSamples();
        const int numCh = std::min(2, buffer.getNumChannels());
        if (!playing.load() || loop_.getNumSamples() == 0 || numCh == 0)
            return;
        const int len = loop_.getNumSamples();
        for (int i = 0; i < n; ++i) {
            for (int ch = 0; ch < numCh; ++ch)
                buffer.setSample(ch, i, loop_.getSample(std::min(ch, loop_.getNumChannels() - 1), position_));
            position_ = (position_ + 1) % len;
        }
        // Wrap the JUCE buffer's pointers as a dawlab::AudioBlock (zero copy) and hand it to the HostedPlugin
        if (plugin_ != nullptr && !bypassed.load())
            plugin_->process(dawlab::AudioBlock(buffer.getArrayOfWritePointers(), numCh, n));
        outputPeak.store(buffer.getMagnitude(0, n));
    }

    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

private:
    dawlab::host::HostedPlugin* plugin_ = nullptr;
    dawlab::AudioBuffer loop_;
    int position_ = 0;
};

// ---------------------------------------------------------------------------
// MainComponent: buttons + status line
// ---------------------------------------------------------------------------
class MainComponent final : public juce::Component, private juce::Timer {
public:
    explicit MainComponent(bool smokeTest) : smokeTest_(smokeTest) {
        for (auto* b : {&loadFileButton_, &loadCompButton_, &loadLimButton_, &editorButton_, &playButton_,
                        &bypassButton_, &audioButton_})
            addAndMakeVisible(*b);
        addAndMakeVisible(status_);
        status_.setJustificationType(juce::Justification::topLeft);
        status_.setFont(juce::FontOptions(14.0f));

        loadFileButton_.onClick = [this] { chooseFile(); };
        loadCompButton_.onClick = [this] { loadPlugin(l13::defaultCompressorVst3()); };
        loadLimButton_.onClick = [this] { loadPlugin(l13::defaultLimiterVst3()); };
        editorButton_.onClick = [this] { toggleEditor(); };
        playButton_.setClickingTogglesState(true);
        playButton_.onClick = [this] { loop_.playing = playButton_.getToggleState(); };
        bypassButton_.setClickingTogglesState(true);
        bypassButton_.onClick = [this] { loop_.bypassed = bypassButton_.getToggleState(); };
        audioButton_.onClick = [this] { showAudioSettings(); };

        if (!smokeTest_) {
            // 0 inputs, 2 outputs; use the system default device
            const auto err = deviceManager_.initialiseWithDefaultDevices(0, 2);
            if (err.isNotEmpty())
                lastMessage_ = l13::u8("Audio device initialisation failed: ") + err;
            deviceManager_.addAudioCallback(&player_);
        }
        player_.setProcessor(&loop_);
        setSize(640, 300);
        startTimerHz(10);
    }

    ~MainComponent() override {
        stopTimer();
        // Shutdown order: editor window -> take off the audio callback -> plugin
        window_.reset();
        deviceManager_.removeAudioCallback(&player_);
        player_.setProcessor(nullptr);
        loop_.setPlugin(nullptr);
        plugin_.reset();
    }

    // message thread: switching plugins
    bool loadPlugin(const juce::File& file) {
        window_.reset();               // 1) the editor must disappear before the plugin
        player_.setProcessor(nullptr); // 2) stop the audio thread's calls into loop_ / the plugin (internally takes a lock and calls releaseResources)
        loop_.setPlugin(nullptr);
        plugin_.reset();               // 3) now it is safe to delete the old plugin

        double sampleRate = 48000.0;
        int blockSize = 512;
        if (auto* device = deviceManager_.getCurrentAudioDevice()) {
            sampleRate = device->getCurrentSampleRate();
            blockSize = device->getCurrentBufferSizeSamples();
        }
        juce::String error;
        auto instance = host_.loadFromFile(file, sampleRate, blockSize, error);
        if (instance != nullptr) {
            plugin_ = std::make_unique<dawlab::host::HostedPlugin>(std::move(instance));
            loop_.setPlugin(plugin_.get());
            lastMessage_ = l13::u8("Loaded: ") + file.getFullPathName();
        } else {
            lastMessage_ = error;
        }
        player_.setProcessor(&loop_); // 4) reconnect to the player: if the device is running, prepareToPlay -> plugin->prepare happens immediately
        updateStatus();
        return plugin_ != nullptr;
    }

    void toggleEditor() {
        if (plugin_ == nullptr)
            return;
        if (window_ != nullptr) {
            window_.reset();
            return;
        }
        window_ = std::make_unique<dawlab::host::PluginWindow>(*plugin_);
        // Close button: let us delete the window (asynchronously, to avoid deleting this inside the window's own callback)
        window_->onCloseRequested = [this] { juce::MessageManager::callAsync([this] { window_.reset(); }); };
    }

    // ---- smoke test: opens no audio device and walks through every feature automatically ----
    int runSmokeTest(juce::String& report) {
        int failures = 0;
        auto check = [&](bool ok, const char* what) {
            report << (ok ? "[ok]   " : "[FAIL] ") << l13::u8(what) << "\n";
            failures += ok ? 0 : 1;
        };
        auto renderBlocks = [&](int blocks) {
            loop_.setPlayConfigDetails(0, 2, 48000.0, 512);
            loop_.prepareToPlay(48000.0, 512);
            loop_.playing = true;
            juce::AudioBuffer<float> buffer(2, 512);
            juce::MidiBuffer midi;
            float peak = 0.0f;
            for (int b = 0; b < blocks; ++b) {
                loop_.processBlock(buffer, midi);
                peak = std::max(peak, buffer.getMagnitude(0, 512));
            }
            loop_.releaseResources();
            return peak;
        };

        check(loadPlugin(l13::defaultCompressorVst3()), "load DawLabCompressor");
        toggleEditor();
        check(window_ != nullptr && !window_->isUsingGenericEditor(), "DawLabCompressor uses its own editor");
        if (window_ != nullptr)
            report << "  editor size: " << window_->getContentComponent()->getWidth() << " x "
                   << window_->getContentComponent()->getHeight() << "\n";
        player_.setProcessor(nullptr); // the smoke test calls processBlock itself
        const float peakComp = renderBlocks(200);
        check(peakComp > 0.0f && peakComp < 1.0f, "DawLabCompressor produces output without clipping");
        loop_.bypassed = true;
        const float peakBypass = renderBlocks(200);
        loop_.bypassed = false;
        report << "  peak (plugin) = " << peakComp << ", peak (bypass) = " << peakBypass << "\n";
        check(peakComp < peakBypass, "compressed peak is lower than the bypass peak");

        check(loadPlugin(l13::defaultLimiterVst3()), "load DawLabLimiter");
        toggleEditor();
        check(window_ != nullptr && window_->isUsingGenericEditor(), "DawLabLimiter has no editor -> GenericAudioProcessorEditor");
        player_.setProcessor(nullptr);
        const float peakLim = renderBlocks(200); // the plugin only reports its latency after prepare
        const int latency = plugin_ != nullptr ? plugin_->getLatencySamples() : -1;
        report << "  DawLabLimiter latency = " << latency << " samples, peak = " << peakLim << "\n";
        check(latency == 240, "DawLabLimiter reports a latency of 240 samples (5 ms @ 48 kHz)");
        check(peakLim <= 0.8913f * 1.001f, "DawLabLimiter output <= ceiling -1 dBFS");
        window_.reset();
        return failures;
    }

    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff2a2e35)); }

    void resized() override {
        auto area = getLocalBounds().reduced(10);
        auto row1 = area.removeFromTop(30);
        loadFileButton_.setBounds(row1.removeFromLeft(140).reduced(2));
        loadCompButton_.setBounds(row1.removeFromLeft(170).reduced(2));
        loadLimButton_.setBounds(row1.removeFromLeft(150).reduced(2));
        auto row2 = area.removeFromTop(30);
        editorButton_.setBounds(row2.removeFromLeft(140).reduced(2));
        playButton_.setBounds(row2.removeFromLeft(100).reduced(2));
        bypassButton_.setBounds(row2.removeFromLeft(100).reduced(2));
        audioButton_.setBounds(row2.removeFromLeft(140).reduced(2));
        status_.setBounds(area.reduced(2, 8));
    }

private:
    void chooseFile() {
        chooser_ = std::make_unique<juce::FileChooser>(l13::u8("Choose a VST3 plugin"), l13::defaultCompressorVst3(),
                                                       "*.vst3");
        // On Windows a .vst3 is mostly a folder (bundle), so both files and folders must be selectable
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles |
                                  juce::FileBrowserComponent::canSelectDirectories,
                              [this](const juce::FileChooser& fc) {
                                  const auto result = fc.getResult();
                                  if (result != juce::File())
                                      loadPlugin(result);
                              });
    }

    void showAudioSettings() {
        auto* selector = new juce::AudioDeviceSelectorComponent(deviceManager_, 0, 0, 2, 2, false, false, true, false);
        selector->setSize(500, 400);
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned(selector);
        options.dialogTitle = "Audio Settings";
        options.useNativeTitleBar = true;
        options.resizable = false;
        options.launchAsync();
    }

    void timerCallback() override { updateStatus(); }

    void updateStatus() {
        juce::String s;
        if (plugin_ != nullptr) {
            s << "Plugin: " << plugin_->getName() << "   latency: " << plugin_->getLatencySamples() << " samples\n";
            s << plugin_->describeBusLayout() << "\n";
            for (int i = 0; i < std::min(8, plugin_->getNumParameters()); ++i)
                s << "  " << plugin_->getParameterName(i) << " = " << plugin_->getParameterText(i) << "\n";
        } else {
            s << l13::u8("No plugin loaded yet\n");
        }
        if (auto* device = deviceManager_.getCurrentAudioDevice())
            s << "Audio: " << device->getName() << "  " << device->getCurrentSampleRate() << " Hz / "
              << device->getCurrentBufferSizeSamples() << " samples   out peak "
              << juce::String(dawlab::gainToDb(loop_.outputPeak.load()), 1) << " dBFS\n";
        s << lastMessage_;
        status_.setText(s, juce::dontSendNotification);
    }

    bool smokeTest_ = false;
    dawlab::host::PluginHost host_;
    juce::AudioDeviceManager deviceManager_;
    juce::AudioProcessorPlayer player_;
    LoopPlayer loop_;
    std::unique_ptr<dawlab::host::HostedPlugin> plugin_;
    std::unique_ptr<dawlab::host::PluginWindow> window_;
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::String lastMessage_;

    juce::TextButton loadFileButton_{"Load VST3..."};
    juce::TextButton loadCompButton_{"DawLabCompressor"};
    juce::TextButton loadLimButton_{"DawLabLimiter"};
    juce::TextButton editorButton_{"Open/Close Editor"};
    juce::TextButton playButton_{"Play"};
    juce::TextButton bypassButton_{"Bypass"};
    juce::TextButton audioButton_{"Audio Settings..."};
    juce::Label status_;
};

// ---------------------------------------------------------------------------
// Application and main window
// ---------------------------------------------------------------------------
class MainWindow final : public juce::DocumentWindow {
public:
    MainWindow(const juce::String& name, bool smokeTest)
        : juce::DocumentWindow(name, juce::Colours::darkgrey, juce::DocumentWindow::allButtons) {
        setUsingNativeTitleBar(true);
        setContentOwned(new MainComponent(smokeTest), true);
        setResizable(true, false);
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
    }
    MainComponent& getMain() { return *static_cast<MainComponent*>(getContentComponent()); }
    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

class GuiHostApplication final : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String& commandLine) override {
        const bool smoke = commandLine.contains("--smoke-test");
        window_ = std::make_unique<MainWindow>("DawLab l13_gui_host", smoke);
        if (smoke) {
            // Wait until the window is really shown before running (the editor needs a peer to exist before it can create a native window)
            juce::Timer::callAfterDelay(500, [this] {
                juce::String report;
                const int failures = window_->getMain().runSmokeTest(report);
                report << (failures == 0 ? "[PASS] l13_gui_host smoke test\n" : "[FAIL] l13_gui_host smoke test\n");
                const auto path = juce::String::fromUTF8(dawlab::outputPath("l13_gui_host_smoke.txt").c_str());
                juce::File::getCurrentWorkingDirectory().getChildFile(path).replaceWithText(report);
                setApplicationReturnValue(failures == 0 ? 0 : 1);
                quit();
            });
        }
    }

    void shutdown() override { window_.reset(); }
    void systemRequestedQuit() override { quit(); }

private:
    std::unique_ptr<MainWindow> window_;
};

} // namespace

START_JUCE_APPLICATION(GuiHostApplication)
