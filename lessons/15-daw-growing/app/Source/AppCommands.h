#pragma once
// AppCommands: the command IDs of DawLab 8 (Steps 13 and 21).
//
// Every user action that can be triggered from more than one place (menu, keyboard, button)
// is a juce::ApplicationCommandManager command. The command manager then gives us, for free:
//   - the keyboard shortcut shown next to the menu item,
//   - one KeyPressMappingSet that turns key presses into commands,
//   - a central "is this command currently possible?" (CommandInfo::setActive).
// MainComponent is the ApplicationCommandTarget that implements them (MainComponent.cpp).
// Exercise 13 extends this list into a full shortcut map.

#include <juce_gui_basics/juce_gui_basics.h>

namespace dawlab::app {

namespace CommandIDs {
enum : juce::CommandID {
    // File
    loadDemoSong = 0x2001,
    newProject,
    loadAudioIntoTrack,   // into the selected track
    exportMixdown,
    showAudioSettings,
    // (quit is juce::StandardApplicationCommandIDs::quit)

    // Transport
    playStop = 0x2101,
    returnToStart,
    toggleLoop,
    seekBackOneBar,
    seekForwardOneBar,

    // Track (the selected track)
    selectNextTrack = 0x2201,
    selectPreviousTrack,
    toggleMuteSelected,
    toggleSoloSelected,

    // View
    zoomIn = 0x2301,
    zoomOut,
    showKeyboardShortcuts,
};
} // namespace CommandIDs

namespace CommandCategories {
inline constexpr const char* file = "File";
inline constexpr const char* transport = "Transport";
inline constexpr const char* track = "Track";
inline constexpr const char* view = "View";
} // namespace CommandCategories

} // namespace dawlab::app
