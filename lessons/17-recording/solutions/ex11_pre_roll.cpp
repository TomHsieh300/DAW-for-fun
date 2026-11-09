// Exercise 11 (solution): pre-roll.
//
// Build and run:  l17_ex11_solution   (ctest, label "solution"; needs -Juce)
// Your version:   exercises/starter/ex11_pre_roll.cpp -> l17_ex11_starter

#include "../exercises/ex11_pre_roll.h"

#include <cmath>

using namespace dawlab;
using namespace dawlab::app;

namespace ex11 {

std::int64_t PreRoll::startFor(std::int64_t position) const {
    const double bar = main_.getEngine().getTransport().getSamplesPerBar();
    return std::max<std::int64_t>(0, position - std::llround(bars_ * bar));
}

bool PreRoll::record() {
    TakeRecorder& recorder = main_.getTakeRecorder();
    engine::Transport& tr = main_.getEngine().getTransport();
    const bool looping = tr.isLoopEnabled() && tr.getLoopEnd() > tr.getLoopStart();
    if (recorder.getPhase() == TakeRecorder::Phase::Recording || bars_ == 0 || looping)
        return main_.toggleRecord();

    const std::int64_t position = tr.getPosition();
    if (recorder.isPunchEnabled() && tr.getLoopEnd() > tr.getLoopStart()) {
        tr.setPosition(startFor(tr.getLoopStart()));
        const bool started = main_.toggleRecord();
        if (!started)
            tr.setPosition(position);
        return started;
    }

    // Lend the punch: the recorder takes its range when it starts.
    const std::int64_t loopStart = tr.getLoopStart(), loopEnd = tr.getLoopEnd();
    tr.setLoopRange(position, position + kOpenEnd);
    recorder.setPunchEnabled(true);
    tr.setPosition(startFor(position));
    const bool started = main_.toggleRecord();
    tr.setLoopRange(loopStart, loopEnd);
    recorder.setPunchEnabled(false);
    if (!started)
        tr.setPosition(position);
    return started;
}

void PreRoll::addToMenu(juce::PopupMenu& menu) {
    const char* names[] = {"No Pre-roll", "Pre-roll 1 Bar", "Pre-roll 2 Bars"};
    for (int bars = 0; bars <= kMaxBars; ++bars)
        menu.addItem(juce::PopupMenu::Item(names[bars]).setTicked(bars_ == bars).setAction([this, bars] { setBars(bars); }));
}

} // namespace ex11

#include "../exercises/ex11_check.h"
