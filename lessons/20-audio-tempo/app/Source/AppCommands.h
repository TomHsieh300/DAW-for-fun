#pragma once
// AppCommands: the command IDs of DawLab 20 (Steps 13 and 21).
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
    openProject,          // chapter 15, Step 8
    saveProject,
    saveProjectAs,
    exportMixdown,
    showAudioSettings,
    importMidi = 0x2051,  // chapter 19: a Standard MIDI File as new instrument tracks, with its tempos
    exportMidi,
    // (quit is juce::StandardApplicationCommandIDs::quit)

    // Transport
    playStop = 0x2101,
    returnToStart,
    toggleLoop,
    seekBackOneBar,
    seekForwardOneBar,
    toggleRecord,         // chapter 17
    togglePunch,
    setRecordingTrim,
    toggleMidiReplace,    // chapter 18: MIDI loop recording replaces instead of overdubbing

    // Track (the selected track)
    selectNextTrack = 0x2201,
    selectPreviousTrack,
    toggleMuteSelected,
    toggleSoloSelected,
    addMonoTrack,         // chapter 15: below the selected track
    addStereoTrack,
    deleteSelectedTrack,
    toggleAutomationLane, // chapter 16
    toggleArmSelected,    // chapter 17
    cycleMonitorSelected,
    addInstrumentTrack,   // chapter 18
    showSynthPanel,       // chapter 19: the built-in synth's settings for the selected track
    loadPluginInstrument, // chapter 19: a VST3 instrument in the synth's place
    useBuiltinSynth,      // chapter 19: ... and the synth again

    // Edit (chapter 15, Step 9)
    undo = 0x2401,
    redo,

    // View
    zoomIn = 0x2301,
    zoomOut,
    showKeyboardShortcuts,
};
} // namespace CommandIDs

namespace CommandCategories {
inline constexpr const char* file = "File";
inline constexpr const char* edit = "Edit";
inline constexpr const char* transport = "Transport";
inline constexpr const char* track = "Track";
inline constexpr const char* view = "View";
} // namespace CommandCategories

} // namespace dawlab::app
