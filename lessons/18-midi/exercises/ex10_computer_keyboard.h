#pragma once
// Exercise 10 - The computer keyboard as a MIDI keyboard (optional, JUCE).
//
// Not everyone has a MIDI keyboard at hand. Most DAWs let the letters play notes, laid out like piano keys:
//
//     w e   t y u   o p          the black keys
//    a s d f g h j k l           the white keys: a = C, from C4 (MIDI 60) up to D#5 with p
//
//   z / x   an octave down / up (-3 .. +3)          c / v   velocity 20 less / more (1 .. 127, from 100)
//
// A computer keyboard is not a MIDI keyboard in three ways, and each one is a bug if it is forgotten:
//
//   Key repeat     a held key sends keyPressed() again and again: only the first one is a note on.
//   No key up      JUCE has no "key released" for a key: keyStateChanged() says that SOME key changed, and the
//                  keyboard asks for each held key whether it is still down (isKeyDown, which a check can replace).
//   Focus          a key that goes up while the window is in the background is never seen. When the window loses
//                  the keyboard focus, every held note has to end (allNotesOff()), or it hangs.
//
// And a key that goes up after an octave change ends the note it started, not the one it would start now.
// Keys with Ctrl, Alt or Cmd are the app's shortcuts: not notes. The notes go where a MIDI keyboard's go, without
// changing the app: AudioHost::pushMidi(), with the time of AudioHost::now() - so they play on armed instrument
// tracks and can be recorded.

#include "MainComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

namespace ex10 {

class ComputerKeyboard final : public juce::KeyListener {
public:
    static constexpr const char* kNoteKeys = "awsedftgyhujkolp"; // C4 .. D#5 at octave 0
    static constexpr int kMaxHeld = 16;

    explicit ComputerKeyboard(dawlab::app::AudioHost& host) : host_(host) {}

    // ---- YOUR TURN (starter/ex10_computer_keyboard.cpp) ----------------------------------------------------------
    // A note key: a note on, unless that key is already held (repeat). z x c v: octave and velocity. True for every
    // key it used (also a repeat); false for the rest, and for any key with Ctrl, Alt or Cmd.
    bool keyPressed(const juce::KeyPress& key, juce::Component* origin) override;
    // Every held key that isKeyDown() says is up: its note off. True if a note ended.
    bool keyStateChanged(bool isKeyDown, juce::Component* origin) override;
    // Every held note ends (the window lost the focus).
    void allNotesOff();

    // ---- provided -----------------------------------------------------------------------------------------------
    int getOctave() const noexcept { return octave_; }
    int getVelocity() const noexcept { return velocity_; }
    int getNumHeld() const noexcept { return numHeld_; }
    // Whether a key (by its key code) is down now. A check replaces it.
    std::function<bool(int keyCode)> isKeyDown = [](int keyCode) { return juce::KeyPress::isKeyCurrentlyDown(keyCode); };
    // The index of a key in kNoteKeys (by its character, any case), or -1.
    static int noteIndexOf(const juce::KeyPress& key) noexcept {
        juce::juce_wchar c = key.getTextCharacter();
        if (c == 0)
            c = static_cast<juce::juce_wchar>(key.getKeyCode());
        c = juce::CharacterFunctions::toLowerCase(c);
        for (int i = 0; kNoteKeys[i] != 0; ++i)
            if (c == static_cast<juce::juce_wchar>(kNoteKeys[i]))
                return i;
        return -1;
    }

private:
    void send(const dawlab::engine::MidiMessage& message) { host_.pushMidi({host_.now(), message}); }

    dawlab::app::AudioHost& host_;
    struct Held {
        int keyCode = 0;
        int pitch = 0;
    };
    std::array<Held, kMaxHeld> held_{};
    int numHeld_ = 0;
    int octave_ = 0;
    int velocity_ = 100;
};

} // namespace ex10
