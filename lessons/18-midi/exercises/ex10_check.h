#pragma once
// Exercise 10 - the self-check (main). Included at the end of the starter and of the solution.

#include "app_rig.h"
#include "ex10_computer_keyboard.h"

#include <dawlab/Check.h>

#include <algorithm>
#include <cstdio>
#include <set>
#include <string>

namespace ex10::check {

using namespace ex18rig;
using dawlab::engine::MidiKind;
using dawlab::engine::TimedMidi;

// A computer keyboard the check plays: which keys are down, and the key presses.
struct Hands {
    ComputerKeyboard& keyboard;
    std::set<int> down;

    explicit Hands(ComputerKeyboard& k) : keyboard(k) {
        keyboard.isKeyDown = [this](int code) { return down.count(code) != 0; };
    }
    static juce::KeyPress keyOf(char c, juce::ModifierKeys mods = {}) {
        return juce::KeyPress(juce::CharacterFunctions::toUpperCase(static_cast<juce::juce_wchar>(c)), mods, static_cast<juce::juce_wchar>(c));
    }
    bool press(char c, juce::ModifierKeys mods = {}) {
        down.insert(keyOf(c).getKeyCode());
        return keyboard.keyPressed(keyOf(c, mods), nullptr);
    }
    bool repeat(char c) { return keyboard.keyPressed(keyOf(c), nullptr); }
    bool lift(char c) {
        down.erase(keyOf(c).getKeyCode());
        return keyboard.keyStateChanged(false, nullptr);
    }
};

// What the keyboard sent since the last call: "on60/100 off61 ", from the AudioHost's record FIFO.
std::string sent(Rig& rig, double* time = nullptr) {
    std::string s;
    TimedMidi m;
    while (rig.main->getAudioHost().getRecordFifo().pop(m)) {
        const MidiKind k = m.message.kind();
        s += k == MidiKind::NoteOn ? "on" + std::to_string(m.message.note()) + "/" + std::to_string(m.message.velocity()) + " "
             : k == MidiKind::NoteOff ? "off" + std::to_string(m.message.note()) + " "
                                       : "? ";
        if (time != nullptr)
            *time = m.time;
    }
    return s;
}

bool expect(const char* what, const std::string& got, const std::string& want) {
    const bool ok = got == want;
    std::printf("  %-48s %s\n", what, ok ? "yes" : ("NO: \"" + got + "\", not \"" + want + "\"").c_str());
    return ok;
}

void run() {
    Rig rig;
    const auto track = rig.instrumentTrack();
    rig.main->setArmed(track, true);
    ComputerKeyboard keyboard(rig.main->getAudioHost());
    Hands hands(keyboard);
    sent(rig);

    rig.clock = 1.25;
    DAWLAB_CHECK(hands.press('a'));
    double time = 0.0;
    DAWLAB_CHECK(expect("a: C4 at velocity 100, at the host's time:", sent(rig, &time), "on60/100 ") && time == 1.25);
    DAWLAB_CHECK(hands.repeat('a') && hands.repeat('a'));
    DAWLAB_CHECK(expect("a held: the repeats send nothing:", sent(rig), ""));
    DAWLAB_CHECK(hands.press('w') && hands.press('k') && hands.press('p'));
    DAWLAB_CHECK(expect("w, k and p:", sent(rig), "on61/100 on72/100 on75/100 "));
    DAWLAB_CHECK(!hands.lift('q') && keyboard.getNumHeld() == 4); // no note key went up
    DAWLAB_CHECK(hands.lift('a'));
    DAWLAB_CHECK(expect("a up:", sent(rig), "off60 "));
    // An octave down while w is held: its note ends as it started.
    DAWLAB_CHECK(hands.press('z') && keyboard.getOctave() == -1);
    DAWLAB_CHECK(hands.lift('z') == false);
    DAWLAB_CHECK(hands.press('a'));
    DAWLAB_CHECK(hands.lift('w'));
    DAWLAB_CHECK(expect("z, a, then w up:", sent(rig), "on48/100 off61 "));
    // Velocity, the octave's limits.
    DAWLAB_CHECK(hands.press('v') && hands.press('v') && keyboard.getVelocity() == 127);
    DAWLAB_CHECK(hands.press('c') && keyboard.getVelocity() == 107);
    for (int i = 0; i < 9; ++i)
        hands.press('x');
    DAWLAB_CHECK(keyboard.getOctave() == 3);
    DAWLAB_CHECK(hands.press('j'));
    DAWLAB_CHECK(expect("v v c, the octave up to +3, then j:", sent(rig), "on107/107 "));
    for (int i = 0; i < 9; ++i)
        hands.press('z');
    DAWLAB_CHECK(keyboard.getOctave() == -3);
    // Shortcuts and other keys are not notes.
    DAWLAB_CHECK(!hands.press('s', juce::ModifierKeys::ctrlModifier) && !hands.press('d', juce::ModifierKeys::altModifier) &&
                 !hands.press('q') && !hands.press('1'));
    DAWLAB_CHECK(expect("Ctrl+s, Alt+d, q, 1:", sent(rig), ""));
    hands.down.clear();
    // The window loses the focus with keys held: every note ends.
    rig.clock = 2.0;
    keyboard.allNotesOff();
    std::string ends = sent(rig);
    DAWLAB_CHECK(keyboard.getNumHeld() == 0 && ends.size() > 0);
    std::set<std::string> offs;
    for (size_t at = 0; at < ends.size();) {
        const size_t space = ends.find(' ', at);
        offs.insert(ends.substr(at, space - at));
        at = space + 1;
    }
    const bool four = offs == std::set<std::string>{"off72", "off75", "off48", "off107"} && std::count(ends.begin(), ends.end(), ' ') == 4;
    DAWLAB_CHECK(expect("focus lost with k, p, a and j held:", four ? "their 4 note offs" : ends, "their 4 note offs"));
    DAWLAB_CHECK(!hands.lift('k'));

    // The armed instrument track plays them.
    rig.clock = 3.0;
    hands.press('h');
    rig.play(512 * 20);
    DAWLAB_CHECK(rig.engineTrack(track)->getSynth()->activeVoices() == 1);
    hands.lift('h');
    rig.play(512 * 60);
    DAWLAB_CHECK(rig.engineTrack(track)->getSynth()->activeVoices() == 0);
    std::printf("  the armed instrument track plays h (A at octave -3), and stops when it goes up\n");
}

} // namespace ex10::check

int main() {
    juce::ScopedJuceInitialiser_GUI juce;
    std::printf("Exercise 10 - the computer keyboard as a MIDI keyboard\n");
    ex10::check::run();
    return dawlab::test::summary("l18_ex10");
}
