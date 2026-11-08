#include "ArrangementView.h"

#include "AudioHost.h"
#include "DawLookAndFeel.h"
#include "MediaLoader.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace dawlab::app {

using engine::Clip;

// ---- WaveformCache ------------------------------------------------------------------------------

const WaveformCache::Peaks& WaveformCache::get(const std::shared_ptr<const AudioBuffer>& buffer) {
    auto it = cache_.find(buffer.get());
    if (it != cache_.end())
        return it->second;
    Peaks peaks;
    peaks.source = buffer;
    const int n = buffer->getNumSamples();
    const int buckets = (n + kSamplesPerBucket - 1) / kSamplesPerBucket;
    peaks.minimum.assign(static_cast<size_t>(buckets), 0.0f);
    peaks.maximum.assign(static_cast<size_t>(buckets), 0.0f);
    for (int ch = 0; ch < buffer->getNumChannels(); ++ch) {
        const float* x = buffer->getReadPointer(ch);
        for (int b = 0; b < buckets; ++b) {
            const int from = b * kSamplesPerBucket;
            const int to = std::min(n, from + kSamplesPerBucket);
            const auto [lo, hi] = std::minmax_element(x + from, x + to);
            peaks.minimum[static_cast<size_t>(b)] = std::min(peaks.minimum[static_cast<size_t>(b)], *lo);
            peaks.maximum[static_cast<size_t>(b)] = std::max(peaks.maximum[static_cast<size_t>(b)], *hi);
        }
    }
    return cache_.emplace(buffer.get(), std::move(peaks)).first->second;
}

void WaveformCache::prune(const Session& session) {
    std::set<const AudioBuffer*> used;
    for (int lane = 0; lane < session.getNumTracks(); ++lane)
        for (const Clip& c : session.getClips(session.getTrackId(lane)))
            used.insert(c.source.get());
    for (auto it = cache_.begin(); it != cache_.end();)
        it = used.count(it->first) ? std::next(it) : cache_.erase(it);
}

// ---- ArrangementView ----------------------------------------------------------------------------

ArrangementView::ArrangementView(Session& session, const AudioHost* audioHost)
    : session_(session), audioHost_(audioHost) {
    session_.addChangeListener(this);
    setOpaque(true);
    scrollBar_.setAutoHide(false);
    scrollBar_.addListener(this);
    addChildComponent(scrollBar_);
    selectedTrack_ = session_.getTrackId(0);
}

ArrangementView::~ArrangementView() { session_.removeChangeListener(this); }

void ArrangementView::changeListenerCallback(juce::ChangeBroadcaster*) {
    waveforms_.prune(session_);
    // Tracks may have come or gone: the selection must name an existing track, and the
    // scroll position must stay inside the (possibly shorter) list of lanes.
    // The same for the automation lanes: a track or an effect that went takes its lane off the screen.
    shownLanes_.erase(std::remove_if(shownLanes_.begin(), shownLanes_.end(),
                                     [this](const auto& shown) {
                                         return !session_.hasTrack(shown.first) ||
                                                (shown.second.effect.isValid() &&
                                                 session_.slotOfEffect(session_.trackRef(shown.first), shown.second.effect) < 0);
                                     }),
                      shownLanes_.end());
    if (!session_.hasTrack(selectedTrack_) && session_.getNumTracks() > 0)
        selectLane(selectedLane_);
    else if (session_.hasTrack(selectedTrack_))
        selectedLane_ = session_.indexOf(selectedTrack_);
    setScrollY(scrollY_);
    updateScrollBar();
    repaint();
}

void ArrangementView::setView(double viewStartSamples, double samplesPerPixel) {
    viewStart_ = std::max(0.0, viewStartSamples);
    samplesPerPixel_ = std::clamp(samplesPerPixel, 4.0, 1.0e6);
    repaint();
}

void ArrangementView::zoomToFit() {
    const auto& tr = session_.getEngine().getTransport();
    const double end = std::max<double>(static_cast<double>(session_.getSongEndSample()), 4.0 * tr.getSamplesPerBar()) +
                       tr.getSamplesPerBar();
    const int width = std::max(100, getWidth() - kHeaderWidth);
    setView(0.0, end / width);
}

void ArrangementView::zoom(double factor, float aroundX) {
    const double anchor = xToSample(aroundX);
    const double spp = std::clamp(samplesPerPixel_ / factor, 4.0, 1.0e6);
    setView(anchor - (aroundX - kHeaderWidth) * spp, spp);
}

float ArrangementView::sampleToX(double sample) const noexcept {
    return static_cast<float>(kHeaderWidth + (sample - viewStart_) / samplesPerPixel_);
}

double ArrangementView::xToSample(float x) const noexcept { return viewStart_ + (x - kHeaderWidth) * samplesPerPixel_; }

int ArrangementView::getLaneHeight() const noexcept {
    const int lanes = std::max(1, session_.getNumTracks());
    const int automation = static_cast<int>(shownLanes_.size()) * kAutomationHeight;
    return std::max(kMinLaneHeight, (getHeight() - kRulerHeight - automation) / lanes);
}

int ArrangementView::rowTop(int lane) const noexcept {
    int top = lane * getLaneHeight();
    for (const auto& shown : shownLanes_)
        if (session_.indexOf(shown.first) < lane)
            top += kAutomationHeight;
    return top;
}

