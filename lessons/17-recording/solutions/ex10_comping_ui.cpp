// Exercise 10 (solution): comping in the app.
//
// Build and run:  l17_ex10_solution   (ctest, label "solution"; needs -Juce)
// Your version:   exercises/starter/ex10_comping_ui.cpp -> l17_ex10_starter

#include "../exercises/ex10_comping_ui.h"

#include <cmath>

using namespace dawlab;
using namespace dawlab::app;

namespace ex10 {

int Comper::takeOf(const engine::Clip& clip, const std::vector<Placement>& takes) {
    for (size_t i = 0; i < takes.size(); ++i) {
        const Placement& t = takes[i];
        if (clip.sourceOffset - t.skipFrames == clip.start - t.timelineStart && clip.start >= t.timelineStart &&
            clip.end() <= t.timelineStart + t.numFrames)
            return static_cast<int>(i);
    }
    return -1;
}

Comp Comper::compOf(engine::TrackId track, int clipIndex) const {
    const std::vector<engine::Clip>& clips = session_.getClips(track);
    if (clipIndex < 0 || clipIndex >= static_cast<int>(clips.size()))
        return {};
    const std::vector<Placement>* takes = session_.getTakesOf(clips[static_cast<size_t>(clipIndex)]);
    if (takes == nullptr)
        return {};
    std::vector<const engine::Clip*> pieces; // the list is sorted by start
    for (const engine::Clip& c : clips)
        if (c.source == clips[static_cast<size_t>(clipIndex)].source)
            pieces.push_back(&c);
    Comp comp;
    for (size_t i = 0; i < pieces.size(); ++i) {
        const engine::Clip& c = *pieces[i];
        const int take = takeOf(c, *takes);
        if (take < 0)
            return {};
        const std::int64_t from = i == 0 ? c.start : c.start + c.fadeInSamples / 2;
        const std::int64_t to = i + 1 == pieces.size() ? c.end() : c.end() - c.fadeOutSamples / 2;
        comp.push_back({from, to, take});
    }
    return comp;
}

bool Comper::choose(engine::TrackId track, int clipIndex, std::int64_t from, std::int64_t to, int take) {
    const Comp comp = compOf(track, clipIndex);
    if (comp.empty())
        return false;
    const std::vector<engine::Clip>& clips = session_.getClips(track);
    const engine::Clip model = clips[static_cast<size_t>(clipIndex)];
    const std::vector<Placement>& takes = *session_.getTakesOf(model);
    if (take < 0 || take >= static_cast<int>(takes.size()))
        return false;
    const Placement& t = takes[static_cast<size_t>(take)];
    from = std::max(from, t.timelineStart);
    to = std::min(to, t.timelineStart + t.numFrames);
    if (to <= from)
        return false;
    const Comp next = chooseTake(comp, from, to, take);
    if (next == comp)
        return false;

    std::vector<engine::Clip> list;
    for (const engine::Clip& c : clips)
        if (c.source != model.source)
            list.push_back(c);
    const double rate = session_.getClipSampleRate();
    const std::int64_t edge = std::llround(kEdgeFadeSeconds * rate);
    const std::vector<CompClip> pieces = compClips(next, takes, std::llround(kFadeSeconds * rate));
    for (size_t i = 0; i < pieces.size(); ++i) {
        const CompClip& p = pieces[i];
        engine::Clip c = model;
        c.start = p.timelineStart;
        c.sourceOffset = p.skipFrames;
        c.length = p.numFrames;
        c.fadeInSamples = static_cast<int>(i == 0 ? std::min<std::int64_t>(edge, p.numFrames / 2) : p.fadeIn);
        c.fadeOutSamples = static_cast<int>(i + 1 == pieces.size() ? std::min<std::int64_t>(edge, p.numFrames / 2) : p.fadeOut);
        list.push_back(c);
    }
    std::stable_sort(list.begin(), list.end(), [](const engine::Clip& a, const engine::Clip& b) { return a.start < b.start; });
    return session_.perform(edit::SetClips{track, std::move(list)}, "Comp Take " + juce::String(take + 1));
}

void Comper::addToMenu(juce::PopupMenu& menu, engine::TrackId track, int clipIndex) {
    if (compOf(track, clipIndex).empty())
        return;
    const engine::Transport& tr = session_.getEngine().getTransport();
    const std::int64_t from = tr.getLoopStart(), to = tr.getLoopEnd();
    const auto numTakes = static_cast<int>(session_.getTakesOf(session_.getClips(track)[static_cast<size_t>(clipIndex)])->size());
    for (int i = 0; i < numTakes; ++i)
        menu.addItem(juce::PopupMenu::Item("Take " + juce::String(i + 1) + " Between the Locators")
                         .setEnabled(to > from)
                         .setAction([this, track, clipIndex, from, to, i] { choose(track, clipIndex, from, to, i); }));
}

} // namespace ex10

#include "../exercises/ex10_check.h"
