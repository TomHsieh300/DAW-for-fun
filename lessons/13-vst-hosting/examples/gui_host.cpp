// l13_gui_host：有視窗的迷你 plugin host
//
// 示範：
//   - 選一個 .vst3（或直接載入本章的 DawLabCompressor / DawLabLimiter）；
//   - 用 PluginWindow 打開 plugin 的 editor（DawLabLimiter 沒有 editor → 自動改用 GenericAudioProcessorEditor）；
//   - 經 juce::AudioDeviceManager + juce::AudioProcessorPlayer 即時播放一段 drum loop 穿過 plugin；
//   - message thread 與 audio thread 的分工：換 plugin 時先把 processor 從 player 拿掉，再刪舊的、建新的。
//
// 執行：l13_gui_host                 開視窗（會用系統預設的音訊輸出，注意音量）
//       l13_gui_host --smoke-test    不開音訊裝置：自動載入兩個 plugin、開關 editor、離線跑幾個 block，
//                                    然後結束；結果寫在 dawlab_output/l13_gui_host_smoke.txt（給自動檢查用）
// 該聽到什麼：Play 之後是 120 BPM 的鼓；把 Threshold 往下轉，鼓頭被壓扁、GR meter 往下長；
//             按 Bypass 可以比較原音（直接切換，會有一點 click —— 練習 7 用 crossfade 解決）。

#include "L13Common.h"

#include <atomic>

namespace {

// ---------------------------------------------------------------------------
// LoopPlayer：host 內部的 AudioProcessor，產生 drum loop 並送進 HostedPlugin。
// AudioProcessorPlayer 會在 audio thread 呼叫它的 processBlock。
// ---------------------------------------------------------------------------
class LoopPlayer final : public juce::AudioProcessor {
public:
    LoopPlayer() : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)) {}

    // 只能在「沒有接在 AudioProcessorPlayer 上」的時候呼叫（message thread）
    void setPlugin(dawlab::host::HostedPlugin* plugin) { plugin_ = plugin; }

    std::atomic<bool> playing{false};
    std::atomic<bool> bypassed{false};
    std::atomic<float> outputPeak{0.0f};

    const juce::String getName() const override { return "LoopPlayer"; }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        // 在 prepare 階段配置：loop 依目前裝置的 sample rate 重新產生
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
        // 把 JUCE buffer 的指標包成 dawlab::AudioBlock（零複製）交給 HostedPlugin
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
// MainComponent：按鈕 + 狀態列
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
            // 0 個輸入、2 個輸出；用系統預設裝置
            const auto err = deviceManager_.initialiseWithDefaultDevices(0, 2);
            if (err.isNotEmpty())
                lastMessage_ = l13::u8("音訊裝置初始化失敗：") + err;
            deviceManager_.addAudioCallback(&player_);
        }
        player_.setProcessor(&loop_);
        setSize(640, 300);
        startTimerHz(10);
    }

    ~MainComponent() override {
        stopTimer();
        // 關閉順序：editor 視窗 → 從 audio callback 拿掉 → plugin
        window_.reset();
        deviceManager_.removeAudioCallback(&player_);
        player_.setProcessor(nullptr);
        loop_.setPlugin(nullptr);
        plugin_.reset();
    }

    // message thread：換 plugin
    bool loadPlugin(const juce::File& file) {
        window_.reset();               // 1) editor 必須先於 plugin 消失
        player_.setProcessor(nullptr); // 2) 停止 audio thread 對 loop_ / plugin 的呼叫（內部會拿 lock 並 releaseResources）
        loop_.setPlugin(nullptr);
        plugin_.reset();               // 3) 現在可以安全刪除舊 plugin

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
            lastMessage_ = l13::u8("已載入：") + file.getFullPathName();
        } else {
            lastMessage_ = error;
        }
        player_.setProcessor(&loop_); // 4) 接回 player：裝置在跑的話會立刻 prepareToPlay → plugin->prepare
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
        // 關閉鈕：交給我們刪除視窗（非同步，避免在視窗自己的 callback 裡 delete this）
        window_->onCloseRequested = [this] { juce::MessageManager::callAsync([this] { window_.reset(); }); };
    }

    // ---- smoke test：不開音訊裝置，自動走過一遍所有功能 ------------------------------------
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

        check(loadPlugin(l13::defaultCompressorVst3()), "載入 DawLabCompressor");
        toggleEditor();
        check(window_ != nullptr && !window_->isUsingGenericEditor(), "DawLabCompressor 用自己的 editor");
        if (window_ != nullptr)
            report << "  editor size: " << window_->getContentComponent()->getWidth() << " x "
                   << window_->getContentComponent()->getHeight() << "\n";
        player_.setProcessor(nullptr); // smoke test 自己呼叫 processBlock
        const float peakComp = renderBlocks(200);
        check(peakComp > 0.0f && peakComp < 1.0f, "DawLabCompressor 有輸出、沒有爆");
        loop_.bypassed = true;
        const float peakBypass = renderBlocks(200);
        loop_.bypassed = false;
        report << "  peak（plugin）= " << peakComp << "，peak（bypass）= " << peakBypass << "\n";
        check(peakComp < peakBypass, "壓縮後 peak 比 bypass 小");

        check(loadPlugin(l13::defaultLimiterVst3()), "載入 DawLabLimiter");
        toggleEditor();
        check(window_ != nullptr && window_->isUsingGenericEditor(), "DawLabLimiter 沒有 editor → GenericAudioProcessorEditor");
        player_.setProcessor(nullptr);
        const float peakLim = renderBlocks(200); // prepare 之後 plugin 才回報 latency
        const int latency = plugin_ != nullptr ? plugin_->getLatencySamples() : -1;
        report << "  DawLabLimiter latency = " << latency << " samples, peak = " << peakLim << "\n";
        check(latency == 240, "DawLabLimiter 回報 latency 240 samples（5 ms @ 48 kHz）");
        check(peakLim <= 0.8913f * 1.001f, "DawLabLimiter 輸出 ≤ ceiling −1 dBFS");
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
        chooser_ = std::make_unique<juce::FileChooser>(l13::u8("選擇 VST3 plugin"), l13::defaultCompressorVst3(),
                                                       "*.vst3");
        // .vst3 在 Windows 上多半是資料夾（bundle），所以檔案與資料夾都要能選
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
            s << l13::u8("尚未載入 plugin\n");
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
// 應用程式與主視窗
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
            // 等視窗真的顯示出來再跑（editor 需要 peer 存在才會建立原生視窗）
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
