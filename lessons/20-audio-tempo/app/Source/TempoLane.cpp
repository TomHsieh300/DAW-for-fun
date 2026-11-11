#include "TempoLane.h"

#include "DawLookAndFeel.h"

#include <algorithm>
#include <cmath>

namespace dawlab::app {

namespace {
constexpr float kGrab = 7.0f; // pixels around a point that take it
constexpr int kBeat = 960;
} // namespace

TempoLane::TempoLane(Session& session, ArrangementView& arrangement) : session_(session), arrangement_(arrangement) {
    session_.addChangeListener(this);
}

TempoLane::~TempoLane() { session_.removeChangeListener(this); }

float TempoLane::bpmToY(double bpm) const noexcept {
    const double t = (std::clamp(bpm, kLowBpm, kHighBpm) - kLowBpm) / (kHighBpm - kLowBpm);
    return static_cast<float>(getHeight() - 4) - static_cast<float>(t) * static_cast<float>(getHeight() - 8);
}

double TempoLane::yToBpm(float y) const noexcept {
    const double t = (static_cast<double>(getHeight() - 4) - static_cast<double>(y)) / static_cast<double>(std::max(1, getHeight() - 8));
    return kLowBpm + std::clamp(t, 0.0, 1.0) * (kHighBpm - kLowBpm);
}

float TempoLane::tickToX(int64_t tick) const noexcept {
    return arrangement_.sampleToX(static_cast<double>(session_.sampleOfTick(tick)));
}

int64_t TempoLane::xToTick(float x, const engine::TempoMap& map, bool snapToBeat) const noexcept {
    const double tick = std::max(0.0, map.tickAtSample(arrangement_.xToSample(x), session_.getClipSampleRate()));
    return snapToBeat ? std::llround(tick / kBeat) * kBeat : std::llround(tick);
}

juce::Point<float> TempoLane::getPointPosition(int index) const {
    const engine::TempoPoint& p = session_.getTempoMap().point(index);
    return {tickToX(p.tick), bpmToY(p.bpm)};
}

int TempoLane::pointAt(juce::Point<float> p) const {
    const engine::TempoMap& map = session_.getTempoMap();
    for (int i = map.size() - 1; i >= 0; --i) // the later one on top
        if (getPointPosition(i).getDistanceFrom(p) <= kGrab)
            return i;
    return -1;
}

void TempoLane::press(juce::Point<float> p, juce::ModifierKeys mods, int clicks) {
    session_.getHistory().endMerge();
    dragPoint_ = -1;
    std::vector<engine::TempoPoint> points = session_.getTempoMap().points();
    const int hit = pointAt(p);
    if (hit >= 0 && mods.isPopupMenu()) {
        if (hit > 0) { // the first point is the song's tempo: it stays
            points.erase(points.begin() + hit);
            session_.setTempoPoints(std::move(points), "Delete tempo point");
        }
        return;
    }
    if (hit >= 0 && mods.isAltDown()) {
        if (hit + 1 < static_cast<int>(points.size())) { // the last point has nothing to ramp to
            points[static_cast<size_t>(hit)].rampToNext = !points[static_cast<size_t>(hit)].rampToNext;
            session_.setTempoPoints(std::move(points), "Tempo ramp");
        }
        return;
    }
    static std::uint32_t nextKey = 0x54500000u; // "TP": every drag its own undo step
    if (hit < 0) {
        if (clicks < 2 || mods.isPopupMenu() || p.x < static_cast<float>(ArrangementView::kHeaderWidth))
            return;
        // A new point on the nearest beat - unless one is there already.
        const int64_t tick = xToTick(p.x, session_.getTempoMap(), true);
        const auto after = std::find_if(points.begin(), points.end(), [tick](const engine::TempoPoint& t) { return t.tick >= tick; });
        if ((after != points.end() && after->tick == tick) || static_cast<int>(points.size()) >= engine::TempoMap::kMaxPoints)
            return;
        dragKey_ = ++nextKey;
        dragMap_ = session_.getTempoMap();
        dragPoint_ = static_cast<int>(after - points.begin());
        points.insert(after, {tick, std::round(yToBpm(p.y)), false});
        if (!session_.setTempoPoints(std::move(points), "Add tempo point", dragKey_))
            dragPoint_ = -1;
        return;
    }
    dragKey_ = ++nextKey;
    dragMap_ = session_.getTempoMap();
    dragPoint_ = hit;
}

void TempoLane::dragTo(juce::Point<float> p, juce::ModifierKeys mods) {
    std::vector<engine::TempoPoint> points = session_.getTempoMap().points();
    if (dragPoint_ < 0 || dragPoint_ >= static_cast<int>(points.size()))
        return;
    engine::TempoPoint& point = points[static_cast<size_t>(dragPoint_)];
    const engine::TempoPoint before = point;
    if (dragPoint_ > 0) { // between its neighbours, a tick from each
        const int64_t low = points[static_cast<size_t>(dragPoint_ - 1)].tick + 1;
        const int64_t high = dragPoint_ + 1 < static_cast<int>(points.size()) ? points[static_cast<size_t>(dragPoint_ + 1)].tick - 1 : INT64_MAX;
        point.tick = std::clamp(xToTick(p.x, dragMap_, !mods.isShiftDown()), low, high);
    }
    const double bpm = yToBpm(p.y);
    point.bpm = std::clamp(mods.isShiftDown() ? bpm : std::round(bpm), engine::TempoMap::kMinBpm, engine::TempoMap::kMaxBpm);
    if (point.tick == before.tick && point.bpm == before.bpm)
        return;
    session_.setTempoPoints(std::move(points), "Move tempo point", dragKey_);
}

void TempoLane::release() {
    dragPoint_ = -1;
    session_.getHistory().endMerge();
}

void TempoLane::paint(juce::Graphics& g) {
    g.fillAll(DawColours::panel());
    g.setColour(DawColours::outline());
    g.drawHorizontalLine(getHeight() - 1, 0.0f, static_cast<float>(getWidth()));
    g.setColour(DawColours::textDim());
    g.setFont(DawLookAndFeel::uiFont(12.0f));
    g.drawText("tempo", getLocalBounds().withWidth(ArrangementView::kHeaderWidth).reduced(8, 0), juce::Justification::centredLeft);

    juce::Graphics::ScopedSaveState save(g);
    g.reduceClipRegion(getLocalBounds().withTrimmedLeft(ArrangementView::kHeaderWidth));
    const engine::TempoMap& map = session_.getTempoMap();
    // The line: level where the tempo holds, sloped on a ramp (straight in ticks; nearly so in samples).
    juce::Path line;
    for (int i = 0; i < map.size(); ++i) {
        const juce::Point<float> at = getPointPosition(i);
        if (i == 0)
            line.startNewSubPath(at);
        else
            line.lineTo(at);
        const bool last = i + 1 == map.size();
        const float nextX = last ? static_cast<float>(getWidth()) + 10.0f : getPointPosition(i + 1).x;
        if (last || !map.point(i).rampToNext)
            line.lineTo(nextX, at.y); // holds, then jumps
    }
    g.setColour(DawColours::accent());
    g.strokePath(line, juce::PathStrokeType(1.5f));
    for (int i = 0; i < map.size(); ++i) {
        const juce::Point<float> at = getPointPosition(i);
        g.setColour(i == dragPoint_ ? DawColours::accentWarm() : DawColours::accent());
        g.fillEllipse(at.x - 3.5f, at.y - 3.5f, 7.0f, 7.0f);
        g.setColour(DawColours::text());
        g.drawText(juce::String(map.point(i).bpm, 1), static_cast<int>(at.x) + 6, static_cast<int>(at.y) - 14, 60, 12, juce::Justification::left);
    }
}

} // namespace dawlab::app
