#pragma once
// Exercise 8 - Recent projects and autosave (optional, JUCE).
//
// Two things every DAW has and DawLab 15 does not, added from outside the app:
//
//   RecentProjects   File > Open Recent: the last 8 projects, newest first, kept in the app's
//                    settings (a juce::PropertiesFile), so they are still there at the next start.
//   Autosaver        every minute at most, while the project has unsaved changes, the whole project
//                    is written NEXT TO the user's file ("Song.autosave.dawlab" beside "Song.dawlab"),
//                    never over it. When the project is saved (or undone back to its saved state),
//                    the autosave is deleted. At the next start, newerAutosave() says whether there
//                    is one that is newer than the project - what a crash left behind.
//
// The app calls both from its UI timer (follow(), poll()); the self-check calls them by hand.

#include "MainComponent.h"
#include "ProjectIO.h"

#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace ex8 {

class RecentProjects {
public:
    static constexpr int kMaxFiles = 8;
    static constexpr const char* kSettingsKey = "recentProjects";

    // Reads the list from `settings` (kSettingsKey); the settings must outlive this object.
    explicit RecentProjects(juce::PropertiesFile& settings);

    // ---- YOUR TURN (starter/ex8_recent_and_autosave.cpp) ----------------------------------------
    // `file` was opened or saved: it goes to the front (once), the list keeps kMaxFiles, and the
    // settings are written at once (a crash must not lose the list).
    void noteProject(const juce::File& file);
    // The UI timer passes MainComponent::getProjectFile(): when it differs from the last one seen
    // and exists, it is noted. That catches Open, Save As and Open Recent without touching them.
    void follow(const juce::File& currentProject);
    // The files that still exist, newest first.
    juce::Array<juce::File> getFiles() const;
    // Adds a submenu "Open Recent" to `menu`: one item per file (its file name), then a separator
    // and "Clear Menu". With no files, a single disabled item "No recent projects". Choosing a file
    // calls open(file).
    void addToMenu(juce::PopupMenu& menu, std::function<void(const juce::File&)> open);
    void clear();

private:
    void store();

    juce::PropertiesFile& settings_;
    juce::StringArray paths_; // newest first
    juce::File lastSeen_;
};

class Autosaver {
public:
    static constexpr double kIntervalSeconds = 60.0;

    // `untitledFolder`: where a project that has no file yet is autosaved.
    Autosaver(dawlab::app::MainComponent& main, juce::File untitledFolder)
        : main_(main), untitledFolder_(std::move(untitledFolder)) {}

    // "Song.dawlab" -> "Song.autosave.dawlab" in the same folder; no file -> "Untitled.autosave.dawlab"
    // in untitledFolder.
    static juce::File autosaveFileFor(const juce::File& project, const juce::File& untitledFolder) {
        if (project == juce::File())
            return untitledFolder.getChildFile("Untitled.autosave.dawlab");
        return project.getSiblingFile(project.getFileNameWithoutExtension() + ".autosave" + project.getFileExtension());
    }
    // Deletes an autosave and its media folder.
    static void removeAutosave(const juce::File& autosave) {
        autosave.deleteFile();
        dawlab::app::ProjectIO::mediaFolderFor(autosave).deleteRecursively();
    }

    // ---- YOUR TURN ----------------------------------------------------------------------------
    // Called by the UI timer with a clock in seconds. Writes the autosave (ProjectIO::save, NOT
    // MainComponent::saveProject: the user's file and the "saved" mark stay as they are) when
    //   - the history says the project is modified,
    //   - at least kIntervalSeconds passed since the last autosave, and
    //   - the project text differs from what the last autosave wrote (nothing changed: nothing to do).
    // When the project is not modified, an existing autosave is removed instead. True if it wrote.
    bool poll(double nowSeconds);
    // The autosave of `project` if it exists and is newer than the project file (or the project
    // file does not exist); otherwise juce::File().
    static juce::File newerAutosave(const juce::File& project, const juce::File& untitledFolder);

private:
    dawlab::app::MainComponent& main_;
    juce::File untitledFolder_;
    juce::File lastFile_;      // the autosave file of the last poll
    juce::String lastText_;    // what was written there
    double lastWrite_ = -1.0e30;
};

} // namespace ex8
