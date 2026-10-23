// l14_ex13_starter -- Exercise 13: a keyboard shortcut map (starter).
//
// Your task: complete every function marked TODO (see exercises/README.md, Exercise 13).
// Until then the self-check at the end prints FAIL - that is expected.
//
// What the finished program shows
//   - How JUCE turns keys into actions: every command declares default key presses in
//     getCommandInfo(); the ApplicationCommandManager collects them in one KeyPressMappingSet,
//     which MainComponent installs as a KeyListener. A key press -> command ID -> perform().
//   - ShortcutCommands: new commands behind the app's own (MainComponent::setExtraCommandTarget):
//     keys 1..8 select track 1..8, Ctrl+Left / Ctrl+Right move the playhead by one beat.
//   - findConflicts(): two commands on the same key would make one of them unreachable. The check
//     runs over all registered commands.
//   - User customisation: remap Play/Stop from Space to P, save only the differences from the
//     defaults (KeyPressMappingSet::createXml(true)) and restore them in a fresh app.
//   - juce::KeyMappingEditorComponent: JUCE's ready-made shortcut editor for the same mapping
//     set (rendered to a PNG here; in the app you would show it in a DialogWindow).
//
// Run
//   build/bin/Debug/l14_ex13_starter.exe
//
// Output
//   dawlab_output/l14_ex13_shortcuts.xml   the user's changes
//   dawlab_output/l14_ex13_editor.png      the KeyMappingEditorComponent
//   Self-check: no conflicts in the defaults, a conflict is detected, keys 1..8 / Ctrl+arrows
//   work, the remap survives save + restore.

#include <dawlab/Common.h>
#include <dawlab/engine/All.h>

#include "AppCommands.h"
#include "MainComponent.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <cstdio>
#include <vector>

using namespace dawlab;
using namespace dawlab::engine;
using namespace dawlab::app;

namespace ex13 {

struct Conflict {
    juce::KeyPress key;
    juce::CommandID first = 0, second = 0;
};

// Every pair of commands (from `commands`) that share a key press.
std::vector<Conflict> findConflicts(juce::KeyPressMappingSet& mappings, const juce::Array<juce::CommandID>& commands) {
    // TODO: for every pair (i < j) of commands, every key of commands[i]
    //       (mappings.getKeyPressesAssignedToCommand) that commands[j] also has is a Conflict.
    (void)mappings;
    (void)commands;
    return {};
}

class ShortcutCommands final : public juce::ApplicationCommandTarget {
public:
    enum : juce::CommandID {
        selectTrack1 = 0x3101, // .. selectTrack1 + 7
        nudgeBack = 0x3110,
        nudgeForward = 0x3111,
    };

    explicit ShortcutCommands(MainComponent& main) : main_(main) { main_.setExtraCommandTarget(this); }
    ~ShortcutCommands() override { main_.setExtraCommandTarget(nullptr); }

    juce::ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
    void getAllCommands(juce::Array<juce::CommandID>& c) override {
        for (int t = 0; t < kNumTracks; ++t)
            c.add(selectTrack1 + t);
        c.add(nudgeBack);
        c.add(nudgeForward);
    }
    void getCommandInfo(juce::CommandID id, juce::ApplicationCommandInfo& info) override {
    // TODO: "Select track N" on key '1' + (N - 1) (category track); "Back one beat" on Ctrl+Left and
    //       "Forward one beat" on Ctrl+Right (category transport; ModifierKeys::commandModifier).
    (void)id;
    (void)info;
}
    bool perform(const InvocationInfo& info) override {
    // TODO: select track -> main_.getArrangement().setSelectedTrack(n);
    //       nudge -> move the transport to the previous / next beat LINE (from 30000 samples at
    //       24000 samples per beat, forward goes to 48000, back to 24000). Return true if handled.
    (void)info;
    return false;
}

private:
    MainComponent& main_;
};

// The user's changes only (keys added or removed relative to the defaults).
bool saveShortcuts(juce::KeyPressMappingSet& mappings, const juce::File& file) {
    // TODO: only the user's changes: mappings.createXml(true), then write it to `file`.
    (void)mappings;
    (void)file;
    return false;
}

bool loadShortcuts(juce::KeyPressMappingSet& mappings, const juce::File& file) {
    // TODO: parse the file (juce::XmlDocument::parse) and mappings.restoreFromXml(*xml).
    (void)mappings;
    (void)file;
    return false;
}

juce::Array<juce::CommandID> allCommands(MainComponent& main, ShortcutCommands& extra) {
    juce::Array<juce::CommandID> ids;
    main.getAllCommands(ids);
    extra.getAllCommands(ids);
    return ids;
}

} // namespace ex13