int ArrangementView::rowsHeight() const noexcept { return rowTop(session_.getNumTracks()); }

juce::Rectangle<int> ArrangementView::getLaneBounds(int lane) const noexcept {
    return {0, kRulerHeight + rowTop(lane) - scrollY_, getWidth(), getLaneHeight()};
}

juce::Rectangle<int> ArrangementView::getAutomationBounds(int lane) const noexcept {
    if (!isAutomationShown(session_.getTrackId(lane)))
        return {};
    return {0, getLaneBounds(lane).getBottom(), getWidth(), kAutomationHeight};
}

int ArrangementView::laneAt(int y) const noexcept {
    if (y < kRulerHeight)
        return -1;
    for (int lane = 0; lane < session_.getNumTracks(); ++lane) {
        const auto row = getLaneBounds(lane);
        if (y >= row.getY() && y < row.getBottom() + getAutomationBounds(lane).getHeight())
            return lane;
    }
    return -1;
}

int ArrangementView::automationLaneAt(int y) const noexcept {
    const int lane = laneAt(y);
    return lane >= 0 && getAutomationBounds(lane).contains(0, y) ? lane : -1;
}

int ArrangementView::getMaxScrollY() const noexcept { return std::max(0, rowsHeight() - (getHeight() - kRulerHeight)); }

void ArrangementView::setScrollY(int pixels) {
    pixels = std::clamp(pixels, 0, getMaxScrollY());
    if (pixels != scrollY_) {
        scrollY_ = pixels;
        repaint();
    }
    scrollBar_.setCurrentRangeStart(scrollY_, juce::dontSendNotification);
}

void ArrangementView::scrollToTrack(engine::TrackId track) {
    const int lane = session_.indexOf(track);
    if (lane < 0)
        return;
    const int top = rowTop(lane);
    const int height = rowTop(lane + 1) - top;
    const int visible = getHeight() - kRulerHeight;
    if (top < scrollY_)
        setScrollY(top);
    else if (top + height > scrollY_ + visible)
        setScrollY(top + height - visible);
}

void ArrangementView::updateScrollBar() {
    const int visible = std::max(0, getHeight() - kRulerHeight);
    scrollBar_.setRangeLimits(0.0, static_cast<double>(rowsHeight()), juce::dontSendNotification);
    scrollBar_.setCurrentRange(scrollY_, visible, juce::dontSendNotification);
    scrollBar_.setVisible(getMaxScrollY() > 0);
}

juce::Rectangle<int> ArrangementView::getClipBounds(int laneIndex, int clipIndex) const {
    const auto& clips = session_.getClips(session_.getTrackId(laneIndex));
    if (clipIndex < 0 || clipIndex >= static_cast<int>(clips.size()))
        return {};
    const Clip& c = clips[static_cast<size_t>(clipIndex)];
    const auto lane = getLaneBounds(laneIndex).reduced(0, 3);
    const float x0 = sampleToX(static_cast<double>(c.start));
    const float x1 = sampleToX(static_cast<double>(c.end()));
    return juce::Rectangle<float>(x0, static_cast<float>(lane.getY()), std::max(2.0f, x1 - x0),
                                  static_cast<float>(lane.getHeight()))
        .getSmallestIntegerContainer();
}

int64_t ArrangementView::snapToBeat(double sample, bool snap) const noexcept {
    const double beat = session_.getEngine().getTransport().getSamplesPerBeat();
    if (snap && beat > 0.0)
        sample = std::round(sample / beat) * beat;
    return std::max<int64_t>(0, static_cast<int64_t>(std::llround(sample)));
}

void ArrangementView::setSelectedTrack(engine::TrackId track) {
    if (!session_.hasTrack(track))
        return;
    if (track != selectedTrack_) {
        selectedTrack_ = track;
        repaint();
    }
    selectedLane_ = session_.indexOf(track);
    scrollToTrack(track);
    if (onTrackSelected)
        onTrackSelected(track);
}

void ArrangementView::selectLane(int lane) {
    if (session_.getNumTracks() > 0)
        setSelectedTrack(session_.getTrackId(std::clamp(lane, 0, session_.getNumTracks() - 1)));
}

int64_t ArrangementView::getPlayheadPosition() const noexcept {
    // The listener hears the sample the engine rendered (mix latency + device latency) ago.
    return audioHost_ != nullptr ? audioHost_->getAudiblePosition() : session_.getEngine().getTransport().getPosition();
}

void ArrangementView::update() {
    const auto& tr = session_.getEngine().getTransport();
    const double pos = static_cast<double>(getPlayheadPosition());
    // Page-scroll while playing, like most DAWs: when the playhead leaves the view, jump one page.
    const double visible = (getWidth() - kHeaderWidth) * samplesPerPixel_;
    if (tr.isPlaying() && drag_ == Drag::None && visible > 0.0 && (pos < viewStart_ || pos > viewStart_ + visible * 0.95))
        setView(pos - visible * 0.05, samplesPerPixel_);

    const float x = sampleToX(pos);
    if (std::abs(x - lastPlayheadX_) >= 0.5f) {
        // Repaint only two thin strips: the old and the new playhead position.
        repaint(static_cast<int>(lastPlayheadX_) - 6, 0, 13, getHeight());
        repaint(static_cast<int>(x) - 6, 0, 13, getHeight());
        lastPlayheadX_ = x;
    }
}

