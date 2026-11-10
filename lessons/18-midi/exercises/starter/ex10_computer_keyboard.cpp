// Exercise 10 (starter): the computer keyboard as a MIDI keyboard.
//
// Fill in the TODOs. Until then the self-check prints FAIL (expected). The solution is
// solutions/ex10_computer_keyboard.cpp. What the functions must do is in ../ex10_computer_keyboard.h.

#include "../ex10_computer_keyboard.h"

namespace ex10 {

bool ComputerKeyboard::keyPressed(const juce::KeyPress& key, juce::Component*) {
    // TODO: modifiers -> false. A note key (noteIndexOf): a repeat of a held key -> true and nothing; otherwise a
    //       note on at 60 + 12 x octave + index, remembered with its key code. z x c v; anything else -> false.
    (void)key;
    return false;
}

bool ComputerKeyboard::keyStateChanged(bool, juce::Component*) {
    // TODO: every held key that isKeyDown() says is up: a note off for the pitch it started.
    return false;
}

void ComputerKeyboard::allNotesOff() {
    // TODO: a note off for every held key.
}

} // namespace ex10

#include "../ex10_check.h"
