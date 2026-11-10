// Exercise 10 (solution): the computer keyboard as a MIDI keyboard.
//
// Build and run:  l18_ex10_solution   (ctest, label "solution"; needs -Juce)
// Your version:   exercises/starter/ex10_computer_keyboard.cpp -> l18_ex10_starter

#include "../exercises/ex10_computer_keyboard.h"

namespace ex10 {

using dawlab::engine::MidiMessage;

bool ComputerKeyboard::keyPressed(const juce::KeyPress& key, juce::Component*) {
    const juce::ModifierKeys mods = key.getModifiers();
    if (mods.isCtrlDown() || mods.isAltDown() || mods.isCommandDown())
        return false; // the app's shortcuts
    const int index = noteIndexOf(key);
    if (index >= 0) {
        for (int i = 0; i < numHeld_; ++i)
            if (held_[static_cast<size_t>(i)].keyCode == key.getKeyCode())
                return true; // a repeat
        const int pitch = 60 + 12 * octave_ + index;
        if (pitch > 127 || numHeld_ == kMaxHeld)
            return true;
        held_[static_cast<size_t>(numHeld_++)] = {key.getKeyCode(), pitch};
        send(MidiMessage::noteOn(0, pitch, velocity_));
        return true;
    }
    juce::juce_wchar c = key.getTextCharacter();
    if (c == 0)
        c = static_cast<juce::juce_wchar>(key.getKeyCode());
    switch (juce::CharacterFunctions::toLowerCase(c)) {
    case 'z': octave_ = std::max(octave_ - 1, -3); return true;
    case 'x': octave_ = std::min(octave_ + 1, 3); return true;
    case 'c': velocity_ = std::max(velocity_ - 20, 1); return true;
    case 'v': velocity_ = std::min(velocity_ + 20, 127); return true;
    default: return false;
    }
}

bool ComputerKeyboard::keyStateChanged(bool, juce::Component*) {
    bool ended = false;
    for (int i = 0; i < numHeld_;) {
        const Held h = held_[static_cast<size_t>(i)];
        if (isKeyDown(h.keyCode)) {
            ++i;
            continue;
        }
        send(MidiMessage::noteOff(0, h.pitch)); // the pitch it started, whatever the octave is now
        for (int j = i + 1; j < numHeld_; ++j)
            held_[static_cast<size_t>(j - 1)] = held_[static_cast<size_t>(j)];
        --numHeld_;
        ended = true;
    }
    return ended;
}

void ComputerKeyboard::allNotesOff() {
    for (int i = 0; i < numHeld_; ++i)
        send(MidiMessage::noteOff(0, held_[static_cast<size_t>(i)].pitch));
    numHeld_ = 0;
}

} // namespace ex10

#include "../exercises/ex10_check.h"