// ---- painting -------------------------------------------------------------------------------------

void ArrangementView::paint(juce::Graphics& g) {
    g.fillAll(DawColours::background());
    {
        // Lanes scroll under the ruler: draw only the visible ones, clipped below the ruler.
        juce::Graphics::ScopedSaveState save(g);
        g.reduceClipRegion(0, kRulerHeight, getWidth(), getHeight() - kRulerHeight);
        for (int lane = 0; lane < session_.getNumTracks(); ++lane) {
            const auto bounds = getLaneBounds(lane);
            if (bounds.getY() >= getHeight())
                break;
            const auto automation = getAutomationBounds(lane);
            if (bounds.getBottom() + automation.getHeight() <= kRulerHeight)
                continue;
            paintLane(g, lane, bounds);
            if (!automation.isEmpty())
                paintAutomation(g, lane, automation);
        }
    }
    paintRuler(g, {0, 0, getWidth(), kRulerHeight});

    // playhead
    const float x = sampleToX(static_cast<double>(getPlayheadPosition()));
    if (x >= kHeaderWidth) {
        g.setColour(DawColours::accent());
        g.drawLine(x, 0.0f, x, static_cast<float>(getHeight()), 1.5f);
        juce::Path tri;
        tri.addTriangle(x - 6.0f, 0.0f, x + 6.0f, 0.0f, x, 9.0f);
        g.fillPath(tri);
    }

    // drop target while audio files are dragged over the view
    if (dropLane_ >= 0) {
        const auto lane = getLaneBounds(dropLane_);
        g.setColour(DawColours::accent().withAlpha(0.18f));
        g.fillRect(lane);
        g.setColour(DawColours::accent());
        g.drawRect(lane, 2);
        g.setFont(DawLookAndFeel::uiFont(13.0f));
        g.drawText("Drop to load into " + session_.getStripName(session_.trackRef(session_.getTrackId(dropLane_))),
                   lane.withTrimmedLeft(kHeaderWidth).reduced(10, 0), juce::Justification::centredLeft);
    }
}

// ---- drag and drop from the file browser ----------------------------------------------------------

bool ArrangementView::isInterestedInFileDrag(const juce::StringArray& files) {
    for (const auto& path : files)
        if (MediaLoader::canRead(juce::File(path)))
            return true;
    return false;
}

void ArrangementView::fileDragEnter(const juce::StringArray& files, int x, int y) { fileDragMove(files, x, y); }

void ArrangementView::fileDragMove(const juce::StringArray&, int, int y) {
    const int lane = laneAt(y);
    if (lane != dropLane_) {
        dropLane_ = lane;
        repaint();
    }
}

void ArrangementView::fileDragExit(const juce::StringArray&) {
    dropLane_ = -1;
    repaint();
}

void ArrangementView::filesDropped(const juce::StringArray& files, int, int y) {
    const engine::TrackId track = session_.getTrackId(laneAt(y));
    dropLane_ = -1;
    repaint();
    if (!track.isValid())
        return;
    juce::StringArray readable;
    for (const auto& path : files)
        if (MediaLoader::canRead(juce::File(path)))
            readable.add(path);
    if (!readable.isEmpty() && onFilesDropped)
        onFilesDropped(track, readable);
}

void ArrangementView::paintRuler(juce::Graphics& g, juce::Rectangle<int> area) {
    const auto& tr = session_.getEngine().getTransport();
    g.setColour(DawColours::panel());
    g.fillRect(area);
    g.setColour(DawColours::textDim());
    g.setFont(DawLookAndFeel::uiFont(12.0f));
    g.drawText("bar", area.withWidth(kHeaderWidth).reduced(8, 0), juce::Justification::centredLeft);

    // loop range
    const float lx0 = std::max<float>(kHeaderWidth, sampleToX(static_cast<double>(tr.getLoopStart())));
    const float lx1 = sampleToX(static_cast<double>(tr.getLoopEnd()));
    if (lx1 > lx0) {
        g.setColour(DawColours::accentWarm().withAlpha(tr.isLoopEnabled() ? 0.45f : 0.12f));
        g.fillRect(juce::Rectangle<float>(lx0, static_cast<float>(area.getBottom() - 8), lx1 - lx0, 8.0f));
    }

    // bar lines and numbers, beat ticks
    const double beat = tr.getSamplesPerBeat();
    const int beatsPerBar = tr.getTimeSignature().numerator;
    if (beat <= 0.0)
        return;
    const double pixelsPerBeat = beat / samplesPerPixel_;
    const int barStep = pixelsPerBeat * beatsPerBar < 30.0 ? 4 : 1; // fewer numbers when zoomed out
    const auto firstBeat = static_cast<juce::int64>(std::floor(viewStart_ / beat));
    const auto lastBeat = static_cast<juce::int64>(std::ceil(xToSample(static_cast<float>(getWidth())) / beat));
    for (auto b = std::max<juce::int64>(0, firstBeat); b <= lastBeat; ++b) {
        const float x = sampleToX(static_cast<double>(b) * beat);
        if (x < kHeaderWidth)
            continue;
        const bool isBar = b % beatsPerBar == 0;
        const auto bar = b / beatsPerBar + 1;
        if (isBar) {
            g.setColour(DawColours::textDim());
            g.drawVerticalLine(static_cast<int>(x), static_cast<float>(area.getY() + 4), static_cast<float>(area.getBottom()));
            if ((bar - 1) % barStep == 0) {
                g.setColour(DawColours::text());
                g.drawText(juce::String(bar), static_cast<int>(x) + 3, area.getY() + 2, 40, 14, juce::Justification::left);
            }
        } else if (pixelsPerBeat > 6.0) {
            g.setColour(DawColours::outline());
            g.drawVerticalLine(static_cast<int>(x), static_cast<float>(area.getBottom() - 6), static_cast<float>(area.getBottom()));
        }
    }
}

