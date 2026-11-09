#pragma once
// Exercise 9 - Say what is wrong with a take (optional, JUCE).
//
// The first real recording of DawLab 17 was three seconds of exact zeros, and the app said "Recorded 1
// take" (Step 7, "On a real device"). Exercise 5 finds such things on the disk thread, while the take is
// written. This one is the cheaper half that needs no change to the recorder: look at each take once it is
// on the timeline, and add to what the recorder says. Without changing the app:
//
//   analyse(audio, channels)  what is wrong with a recording: exact zeros throughout, a peak below
//                             -50 dBFS, clipping (3 or more samples in a row at |x| >= 0.999, of one sign,
//                             on one channel - Exercise 5's rule)
//   describe(report, name)    one sentence per problem, for the status line
//   TakeWatcher::attach()     wraps the recorder's onMessage: every recording that is new on a track is
//                             analysed, once, and if anything is wrong the app's handler is given
//                             "<the recorder's message>. <sentence>; <sentence>" as an alarm (it puts it on
//                             the status line, in red); otherwise the message as it came
//
// "New": a clip source that was on no track when attach() ran, and that has not been looked at since. A
// loop recording is one source with several takes (Step 4): one report. Undo and redo bring back a
// source that was already looked at: nothing is said again.

#include "MainComponent.h"

#include <dawlab/AudioBuffer.h>

#include <cstdint>
#include <memory>
#include <set>
#include <vector>

namespace ex9 {

struct TakeReport {
    bool silent = false;    // every sample of every channel is exactly 0
    float peakDb = -999.0f; // the largest |sample|, in dBFS (-999 for silence)
    int clips = 0;          // runs of kClipRun or more full-scale samples of one sign, on one channel
    int longestClip = 0;    // the longest such run, in samples

    bool quiet() const noexcept { return !silent && peakDb < kQuietDb; }
    bool anythingWrong() const noexcept { return silent || quiet() || clips > 0; }

    static constexpr float kFullScale = 0.999f;
    static constexpr int kClipRun = 3;
    static constexpr float kQuietDb = -50.0f;
};

// ---- YOUR TURN (starter/ex9_take_warnings.cpp) -------------------------------------------------------
// The whole buffer, its first `numChannels` channels: a mono track's take is loaded as two equal channels
// (the engine is stereo), and its clips are counted once.
TakeReport analyse(const dawlab::AudioBuffer& audio, int numChannels);
// Empty if nothing is wrong. Otherwise one sentence per problem, joined with "; ", each starting with the
// track's name:
//   silent    "<name>: the take is exactly 0 - is the input muted?"
//   quiet     "<name>: the take is very quiet (peak -64.4 dBFS) - turn the input gain up"
//   clipped   "<name>: the take clipped 2 times (the longest 5 samples) - turn the input gain down"
//             ("1 time" for one)
juce::String describe(const TakeReport& report, const juce::String& trackName);

class TakeWatcher {
public:
    explicit TakeWatcher(dawlab::app::MainComponent& main) : main_(main) {}

    // ---- YOUR TURN --------------------------------------------------------------------------------
    // Remembers every source on the tracks now, and wraps the recorder's onMessage (keep the old function:
    // it is the app's). On each message: every source on a track that is new is analysed (one channel on a
    // mono track, two on a stereo one: Session::getTrackFormat), and the sentences
    // of all of them (tracks in display order) go after the message, as above, before the old function gets
    // it. They are also what lastWarnings() returns until the next message; a message with nothing new
    // clears it.
    void attach();

    // ---- provided ---------------------------------------------------------------------------------
    const juce::StringArray& lastWarnings() const noexcept { return warnings_; }

private:
    dawlab::app::MainComponent& main_;
    std::set<std::shared_ptr<const dawlab::AudioBuffer>> seen_;
    juce::StringArray warnings_;
};

} // namespace ex9
