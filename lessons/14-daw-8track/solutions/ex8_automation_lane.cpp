// l14_ex8_solution -- Exercise 8: an automation lane in the GUI (solution).
//
// What it shows
//   - AutomationLaneComponent: a juce::Component that sits under the arrangement and shares its
//     time axis (ArrangementView::sampleToX / xToSample). Click adds a breakpoint (snapped to the
//     beat grid, Alt = free), clicking an existing point selects it, dragging moves it (never past
//     its neighbours, so the list stays sorted), right-click removes it.
//   - AutomatedGainEffect: an insert that applies the lane (values in dB) sample-accurately,
//     using the PlayHead segments the mixer passes to every insert (stage 1, Step 5).
//   - The real-time hand-over of a new lane to the audio thread without locks: the UI publishes
//     an immutable AutomationLaneData through an atomic pointer; the audio thread swaps it in at
//     the start of a block and returns the old one through an SpscQueue (the engine's own queue
//     template, Step 7); the UI deletes it.
//
// Run
//   build/bin/Debug/l14_ex8_solution.exe        (from the build directory, as ctest does)
//
// Output
//   dawlab_output/l14_ex8_lane.png   a picture of the lane after the edits
//   Self-check: mapping, gestures, -12.00 dB halfway between 0 dB and -24 dB, block-size
//   invariance, and no lane object leaked after 200 publishes during playback.

#include <dawlab/Common.h>
#include <dawlab/engine/All.h>

#include "ArrangementView.h"
#include "DawLookAndFeel.h"
#include "Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <vector>

using namespace dawlab;
using namespace dawlab::engine;
using dawlab::app::ArrangementView;
using dawlab::app::Session;

namespace ex8 {

struct AutomationPoint {
    int64_t time = 0; // timeline sample
    float value = 0.0f; // dB
};

// An immutable, sorted breakpoint list. Once published it is only read.
class AutomationLaneData {
public:
    static inline std::atomic<int> liveCount{0};

    AutomationLaneData(std::vector<AutomationPoint> points, float defaultValue)
        : points_(std::move(points)), default_(defaultValue) {
        std::stable_sort(points_.begin(), points_.end(),
                         [](const AutomationPoint& a, const AutomationPoint& b) { return a.time < b.time; });
        ++liveCount;
    }
    ~AutomationLaneData() { --liveCount; }
    AutomationLaneData(const AutomationLaneData&) = delete;
    AutomationLaneData& operator=(const AutomationLaneData&) = delete;

    // Linear interpolation between breakpoints; the first / last value holds outside them.
    //   v(t) = v_k + (v_{k+1} - v_k) * (t - t_k) / (t_{k+1} - t_k)
    float valueAt(int64_t t) const noexcept {
        if (points_.empty())
            return default_;
        if (t <= points_.front().time)
            return points_.front().value;
        if (t >= points_.back().time)
            return points_.back().value;
        // first point with time > t (binary search), the one before it is <= t
        const auto next = std::upper_bound(points_.begin(), points_.end(), t,
                                           [](int64_t s, const AutomationPoint& p) { return s < p.time; });
        const auto prev = next - 1;
        const double span = static_cast<double>(next->time - prev->time);
        const double frac = span > 0.0 ? static_cast<double>(t - prev->time) / span : 0.0;
        return static_cast<float>(prev->value + (next->value - prev->value) * frac);
    }

    const std::vector<AutomationPoint>& points() const noexcept { return points_; }

private:
    std::vector<AutomationPoint> points_;
    float default_;
};

// Applies a volume lane (dB) to the block, sample by sample, following the timeline segments.
class AutomatedGainEffect final : public InsertEffect {
public:
    AutomatedGainEffect() = default;
    ~AutomatedGainEffect() override {
        delete pending_.exchange(nullptr);
        delete current_;
        collectRetired();
    }

    const char* getName() const noexcept override { return "AutomatedGain"; }
    void prepare(double, int, int) override {}
    void reset() noexcept override {}
    void setPlayHead(const PlayHead& playHead) noexcept override { playHead_ = playHead; }