void ArrangementView::paintLane(juce::Graphics& g, int lane, juce::Rectangle<int> area) {
    const auto& tr = session_.getEngine().getTransport();
    const engine::TrackId track = session_.getTrackId(lane);
    const bool selected = track == selectedTrack_;
    const engine::ChannelStrip* strip = session_.getEngine().getStrip(session_.trackRef(track));
    if (strip == nullptr)
        return;

    // lane background + bar grid
    g.setColour(selected ? DawColours::panelLight().withAlpha(0.6f) : (lane % 2 ? DawColours::background() : DawColours::background().brighter(0.03f)));
    g.fillRect(area);
    const double bar = tr.getSamplesPerBar();
    if (bar > 0.0 && bar / samplesPerPixel_ > 8.0) {
        g.setColour(DawColours::outline().withAlpha(0.5f));
        for (double s = std::floor(viewStart_ / bar) * bar; sampleToX(s) < getWidth(); s += bar)
            if (sampleToX(s) >= kHeaderWidth)
                g.drawVerticalLine(static_cast<int>(sampleToX(s)), static_cast<float>(area.getY()), static_cast<float>(area.getBottom()));
    }

    // clips
    const auto& clips = session_.getClips(track);
    {
        juce::Graphics::ScopedSaveState save(g);
        g.reduceClipRegion(area.withTrimmedLeft(kHeaderWidth));
        for (int i = 0; i < static_cast<int>(clips.size()); ++i) {
            const auto bounds = getClipBounds(lane, i);
            if (bounds.getRight() < kHeaderWidth || bounds.getX() > getWidth())
                continue;
            paintClip(g, track, clips[static_cast<size_t>(i)], bounds,
                      drag_ == Drag::Clip && dragTrack_ == track && dragClip_ == i);
        }
        paintLiveTake(g, track, area);
    }

    // header
    auto header = area.withWidth(kHeaderWidth);
    g.setColour(DawColours::panel());
    g.fillRect(header);
    g.setColour(DawColours::track(track));
    g.fillRect(header.removeFromLeft(5));
    header.reduce(8, 2);
    g.setColour(selected ? DawColours::text() : DawColours::text().withAlpha(0.8f));
    g.setFont(DawLookAndFeel::uiFont(14.0f, selected));
    g.drawText(juce::String(lane + 1) + "  " + juce::String(strip->getName()), header.removeFromTop(header.getHeight() / 2 + 4),
               juce::Justification::bottomLeft);
    g.setFont(DawLookAndFeel::uiFont(11.0f));
    juce::String info;
    if (strip->isMuted())
        info << "M ";
    if (strip->isSoloed())
        info << "S ";
    info << juce::String(static_cast<int>(clips.size())) << (clips.size() == 1 ? " clip" : " clips");
    g.setColour(strip->isMuted() ? DawColours::mute() : (strip->isSoloed() ? DawColours::accentWarm() : DawColours::textDim()));
    g.drawText(info, header, juce::Justification::topLeft);
    const auto toggle = getAutomationToggleBounds(lane);
    const bool shown = isAutomationShown(track);
    g.setColour(shown ? DawColours::accent() : DawColours::outline());
    g.drawRect(toggle);
    g.setColour(shown ? DawColours::accent() : DawColours::textDim());
    g.setFont(DawLookAndFeel::uiFont(11.0f, shown));
    g.drawText("A", toggle, juce::Justification::centred);

    const engine::Track* recordable = session_.getEngine().getTrack(track);
    const bool armed = recordable != nullptr && recordable->isArmed();
    const auto armBox = getArmBounds(lane);
    g.setColour(armed ? DawColours::mute() : DawColours::outline());
    if (armed)
        g.fillRect(armBox);
    else
        g.drawRect(armBox);
    g.setColour(armed ? juce::Colours::white : DawColours::textDim());
    g.setFont(DawLookAndFeel::uiFont(11.0f, armed));
    g.drawText("R", armBox, juce::Justification::centred);
    const engine::MonitorMode mode = recordable != nullptr ? recordable->getMonitor().mode() : engine::MonitorMode::Off;
    const bool monitoring = mode != engine::MonitorMode::Off;
    const bool howling = recordable != nullptr && recordable->getMonitor().feedbackDetected();
    const auto monitorBox = getMonitorBounds(lane);
    g.setColour(howling ? DawColours::mute() : (monitoring ? DawColours::accentWarm() : DawColours::outline()));
    g.drawRect(monitorBox);
    g.setColour(howling ? DawColours::mute() : (monitoring ? DawColours::accentWarm() : DawColours::textDim()));
    g.setFont(DawLookAndFeel::uiFont(11.0f, monitoring));
    g.drawText(mode == engine::MonitorMode::Auto ? "a" : "I", monitorBox, juce::Justification::centred);

    g.setColour(DawColours::outline());
    g.drawHorizontalLine(area.getBottom() - 1, 0.0f, static_cast<float>(getWidth()));
    g.drawVerticalLine(kHeaderWidth - 1, static_cast<float>(area.getY()), static_cast<float>(area.getBottom()));
}