int main() {
    const juce::ScopedJuceInitialiser_GUI juceInit;
    using namespace ex13;
    const juce::File xmlFile = juce::File::getCurrentWorkingDirectory().getChildFile(outputPath("l14_ex13_shortcuts.xml"));
    const juce::KeyPress space(juce::KeyPress::spaceKey), keyP('p');

    {
        MainComponent main(MainComponent::Mode::Offline);
        ShortcutCommands extra(main);
        auto& mappings = *main.getCommandManager().getKeyMappings();
        const auto ids = allCommands(main, extra);

        auto conflicts = findConflicts(mappings, ids);
        std::printf("%d commands, %zu conflicts in the default map\n", ids.size(), conflicts.size());
        DAWLAB_CHECK(conflicts.empty());

        // the new commands
        mappings.keyPressed(juce::KeyPress('3'), &main);
        DAWLAB_CHECK(main.getArrangement().getSelectedTrack() == 2);
        main.getEngine().getTransport().setPosition(30000); // between beat 1 (24000) and beat 2 (48000)
        mappings.keyPressed(juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys::commandModifier, 0), &main);
        DAWLAB_CHECK(main.getEngine().getTransport().getPosition() == 48000);
        mappings.keyPressed(juce::KeyPress(juce::KeyPress::leftKey, juce::ModifierKeys::commandModifier, 0), &main);
        mappings.keyPressed(juce::KeyPress(juce::KeyPress::leftKey, juce::ModifierKeys::commandModifier, 0), &main);
        DAWLAB_CHECK(main.getEngine().getTransport().getPosition() == 0);

        // a conflict is detected: put L (Loop) on Play/Stop as well
        mappings.addKeyPress(CommandIDs::playStop, juce::KeyPress('l'));
        conflicts = findConflicts(mappings, ids);
        DAWLAB_CHECK(conflicts.size() == 1);
        if (!conflicts.empty())
            std::printf("conflict found: '%s' is on both \"%s\" and \"%s\"\n", conflicts[0].key.getTextDescription().toRawUTF8(),
                        main.getCommandManager().getNameOfCommand(conflicts[0].first).toRawUTF8(),
                        main.getCommandManager().getNameOfCommand(conflicts[0].second).toRawUTF8());
        mappings.removeKeyPress(CommandIDs::playStop, 1);

        // the user's remap: Play/Stop on P instead of Space
        mappings.removeKeyPress(space);
        mappings.addKeyPress(CommandIDs::playStop, keyP);
        DAWLAB_CHECK(findConflicts(mappings, ids).empty());
        DAWLAB_CHECK(saveShortcuts(mappings, xmlFile));

        // JUCE's editor for the same mapping set (given; see the solution for the explanation of
        // the setRootItem() trick: the TreeView lays out its rows only through the message loop).
        juce::Component holder;
        holder.setLookAndFeel(&main.getDawLookAndFeel());
        juce::KeyMappingEditorComponent editor(mappings, true);
        editor.setColours(DawColours::panel(), DawColours::text());
        holder.setSize(520, 700);
        holder.addAndMakeVisible(editor);
        editor.setBounds(holder.getLocalBounds());
        for (auto* child : editor.getChildren())
            if (auto* tree = dynamic_cast<juce::TreeView*>(child)) {
                juce::TreeViewItem* root = tree->getRootItem();
                tree->setRootItem(nullptr);
                tree->setRootItem(root);
            }
        const juce::Image image = holder.createComponentSnapshot(holder.getLocalBounds());
        const juce::File png = juce::File::getCurrentWorkingDirectory().getChildFile(outputPath("l14_ex13_editor.png"));
        png.deleteFile();
        juce::FileOutputStream out(png);
        juce::PNGImageFormat format;
        DAWLAB_CHECK(out.openedOk() && format.writeImageToStream(image, out));
        holder.removeChildComponent(&editor);
        holder.setLookAndFeel(nullptr);
    }
    std::printf("saved the user's shortcuts to %s:\n%s\n", xmlFile.getFullPathName().toRawUTF8(),
                xmlFile.loadFileAsString().toRawUTF8());
    {
        MainComponent main(MainComponent::Mode::Offline); // "the next start of the app"
        ShortcutCommands extra(main);
        auto& mappings = *main.getCommandManager().getKeyMappings();
        DAWLAB_CHECK(loadShortcuts(mappings, xmlFile));
        Transport& tr = main.getEngine().getTransport();
        const bool before = tr.isPlaying();
        mappings.keyPressed(space, &main);
        DAWLAB_CHECK(tr.isPlaying() == before); // Space does nothing any more
        mappings.keyPressed(keyP, &main);
        DAWLAB_CHECK(tr.isPlaying() != before); // P plays
        DAWLAB_CHECK(mappings.getKeyPressesAssignedToCommand(CommandIDs::playStop).contains(keyP));
        DAWLAB_CHECK(findConflicts(mappings, allCommands(main, extra)).empty());
    }
    return test::summary("l14_ex13_starter");
}
