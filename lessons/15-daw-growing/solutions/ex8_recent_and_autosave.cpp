// Exercise 8 (solution): recent projects and autosave.
//
// Build and run:  l15_ex8_solution   (ctest, label "solution"; needs -Juce)
// Your version:   exercises/starter/ex8_recent_and_autosave.cpp -> l15_ex8_starter

#include "../exercises/ex8_recent_and_autosave.h"

#include "ProjectFormat.h"

using namespace dawlab;
using namespace dawlab::app;

namespace ex8 {

RecentProjects::RecentProjects(juce::PropertiesFile& settings) : settings_(settings) {
    paths_.addLines(settings_.getValue(kSettingsKey));
    paths_.removeEmptyStrings();
}

void RecentProjects::store() {
    settings_.setValue(kSettingsKey, paths_.joinIntoString("\n"));
    settings_.saveIfNeeded(); // now, not at exit: a crash must not lose the list
}

void RecentProjects::noteProject(const juce::File& file) {
    const juce::String path = file.getFullPathName();
    for (int i = paths_.size(); --i >= 0;)
        if (juce::File(paths_[i]) == file) // juce::File compares as the file system does
            paths_.remove(i);
    paths_.insert(0, path);
    while (paths_.size() > kMaxFiles)
        paths_.remove(paths_.size() - 1);
    store();
}

void RecentProjects::follow(const juce::File& currentProject) {
    if (currentProject == lastSeen_)
        return;
    lastSeen_ = currentProject;
    if (currentProject.existsAsFile())
        noteProject(currentProject);
}

juce::Array<juce::File> RecentProjects::getFiles() const {
    juce::Array<juce::File> files;
    for (const juce::String& path : paths_)
        if (juce::File(path).existsAsFile()) // a moved or deleted project is not offered
            files.add(juce::File(path));
    return files;
}

void RecentProjects::addToMenu(juce::PopupMenu& menu, std::function<void(const juce::File&)> open) {
    juce::PopupMenu recent;
    const juce::Array<juce::File> files = getFiles();
    if (files.isEmpty()) {
        recent.addItem(juce::PopupMenu::Item("No recent projects").setEnabled(false));
    } else {
        for (const juce::File& file : files)
            recent.addItem(juce::PopupMenu::Item(file.getFileName()).setAction([open, file] { open(file); }));
        recent.addSeparator();
        recent.addItem(juce::PopupMenu::Item("Clear Menu").setAction([this] { clear(); }));
    }
    menu.addSubMenu("Open Recent", recent);
}

void RecentProjects::clear() {
    paths_.clear();
    store();
}

bool Autosaver::poll(double nowSeconds) {
    const juce::File file = autosaveFileFor(main_.getProjectFile(), untitledFolder_);
    if (file != lastFile_) { // another project: what was written for the last one says nothing
        lastFile_ = file;
        lastText_.clear();
        lastWrite_ = -1.0e30;
    }
    Session& session = main_.getSession();
    if (!session.getHistory().isModified()) {
        if (file.existsAsFile()) // saved, or undone back to the saved state: the file is the truth
            removeAutosave(file);
        lastText_.clear();
        return false;
    }
    if (nowSeconds - lastWrite_ < kIntervalSeconds)
        return false;
    const ProjectIO::Captured captured = ProjectIO::capture(session, ProjectIO::mediaFolderFor(file).getFileName());
    const juce::String text(project::writeProject(captured.state));
    if (text == lastText_ && file.existsAsFile())
        return false;
    juce::String error;
    if (!ProjectIO::save(session, file, error)) {
        main_.setStatus("Autosave failed: " + error);
        return false;
    }
    lastText_ = text;
    lastWrite_ = nowSeconds;
    return true;
}

juce::File Autosaver::newerAutosave(const juce::File& project, const juce::File& untitledFolder) {
    const juce::File autosave = autosaveFileFor(project, untitledFolder);
    if (!autosave.existsAsFile())
        return {};
    if (project != juce::File() && project.existsAsFile() && autosave.getLastModificationTime() <= project.getLastModificationTime())
        return {};
    return autosave;
}

} // namespace ex8

#include "../exercises/ex8_check.h"