void ArrangementView::paintClip(juce::Graphics& g, engine::TrackId track, const Clip& clip, juce::Rectangle<int> bounds,
                                bool selected) {
    const auto colour = DawColours::track(track);
    const auto r = bounds.toFloat().reduced(0.5f, 0.0f);
    g.setColour(colour.withAlpha(clip.muted ? 0.08f : 0.22f));
    g.fillRoundedRectangle(r, 3.0f);
    g.setColour(selected ? DawColours::text() : colour.withAlpha(0.8f));
    g.drawRoundedRectangle(r, 3.0f, selected ? 1.5f : 1.0f);

    // waveform: one vertical line per pixel column from the min to the max of the buckets it covers
    if (clip.source != nullptr && clip.source->getNumSamples() > 0) {
        const auto& peaks = waveforms_.get(clip.source);
        const float gain = dbToGain(clip.gainDb);
        const float midY = r.getCentreY() + 5.0f;
        const float halfH = (r.getHeight() - 14.0f) * 0.5f;
        const int x0 = std::max(bounds.getX(), kHeaderWidth);
        const int x1 = std::min(bounds.getRight(), getWidth());
        g.setColour(colour.withAlpha(0.85f));
        const int numBuckets = static_cast<int>(peaks.minimum.size());
        for (int px = x0; px < x1; ++px) {
            const double t0 = xToSample(static_cast<float>(px)) - static_cast<double>(clip.start) + static_cast<double>(clip.sourceOffset);
            const double t1 = t0 + samplesPerPixel_;
            int b0 = static_cast<int>(std::floor(t0 / WaveformCache::kSamplesPerBucket));
            int b1 = static_cast<int>(std::ceil(t1 / WaveformCache::kSamplesPerBucket));
            b0 = std::clamp(b0, 0, numBuckets);
            b1 = std::clamp(std::max(b1, b0 + 1), 0, numBuckets);
            if (b0 >= b1)
                continue;
            float lo = 0.0f, hi = 0.0f;
            for (int b = b0; b < b1; ++b) {
                lo = std::min(lo, peaks.minimum[static_cast<size_t>(b)]);
                hi = std::max(hi, peaks.maximum[static_cast<size_t>(b)]);
            }
            const float yTop = midY - std::min(1.0f, hi * gain) * halfH;
            const float yBottom = midY - std::max(-1.0f, lo * gain) * halfH;
            g.drawVerticalLine(px, yTop, std::max(yBottom, yTop + 1.0f));
        }
    }
    if (bounds.getWidth() > 30) {
        g.setColour(DawColours::text().withAlpha(0.9f));
        g.setFont(DawLookAndFeel::uiFont(11.0f));
        g.drawText(juce::String(clip.name), bounds.reduced(4, 1).withHeight(13), juce::Justification::topLeft, true);
    }
}

void ArrangementView::resized() {
    if (samplesPerPixel_ == 1000.0) // first layout: fit the song
        zoomToFit();
    scrollBar_.setBounds(getWidth() - kScrollBarWidth, kRulerHeight, kScrollBarWidth, std::max(0, getHeight() - kRulerHeight));
    setScrollY(scrollY_);
    updateScrollBar();
}

// ---- mouse ----------------------------------------------------------------------------------------

int ArrangementView::clipAt(int lane, float x) const {
    const auto& clips = session_.getClips(session_.getTrackId(lane));
    const double s = xToSample(x);
    // Topmost = last drawn = highest index.
    for (int i = static_cast<int>(clips.size()) - 1; i >= 0; --i) {
        const Clip& c = clips[static_cast<size_t>(i)];
        if (s >= static_cast<double>(c.start) && s < static_cast<double>(c.end()))
            return i;
    }
    return -1;
}

void ArrangementView::seekToX(float x, bool snap) {
    session_.getEngine().getTransport().setPosition(snapToBeat(xToSample(std::max<float>(x, kHeaderWidth)), snap));
    repaint();
}

