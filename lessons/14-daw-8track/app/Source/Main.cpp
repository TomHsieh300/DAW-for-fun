// DawLab 8 - the application entry point (Step 12).
//
//   "DawLab 8.exe"                        the DAW: main window 1400 x 900, default audio output
//   "DawLab 8.exe" --smoke-test           automated check without audio device (exit code 0/1)
//   "DawLab 8.exe" --snapshot <file.png>  render the main window with the demo song to a PNG
//
// JUCEApplication gives us the message loop (the "message thread" = the GUI thread). The main
// window is a DocumentWindow whose content is MainComponent; MainComponent owns everything
// else. The two test modes run inside initialise() and quit immediately afterwards.

#include "MainComponent.h"
#include "SmokeTest.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdio>

#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace dawlab::app {

class MainWindow final : public juce::DocumentWindow {
public:
    explicit MainWindow(const juce::String& name)
        : juce::DocumentWindow(name, DawColours::background(), juce::DocumentWindow::allButtons) {
        setUsingNativeTitleBar(true);
        auto content = std::make_unique<MainComponent>(MainComponent::Mode::Live);
        content_ = content.get();
        setContentOwned(content.release(), true);
        setResizable(true, true);
        setResizeLimits(1100, 720, 4000, 3000);
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
        content_->grabKeyboardFocus();
    }

    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

private:
    MainComponent* content_ = nullptr;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

class DawLabApplication final : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "DawLab 8"; }
    const juce::String getApplicationVersion() override { return "1.0.0"; }
    bool moreThanOneInstanceAllowed() override { return true; } // tests may run next to the app

    void initialise(const juce::String& commandLine) override {
        const juce::StringArray args = juce::StringArray::fromTokens(commandLine, true);
        if (args.contains("--smoke-test")) {
            attachConsoleIfNeeded();
            // Asynchronous: the last section waits for an export thread through the message loop.
            runSmokeTest([this](int exitCode) {
                setApplicationReturnValue(exitCode);
                std::fflush(stdout);
                quit();
            });
            return;
        }
        const int snap = args.indexOf("--snapshot");
        if (snap >= 0) {
            attachConsoleIfNeeded();
            const juce::String path = args[snap + 1].unquoted();
            if (path.isEmpty()) {
                std::printf("usage: \"DawLab 8.exe\" --snapshot <file.png>\n");
                setApplicationReturnValue(2);
            } else {
                setApplicationReturnValue(runSnapshot(juce::File::getCurrentWorkingDirectory().getChildFile(path)));
            }
            std::fflush(stdout);
            quit();
            return;
        }
        mainWindow_ = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override { mainWindow_.reset(); }
    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted(const juce::String&) override {}

private:
    // A GUI program has no console. When started from a terminal (not from ctest, which already
    // redirects stdout to a pipe), attach to the parent's console so printf output is visible.
    static void attachConsoleIfNeeded() {
#if JUCE_WINDOWS
        const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
        if ((out == nullptr || out == INVALID_HANDLE_VALUE) && AttachConsole(ATTACH_PARENT_PROCESS)) {
            FILE* unused = nullptr;
            freopen_s(&unused, "CONOUT$", "w", stdout);
            freopen_s(&unused, "CONOUT$", "w", stderr);
        }
#endif
    }

    std::unique_ptr<MainWindow> mainWindow_;
};

} // namespace dawlab::app

START_JUCE_APPLICATION(dawlab::app::DawLabApplication)
