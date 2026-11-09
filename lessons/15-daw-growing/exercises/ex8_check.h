#pragma once
// Exercise 8 - the self-check (main). Included at the end of the starter and of the solution.

#include "ex8_recent_and_autosave.h"

#include <dawlab/Check.h>

#include <cstdio>

namespace ex8::check {

using namespace dawlab;
using namespace dawlab::app;

// The submenu called `name` of `menu`, or nullptr.
inline const juce::PopupMenu* subMenu(const juce::PopupMenu& menu, const juce::String& name) {
    for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
        if (it.getItem().text == name && it.getItem().subMenu != nullptr)
            return it.getItem().subMenu.get();
    return nullptr;
}

inline juce::Array<juce::PopupMenu::Item> itemsOf(const juce::PopupMenu& menu) {
    juce::Array<juce::PopupMenu::Item> items;
    for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
        if (!it.getItem().isSeparator)
            items.add(it.getItem());
    return items;
}

inline bool setGain(Session& session, int trackIndex, float db) {
    return session.perform(edit::SetValue{StripTarget::ofTrack(session.getTrackId(trackIndex)), edit::Field::Gain, 0, db}, "Gain");
}

inline void checkRecent(MainComponent& main, const juce::File& folder) {
    juce::PropertiesFile settings(folder.getChildFile("settings.xml"), juce::PropertiesFile::Options());
    RecentProjects recent(settings);
    DAWLAB_CHECK(recent.getFiles().isEmpty());

    const juce::File song = folder.getChildFile("Song.dawlab"), other = folder.getChildFile("Other.dawlab");
    DAWLAB_CHECK(main.saveProject(song));
    recent.follow(main.getProjectFile());
    recent.follow(main.getProjectFile()); // the same file again: nothing new
    DAWLAB_CHECK(main.saveProject(other));
    recent.follow(main.getProjectFile());
    DAWLAB_CHECK((recent.getFiles() == juce::Array<juce::File>{other, song}));
    recent.noteProject(song); // to the front, once
    DAWLAB_CHECK((recent.getFiles() == juce::Array<juce::File>{song, other}));

    // At most 8, newest first.
    for (int i = 0; i < 9; ++i) {
        const juce::File f = folder.getChildFile("P" + juce::String(i) + ".dawlab");
        DAWLAB_CHECK(f.replaceWithText("not opened"));
        recent.noteProject(f);
    }
    juce::Array<juce::File> files = recent.getFiles();
    DAWLAB_CHECK(files.size() == RecentProjects::kMaxFiles);
    DAWLAB_CHECK(files.size() > 0 && files[0] == folder.getChildFile("P8.dawlab"));
    DAWLAB_CHECK(!files.contains(song) && !files.contains(other)); // the oldest went
    // A deleted project is not offered; the list survives a restart.
    DAWLAB_CHECK(folder.getChildFile("P5.dawlab").deleteFile());
    DAWLAB_CHECK(recent.getFiles().size() == RecentProjects::kMaxFiles - 1);
    recent.noteProject(song);
    recent.noteProject(other);
    {
        juce::PropertiesFile reread(folder.getChildFile("settings.xml"), juce::PropertiesFile::Options());
        RecentProjects restarted(reread);
        DAWLAB_CHECK(restarted.getFiles() == recent.getFiles());
    }

    // The menu: one item per file, Clear Menu; choosing one opens it.
    juce::File opened;
    juce::PopupMenu file;
    recent.addToMenu(file, [&opened, &main](const juce::File& f) {
        opened = f;
        main.openProject(f);
    });
    const juce::PopupMenu* sub = subMenu(file, "Open Recent");
    DAWLAB_CHECK(sub != nullptr);
    if (sub != nullptr) {
        const juce::Array<juce::PopupMenu::Item> items = itemsOf(*sub);
        files = recent.getFiles();
        DAWLAB_CHECK(items.size() == files.size() + 1);
        DAWLAB_CHECK(items.size() > 2 && items[0].text == "Other.dawlab" && items[1].text == "Song.dawlab");
        DAWLAB_CHECK(items.size() > 0 && items.getLast().text == "Clear Menu");
        if (items.size() > 2 && items[1].action) {
            items[1].action();
            DAWLAB_CHECK(opened == song && main.getProjectFile() == song);
        } else {
            DAWLAB_CHECK(false); // the items need an action
        }
        if (items.size() > 0 && items.getLast().action)
            items.getLast().action();
        DAWLAB_CHECK(recent.getFiles().isEmpty());
    }
    juce::PopupMenu empty;
    recent.addToMenu(empty, [](const juce::File&) {});
    const juce::PopupMenu* none = subMenu(empty, "Open Recent");
    DAWLAB_CHECK(none != nullptr && itemsOf(*none).size() == 1 && !itemsOf(*none)[0].isEnabled);
}

inline void checkAutosave(MainComponent& main, const juce::File& folder) {
    const juce::File song = folder.getChildFile("Song.dawlab");
    DAWLAB_CHECK(main.openProject(song).ok);
    // The project file is older than anything written from now on.
    DAWLAB_CHECK(song.setLastModificationTime(juce::Time::getCurrentTime() - juce::RelativeTime::minutes(10)));
    Autosaver autosaver(main, folder);
    const juce::File autosave = Autosaver::autosaveFileFor(song, folder);
    DAWLAB_CHECK(autosave == folder.getChildFile("Song.autosave.dawlab"));

    DAWLAB_CHECK(!autosaver.poll(0.0)); // nothing changed
    DAWLAB_CHECK(!autosave.exists());
    Session& session = main.getSession();
    DAWLAB_CHECK(setGain(session, 1, -12.0f));
    DAWLAB_CHECK(autosaver.poll(1.0));
    DAWLAB_CHECK(autosave.existsAsFile() && ProjectIO::mediaFolderFor(autosave).isDirectory());
    DAWLAB_CHECK(main.getProjectFile() == song && session.getHistory().isModified()); // the user's file stays theirs
    DAWLAB_CHECK(!autosaver.poll(2.0));  // too soon
    DAWLAB_CHECK(setGain(session, 2, -3.0f));
    DAWLAB_CHECK(!autosaver.poll(30.0)); // still too soon
    DAWLAB_CHECK(autosaver.poll(61.5));  // a minute after the last one
    DAWLAB_CHECK(!autosaver.poll(200.0)); // nothing new since
    DAWLAB_CHECK(Autosaver::newerAutosave(song, folder) == autosave);
    // An autosave older than the project file is not offered (the user saved somewhere else since).
    DAWLAB_CHECK(song.setLastModificationTime(juce::Time::getCurrentTime() + juce::RelativeTime::minutes(1)));
    DAWLAB_CHECK(Autosaver::newerAutosave(song, folder) == juce::File());
    DAWLAB_CHECK(song.setLastModificationTime(juce::Time::getCurrentTime() - juce::RelativeTime::minutes(10)));

    // What a crash leaves behind opens as the project was.
    {
        MainComponent after(MainComponent::Mode::Offline);
        DAWLAB_CHECK(after.openProject(autosave).ok);
        const Session& s = after.getSession();
        DAWLAB_CHECK(s.getNumTracks() == session.getNumTracks());
        if (s.getNumTracks() > 2) {
            DAWLAB_CHECK(s.snapshotTrack(s.getTrackId(1)).gainDb == -12.0f);
            DAWLAB_CHECK(s.snapshotTrack(s.getTrackId(2)).gainDb == -3.0f);
        }
    }

    // Saved: the autosave goes.
    DAWLAB_CHECK(main.saveProject(song));
    DAWLAB_CHECK(!autosaver.poll(300.0));
    DAWLAB_CHECK(!autosave.exists() && !ProjectIO::mediaFolderFor(autosave).exists());
    DAWLAB_CHECK(Autosaver::newerAutosave(song, folder) == juce::File());

    // Undone back to the saved state: the autosave goes too.
    DAWLAB_CHECK(setGain(session, 1, 0.0f));
    DAWLAB_CHECK(autosaver.poll(400.0));
    DAWLAB_CHECK(autosave.existsAsFile());
    DAWLAB_CHECK(main.undo());
    DAWLAB_CHECK(!autosaver.poll(401.0));
    DAWLAB_CHECK(!autosave.exists());

    // A project without a file is autosaved in the untitled folder.
    main.newProject();
    const juce::File untitled = folder.getChildFile("Untitled.autosave.dawlab");
    DAWLAB_CHECK(setGain(session, 0, -6.0f));
    DAWLAB_CHECK(autosaver.poll(402.0)); // another file: no waiting for the last one's minute
    DAWLAB_CHECK(untitled.existsAsFile());
    DAWLAB_CHECK(Autosaver::newerAutosave(juce::File(), folder) == untitled);
    std::printf("  autosaves next to Song.dawlab and in the untitled folder: %s\n", untitled.existsAsFile() ? "yes" : "no");
}

inline void run() {
    const juce::File folder = juce::File::getCurrentWorkingDirectory().getChildFile("dawlab_output").getChildFile("l15_ex8");
    folder.deleteRecursively();
    folder.createDirectory();
    MainComponent main(MainComponent::Mode::Offline);
    DAWLAB_CHECK(main.loadDemoSongNow() == 0);
    main.simulatePlayback(512);
    checkRecent(main, folder);
    checkAutosave(main, folder);
}

} // namespace ex8::check

int main() {
    const juce::ScopedJuceInitialiser_GUI juceInit;
    std::printf("Exercise 8 - recent projects and autosave\n");
    ex8::check::run();
    return dawlab::test::summary("l15_ex8");
}