void ArrangementView::mouseDown(const juce::MouseEvent& e) {
    const bool snap = !e.mods.isAltDown();
    drag_ = Drag::None;
    if (e.y < kRulerHeight) {
        drag_ = Drag::Seek;
        seekToX(static_cast<float>(e.x), snap);
        return;
    }
    const int lane = laneAt(e.y);
    if (lane < 0)
        return;
    const engine::TrackId track = session_.getTrackId(lane);
    setSelectedTrack(track);
    if (automationLaneAt(e.y) == lane) {
        mouseDownInAutomation(e, lane);
        return;
    }
    if (getAutomationToggleBounds(lane).contains(e.x, e.y)) {
        toggleAutomation(track);
        return;
    }
    if (getArmBounds(lane).contains(e.x, e.y) || getMonitorBounds(lane).contains(e.x, e.y)) {
        if (e.mods.isPopupMenu()) {
            if (onRecordMenu)
                onRecordMenu(track);
        } else if (getArmBounds(lane).contains(e.x, e.y)) {
            if (onArmClicked)
                onArmClicked(track);
        } else if (onMonitorClicked) {
            onMonitorClicked(track);
        }
        repaint();
        return;
    }
    if (e.x < kHeaderWidth)
        return;
    const int clip = clipAt(lane, static_cast<float>(e.x));
    if (clip >= 0 && e.mods.isPopupMenu()) {
        if (onClipMenu)
            onClipMenu(track, clip);
        return;
    }
    if (clip >= 0) {
        session_.getHistory().endMerge(); // this drag is its own undo step (Step 9)
        drag_ = Drag::Clip;
        dragTrack_ = track;
        dragClip_ = clip;
        dragGrabOffset_ = xToSample(static_cast<float>(e.x)) -
                          static_cast<double>(session_.getClips(track)[static_cast<size_t>(clip)].start);
        repaint();
    } else if (e.getNumberOfClicks() > 1) {
        seekToX(static_cast<float>(e.x), snap);
    }
}

void ArrangementView::mouseDrag(const juce::MouseEvent& e) {
    const bool snap = !e.mods.isAltDown();
    if (drag_ == Drag::Seek) {
        seekToX(static_cast<float>(e.x), snap);
    } else if (drag_ == Drag::Clip) {
        const auto& clips = session_.getClips(dragTrack_);
        if (dragClip_ < 0 || dragClip_ >= static_cast<int>(clips.size()))
            return;
        const int64_t newStart = snapToBeat(xToSample(static_cast<float>(e.x)) - dragGrabOffset_, snap);
        if (newStart != clips[static_cast<size_t>(dragClip_)].start)
            dragClip_ = session_.moveClip(dragTrack_, dragClip_, newStart); // -> repaint via change message
    } else if (drag_ == Drag::Point) {
        if (const LaneAddress* address = getShownLane(dragTrack_))
            dragPoint_ = session_.movePoint(*address, dragPoint_, snapToBeat(xToSample(static_cast<float>(e.x)), snap),
                                            yToValue(dragLane_, static_cast<float>(e.y)), dragPointKey_);
    }
}

void ArrangementView::mouseUp(const juce::MouseEvent&) {
    if (drag_ == Drag::Clip || drag_ == Drag::Point)
        session_.getHistory().endMerge();
    drag_ = Drag::None;
    dragClip_ = -1;
    dragPoint_ = -1;
    repaint();
}

void ArrangementView::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    if (e.mods.isCtrlDown() || e.mods.isCommandDown()) {
        zoom(wheel.deltaY > 0 ? 1.25 : 0.8, static_cast<float>(e.x));
    } else if (getMaxScrollY() > 0 && !e.mods.isShiftDown() && std::abs(wheel.deltaY) >= std::abs(wheel.deltaX)) {
        setScrollY(scrollY_ - static_cast<int>(std::lround(wheel.deltaY * 4.0f * static_cast<float>(getLaneHeight()))));
    } else {
        const float delta = std::abs(wheel.deltaX) > std::abs(wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
        setView(viewStart_ - delta * 200.0 * samplesPerPixel_, samplesPerPixel_);
    }
}

// ---- automation lanes (chapter 16) ------------------------------------------------------------------

const LaneAddress* ArrangementView::getShownLane(engine::TrackId track) const noexcept {
    for (const auto& shown : shownLanes_)
        if (shown.first == track)
            return &shown.second;
    return nullptr;
}

bool ArrangementView::isAutomationShown(engine::TrackId track) const noexcept { return getShownLane(track) != nullptr; }

void ArrangementView::showAutomation(engine::TrackId track, const LaneAddress& address) {
    if (!session_.hasTrack(track))
        return;
    hideAutomation(track);
    shownLanes_.emplace_back(track, address);
    setScrollY(scrollY_);
    updateScrollBar();
    repaint();
}

void ArrangementView::hideAutomation(engine::TrackId track) {
    shownLanes_.erase(std::remove_if(shownLanes_.begin(), shownLanes_.end(), [track](const auto& shown) { return shown.first == track; }),
                      shownLanes_.end());
    setScrollY(scrollY_);
    updateScrollBar();
    repaint();
}

void ArrangementView::toggleAutomation(engine::TrackId track) {
    if (isAutomationShown(track))
        hideAutomation(track);
    else
        showAutomation(track, {StripTarget::ofTrack(track), engine::EffectId{}, "Gain"});
}

juce::Rectangle<int> ArrangementView::getArmBounds(int lane) const noexcept {
    // Under the "A", and low enough to fit the lowest row.
    return getAutomationToggleBounds(lane).translated(0, 17).withHeight(14);
}

juce::Rectangle<int> ArrangementView::getMonitorBounds(int lane) const noexcept {
    return getArmBounds(lane).translated(-20, 0);
}

