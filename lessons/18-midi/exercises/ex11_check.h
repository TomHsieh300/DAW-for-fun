#pragma once
// Exercise 11 - the self-check (main). Included at the end of the starter and of the solution.

#include "app_rig.h"
#include "ex11_note_editor.h"

#include <dawlab/Check.h>

#include <cstdio>
#include <string>

namespace ex11::check {

using namespace ex18rig;

// "60@0 64@0 ..." with a * for each selected note.
std::string show(NoteEditor& editor) {
    std::string s;
    const MidiClipModel* clip = editor.getClip();
    if (clip == nullptr)
        return "(gone)";
    for (size_t i = 0; i < clip->played.size(); ++i)
        s += std::to_string(clip->played[i].pitch) + "@" + std::to_string(clip->played[i].start) + (editor.isSelected(i) ? "* " : " ");
    return s;
}

// The clip, or an empty one when it is gone (a starter may undo more than it did, and take the track away).
const MidiClipModel& clipOf(NoteEditor& editor) {
    static const MidiClipModel none;
    const MidiClipModel* clip = editor.getClip();
    return clip != nullptr ? *clip : none;
}

bool expect(const char* what, const std::string& got, const std::string& want) {
    const bool ok = got == want;
    std::printf("  %-44s %s\n", what, ok ? "yes" : ("NO: \"" + got + "\", not \"" + want + "\"").c_str());
    return ok;
}

juce::KeyPress key(int code, juce::ModifierKeys mods = {}) {
    return juce::KeyPress(code, mods, 0);
}

void run() {
    Rig rig;
    const TrackId track = rig.instrumentTrack();
    MidiClipModel clip;
    clip.length = 3840;
    clip.played = {{0, 480, 60, 100, 0}, {0, 480, 64, 100, 0}, {0, 480, 67, 100, 0}, {960, 480, 72, 90, 0}, {1920, 240, 74, 80, 0}};
    rig.session.setMidiClips(track, {clip});
    rig.engine.applyPendingCommandsOffline();
    const int id = rig.session.getMidiClips(track)[0].id;
    NoteEditor editor(rig.session, track, id);
    const size_t undo0 = rig.session.getHistory().undoCount();

    // The chord up a whole tone: one undo step, the engine plays it.
    editor.selectRect(0, 1, 60, 67, false);
    DAWLAB_CHECK(editor.numSelected() == 3);
    DAWLAB_CHECK(editor.transpose(2));
    DAWLAB_CHECK(expect("the chord up a whole tone:", show(editor), "62@0* 66@0* 69@0* 72@960 74@1920 "));
    rig.engine.applyPendingCommandsOffline();
    const auto* engineClips = rig.engineTrack(track)->getMidiClips();
    DAWLAB_CHECK(engineClips != nullptr && engineClips->size() == 1 && (*engineClips)[0].notes[0].pitch == 62 &&
                 rig.session.getHistory().undoCount() == undo0 + 1);
    // Undo: the clip changed under the editor, so the selection is gone - transposing does nothing.
    DAWLAB_CHECK(rig.session.undo());
    DAWLAB_CHECK(expect("undo; the selection is cleared:", show(editor), "60@0 64@0 67@0 72@960 74@1920 "));
    DAWLAB_CHECK(!editor.transpose(1) && rig.session.getHistory().undoCount() == undo0);

    // The last note to the start: it stays selected where the sort puts it.
    editor.selectRect(1920, 1921, 0, 127, false);
    DAWLAB_CHECK(editor.move(-1920));
    DAWLAB_CHECK(expect("74 moved to the start, still selected:", show(editor), "60@0 64@0 67@0 74@0* 72@960 "));
    DAWLAB_CHECK(!editor.move(-1)); // would start before 0
    DAWLAB_CHECK(editor.transpose(-2));
    DAWLAB_CHECK(expect("then only it a tone down:", show(editor), "60@0 64@0 67@0 72@0* 72@960 "));
    // All or nothing at the edges of MIDI.
    editor.selectAll();
    DAWLAB_CHECK(!editor.transpose(56) && editor.transpose(55));
    DAWLAB_CHECK(!editor.transpose(1) && !editor.transpose(-128));
    DAWLAB_CHECK(editor.transpose(-55));

    // Duplicate the chord and the note after it: the span 0 .. 1440, so the copies at 1440, selected.
    editor.selectRect(0, 1000, 60, 72, false);
    DAWLAB_CHECK(editor.numSelected() == 5);
    editor.selectRect(0, 1, 60, 67, false); // the chord, not the 72 at 0,
    editor.selectRect(960, 961, 72, 72, true); // and the 72 at 960 added
    DAWLAB_CHECK(editor.numSelected() == 4);
    DAWLAB_CHECK(editor.duplicate());
    DAWLAB_CHECK(expect("duplicate the chord and 72@960:", show(editor), "60@0 64@0 67@0 72@0 72@960 60@1440* 64@1440* 67@1440* 72@2400* "));
    DAWLAB_CHECK(clipOf(editor).length == 3840);
    // Again with a grid of a bar: the span rounds up to 3840, and the clip grows to two bars.
    editor.setGrid(3840);
    DAWLAB_CHECK(editor.duplicate());
    DAWLAB_CHECK(clipOf(editor).length == 2 * 3840 && editor.numSelected() == 4 && clipOf(editor).played.size() == 13);
    DAWLAB_CHECK(clipOf(editor).played.size() > 9 && editor.isSelected(9) && clipOf(editor).played[9].start == 1440 + 3840); // the span 1440 rounded up to the bar
    DAWLAB_CHECK(editor.removeSelected() && editor.numSelected() == 0 && clipOf(editor).played.size() == 9);
    std::printf("  duplicate on a grid of a bar grows the clip to two bars; remove takes the copies away\n");

    // The keys.
    editor.setGrid(240);
    editor.selectRect(0, 1, 60, 60, false);
    DAWLAB_CHECK(editor.keyPressed(key(juce::KeyPress::upKey)) && editor.keyPressed(key(juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier)));
    DAWLAB_CHECK(editor.keyPressed(key(juce::KeyPress::rightKey)) && editor.keyPressed(key(juce::KeyPress::rightKey)));
    DAWLAB_CHECK(editor.keyPressed(key(juce::KeyPress::downKey)));
    DAWLAB_CHECK(expect("Up, Shift+Up, Right, Right, Down:", show(editor), "64@0 67@0 72@0 72@480* 72@960 60@1440 64@1440 67@1440 72@2400 "));
    DAWLAB_CHECK(editor.keyPressed(key('D', juce::ModifierKeys::ctrlModifier)) && editor.numSelected() == 1 && clipOf(editor).played.size() == 10);
    DAWLAB_CHECK(editor.keyPressed(key(juce::KeyPress::deleteKey)) && clipOf(editor).played.size() == 9);
    DAWLAB_CHECK(editor.keyPressed(key('A', juce::ModifierKeys::ctrlModifier)) && editor.numSelected() == 9);
    DAWLAB_CHECK(editor.keyPressed(key(juce::KeyPress::backspaceKey)) && clipOf(editor).played.empty());
    DAWLAB_CHECK(!editor.keyPressed(key('Q')) && editor.keyPressed(key(juce::KeyPress::upKey))); // nothing to move: still its key
    std::printf("  Ctrl+D, Delete, Ctrl+A, Backspace: the clip is empty\n");

    // Every step was one undo step; undo them all and the clip is back.
    while (rig.session.getHistory().undoCount() > undo0)
        rig.session.undo();
    DAWLAB_CHECK(expect("everything undone:", show(editor), "60@0 64@0 67@0 72@960 74@1920 "));

    // The clip deleted under the editor: nothing happens, nothing breaks.
    editor.selectAll();
    DAWLAB_CHECK(rig.session.removeMidiClip(track, 0));
    DAWLAB_CHECK(editor.getClip() == nullptr && editor.numSelected() == 0 && !editor.transpose(1) && !editor.duplicate() && !editor.move(1) &&
                 !editor.removeSelected());
    std::printf("  the clip deleted: every operation refused\n");
}

} // namespace ex11::check

int main() {
    juce::ScopedJuceInitialiser_GUI juce;
    std::printf("Exercise 11 - edit many notes at once\n");
    ex11::check::run();
    return dawlab::test::summary("l18_ex11");
}