    void process(AudioBlock block) noexcept override {
        // 1. Take a newly published lane, retire the old one (no delete on this thread).
        if (AutomationLaneData* fresh = pending_.exchange(nullptr, std::memory_order_acq_rel)) {
            if (current_ != nullptr && !retired_.push(current_))
                leaked_.fetch_add(1, std::memory_order_relaxed); // UI never collects: leak, never block
            current_ = fresh;
        }
        if (current_ == nullptr)
            return;
        // 2. One gain per sample, from the timeline position of that sample.
        if (!playHead_.isPlaying()) {
            block.multiplyBy(lastGain_); // stopped: hold the last value
            return;
        }
        for (int s = 0; s < playHead_.numSegments; ++s) {
            const TimelineSegment& seg = playHead_.segments[s];
            for (int i = 0; i < seg.numSamples; ++i) {
                const float g = dbToGain(current_->valueAt(seg.timelineStart + i));
                for (int ch = 0; ch < block.getNumChannels(); ++ch)
                    block.getChannelPointer(ch)[seg.blockOffset + i] *= g;
                lastGain_ = g;
            }
        }
    }

    int getNumParameters() const noexcept override { return 0; }
    ParameterInfo getParameterInfo(int) const noexcept override { return {}; }
    float getParameter(int) const noexcept override { return 0.0f; }
    void setParameter(int, float) noexcept override {}

    // UI thread: hand a new lane to the audio thread. A lane that is still pending (the audio
    // thread has not picked it up) was never seen by it and can be deleted right here.
    void publish(std::unique_ptr<AutomationLaneData> lane) {
        collectRetired();
        delete pending_.exchange(lane.release(), std::memory_order_acq_rel);
    }

    // UI thread: free what the audio thread has finished with.
    int collectRetired() {
        int n = 0;
        AutomationLaneData* old = nullptr;
        while (retired_.pop(old)) {
            delete old;
            ++n;
        }
        return n;
    }

private:
    std::atomic<AutomationLaneData*> pending_{nullptr};
    AutomationLaneData* current_ = nullptr; // audio thread only
    SpscQueue<AutomationLaneData*> retired_{64};
    std::atomic<int> leaked_{0};
    PlayHead playHead_;
    float lastGain_ = 1.0f;
};

// The lane editor. It shares the x axis with the ArrangementView it sits under.
class AutomationLaneComponent final : public juce::Component {
public:
    static constexpr float kMinDb = -48.0f;
    static constexpr float kMaxDb = 6.0f;
    static constexpr float kHitRadius = 6.0f;

    AutomationLaneComponent(const ArrangementView& view, AutomatedGainEffect& effect) : view_(view), effect_(effect) {}

    // value (dB) <-> y: kMaxDb at the top edge, kMinDb at the bottom edge, linear in dB
    float valueToY(float db) const noexcept {
        const float p = (std::clamp(db, kMinDb, kMaxDb) - kMinDb) / (kMaxDb - kMinDb);
        return (1.0f - p) * static_cast<float>(getHeight());
    }
    float yToValue(float y) const noexcept {
        const float h = static_cast<float>(std::max(1, getHeight()));
        return std::clamp(kMaxDb - (y / h) * (kMaxDb - kMinDb), kMinDb, kMaxDb);
    }

    AutomationPoint pointFromPosition(juce::Point<float> p, bool snap) const {
        return {view_.snapToBeat(view_.xToSample(p.x), snap), yToValue(p.y)};
    }

    int hitTest(juce::Point<float> p) const {
        for (int i = 0; i < static_cast<int>(points_.size()); ++i) {
            const auto& q = points_[static_cast<size_t>(i)];
            const juce::Point<float> c(view_.sampleToX(static_cast<double>(q.time)), valueToY(q.value));
            if (c.getDistanceFrom(p) <= kHitRadius)
                return i;
        }
        return -1;
    }

    // Click: select a point under the mouse, or insert a new one at the sorted position.
    int addOrSelectPoint(juce::Point<float> p, bool snap) {
        selected_ = hitTest(p);
        if (selected_ >= 0)
            return selected_;
        const AutomationPoint np = pointFromPosition(p, snap);
        auto it = std::upper_bound(points_.begin(), points_.end(), np.time,
                                   [](int64_t t, const AutomationPoint& q) { return t < q.time; });
        selected_ = static_cast<int>(it - points_.begin());
        points_.insert(it, np);
        publish();
        return selected_;
    }

    // Drag: move the selected point, but never past its neighbours (the list stays sorted).
    void dragSelectedPoint(juce::Point<float> p, bool snap) {
        if (selected_ < 0 || selected_ >= static_cast<int>(points_.size()))
            return;
        AutomationPoint np = pointFromPosition(p, snap);
        const auto idx = static_cast<size_t>(selected_);
        const int64_t lo = idx > 0 ? points_[idx - 1].time : 0;
        const int64_t hi = idx + 1 < points_.size() ? points_[idx + 1].time : std::numeric_limits<int64_t>::max();
        np.time = std::clamp(np.time, lo, hi);
        points_[idx] = np;
        publish();
    }

