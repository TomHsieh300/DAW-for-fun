// Exercise 8 (starter): a "Measure Latency" button.
//
// Fill in the TODOs. Until then the self-check prints FAIL (expected). The solution is
// solutions/ex8_latency_button.cpp. What each function must do is in ../ex8_latency_button.h.
// Build with -Juce: .\scripts\build.ps1 -Only 17-recording -Juce

#include "../ex8_latency_button.h"

using namespace dawlab;
using namespace dawlab::app;

namespace ex8 {

Result LatencyMeter::measure() {
    // TODO: refuse while main_.getTakeRecorder().isBusy(). Otherwise play makeProbe(sample rate) through
    //       device_, collect the heard lags of chirpLags(input, probe, kMaxLagSeconds * rate), sort them:
    //       roundTrip = the middle one (index size / 2), spread = last - first, trim = roundTrip minus the
    //       reported input and output latencies. Then the four cases of the header; only the last one sets
    //       the trim (after remembering the old one). The message goes to main_.setStatus() too.
    Result result;
    result.message = "not measured yet";
    return result;
}

bool LatencyMeter::revert() {
    // TODO: if there is a trim to go back to and the recorder is idle: set it, forget it, true.
    (void)previousTrim_;
    (void)canRevert_;
    return false;
}

void LatencyMeter::addToMenu(juce::PopupMenu& menu) {
    // TODO: two juce::PopupMenu::Item with setEnabled() and setAction(): "Measure Latency..." and
    //       "Undo Latency Measurement".
    (void)menu;
}

} // namespace ex8

#include "../ex8_check.h"
