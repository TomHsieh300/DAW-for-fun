#pragma once
// Exercise 8 - the self-check (main). Included at the end of the starter and of the solution.

#include "ex8_latency_button.h"

#include "app_rig.h"

#include <dawlab/Check.h>

#include <cstdio>

namespace ex8::check {

using namespace dawlab;
using namespace dawlab::app;
using ex17rig::Rig;

// A simulated room: chirp c comes back delays[c] samples late at gains[c] (0: not heard), over a little
// noise. `calls` counts the measurements that played the probe.
struct Room {
    std::vector<int> delays;
    std::vector<float> gains;
    engine::Latencies reported;
    int calls = 0;

    Room(int delay, engine::Latencies driver) : delays(Probe::kChirps, delay), gains(Probe::kChirps, 0.3f), reported(driver) {}

    PlayAndRecord device() {
        return [this](const std::vector<float>& signal) {
            ++calls;
            const Probe probe = makeProbe(MainComponent::kOfflineSampleRate);
            RoundTrip back;
            back.reported = reported;
            back.input.resize(signal.size());
            std::uint32_t seed = 12345;
            for (float& x : back.input) {
                seed = seed * 1664525u + 1013904223u;
                x = 0.002f * (static_cast<float>(seed >> 8) / 16777216.0f - 0.5f);
            }
            for (int c = 0; c < Probe::kChirps; ++c)
                for (size_t k = 0; k < probe.chirp.size(); ++k) {
                    const auto at = static_cast<size_t>(probe.chirpAt[static_cast<size_t>(c)] + delays[static_cast<size_t>(c)]) + k;
                    if (at < back.input.size())
                        back.input[at] += gains[static_cast<size_t>(c)] * probe.chirp[k];
                }
            return back;
        };
    }
};

inline void run() {
    Rig rig;
    MainComponent& main = *rig.main;
    TakeRecorder& recorder = rig.recorder;
    const engine::Latencies none{};

    // A device that reports nothing, and a round trip of 1234 samples: the trim is all of it.
    Room room(1234, none);
    LatencyMeter meter(main, room.device());
    recorder.setTrimSamples(-7);
    const Result first = meter.measure();
    DAWLAB_CHECK(room.calls == 1);
    DAWLAB_CHECK(first.applied && first.heard == 8 && first.roundTrip == 1234 && first.spread == 0 && first.trim == 1234);
    DAWLAB_CHECK(recorder.getTrimSamples() == 1234);
    DAWLAB_CHECK(first.message.contains("trim") && first.message.contains("1234"));
    DAWLAB_CHECK(main.getStatus() == first.message);
    std::printf("  %s\n", first.message.toRawUTF8());

    // With that trim, a take lands where it was played: a click played at 120000 is at 120000 in the take.
    const engine::TrackId track = rig.track(0);
    DAWLAB_CHECK(main.setArmed(track, true));
    rig.engine.getTransport().setPosition(96000);
    const std::int64_t clickAt = 120000;
    const std::int64_t heardAt = rig.rendered + (clickAt - 96000) + rig.engine.getLatencySamples() + 1234; // a render frame
    const ex17rig::InputFn input = [heardAt](int, std::int64_t frame) { return frame == heardAt ? 0.9f : 0.0f; };
    DAWLAB_CHECK(main.toggleRecord());

    // While recording, the button does nothing: not even the probe is played.
    const Result busy = meter.measure();
    DAWLAB_CHECK(room.calls == 1 && !busy.applied && busy.message.contains("recording"));
    DAWLAB_CHECK(!meter.revert());
    juce::PopupMenu busyMenu;
    meter.addToMenu(busyMenu);
    const auto busyItems = ex17rig::itemsOf(busyMenu);
    DAWLAB_CHECK(busyItems.size() == 2 && !busyItems[0].isEnabled && !busyItems[1].isEnabled);

    rig.play(48000, input);
    rig.stop(input);
    DAWLAB_CHECK(rig.waitForTakes());
    const auto& clips = rig.session.getClips(track);
    DAWLAB_CHECK(clips.size() == 1);
    if (clips.size() == 1) {
        const engine::Clip& take = clips[0];
        auto takeAt = [&take](std::int64_t p) { return take.source->getSample(0, static_cast<int>(take.sourceOffset + p - take.start)); };
        DAWLAB_CHECK(takeAt(clickAt) == 0.9f && takeAt(clickAt - 1) == 0.0f && takeAt(clickAt + 1) == 0.0f);
        std::printf("  a click played at %lld is at %lld in the take\n", static_cast<long long>(clickAt),
                    static_cast<long long>(takeAt(clickAt) == 0.9f ? clickAt : -1));
    }

    // Undo the measurement: the trim from before, once.
    DAWLAB_CHECK(meter.revert());
    DAWLAB_CHECK(recorder.getTrimSamples() == -7);
    DAWLAB_CHECK(!meter.revert());
    DAWLAB_CHECK(recorder.getTrimSamples() == -7);

    // A driver that reports some of it: the trim is the rest.
    Room reporting(1234, engine::Latencies{300, 400, 0, 0});
    LatencyMeter meter2(main, reporting.device());
    const Result second = meter2.measure();
    DAWLAB_CHECK(second.applied && second.roundTrip == 1234 && second.trim == 534 && recorder.getTrimSamples() == 534);

    // A latency that moves: 1 ms (48 samples) is still one latency, 49 samples is not.
    Room within(1234, none);
    for (int c = 0; c < Probe::kChirps; c += 2)
        within.delays[static_cast<size_t>(c)] = 1234 + 48;
    LatencyMeter meter3(main, within.device());
    const Result moves48 = meter3.measure();
    DAWLAB_CHECK(moves48.applied && moves48.spread == 48 && moves48.roundTrip == 1234 + 48 && recorder.getTrimSamples() == 1282);
    within.delays[0] = 1234 + 49;
    const Result moves49 = meter3.measure();
    DAWLAB_CHECK(!moves49.applied && moves49.spread == 49 && recorder.getTrimSamples() == 1282);
    DAWLAB_CHECK(moves49.message.contains("moves") && moves49.message.contains("1.0 ms"));
    // A laptop's microphone (Step 7): 10 to 90 ms apart.
    Room laptop(480, none);
    for (int c = 0; c < Probe::kChirps; ++c)
        laptop.delays[static_cast<size_t>(c)] = 480 + 550 * c;
    LatencyMeter meter4(main, laptop.device());
    const Result wanders = meter4.measure();
    DAWLAB_CHECK(!wanders.applied && wanders.message.contains("moves") && wanders.message.contains("80.2 ms"));
    std::printf("  %s\n", wanders.message.toRawUTF8());

    // Too few chirps heard: 6 of 8 is enough, 5 is not.
    Room quiet(1000, none);
    quiet.gains[0] = quiet.gains[3] = 0.0f;
    LatencyMeter meter5(main, quiet.device());
    const Result six = meter5.measure();
    DAWLAB_CHECK(six.applied && six.heard == 6 && recorder.getTrimSamples() == 1000);
    quiet.gains[7] = 0.0f;
    const Result five = meter5.measure();
    DAWLAB_CHECK(!five.applied && five.heard == 5 && recorder.getTrimSamples() == 1000);
    DAWLAB_CHECK(five.message.contains("only 5 of 8"));
    // A muted microphone.
    Room muted(1000, none);
    muted.gains.assign(Probe::kChirps, 0.0f);
    LatencyMeter meter6(main, muted.device());
    const Result silent = meter6.measure();
    DAWLAB_CHECK(!silent.applied && silent.heard == 0 && silent.message.contains("none") && recorder.getTrimSamples() == 1000);
    DAWLAB_CHECK(main.getStatus() == silent.message);

    // The revert of meter5 goes back to what was there before its own measurement.
    DAWLAB_CHECK(meter5.revert() && recorder.getTrimSamples() == 1282);

    // The menu, idle: Measure works; Undo only after a measurement that set the trim.
    LatencyMeter meter7(main, room.device());
    juce::PopupMenu menu;
    meter7.addToMenu(menu);
    const auto items = ex17rig::itemsOf(menu);
    DAWLAB_CHECK(items.size() == 2);
    if (items.size() == 2) {
        DAWLAB_CHECK(items[0].text == "Measure Latency..." && items[0].isEnabled);
        DAWLAB_CHECK(items[1].text == "Undo Latency Measurement" && !items[1].isEnabled);
        if (items[0].action)
            items[0].action();
        DAWLAB_CHECK(recorder.getTrimSamples() == 1234);
    }
    juce::PopupMenu after;
    meter7.addToMenu(after);
    const auto afterItems = ex17rig::itemsOf(after);
    DAWLAB_CHECK(afterItems.size() == 2 && afterItems[1].isEnabled);
    if (afterItems.size() == 2 && afterItems[1].action)
        afterItems[1].action();
    DAWLAB_CHECK(recorder.getTrimSamples() == 1282);
}

} // namespace ex8::check

int main() {
    const juce::ScopedJuceInitialiser_GUI juceInit;
    std::printf("Exercise 8 - a Measure Latency button\n");
    ex8::check::run();
    return dawlab::test::summary("l17_ex8");
}