    bool removePointAt(juce::Point<float> p) {
        const int i = hitTest(p);
        if (i < 0)
            return false;
        points_.erase(points_.begin() + i);
        selected_ = -1;
        publish();
        return true;
    }

    const std::vector<AutomationPoint>& getPoints() const noexcept { return points_; }
    int getSelected() const noexcept { return selected_; }

    void paint(juce::Graphics& g) override {
        using dawlab::app::DawColours;
        g.fillAll(DawColours::panel());
        g.setColour(DawColours::outline());
        g.drawHorizontalLine(static_cast<int>(valueToY(0.0f)), 0.0f, static_cast<float>(getWidth()));
        g.setColour(DawColours::textDim());
        g.setFont(dawlab::app::DawLookAndFeel::uiFont(12.0f));
        g.drawText("Volume (dB)", 6, 2, ArrangementView::kHeaderWidth - 10, 16, juce::Justification::left);
        if (points_.empty())
            return;
        juce::Path line;
        line.startNewSubPath(static_cast<float>(ArrangementView::kHeaderWidth), valueToY(points_.front().value));
        for (const auto& q : points_)
            line.lineTo(view_.sampleToX(static_cast<double>(q.time)), valueToY(q.value));
        line.lineTo(static_cast<float>(getWidth()), valueToY(points_.back().value));
        g.setColour(DawColours::accent());
        g.strokePath(line, juce::PathStrokeType(1.5f));
        for (int i = 0; i < static_cast<int>(points_.size()); ++i) {
            const auto& q = points_[static_cast<size_t>(i)];
            const juce::Point<float> c(view_.sampleToX(static_cast<double>(q.time)), valueToY(q.value));
            g.setColour(i == selected_ ? DawColours::accentWarm() : DawColours::text());
            g.fillEllipse(juce::Rectangle<float>(2 * kHitRadius - 2, 2 * kHitRadius - 2).withCentre(c));
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (e.mods.isPopupMenu())
            removePointAt(e.position);
        else
            addOrSelectPoint(e.position, !e.mods.isAltDown());
        repaint();
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (!e.mods.isPopupMenu()) {
            dragSelectedPoint(e.position, !e.mods.isAltDown());
            repaint();
        }
    }

private:
    void publish() { effect_.publish(std::make_unique<AutomationLaneData>(points_, 0.0f)); }

    const ArrangementView& view_;
    AutomatedGainEffect& effect_;
    std::vector<AutomationPoint> points_;
    int selected_ = -1;
};

} // namespace ex8

namespace {

constexpr double kSr = 48000.0;
constexpr float kDc = 0.25f;

AudioBuffer render(Engine& engine, int64_t numSamples, int blockSize = 512) {
    RenderOptions ro;
    ro.numSamples = numSamples;
    ro.blockSize = blockSize;
    ro.compensateLatency = false;
    return renderOffline(engine, ro);
}

double levelDbAt(const AudioBuffer& b, int i) { return 20.0 * std::log10(std::abs(b.getSample(0, i)) / kDc); }

} // namespace

int main() {
    const juce::ScopedJuceInitialiser_GUI juceInit;
    using namespace ex8;

    Engine engine;
    engine.prepare(kSr, 512);
    Session session(engine);
    session.newProject();
    session.clearInsert(ChannelRef::master(), 3); // no master limiter: keep the maths linear
    // A DC "clip" of 4 beats on track 1, balance mode so that the centre is unity gain.
    auto dc = std::make_shared<AudioBuffer>(2, 4 * 24000);
    for (int ch = 0; ch < 2; ++ch)
        std::fill_n(dc->getWritePointer(ch), dc->getNumSamples(), kDc);
    session.setTrackAudio(0, dc, "dc");
    engine.getMixer().getTrack(0).setPanMode(PanMode::Balance);
    auto fx = std::make_unique<AutomatedGainEffect>();
    AutomatedGainEffect& gain = *fx;
    engine.addInsert(ChannelRef::track(0), -1, std::move(fx));
    engine.applyPendingCommandsOffline();

    ArrangementView view(session);
    view.setSize(1000, 300);
    view.setView(0.0, 192.0); // one beat (24000 samples) = 125 px
    AutomationLaneComponent lane(view, gain);
    lane.setSize(1000, 80);

    // mapping
    DAWLAB_CHECK_NEAR(lane.valueToY(AutomationLaneComponent::kMaxDb), 0.0, 1e-4);
    DAWLAB_CHECK_NEAR(lane.valueToY(AutomationLaneComponent::kMinDb), 80.0, 1e-4);
    DAWLAB_CHECK_NEAR(lane.yToValue(lane.valueToY(-12.0f)), -12.0, 1e-4);
    const auto at = [&](double sample, float db) {
        return juce::Point<float>(view.sampleToX(sample), lane.valueToY(db));
    };

    // gestures: three clicks, a click on an existing point, a drag, a right-click
    lane.addOrSelectPoint(at(0.0, 0.0f), true);
    lane.addOrSelectPoint(at(48000.0, -24.0f).translated(3.0f, 0.0f), true); // 3 px off the grid: snaps
    lane.addOrSelectPoint(at(72000.0, -24.0f), true);
    DAWLAB_CHECK(lane.getPoints().size() == 3);
    DAWLAB_CHECK(lane.getPoints().size() > 1 && lane.getPoints()[1].time == 48000);
    DAWLAB_CHECK(lane.getPoints().size() > 1 && std::abs(lane.getPoints()[1].value + 24.0f) < 0.05f);
    DAWLAB_CHECK(lane.addOrSelectPoint(at(48000.0, -24.0f), true) == 1); // hit: select, no new point
    DAWLAB_CHECK(lane.getPoints().size() == 3);
    lane.dragSelectedPoint(at(30000.0, -24.0f), true); // 1.25 beats -> snaps to beat 1 = 24000
    DAWLAB_CHECK(lane.getPoints().size() > 1 && lane.getPoints()[1].time == 24000);
    lane.dragSelectedPoint(at(90000.0, -24.0f), true); // cannot pass the next point (72000)
    DAWLAB_CHECK(lane.getPoints().size() > 1 && lane.getPoints()[1].time == 72000);
    lane.dragSelectedPoint(at(24000.0, -24.0f), true);

    // the sound: 0 dB at 0, -24 dB at 24000 -> exactly -12 dB at 12000 (linear in dB)
    AudioBuffer a = render(engine, 96000);
    const double mid = levelDbAt(a, 12000), flat = levelDbAt(a, 60000);
    std::printf("lane 0 dB @0, -24 dB @24000, -24 dB @72000: level at 12000 = %.2f dB, at 60000 = %.2f dB\n", mid, flat);
    DAWLAB_CHECK_NEAR(mid, -12.0, 0.01);
    DAWLAB_CHECK_NEAR(flat, -24.0, 0.01);
    AudioBuffer b = render(engine, 96000, 37);
    float worst = 0.0f;
    for (int i = 0; i < a.getNumSamples(); ++i)
        worst = std::max(worst, std::abs(a.getSample(0, i) - b.getSample(0, i)));
    std::printf("block size 512 vs 37: max difference %.3g\n", static_cast<double>(worst));
    DAWLAB_CHECK(worst == 0.0f);

    // right-click removes the middle point: now 0 dB @0 -> -24 dB @72000, so -12 dB at 36000
    DAWLAB_CHECK(lane.removePointAt(at(24000.0, -24.0f)));
    DAWLAB_CHECK(lane.getPoints().size() == 2);
    AudioBuffer c = render(engine, 96000);
    std::printf("after removing the middle point: level at 36000 = %.2f dB\n", levelDbAt(c, 36000));
    DAWLAB_CHECK_NEAR(levelDbAt(c, 36000), -12.0, 0.01);

    // real-time hand-over: 200 publishes while "the audio thread" runs blocks in between
    engine.getTransport().setPosition(0);
    engine.getTransport().play();
    AudioBuffer block(2, 256);
    for (int k = 0; k < 200; ++k) {
        lane.dragSelectedPoint(at(24000.0, -6.0f - static_cast<float>(k % 10)), true);
        lane.addOrSelectPoint(at(72000.0, -24.0f), true); // reselect the last point
        if (k % 3 == 0)
            engine.processBlock(block);
    }
    engine.processBlock(block);
    gain.collectRetired();
    std::printf("lane objects alive after 200 publishes: %d (1 = only the one in use)\n",
                AutomationLaneData::liveCount.load());
    DAWLAB_CHECK(AutomationLaneData::liveCount.load() == 1);

    // a picture of the lane
    const juce::Image image = lane.createComponentSnapshot(lane.getLocalBounds());
    const juce::File png = juce::File::getCurrentWorkingDirectory().getChildFile(outputPath("l14_ex8_lane.png"));
    png.deleteFile();
    {
        juce::FileOutputStream out(png);
        juce::PNGImageFormat format;
        DAWLAB_CHECK(out.openedOk() && format.writeImageToStream(image, out));
    }
    return test::summary("l14_ex8");
}
