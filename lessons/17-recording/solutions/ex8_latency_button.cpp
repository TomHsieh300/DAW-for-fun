// Exercise 8 (solution): a "Measure Latency" button.
//
// Build and run:  l17_ex8_solution   (ctest, label "solution"; needs -Juce)
// Your version:   exercises/starter/ex8_latency_button.cpp -> l17_ex8_starter

#include "../exercises/ex8_latency_button.h"

using namespace dawlab;
using namespace dawlab::app;

namespace ex8 {

Result LatencyMeter::measure() {
    Result result;
    TakeRecorder& recorder = main_.getTakeRecorder();
    if (recorder.isBusy()) {
        result.message = "Not while recording: the measurement needs the device to itself";
        main_.setStatus(result.message);
        return result;
    }
    const double rate = main_.getSession().getSampleRate();
    const Probe probe = makeProbe(rate);
    const RoundTrip back = device_(probe.signal);
    std::vector<int> heard;
    for (const ChirpLag& c : chirpLags(back.input, probe, static_cast<int>(kMaxLagSeconds * rate)))
        if (c.heard)
            heard.push_back(c.lag);
    result.heard = static_cast<int>(heard.size());
    if (heard.empty()) {
        result.message = "Latency: heard none of the chirps. Is the input muted, or the volume down?";
    } else {
        std::sort(heard.begin(), heard.end());
        result.roundTrip = heard[heard.size() / 2];
        result.spread = heard.back() - heard.front();
        result.trim = result.roundTrip - back.reported.input - back.reported.output;
        const juce::String kept = ". The trim stays " + juce::String(recorder.getTrimSamples()) + " samples";
        if (4 * result.heard < 3 * Probe::kChirps) {
            result.message = "Latency: heard only " + juce::String(result.heard) + " of " + juce::String(Probe::kChirps) +
                             " chirps: too quiet or too noisy to trust" + kept;
        } else if (result.spread > static_cast<int>(rate / 1000.0)) {
            result.message = "Latency: it moves, by " + juce::String(1000.0 * result.spread / rate, 1) +
                             " ms within one test. A trim cannot fix that: try without the system's voice processing, or another input" + kept;
        } else {
            previousTrim_ = recorder.getTrimSamples();
            canRevert_ = true;
            recorder.setTrimSamples(result.trim);
            result.applied = true;
            result.message = "Latency trim: " + juce::String(result.trim) + " samples (the round trip is " +
                             juce::String(1000.0 * result.roundTrip / rate, 1) + " ms)";
        }
    }
    main_.setStatus(result.message);
    return result;
}

bool LatencyMeter::revert() {
    TakeRecorder& recorder = main_.getTakeRecorder();
    if (!canRevert_ || recorder.isBusy())
        return false;
    recorder.setTrimSamples(previousTrim_);
    canRevert_ = false;
    main_.setStatus("Latency trim back to " + juce::String(previousTrim_) + " samples");
    return true;
}

void LatencyMeter::addToMenu(juce::PopupMenu& menu) {
    const bool idle = !main_.getTakeRecorder().isBusy();
    menu.addItem(juce::PopupMenu::Item("Measure Latency...").setEnabled(idle).setAction([this] { measure(); }));
    menu.addItem(juce::PopupMenu::Item("Undo Latency Measurement").setEnabled(idle && canRevert_).setAction([this] { revert(); }));
}

} // namespace ex8

#include "../exercises/ex8_check.h"
