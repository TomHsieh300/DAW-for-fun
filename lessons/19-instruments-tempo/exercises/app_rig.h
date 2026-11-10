#pragma once
// Exercises 7-9 - what their self-checks share: DawLab 19 in its offline mode (the smoke test's way,
// app/Source/SmokeTest.cpp; chapter 18's exercises/app_rig.h). There is no audio device: play() drives the engine
// as a device would.

#include "MainComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace ex19rig {

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
        session.setTempoMap({{0, 120.0, false}});
        engine.applyPendingCommandsOffline();
        main->getAudioHost().setClock([this] { return clock; });
    }
    ~Rig() {
        main->getAudioHost().setClock({});
        main.reset();
    }
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;

    // The points of the project's tempo map.
    std::vector<engine::TempoPoint> tempoPoints() const {
        std::vector<engine::TempoPoint> points;
        for (int i = 0; i < session.getTempoMap().size(); ++i)
            points.push_back(session.getTempoMap().point(i));
        return points;
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

    // From the start of the song: stops, lets what still sounds die away, and plays `samples`.
    std::vector<float> playFromStart(std::int64_t samples) {
        engine.getTransport().stop();
        play(96000);
        engine.getTransport().setPosition(0);
        engine.applyPendingCommandsOffline();
        engine.getTransport().play();
        std::vector<float> out = play(samples);
        engine.getTransport().stop();
        return out;
    }
};

inline float peakOf(const std::vector<float>& x, size_t from = 0, size_t to = SIZE_MAX) {
    float peak = 0.0f;
    for (size_t i = from; i < std::min(to, x.size()); ++i)
        peak = std::max(peak, std::fabs(x[i]));
    return peak;
}

} // namespace ex19rig