// The last pass of what has come in so far: what is being played now.
void ArrangementView::paintLiveTake(juce::Graphics& g, engine::TrackId track, juce::Rectangle<int> area) {
    engine::LiveTake live;
    if (!liveTakeOf || !liveTakeOf(track, live))
        return;
    const std::size_t numPeaks = live.trail->size();
    const auto numFrames = static_cast<std::int64_t>(numPeaks) * engine::PeakTrail::kFramesPerPeak;
    const std::vector<engine::Placement> takes = engine::takesOf(live.stampedPosition, live.latencies, live.range, numFrames);
    if (takes.empty())
        return;
    const engine::Placement& take = takes.back();
    const float x0 = sampleToX(static_cast<double>(take.timelineStart));
    const float x1 = sampleToX(static_cast<double>(take.timelineStart + take.numFrames));
    const auto box = juce::Rectangle<float>(x0, static_cast<float>(area.getY() + 1), std::max(1.0f, x1 - x0), static_cast<float>(area.getHeight() - 2));
    g.setColour(DawColours::mute().withAlpha(0.25f));
    g.fillRect(box);
    g.setColour(DawColours::mute());
    g.drawRect(box);
    const float middle = box.getCentreY();
    const float half = box.getHeight() * 0.5f - 2.0f;
    const int left = std::max(static_cast<int>(x0), kHeaderWidth);
    const int right = std::min(static_cast<int>(x1), getWidth());
    for (int x = left; x < right; ++x) {
        const double from = (xToSample(static_cast<float>(x)) - static_cast<double>(take.timelineStart + 0)) + static_cast<double>(take.skipFrames);
        const double to = from + samplesPerPixel_;
        float peak = 0.0f;
        const auto first = static_cast<std::size_t>(std::max(0.0, from / engine::PeakTrail::kFramesPerPeak));
        const auto last = std::min(numPeaks, static_cast<std::size_t>(std::max(0.0, to / engine::PeakTrail::kFramesPerPeak)) + 1);
        for (std::size_t i = first; i < last; ++i)
            peak = std::max(peak, live.trail->at(i));
        const float h = std::min(1.0f, peak) * half;
        g.drawVerticalLine(x, middle - h, middle + h + 1.0f);
    }
}

juce::Rectangle<int> ArrangementView::getAutomationToggleBounds(int lane) const noexcept {
    const auto row = getLaneBounds(lane);
    return {kHeaderWidth - 22, row.getY() + 3, 18, 16};
}

float ArrangementView::valueToY(int lane, float value) const noexcept {
    const auto area = getAutomationBounds(lane).reduced(0, kPointRadius + 1);
    return static_cast<float>(area.getBottom()) - value * static_cast<float>(area.getHeight());
}

float ArrangementView::yToValue(int lane, float y) const noexcept {
    const auto area = getAutomationBounds(lane).reduced(0, kPointRadius + 1);
    return std::clamp((static_cast<float>(area.getBottom()) - y) / static_cast<float>(std::max(1, area.getHeight())), 0.0f, 1.0f);
}

int ArrangementView::pointAt(int lane, float x, float y) const {
    const LaneAddress* address = getShownLane(session_.getTrackId(lane));
    const LaneContent* content = address != nullptr ? session_.getLane(*address) : nullptr;
    if (content == nullptr)
        return -1;
    const auto& points = content->curve.points();
    const float reach = static_cast<float>(kPointRadius + 2);
    for (int i = static_cast<int>(points.size()) - 1; i >= 0; --i) { // the one on top
        const auto& point = points[static_cast<size_t>(i)];
        if (std::abs(sampleToX(static_cast<double>(point.position)) - x) <= reach && std::abs(valueToY(lane, point.value) - y) <= reach)
            return i;
    }
    return -1;
}

namespace {
const char* modeName(engine::AutomationMode mode) noexcept {
    switch (mode) {
    case engine::AutomationMode::Read: return "Read";
    case engine::AutomationMode::Touch: return "Touch";
    case engine::AutomationMode::Latch: return "Latch";
    case engine::AutomationMode::Write: return "Write";
    }
    return "Read";
}
} // namespace

