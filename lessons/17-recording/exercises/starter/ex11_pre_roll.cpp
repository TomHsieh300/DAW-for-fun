// Exercise 11 (starter): pre-roll.
//
// Fill in the TODOs. Until then the self-check prints FAIL (expected). The solution is
// solutions/ex11_pre_roll.cpp. What each function must do is in ../ex11_pre_roll.h.
// Build with -Juce: .\scripts\build.ps1 -Only 17-recording -Juce

#include "../ex11_pre_roll.h"

#include <cmath>

using namespace dawlab;
using namespace dawlab::app;

namespace ex11 {

std::int64_t PreRoll::startFor(std::int64_t position) const {
    // TODO: position minus bars_ bars (main_.getEngine().getTransport().getSamplesPerBar(), std::llround), >= 0.
    return position;
}

bool PreRoll::record() {
    // TODO: the four cases of the header. Remember the position, the locators and the punch switch before
    //       you change them; give the locators and the switch back right after toggleRecord(), and the
    //       position too if it returned false.
    return main_.toggleRecord();
}

void PreRoll::addToMenu(juce::PopupMenu& menu) {
    // TODO: three juce::PopupMenu::Item, ticked for the bars in use, whose actions call setBars().
    (void)menu;
}

} // namespace ex11

#include "../ex11_check.h"
