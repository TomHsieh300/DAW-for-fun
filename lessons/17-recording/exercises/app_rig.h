#pragma once
// Exercises 8-11 - what their self-checks share: DawLab 17 in its offline mode, recording through the
// real paths (the smoke test's way, app/Source/SmokeTest.cpp). There is no audio device: the input comes
// from the check, through the same processBlock() a device calls, and the disk thread is real.

#include "MainComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace ex17rig {

using namespace dawlab;
using namespace dawlab::app;

// A takes folder of its own, removed afterwards.
class TakesFolder {
public:
    TakesFolder() {
        folder_ = juce::File::getCurrentWorkingDirectory().getChildFile("dawlab_output").getChildFile(
            "l17_ex_takes_" + juce::String(juce::Time::getHighResolutionTicks()));
        folder_.createDirectory();
    }
    ~TakesFolder() { folder_.deleteRecursively(); }
    TakesFolder(const TakesFolder&) = delete;
    TakesFolder& operator=(const TakesFolder&) = delete;
    const juce::File& get() const noexcept { return folder_; }

private:
    juce::File folder_;
};

// What frame f of an input is when nothing else is asked for: exact in a float, different for each
// frame, never 0, at most -6 dBFS.
inline float inputAt(std::int64_t frame) { return static_cast<float>(frame % (1 << 20) + 1) / static_cast<float>(1 << 21); }

// Input channel `ch` at render frame `frame`.
using InputFn = std::function<float(int ch, std::int64_t frame)>;

struct Rig {
    TakesFolder folder;
    std::unique_ptr<MainComponent> main = std::make_unique<MainComponent>(MainComponent::Mode::Offline);
    Session& session = main->getSession();
    engine::Engine& engine = main->getEngine();
    TakeRecorder& recorder = main->getTakeRecorder();
    std::int64_t rendered = 0; // render frames so far: what the input functions are given

    Rig() {
        main->setSize(MainComponent::kDefaultWidth, MainComponent::kDefaultHeight);
        session.newProject();
        engine.applyPendingCommandsOffline();
        recorder.setFolder(folder.get());
    }
    ~Rig() { main.reset(); }
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;

    engine::TrackId track(int index) const { return session.getTrackId(index); }

    // Plays `frames` frames with two input channels from `input`, and ticks now and then.
    void play(std::int64_t frames, const InputFn& input, int block = 512) {
        std::vector<float> left(static_cast<size_t>(block)), right(static_cast<size_t>(block));
        const float* channels[] = {left.data(), right.data()};
        for (std::int64_t done = 0; done < frames; done += block) {
            const int n = static_cast<int>(std::min<std::int64_t>(block, frames - done));
            for (int i = 0; i < n; ++i) {
                left[static_cast<size_t>(i)] = input(0, rendered + i);
                right[static_cast<size_t>(i)] = input(1, rendered + i);
            }
            main->simulatePlayback(n, n, engine::InputBlock{channels, 2});
            rendered += n;
            if ((done / block) % 16 == 0) {
                main->tick();
                juce::Thread::yield();
            }
        }
    }
    // The transport stops; one block more, the audio thread's way of hearing about it.
    void stop(const InputFn& input) {
        engine.getTransport().stop();
        play(512, input);
    }
    // Ticks until the recording is over and its clips are there.
    bool waitForTakes() {
        for (int i = 0; i < 3000 && recorder.isBusy(); ++i) {
            main->tick();
            juce::Thread::sleep(2);
        }
        main->tick();
        return !recorder.isBusy();
    }
};

// The items of a menu, top level, in order (section headers and separators left out).
inline std::vector<juce::PopupMenu::Item> itemsOf(const juce::PopupMenu& menu) {
    std::vector<juce::PopupMenu::Item> items;
    juce::PopupMenu::MenuItemIterator it(menu);
    while (it.next())
        if (!it.getItem().isSectionHeader && !it.getItem().isSeparator)
            items.push_back(it.getItem());
    return items;
}

} // namespace ex17rig
