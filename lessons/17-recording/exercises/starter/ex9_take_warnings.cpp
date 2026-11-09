// Exercise 9 (starter): say what is wrong with a take.
//
// Fill in the TODOs. Until then the self-check prints FAIL (expected). The solution is
// solutions/ex9_take_warnings.cpp. What each function must do is in ../ex9_take_warnings.h.
// Build with -Juce: .\scripts\build.ps1 -Only 17-recording -Juce

#include "../ex9_take_warnings.h"

#include <algorithm>
#include <cmath>
#include <utility>

using namespace dawlab;
using namespace dawlab::app;

namespace ex9 {

TakeReport analyse(const AudioBuffer& audio, int numChannels) {
    // TODO: the peak over all channels (silent when it is 0; peakDb = 20 log10(peak)), and per channel the
    //       runs of samples >= kFullScale or <= -kFullScale of one sign: a run of kClipRun or more is a clip.
    //       A run ends at a sample of the other sign, at a sample below full scale, and at the channel's end.
    (void)audio;
    (void)numChannels;
    return {};
}

juce::String describe(const TakeReport& report, const juce::String& trackName) {
    // TODO: the sentences of the header, joined with "; ". juce::String(peakDb, 1) has one decimal.
    (void)report;
    (void)trackName;
    return {};
}

void TakeWatcher::attach() {
    // TODO: put every clip source on the tracks into seen_. Then replace the recorder's onMessage with a
    //       function that clears warnings_, analyses (1 or 2 channels: the track's format) every source on a track that seen_.insert() says is
    //       new, collects the non-empty describe()s (the track's name: Session::getStripName(trackRef(track)))
    //       and calls the old function (moved into the lambda) with the message - followed by ". " and the
    //       warnings joined with "; " if there are any, and then as an alarm.
    (void)main_;
    (void)seen_;
}

} // namespace ex9

#include "../ex9_check.h"
