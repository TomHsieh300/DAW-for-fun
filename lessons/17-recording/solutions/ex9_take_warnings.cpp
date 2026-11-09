// Exercise 9 (solution): say what is wrong with a take.
//
// Build and run:  l17_ex9_solution   (ctest, label "solution"; needs -Juce)
// Your version:   exercises/starter/ex9_take_warnings.cpp -> l17_ex9_starter

#include "../exercises/ex9_take_warnings.h"

#include <algorithm>
#include <cmath>
#include <utility>

using namespace dawlab;
using namespace dawlab::app;

namespace ex9 {

TakeReport analyse(const AudioBuffer& audio, int numChannels) {
    TakeReport report;
    float peak = 0.0f;
    for (int ch = 0; ch < std::min(numChannels, audio.getNumChannels()); ++ch) {
        const float* x = audio.getReadPointer(ch);
        int run = 0; // > 0: that many full-scale samples in a row, positive; < 0: negative
        auto endRun = [&report, &run] {
            const int length = std::abs(run);
            if (length >= TakeReport::kClipRun) {
                ++report.clips;
                report.longestClip = std::max(report.longestClip, length);
            }
            run = 0;
        };
        for (int i = 0; i < audio.getNumSamples(); ++i) {
            peak = std::max(peak, std::abs(x[i]));
            const int sign = x[i] >= TakeReport::kFullScale ? 1 : (x[i] <= -TakeReport::kFullScale ? -1 : 0);
            if (sign == 0 || (run != 0 && (run > 0) != (sign > 0)))
                endRun();
            run += sign;
        }
        endRun(); // a run at the very end counts too; the next channel starts afresh
    }
    report.silent = peak == 0.0f;
    report.peakDb = report.silent ? -999.0f : 20.0f * std::log10(peak);
    return report;
}

juce::String describe(const TakeReport& report, const juce::String& trackName) {
    juce::StringArray sentences;
    if (report.silent)
        sentences.add(trackName + ": the take is exactly 0 - is the input muted?");
    if (report.quiet())
        sentences.add(trackName + ": the take is very quiet (peak " + juce::String(report.peakDb, 1) + " dBFS) - turn the input gain up");
    if (report.clips > 0)
        sentences.add(trackName + ": the take clipped " + juce::String(report.clips) + (report.clips == 1 ? " time" : " times") +
                      " (the longest " + juce::String(report.longestClip) + " samples) - turn the input gain down");
    return sentences.joinIntoString("; ");
}

void TakeWatcher::attach() {
    Session& session = main_.getSession();
    for (int t = 0; t < session.getNumTracks(); ++t)
        for (const engine::Clip& clip : session.getClips(session.getTrackId(t)))
            seen_.insert(clip.source);
    TakeRecorder& recorder = main_.getTakeRecorder();
    recorder.onMessage = [this, previous = std::move(recorder.onMessage)](const juce::String& message, bool alarm) {
        warnings_.clear();
        Session& s = main_.getSession();
        for (int t = 0; t < s.getNumTracks(); ++t) {
            const engine::TrackId track = s.getTrackId(t);
            const int channels = s.getTrackFormat(track) == engine::TrackFormat::Stereo ? 2 : 1;
            for (const engine::Clip& clip : s.getClips(track))
                if (clip.source != nullptr && seen_.insert(clip.source).second)
                    if (const juce::String said = describe(analyse(*clip.source, channels), s.getStripName(s.trackRef(track))); said.isNotEmpty())
                        warnings_.add(said);
        }
        if (previous)
            previous(warnings_.isEmpty() ? message : message + ". " + warnings_.joinIntoString("; "), alarm || !warnings_.isEmpty());
    };
}

} // namespace ex9

#include "../exercises/ex9_check.h"
