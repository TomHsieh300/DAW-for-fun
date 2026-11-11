#pragma once
// Exercises 7-9 - what their self-checks share: DawLab 20 in its offline mode (the smoke test's way,
// app/Source/SmokeTest.cpp; chapter 19's exercises/app_rig.h). There is no audio device: play() drives the engine
// as a device would.

#include "MainComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace ex20rig {

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

    // A new audio track at the end.
    engine::TrackId addTrack() {
        const engine::TrackId id = main->addTrack(engine::TrackFormat::Mono, false);
        engine.applyPendingCommandsOffline();
        return id;
    }

    // One tempo for the whole song, as an undo step.
    bool setTempo(double bpm) {
        const bool ok = session.setTempoPoints({{0, bpm, false}}, "Tempo");
        engine.applyPendingCommandsOffline();
        return ok;
    }

    // Plays `samples` in blocks of 512 and returns the left channel of the master output.
    std::vector<float> play(std::int64_t samples) {
        std::vector<float> left;
        left.reserve(static_cast<size_t>(samples));
        constexpr int kBlock = MainComponent::kOfflineBlockSize;
        std::vector<float> l(kBlock), r(kBlock);
        float* channels[] = {l.data(), r.data()};
        for (std::int64_t done = 0; done < samples; done += kBlock) {
            const int n = static_cast<int>(std::min<std::int64_t>(kBlock, samples - done));
            main->getAudioHost().processOffline(channels, 2, n, engine::InputBlock{});
            left.insert(left.end(), l.begin(), l.begin() + n);
            clock += n / MainComponent::kOfflineSampleRate;
            if ((done / kBlock) % 8 == 0)
                main->tick();
        }
        return left;
    }

    // From the start of the song, with every render of a clip that follows the tempo done first (no placeholders).
    std::vector<float> playFromStart(std::int64_t samples) {
        session.finishRenders();
        engine.getTransport().stop();
        play(96000);
        engine.getTransport().setPosition(0);
        engine.applyPendingCommandsOffline();
        engine.getTransport().play();
        std::vector<float> out = play(samples);
        engine.getTransport().stop();
        return out;
    }

    // Where the clicks of `out` are, in samples of the SONG: peaks above `threshold`, at least 100 ms apart, less
    // the delay of the mix.
    std::vector<std::int64_t> clicksIn(const std::vector<float>& out, float threshold = 0.1f) const {
        std::vector<std::int64_t> at;
        const std::int64_t delay = engine.getLatencySamples();
        for (size_t i = 0; i < out.size(); ++i) {
            const float v = std::fabs(out[i]);
            if (v < threshold)
                continue;
            size_t top = i; // the largest sample of this click
            for (size_t j = i; j < std::min(out.size(), i + 480); ++j)
                if (std::fabs(out[j]) > std::fabs(out[top]))
                    top = j;
            at.push_back(static_cast<std::int64_t>(top) - delay);
            i += 4800;
        }
        return at;
    }
};

// A click: 6 ms of a decaying 3 kHz tone, peak `amplitude`, starting at `at` in `audio` (mono).
inline void addClick(AudioBuffer& audio, std::int64_t at, float amplitude = 0.8f) {
    for (int k = 0; k < 288; ++k)
        if (at + k >= 0 && at + k < audio.getNumSamples())
            audio.setSample(0, static_cast<int>(at + k),
                            amplitude * static_cast<float>(std::exp(-k / 60.0) * std::sin(2.0 * 3.14159265358979 * 3000.0 * k / 48000.0 + 1.5707963)));
}

// The sample of a click's peak: the first sample, where the cosine starts at 1.
constexpr std::int64_t kClickPeak = 0;

} // namespace ex20rig