void ArrangementView::paintAutomation(juce::Graphics& g, int lane, juce::Rectangle<int> area) {
    const engine::TrackId track = session_.getTrackId(lane);
    const LaneAddress* address = getShownLane(track);
    if (address == nullptr)
        return;
    const LaneContent* content = session_.getLane(*address);
    const bool live = session_.isLaneLive(*address);
    g.setColour(DawColours::background().darker(0.25f));
    g.fillRect(area);
    {
        juce::Graphics::ScopedSaveState save(g);
        g.reduceClipRegion(area.withTrimmedLeft(kHeaderWidth));
        g.setColour(DawColours::outline().withAlpha(0.5f));
        const float middle = valueToY(lane, 0.5f);
        g.drawHorizontalLine(static_cast<int>(middle), static_cast<float>(kHeaderWidth), static_cast<float>(getWidth()));
        if (content != nullptr && content->curve.size() > 0) {
            // One value per two pixels: the same curve the audio thread reads, Hold and jumps included.
            juce::Path line;
            for (int x = kHeaderWidth; x <= getWidth(); x += 2) {
                const auto position = static_cast<std::int64_t>(xToSample(static_cast<float>(x)));
                const float y = valueToY(lane, content->curve.valueAt(position));
                if (x == kHeaderWidth)
                    line.startNewSubPath(static_cast<float>(x), y);
                else
                    line.lineTo(static_cast<float>(x), y);
            }
            const juce::Colour colour = live ? DawColours::track(track) : DawColours::textDim();
            g.setColour(colour);
            g.strokePath(line, juce::PathStrokeType(1.5f));
            for (const engine::AutomationPoint& point : content->curve.points()) {
                const float x = sampleToX(static_cast<double>(point.position));
                const float y = valueToY(lane, point.value);
                g.setColour(colour.brighter(0.4f));
                g.fillEllipse(x - kPointRadius, y - kPointRadius, 2.0f * kPointRadius, 2.0f * kPointRadius);
            }
        } else {
            g.setColour(DawColours::textDim());
            g.setFont(DawLookAndFeel::uiFont(11.0f));
            g.drawText("Click to add a point", area.withTrimmedLeft(kHeaderWidth + 8), juce::Justification::centredLeft);
        }
    }

    auto header = area.withWidth(kHeaderWidth);
    g.setColour(DawColours::panel().darker(0.15f));
    g.fillRect(header);
    g.setColour(DawColours::track(track).withAlpha(0.5f));
    g.fillRect(header.removeFromLeft(5));
    header.reduce(8, 4);
    const int effectSlot = session_.slotOfEffect(session_.trackRef(track), address->effect);
    const juce::String owner = effectSlot >= 0 ? session_.getSlot(session_.trackRef(track), effectSlot).name + ": " : juce::String();
    g.setColour(live ? DawColours::text() : DawColours::mute());
    g.setFont(DawLookAndFeel::uiFont(12.0f));
    g.drawText(owner + address->parameter + (live ? "" : " (unknown)"), header.removeFromTop(header.getHeight() / 2), juce::Justification::centredLeft, true);
    const auto mode = content != nullptr ? content->mode : engine::AutomationMode::Read;
    g.setColour(mode == engine::AutomationMode::Read ? DawColours::textDim() : DawColours::accentWarm());
    g.drawText(modeName(mode), header, juce::Justification::centredLeft);
    g.setColour(DawColours::outline());
    g.drawHorizontalLine(area.getBottom() - 1, 0.0f, static_cast<float>(getWidth()));
    g.drawVerticalLine(kHeaderWidth - 1, static_cast<float>(area.getY()), static_cast<float>(area.getBottom()));
}

void ArrangementView::mouseDownInAutomation(const juce::MouseEvent& e, int lane) {
    const engine::TrackId track = session_.getTrackId(lane);
    const LaneAddress* address = getShownLane(track);
    if (address == nullptr)
        return;
    if (e.x < kHeaderWidth) { // the upper half is the parameter, the lower half the mode
        if (e.y < getAutomationBounds(lane).getCentreY())
            showParameterMenu(track);
        else
            showModeMenu(track);
        return;
    }
    const LaneAddress target = *address; // the edits below may change what `address` points into
    const auto x = static_cast<float>(e.x), y = static_cast<float>(e.y);
    const int hit = pointAt(lane, x, y);
    if (e.mods.isPopupMenu()) {
        if (hit >= 0)
            session_.removePoint(target, hit);
        return;
    }
    session_.getHistory().endMerge();
    static std::uint32_t nextKey = 0x4c000000u; // "L": every drag of a point its own undo step
    dragPointKey_ = ++nextKey;
    dragTrack_ = track;
    dragLane_ = lane;
    dragPoint_ = hit >= 0 ? hit
                          : session_.addPoint(target, snapToBeat(xToSample(x), !e.mods.isAltDown()), yToValue(lane, y), dragPointKey_);
    if (dragPoint_ >= 0)
        drag_ = Drag::Point;
}

void ArrangementView::showParameterMenu(engine::TrackId track) {
    const auto parameters = session_.getAutomatableParameters(session_.trackRef(track));
    juce::PopupMenu menu;
    for (size_t i = 0; i < parameters.size(); ++i) {
        const LaneAddress* shown = getShownLane(track);
        menu.addItem(static_cast<int>(i) + 1, parameters[i].first, true, shown != nullptr && *shown == parameters[i].second);
    }
    juce::Component::SafePointer<ArrangementView> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [safe, track, parameters](int chosen) {
        if (safe != nullptr && chosen > 0)
            safe->showAutomation(track, parameters[static_cast<size_t>(chosen) - 1].second);
    });
}

void ArrangementView::showModeMenu(engine::TrackId track) {
    const LaneAddress* shown = getShownLane(track);
    if (shown == nullptr)
        return;
    const LaneAddress address = *shown;
    const LaneContent* content = session_.getLane(address);
    const auto current = content != nullptr ? content->mode : engine::AutomationMode::Read;
    juce::PopupMenu menu;
    const engine::AutomationMode modes[] = {engine::AutomationMode::Read, engine::AutomationMode::Touch, engine::AutomationMode::Latch,
                                            engine::AutomationMode::Write};
    for (const auto mode : modes)
        menu.addItem(static_cast<int>(mode) + 1, modeName(mode), true, mode == current);
    juce::Component::SafePointer<ArrangementView> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [safe, address](int chosen) {
        if (safe != nullptr && chosen > 0)
            safe->session_.setLaneMode(address, static_cast<engine::AutomationMode>(chosen - 1));
    });
}

} // namespace dawlab::app
