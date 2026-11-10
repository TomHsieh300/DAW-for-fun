#pragma once
// Exercises 8-12 - what their self-checks share: DawLab 18 in its offline mode (the smoke test's way,
// app/Source/SmokeTest.cpp). There is no audio device: simulatePlayback() drives the engine as a device
// would, and a MIDI keyboard is the check pushing messages into the AudioHost at a clock the check sets.

#include "MainComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace ex18rig {

using namespace dawlab;
using namespace dawlab::app;

struct Rig {
    std::unique_ptr<MainComponent> main = std::make_unique<MainComponent>(MainComponent::Mode::Offline);
    Session& session = main->getSession();
    engine::Engine& engine = main->getEngine();
    double clock = 0.0; // seconds: the AudioHost's clock, moved on by play()

    Rig() {
        main->setSize(MainComponent::kDefaultWidth, MainComponent::kDefaultHeight);
        session.newProject();
        engine.applyPendingCommandsOffline();
        main->getAudioHost().setClock([this] { return clock; });
    }
    ~Rig() {
        main->getAudioHost().setClock({});
        main.reset();
    }
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;

    // A new instrument track (one undo step), its id.
    engine::TrackId instrumentTrack() {
        const engine::TrackId id = main->addTrack(engine::TrackFormat::Mono, true);
        engine.applyPendingCommandsOffline();
        return id;
    }
    engine::Track* engineTrack(engine::TrackId id) {
        const int slot = engine.slotOf(id);
        return slot >= 0 ? &engine.getMixer().getTrack(slot) : nullptr;
    }

    // Plays `samples` in blocks of 512 and returns the left channel of the master output; the clock moves with
    // the blocks, and the app ticks now and then.
    std::vector<float> play(std::int64_t samples) {
        std::vector<float> left;
        left.reserve(static_cast<size_t>(samples));
        constexpr int kBlock = MainComponent::kOfflineBlockSize;
        std::vector<float> l(kBlock), r(kBlock);
        float* channels[] = {l.data(), r.data()};
        for (std::int64_t done = 0; done < samples; done += kBlock) {
            const int n = static_cast<int>(std::min<std::int64_t>(kBlock, samples - done));
            main->getAudioHost().processOffline(channels, 2, n, engine::InputBlock{}); // what simulatePlayback() does
            left.insert(left.end(), l.begin(), l.begin() + n);
            clock += n / MainComponent::kOfflineSampleRate;
            if ((done / kBlock) % 8 == 0)
                main->tick();
        }
        return left;
    }
};

inline float peakOf(const std::vector<float>& x, size_t from = 0, size_t to = SIZE_MAX) {
    float peak = 0.0f;
    for (size_t i = from; i < std::min(to, x.size()); ++i)
        peak = std::max(peak, std::fabs(x[i]));
    return peak;
}

} // namespace ex18rig
