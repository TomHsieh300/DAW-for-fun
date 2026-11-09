// Exercise 8 (starter): recent projects and autosave.
//
// Fill in the TODOs. Until then the self-check prints FAIL (expected). The solution is
// solutions/ex8_recent_and_autosave.cpp. What each function must do is in ../ex8_recent_and_autosave.h.
// Build with -Juce: .\scripts\build.ps1 -Only 15-daw-growing -Juce

#include "../ex8_recent_and_autosave.h"

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
    settings_.saveIfNeeded();
}

void RecentProjects::noteProject(const juce::File& file) {
    // TODO: remove `file` wherever it is in paths_ (compare as juce::File), insert it at the front,
    //       keep at most kMaxFiles, store().
    (void)file;
}

void RecentProjects::follow(const juce::File& currentProject) {
    // TODO: a project file other than the last one seen, which exists -> noteProject().
    (void)currentProject;
}

juce::Array<juce::File> RecentProjects::getFiles() const {
    // TODO: the paths whose files still exist, newest first.
    return {};
}

void RecentProjects::addToMenu(juce::PopupMenu& menu, std::function<void(const juce::File&)> open) {
    // TODO: a submenu "Open Recent": juce::PopupMenu::Item(file name).setAction(...) per file, a
    //       separator, "Clear Menu"; or one disabled item "No recent projects".
    (void)menu;
    (void)open;
}

void RecentProjects::clear() {
    paths_.clear();
    store();
}

bool Autosaver::poll(double nowSeconds) {
    // TODO:
    //   file = autosaveFileFor(main_.getProjectFile(), untitledFolder_); another file than last time:
    //   forget lastText_ and lastWrite_.
    //   Not modified (main_.getSession().getHistory()): remove an existing autosave, return false.
    //   Less than kIntervalSeconds since lastWrite_: false.
    //   text = project::writeProject(ProjectIO::capture(session, ProjectIO::mediaFolderFor(file)
    //          .getFileName()).state); the same as lastText_: false.
    //   ProjectIO::save(session, file, error); remember text and time; true.
    (void)nowSeconds;
    return false;
}

juce::File Autosaver::newerAutosave(const juce::File& project, const juce::File& untitledFolder) {
    // TODO: autosaveFileFor(); it must exist and be newer than the project file (if there is one).
    (void)project;
    (void)untitledFolder;
    return {};
}

} // namespace ex8

#include "../ex8_check.h"
