#pragma once
// MainComponent: the content of the main window - and the application controller (Step 12).
//
// It owns, in this order (members are destroyed in reverse order, which matters):
//
//   lookAndFeel_   must outlive every component that uses it
//   pluginHost_    the VST3 format manager (chapter 13) must outlive the plugin instances
//   engine_        stage 1: transport, mixer, inserts (owns the effects and the VST3 inserts)
//   session_       GUI-side project model, the only sender of structural engine commands
//   audioHost_     the audio device; destroyed first of the four, so no callback runs after it
//   loader_        background thread for files and the demo song
//   takeRecorder_  chapter 17: the disk thread of a recording, and takes that become clips
//   GUI            menu bar, TransportBar, ArrangementView, MixerView, status bar, windows
//
// Threads: everything here runs on the message thread. It talks to the audio thread only
// through the engine: atomics (fader, pan, transport...) and the command queue (Session). A
// 30 Hz timer (tick) collects the engine's garbage and refreshes every display from atomics.
//
// Mode::Offline builds the same window without opening an audio device (the smoke test, the
// --snapshot mode and the exercises); simulatePlayback() then drives the engine the way the
// device callback would.

#include "AppCommands.h"
#include "ArrangementView.h"
#include "AudioHost.h"
#include "DawLookAndFeel.h"
#include "Exporter.h"
#include "MediaLoader.h"
#include "MixerView.h"
#include "ParameterPanel.h"
#include "ProjectIO.h"
#include "Session.h"
#include "TakeRecorder.h"
#include "TransportBar.h"

#include <dawlab/engine/All.h>
#include <dawlab/host/PluginHost.h>

#include <juce_audio_utils/juce_audio_utils.h>

#include <atomic>
#include <map>
#include <memory>

