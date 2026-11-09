#pragma once
// Exercise 11 - the self-check (main). Included at the end of the starter and of the solution.

#include "ex11_pre_roll.h"

#include "app_rig.h"

#include <dawlab/Check.h>

#include <cstdio>

namespace ex11::check {

using namespace dawlab;
using namespace dawlab::app;
using ex17rig::Rig;

inline void run() {
    Rig rig;
    MainComponent& main = *rig.main;
    Session& session = rig.session;
    engine::Transport& tr = rig.engine.getTransport();
    TakeRecorder& recorder = rig.recorder;
    const engine::TrackId track = rig.track(0);
    const ex17rig::InputFn input = [](int, std::int64_t f) { return ex17rig::inputAt(f); };
    PreRoll preRoll(main);

    // Where the transport starts: whole bars of the tempo and the meter, never before 0.
    preRoll.setBars(1);
    tr.setTempo(60.0);
    tr.setTimeSignature(3, 4);
    DAWLAB_CHECK(preRoll.startFor(480000) == 336000);
    tr.setTempo(133.0);
    tr.setTimeSignature(4, 4);
    DAWLAB_CHECK(preRoll.startFor(200000) == 200000 - 86617); // 86616.54 samples a bar
    tr.setTempo(120.0);
    preRoll.setBars(2);
    DAWLAB_CHECK(preRoll.startFor(288000) == 96000);
    DAWLAB_CHECK(preRoll.startFor(150000) == 0);
    DAWLAB_CHECK(preRoll.startFor(0) == 0);

    // An old take under the new one, the user's locators somewhere else, no punch.
    auto old = std::make_shared<AudioBuffer>(1, 480000);
    for (int i = 0; i < 480000; ++i)
        old->setSample(0, i, 0.25f);
    session.setClips(track, {engine::Clip::fromBuffer(old, 0, "old")});
    DAWLAB_CHECK(main.setArmed(track, true));
    tr.setLoopRange(48000, 72000);
    tr.setLoopEnabled(false);
    tr.setPosition(288000);

    // Two bars of pre-roll: the transport starts at bar 2, the take at bar 4.
    const std::int64_t r0 = rig.rendered;
    DAWLAB_CHECK(preRoll.record());
    DAWLAB_CHECK(tr.getPosition() == 96000);
    DAWLAB_CHECK(tr.getLoopStart() == 48000 && tr.getLoopEnd() == 72000 && !recorder.isPunchEnabled()); // given back at once
    DAWLAB_CHECK(recorder.getPhase() == TakeRecorder::Phase::Recording);
    const std::int64_t latency = rig.engine.getLatencySamples();
    rig.play(2 * 96000 + 24000 + latency, input);
    rig.stop(input);
    DAWLAB_CHECK(rig.waitForTakes());
    const auto& clips = session.getClips(track);
    DAWLAB_CHECK(clips.size() == 3);
    if (clips.size() == 3) {
        const engine::Clip& take = clips[1];
        DAWLAB_CHECK(clips[0].source == old && clips[0].start == 0 && clips[0].end() == 288000); // the old take up to there
        DAWLAB_CHECK(take.start == 288000 && take.length >= 24000);
        // The pre-roll is in the file, in front of the take: drag the clip's start to hear it.
        DAWLAB_CHECK(take.sourceOffset == 288000 - 96000 + latency);
        DAWLAB_CHECK(take.source->getSample(0, static_cast<int>(take.sourceOffset)) == ex17rig::inputAt(r0 + take.sourceOffset));
        DAWLAB_CHECK(clips[2].source == old && clips[2].start == take.end());
        std::printf("  2 bars of pre-roll from 96000: the take starts at %lld, %lld frames into its file\n", static_cast<long long>(take.start),
                    static_cast<long long>(take.sourceOffset));
    }
    DAWLAB_CHECK(main.undo() && session.getClips(track).size() == 1); // one undo step

    // With the punch on, the pre-roll is before the punch-in, and the punch does the rest.
    preRoll.setBars(1);
    recorder.setPunchEnabled(true);
    tr.setLoopRange(480000, 528000);
    tr.setPosition(700000);
    DAWLAB_CHECK(preRoll.record());
    DAWLAB_CHECK(tr.getPosition() == 384000);
    DAWLAB_CHECK(tr.getLoopStart() == 480000 && tr.getLoopEnd() == 528000 && recorder.isPunchEnabled());
    rig.play(96000 + 48000 + latency + 4800, input);
    rig.stop(input);
    DAWLAB_CHECK(rig.waitForTakes());
    {
        const auto& punched = session.getClips(track);
        DAWLAB_CHECK(punched.size() == 2 && punched.back().start == 480000 && punched.back().length == 48000);
    }
    recorder.setPunchEnabled(false);

    // A loop is its own pre-roll; and without bars, there is none. Pressed again, it stops.
    tr.setLoopRange(600000, 648000);
    tr.setLoopEnabled(true);
    tr.setPosition(600000);
    DAWLAB_CHECK(preRoll.record());
    DAWLAB_CHECK(tr.getPosition() == 600000 && tr.getLoopStart() == 600000 && !recorder.isPunchEnabled());
    rig.play(4800, input);
    DAWLAB_CHECK(preRoll.record());
    rig.play(512, input);
    DAWLAB_CHECK(rig.waitForTakes() && tr.isPlaying()); // the recording ended, the song plays on
    tr.setLoopEnabled(false);
    preRoll.setBars(0);
    tr.setPosition(800000);
    DAWLAB_CHECK(preRoll.record());
    DAWLAB_CHECK(tr.getPosition() == 800000);
    rig.play(4800, input);
    DAWLAB_CHECK(preRoll.record());
    rig.stop(input);
    DAWLAB_CHECK(rig.waitForTakes());
    // No pre-roll with the punch on: the playhead stays where it is, too.
    recorder.setPunchEnabled(true);
    tr.setLoopRange(900000, 948000);
    tr.setPosition(850000);
    DAWLAB_CHECK(preRoll.record());
    DAWLAB_CHECK(tr.getPosition() == 850000);
    rig.play(4800, input);
    DAWLAB_CHECK(preRoll.record());
    rig.stop(input);
    recorder.setPunchEnabled(false);
    DAWLAB_CHECK(rig.waitForTakes());

    // Nothing armed: nothing starts, and nothing has changed - neither way.
    DAWLAB_CHECK(main.setArmed(track, false));
    preRoll.setBars(2);
    tr.setLoopRange(48000, 72000);
    tr.setPosition(300000);
    DAWLAB_CHECK(!preRoll.record());
    DAWLAB_CHECK(tr.getPosition() == 300000 && tr.getLoopStart() == 48000 && tr.getLoopEnd() == 72000 && !recorder.isPunchEnabled());
    recorder.setPunchEnabled(true);
    DAWLAB_CHECK(!preRoll.record());
    DAWLAB_CHECK(tr.getPosition() == 300000 && recorder.isPunchEnabled());
    recorder.setPunchEnabled(false);

    // The menu.
    juce::PopupMenu menu;
    preRoll.addToMenu(menu);
    const auto items = ex17rig::itemsOf(menu);
    DAWLAB_CHECK(items.size() == 3);
    if (items.size() == 3) {
        DAWLAB_CHECK(items[0].text == "No Pre-roll" && items[1].text == "Pre-roll 1 Bar" && items[2].text == "Pre-roll 2 Bars");
        DAWLAB_CHECK(!items[0].isTicked && !items[1].isTicked && items[2].isTicked);
        if (items[1].action)
            items[1].action();
        DAWLAB_CHECK(preRoll.getBars() == 1);
        if (items[0].action)
            items[0].action();
        DAWLAB_CHECK(preRoll.getBars() == 0);
    }
}

} // namespace ex11::check

int main() {
    const juce::ScopedJuceInitialiser_GUI juceInit;
    std::printf("Exercise 11 - pre-roll\n");
    ex11::check::run();
    return dawlab::test::summary("l17_ex11");
}
