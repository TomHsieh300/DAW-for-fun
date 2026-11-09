#pragma once
// Exercise 11 - Pre-roll (optional, JUCE).
//
// Press Record in DawLab 17 and the recording starts where the playhead is, at once. A player needs a bar or
// two to come in: pre-roll starts the transport earlier and records only from the playhead on. DawLab 17 has
// what it takes - a punch records exactly between two points, wherever the blocks fall (Step 3), and keeps
// the frames before the punch-in in the file - but its punch points are the loop's locators. Without changing
// the app:
//
//   PreRoll::startFor(position)   where the transport starts: `bars` bars before, not before 0
//   PreRoll::record()             the Record button, with pre-roll:
//       recording now                stop it (MainComponent::toggleRecord())
//       no pre-roll, or the loop on   just toggleRecord(): a loop's passes are their own pre-roll
//       punch on                      the transport starts `bars` before the punch-in (the loop start), and
//                                     the punch does the rest
//       otherwise                     the punch is lent for a moment: the locators go to [position,
//                                     position + kOpenEnd), punch on, the transport to startFor(position),
//                                     toggleRecord() - and the locators and the punch switch go back at once.
//                                     TakeRecorder::start() takes the range when it starts, so what it
//                                     records is not changed by that. If the recording does not start, the
//                                     playhead goes back too: nothing has changed.
//   PreRoll::addToMenu(menu)       "No Pre-roll", "Pre-roll 1 Bar", "Pre-roll 2 Bars", the one in use ticked

#include "MainComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cstdint>

namespace ex11 {

class PreRoll {
public:
    static constexpr std::int64_t kOpenEnd = std::int64_t{1} << 40; // a punch-out that is never reached
    static constexpr int kMaxBars = 2;

    explicit PreRoll(dawlab::app::MainComponent& main) : main_(main) {}

    // ---- provided -----------------------------------------------------------------------------------
    void setBars(int bars) noexcept { bars_ = std::clamp(bars, 0, kMaxBars); }
    int getBars() const noexcept { return bars_; }

    // ---- YOUR TURN (starter/ex11_pre_roll.cpp) --------------------------------------------------------
    // position - bars x Transport::getSamplesPerBar() (rounded to the nearest sample), at least 0.
    std::int64_t startFor(std::int64_t position) const;
    // As above. Returns what toggleRecord() returned.
    bool record();
    // Three items whose actions setBars(0), (1), (2).
    void addToMenu(juce::PopupMenu& menu);

private:
    dawlab::app::MainComponent& main_;
    int bars_ = 1;
};

} // namespace ex11