namespace dawlab::app {

class MainComponent final : public juce::Component,
                            public juce::ApplicationCommandTarget,
                            public juce::MenuBarModel,
                            private juce::Timer {
public:
    enum class Mode { Live, Offline };

    static constexpr int kDefaultWidth = 1400;
    static constexpr int kDefaultHeight = 900;
    static constexpr double kOfflineSampleRate = 48000.0;
    static constexpr int kOfflineBlockSize = 512;

    explicit MainComponent(Mode mode = Mode::Live);
    ~MainComponent() override;

    // ---- access (tests, exercises, the snapshot) -------------------------------------------
    engine::Engine& getEngine() noexcept { return engine_; }
    Session& getSession() noexcept { return session_; }
    AudioHost& getAudioHost() noexcept { return audioHost_; }
    MediaLoader& getMediaLoader() noexcept { return loader_; }
    TakeRecorder& getTakeRecorder() noexcept { return takeRecorder_; }
    host::PluginHost& getPluginHost() noexcept { return pluginHost_; }
    juce::ApplicationCommandManager& getCommandManager() noexcept { return commandManager_; }
    TransportBar& getTransportBar() noexcept { return transportBar_; }
    ArrangementView& getArrangement() noexcept { return arrangement_; }
    MixerView& getMixer() noexcept { return mixer_; }
    DawLookAndFeel& getDawLookAndFeel() noexcept { return lookAndFeel_; }
    Mode getMode() const noexcept { return mode_; }

    // ---- actions (the commands call these) ----------------------------------------------------
    void togglePlay();
    void returnToStart();
    void toggleLoop();
    void seekBars(int deltaBars);
    // ---- recording (chapter 17, Step 7) -----------------------------------------------------------
    // Starts a recording of the armed tracks, and the transport with it; while one runs, ends it and
    // leaves the transport playing. False if it could not start: the status line says why.
    bool toggleRecord();
    // Arms or disarms a track. The first one opens the device's inputs. Saved with nothing: an armed
    // track is about now, not about the song.
    bool setArmed(engine::TrackId track, bool armed);
    void cycleMonitor(engine::TrackId track); // Off, Auto, Always
    void showRecordMenu(engine::TrackId track);
    void showClipMenu(engine::TrackId track, int clipIndex);
    void askForTrim();
    // True, and it says so on the status line, while a recording prevents `what`.
    bool refuseWhileRecording(const juce::String& what);
    // The same for a new tempo, from the transport bar or the tempo lane (chapter 19); the tempo slider goes back.
    bool refuseTempoChange();
    // The same for what replaces the whole project (new, the demo song): also not during an export.
    bool refuseProjectChange(const juce::String& what);
    void newProject();
    // Project files (chapter 15, Step 8). openProject() and saveProject() work synchronously and
    // report on the status line (and, in Live mode, in a message box); the *Interactive versions
    // ask for the file first. Save asks only for a project that has no file yet - or for one whose
    // opening left something out (a warning): that is still in the file, and saveProject() never
    // writes over it, since the session cannot write it back. It is saved under another name.
    ProjectIO::OpenResult openProject(const juce::File& file);
    bool saveProject(const juce::File& file);
    void openProjectInteractive();
    void saveProjectInteractive(bool saveAs, std::function<void()> afterSave = {});
    // Step 9: when the project has unsaved changes, asks "Save / Don't save / Cancel" first (Live
    // mode only) and calls `then` unless the user cancels. New, Open, the demo song and Quit use it.
    void askToSaveChanges(std::function<void()> then);
    // Edit > Undo / Redo (Step 9): false when there was nothing to do or the entry did not apply.
    bool undo();
    bool redo();
    juce::File getProjectFile() const { return projectFile_; }
    // "DawLab 17 - <project name>", with " *" while there are unsaved changes (Step 9);
    // onTitleChanged tells the window when it changes (from tick()).
    juce::String getWindowTitle() const;
    std::function<void(const juce::String& title)> onTitleChanged;
    void loadDemoSongAsync();       // generates on the loader thread
    int loadDemoSongNow();          // synchronous (snapshot, tests); returns unknown parameter count, -1 if refused
    void chooseAudioFileForTrack(engine::TrackId track);
    void loadAudioFile(engine::TrackId track, const juce::File& file); // asynchronous (loader thread)
    // Files dropped on a lane: the first loads into `track`, the next ones into the tracks below;
    // when no track is left below, new ones are added at the end (chapter 15).
    void loadDroppedFiles(engine::TrackId track, const juce::StringArray& files);
    // Chapter 15: a new track below the selected one, selected and scrolled into view in the
    // arrangement and the mixer. Invalid id (and a status message) when the pool is full.
    engine::TrackId addTrack(engine::TrackFormat format);
    // Deletes the selected track with its clips and inserts (their editors close first); the
    // track that takes its place becomes the selection. False if nothing was deleted.
    bool deleteSelectedTrack();
    void exportMixdownInteractive(); // file chooser, then exportMixdown()
    void exportMixdown(const juce::File& file);
    void showAudioSettings();
    void showKeyboardShortcuts();

    // Insert slots (MixerView callbacks)
    void showSlotMenu(engine::ChannelRef ref, int slot, juce::Component& anchor);
    void openInsertEditor(engine::ChannelRef ref, int slot, bool generic);
    bool loadVst3Into(engine::ChannelRef ref, int slot, const juce::File& file, juce::String& error);
    ParameterWindow* getParameterWindow(engine::InsertEffect* effect) const;
    void closeAllEditors();

    // ---- status --------------------------------------------------------------------------------
    void setStatus(const juce::String& text);
    juce::String getStatus() const { return status_.getText(); }
    bool isExporting() const noexcept { return exportJob_ != nullptr; }
    // True while the clips are being converted to a new device sample rate (loader thread).
    bool isConvertingSampleRate() const noexcept { return conversionRunning_; }
    std::function<void(const ExportResult&)> onExportFinished; // tests / exercises

    // ---- the UI tick (the 30 Hz timer calls it; public for the snapshot) ---------------------
    void tick();
    // Renders `numSamples` through AudioHost::processOffline() in device-sized blocks, as if an
    // audio device were running. Offline mode only (no device may be calling the engine).
    void simulatePlayback(int64_t numSamples, int blockSize = kOfflineBlockSize, engine::InputBlock input = {});

    // ---- extension points (Exercises 11 and 13) ------------------------------------------------
    // An extra ApplicationCommandTarget behind this one: its commands are registered with the
    // command manager (menu items, default key presses) and invoked when we do not handle them.
    void setExtraCommandTarget(juce::ApplicationCommandTarget* target);
    // Called after a menu of the menu bar was built (0 = File, 1 = Transport, 2 = Track, 3 = View).
    std::function<void(int menuIndex, juce::PopupMenu& menu)> onExtendMenu;

    // ---- juce::Component -------------------------------------------------------------------------
    void paint(juce::Graphics&) override;
    void resized() override;

    // ---- juce::ApplicationCommandTarget -------------------------------------------------------------
    juce::ApplicationCommandTarget* getNextCommandTarget() override { return extraCommandTarget_; }
    void getAllCommands(juce::Array<juce::CommandID>& commands) override;
    void getCommandInfo(juce::CommandID id, juce::ApplicationCommandInfo& info) override;
    bool perform(const InvocationInfo& info) override;

    // ---- juce::MenuBarModel ------------------------------------------------------------------------
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected(int menuItemID, int topLevelMenuIndex) override;

private:
    class ExportOverlay;
    void timerCallback() override { tick(); }
    void handleSlotMenuResult(engine::ChannelRef ref, int slot, int result);
    void chooseVst3For(engine::ChannelRef ref, int slot);
    void finishExport(const ExportResult& result);
    void startClipConversion();
    void finishClipConversion(double fromRate, double toRate, const ConvertedBuffers& converted);
    void recoverCrashedTakes();

    Mode mode_;
    DawLookAndFeel lookAndFeel_;
    host::PluginHost pluginHost_;
    engine::Engine engine_;
    Session session_{engine_};
    AudioHost audioHost_{engine_};
    MediaLoader loader_;
    TakeRecorder takeRecorder_{session_, audioHost_};
    std::unique_ptr<juce::PropertiesFile> settings_; // the trim belongs to the machine, not to a song
    juce::ApplicationCommandManager commandManager_;

    juce::MenuBarComponent menuBar_;
    TransportBar transportBar_{engine_, audioHost_};
    ArrangementView arrangement_{session_, &audioHost_}; // playhead = what is heard (Step 14)
    MixerView mixer_{session_};
    juce::Label status_, deviceInfo_;
    juce::TooltipWindow tooltips_{this, 600};
    std::unique_ptr<ExportOverlay> exportOverlay_;
    std::unique_ptr<ExportJob> exportJob_;
    std::unique_ptr<juce::FileChooser> fileChooser_;
    std::map<engine::InsertEffect*, std::unique_ptr<ParameterWindow>> parameterWindows_;
    AudioBuffer offlineScratch_;
    juce::ApplicationCommandTarget* extraCommandTarget_ = nullptr;
    bool conversionRunning_ = false;                 // message thread only
    juce::File projectFile_;                         // where Save writes; none for a new project
    juce::StringArray leftOut_;                      // what opening projectFile_ left out: not written over
    juce::String lastTitle_;
    std::atomic<bool> resumeAfterConversion_{false}; // also set by the device thread

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

} // namespace dawlab::app
