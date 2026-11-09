#pragma once
// Exercise 9 - the self-check (main). Included at the end of the starter and of the solution.

#include "ex9_take_warnings.h"

#include "app_rig.h"

#include <dawlab/Check.h>

#include <cstdio>

namespace ex9::check {

using namespace dawlab;
using namespace dawlab::app;
using ex17rig::Rig;

inline TakeReport analyseAll(const AudioBuffer& b) { return analyse(b, b.getNumChannels()); }

inline AudioBuffer bufferOf(std::vector<std::vector<float>> channels) {
    AudioBuffer b(static_cast<int>(channels.size()), static_cast<int>(channels[0].size()));
    for (size_t ch = 0; ch < channels.size(); ++ch)
        for (size_t i = 0; i < channels[ch].size(); ++i)
            b.setSample(static_cast<int>(ch), static_cast<int>(i), channels[ch][i]);
    return b;
}

inline void analyseAndDescribe() {
    // Silence, and the opposite of it.
    const TakeReport zeros = analyseAll(bufferOf({{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}}));
    DAWLAB_CHECK(zeros.silent && zeros.clips == 0 && !zeros.quiet() && zeros.anythingWrong());
    DAWLAB_CHECK(zeros.peakDb == -999.0f); // not log10(0)
    DAWLAB_CHECK(describe(zeros, "Vocal") == "Vocal: the take is exactly 0 - is the input muted?");
    const TakeReport tiny = analyseAll(bufferOf({{0.0f, 1e-30f, 0.0f}}));
    DAWLAB_CHECK(!tiny.silent && tiny.quiet());
    // -64.4 dBFS: what the first real take peaked at after unmuting (Step 7).
    const TakeReport quiet = analyseAll(bufferOf({{0.0f, 0.0006f, -0.0002f}, {0.0f, -0.0003f, 0.0f}}));
    DAWLAB_CHECK(!quiet.silent && quiet.quiet());
    DAWLAB_CHECK_NEAR(quiet.peakDb, -64.437, 0.01);
    DAWLAB_CHECK(describe(quiet, "Vocal") == "Vocal: the take is very quiet (peak -64.4 dBFS) - turn the input gain up");
    // -50 dBFS itself is not too quiet.
    DAWLAB_CHECK(!analyseAll(bufferOf({{0.0031623f}})).quiet() && analyseAll(bufferOf({{0.0031622f}})).quiet());
    const TakeReport fine = analyseAll(bufferOf({{0.5f, -0.5f, 0.999f, 0.999f}}));
    DAWLAB_CHECK(!fine.anythingWrong() && fine.clips == 0 && describe(fine, "Vocal").isEmpty());

    // Clips: three in a row of one sign, on one channel. A run at the very end counts; a run does not go
    // on from the end of one channel into the next.
    const TakeReport clipped = analyseAll(bufferOf({{0.0f, 1.0f, 0.999f, 1.0f, 0.0f, -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, -1.0f, -1.0f, 0.99f, 1.0f, 1.0f},
                                                 {1.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.999f, -1.0f, -1.0f}}));
    DAWLAB_CHECK(clipped.clips == 3 && clipped.longestClip == 4);
    DAWLAB_CHECK(describe(clipped, "Gtr") == "Gtr: the take clipped 3 times (the longest 4 samples) - turn the input gain down");
    const TakeReport once = analyseAll(bufferOf({{0.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, 0.0f}}));
    DAWLAB_CHECK(once.clips == 1 && once.longestClip == 5);
    DAWLAB_CHECK(describe(once, "Gtr") == "Gtr: the take clipped 1 time (the longest 5 samples) - turn the input gain down");
    const TakeReport tail = analyseAll(bufferOf({{0.0f, 1.0f, 1.0f, 1.0f}}));
    DAWLAB_CHECK(tail.clips == 1 && tail.longestClip == 3);
    const TakeReport across = analyseAll(bufferOf({{0.0f, 0.0f, 1.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 0.0f}}));
    DAWLAB_CHECK(across.clips == 0);
    // A mono take: one channel of the two.
    const AudioBuffer mono = bufferOf({{0.0f, 1.0f, 1.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 1.0f, 1.0f, 0.0f}});
    DAWLAB_CHECK(analyse(mono, 1).clips == 1 && analyse(mono, 2).clips == 2);
    DAWLAB_CHECK(analyse(bufferOf({{0.0f, 0.0f}, {0.5f, 0.0f}}), 1).silent);
}

inline void inTheApp() {
    Rig rig;
    MainComponent& main = *rig.main;
    Session& session = rig.session;
    const engine::TrackId a = rig.track(0), b = rig.track(1), c = rig.track(2);
    const juce::String nameA = session.getStripName(session.trackRef(a));
    const juce::String nameB = session.getStripName(session.trackRef(b));

    // A silent clip that was there before: not a take of ours.
    session.setClips(c, {engine::Clip::fromBuffer(std::make_shared<AudioBuffer>(1, 4800), 0, "old")});

    TakeWatcher watcher(main);
    // Stands in for the app's handler (the status line may be showing the CPU meter's warning by now).
    int appHandler = 0;
    juce::String appSaid;
    bool appAlarm = false;
    rig.recorder.onMessage = [&appHandler, &appSaid, &appAlarm](const juce::String& message, bool alarm) {
        ++appHandler;
        appSaid = message;
        appAlarm = alarm;
    };
    watcher.attach();
    DAWLAB_CHECK(rig.recorder.onMessage != nullptr);

    // Track A, a muted microphone.
    const ex17rig::InputFn zeros = [](int, std::int64_t) { return 0.0f; };
    DAWLAB_CHECK(main.setArmed(a, true));
    rig.engine.getTransport().setPosition(48000);
    DAWLAB_CHECK(main.toggleRecord());
    rig.play(24000, zeros);
    rig.stop(zeros);
    DAWLAB_CHECK(rig.waitForTakes());
    DAWLAB_CHECK(watcher.lastWarnings().size() == 1);
    DAWLAB_CHECK(watcher.lastWarnings().joinIntoString("|") == nameA + ": the take is exactly 0 - is the input muted?");
    DAWLAB_CHECK(appHandler == 1 && appAlarm);
    DAWLAB_CHECK(appSaid == "Recorded 1 take. " + nameA + ": the take is exactly 0 - is the input muted?");
    std::printf("  the status line: %s\n", appSaid.toRawUTF8());

    // Undo and redo bring the same recording back: it is not new.
    DAWLAB_CHECK(main.undo() && main.redo());
    // A good take now: nothing to say about it, nor again about the first.
    rig.engine.getTransport().setPosition(144000);
    DAWLAB_CHECK(main.toggleRecord());
    rig.play(24000, [](int, std::int64_t f) { return ex17rig::inputAt(f); });
    rig.stop(zeros);
    DAWLAB_CHECK(rig.waitForTakes());
    DAWLAB_CHECK(appHandler == 2 && watcher.lastWarnings().isEmpty());
    DAWLAB_CHECK(appSaid == "Recorded 1 take" && !appAlarm);
    // Three tracks at once: A stereo (inputs 1 and 2), B mono on input 2, C mono on input 1. Input 1 is
    // quiet, input 2 clips; the take on a mono track is counted once.
    session.setTrackFormat(a, engine::TrackFormat::Stereo);
    rig.engine.applyPendingCommandsOffline();
    DAWLAB_CHECK(main.setArmed(b, true));
    rig.engine.getTrack(b)->setInputChannel(1);
    DAWLAB_CHECK(main.setArmed(c, true));
    const juce::String nameC = session.getStripName(session.trackRef(c));
    rig.engine.getTransport().setPosition(240000);
    const std::int64_t r0 = rig.rendered;
    const ex17rig::InputFn twoInputs = [r0](int ch, std::int64_t frame) {
        const std::int64_t i = frame - r0;
        if (ch == 0)
            return i == 500 ? 0.0006f : 0.0001f;
        if ((i >= 1000 && i < 1003) || (i >= 5000 && i < 5007))
            return 1.0f; // two clips, of 3 and 7
        if (i >= 2000 && i < 2002)
            return -1.0f; // two: a peak, not a clip
        if (i == 3000)
            return 1.0f;
        if (i > 3000 && i < 3005)
            return -0.999f; // after one at +1: a clip of 4
        if (i >= 4000 && i < 4005)
            return 0.998f; // below full scale
        return 0.25f;
    };
    DAWLAB_CHECK(main.toggleRecord());
    rig.play(24000, twoInputs);
    rig.stop(twoInputs);
    DAWLAB_CHECK(rig.waitForTakes());
    DAWLAB_CHECK(watcher.lastWarnings().size() == 3);
    if (watcher.lastWarnings().size() == 3) {
        DAWLAB_CHECK(watcher.lastWarnings()[0] == nameA + ": the take clipped 3 times (the longest 7 samples) - turn the input gain down");
        DAWLAB_CHECK(watcher.lastWarnings()[1] == nameB + ": the take clipped 3 times (the longest 7 samples) - turn the input gain down");
        DAWLAB_CHECK(watcher.lastWarnings()[2] == nameC + ": the take is very quiet (peak -64.4 dBFS) - turn the input gain up");
    }
    DAWLAB_CHECK(appSaid == "Recorded 3 takes. " + watcher.lastWarnings().joinIntoString("; ") && appAlarm);
    std::printf("  the status line: %s\n", appSaid.toRawUTF8());

    // A loop of 3.5 passes, silent: one recording, one sentence.
    DAWLAB_CHECK(main.setArmed(b, false) && main.setArmed(c, false));
    engine::Transport& tr = rig.engine.getTransport();
    tr.setLoopRange(480000, 528000);
    tr.setLoopEnabled(true);
    tr.setPosition(480000);
    DAWLAB_CHECK(main.toggleRecord());
    rig.play(rig.engine.getLatencySamples() + 48000 * 3 + 24000, zeros);
    rig.stop(zeros);
    DAWLAB_CHECK(rig.waitForTakes());
    DAWLAB_CHECK(appSaid.startsWith("Recorded 4 takes. "));
    DAWLAB_CHECK(watcher.lastWarnings().size() == 1);
}

} // namespace ex9::check

int main() {
    const juce::ScopedJuceInitialiser_GUI juceInit;
    std::printf("Exercise 9 - say what is wrong with a take\n");
    ex9::check::analyseAndDescribe();
    ex9::check::inTheApp();
    return dawlab::test::summary("l17_ex9");
}
