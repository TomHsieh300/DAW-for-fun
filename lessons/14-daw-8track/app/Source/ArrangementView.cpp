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
    for (int t = 0; t < engine::kNumTracks; ++t)
        for (const Clip& c : session.getClips(t))
            used.insert(c.source.get());
    for (auto it = cache_.begin(); it != cache_.end();)
        it = used.count(it->first) ? std::next(it) : cache_.erase(it);
}

// ---- ArrangementView ----------------------------------------------------------------------------

ArrangementView::ArrangementView(Session& session, const AudioHost* audioHost)
    : session_(session), audioHost_(audioHost) {
    session_.addChangeListener(this);
    setOpaque(true);
}

ArrangementView::~ArrangementView() { session_.removeChangeListener(this); }

void ArrangementView::changeListenerCallback(juce::ChangeBroadcaster*) {
    waveforms_.prune(session_);
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

juce::Rectangle<int> ArrangementView::getLaneBounds(int track) const noexcept {
    const int laneH = (getHeight() - kRulerHeight) / engine::kNumTracks;
    return {0, kRulerHeight + track * laneH, getWidth(), laneH};
}

int ArrangementView::laneAt(int y) const noexcept {
    if (y < kRulerHeight)
        return -1;
    const int laneH = std::max(1, (getHeight() - kRulerHeight) / engine::kNumTracks);
    const int lane = (y - kRulerHeight) / laneH;
    return lane < engine::kNumTracks ? lane : -1;
}

juce::Rectangle<int> ArrangementView::getClipBounds(int track, int clipIndex) const {
    const auto& clips = session_.getClips(track);
    if (clipIndex < 0 || clipIndex >= static_cast<int>(clips.size()))
        return {};
    const Clip& c = clips[static_cast<size_t>(clipIndex)];
    const auto lane = getLaneBounds(track).reduced(0, 3);
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

void ArrangementView::setSelectedTrack(int track) {
    track = std::clamp(track, 0, engine::kNumTracks - 1);
    if (track != selectedTrack_) {
        selectedTrack_ = track;
        repaint();
    }
    if (onTrackSelected)
        onTrackSelected(track);
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
    paintRuler(g, {0, 0, getWidth(), kRulerHeight});
    for (int t = 0; t < engine::kNumTracks; ++t)
        paintLane(g, t, getLaneBounds(t));

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
    if (dropTrack_ >= 0) {
        const auto lane = getLaneBounds(dropTrack_);
        g.setColour(DawColours::accent().withAlpha(0.18f));
        g.fillRect(lane);
        g.setColour(DawColours::accent());
        g.drawRect(lane, 2);
        g.setFont(DawLookAndFeel::uiFont(13.0f));
        g.drawText("Drop to load into track " + juce::String(dropTrack_ + 1),
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
    const int track = laneAt(y);
    if (track != dropTrack_) {
        dropTrack_ = track;
        repaint();
    }
}

void ArrangementView::fileDragExit(const juce::StringArray&) {
    dropTrack_ = -1;
    repaint();
}

void ArrangementView::filesDropped(const juce::StringArray& files, int, int y) {
    const int track = laneAt(y);
    dropTrack_ = -1;
    repaint();
    if (track < 0)
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

void ArrangementView::paintLane(juce::Graphics& g, int track, juce::Rectangle<int> area) {
    const auto& tr = session_.getEngine().getTransport();
    const bool selected = track == selectedTrack_;
    const auto& strip = session_.getEngine().getMixer().getTrack(track);

    // lane background + bar grid
    g.setColour(selected ? DawColours::panelLight().withAlpha(0.6f) : (track % 2 ? DawColours::background() : DawColours::background().brighter(0.03f)));
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
            const auto bounds = getClipBounds(track, i);
            if (bounds.getRight() < kHeaderWidth || bounds.getX() > getWidth())
                continue;
            paintClip(g, track, clips[static_cast<size_t>(i)], bounds,
                      drag_ == Drag::Clip && dragTrack_ == track && dragClip_ == i);
        }
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
    g.drawText(juce::String(track + 1) + "  " + juce::String(strip.getName()), header.removeFromTop(header.getHeight() / 2 + 4),
               juce::Justification::bottomLeft);
    g.setFont(DawLookAndFeel::uiFont(11.0f));
    juce::String info;
    if (strip.isMuted())
        info << "M ";
    if (strip.isSoloed())
        info << "S ";
    info << juce::String(static_cast<int>(clips.size())) << (clips.size() == 1 ? " clip" : " clips");
    g.setColour(strip.isMuted() ? DawColours::mute() : (strip.isSoloed() ? DawColours::accentWarm() : DawColours::textDim()));
    g.drawText(info, header, juce::Justification::topLeft);

    g.setColour(DawColours::outline());
    g.drawHorizontalLine(area.getBottom() - 1, 0.0f, static_cast<float>(getWidth()));
    g.drawVerticalLine(kHeaderWidth - 1, static_cast<float>(area.getY()), static_cast<float>(area.getBottom()));
}

void ArrangementView::paintClip(juce::Graphics& g, int track, const Clip& clip, juce::Rectangle<int> bounds,
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
}

// ---- mouse ----------------------------------------------------------------------------------------

int ArrangementView::clipAt(int track, float x) const {
    const auto& clips = session_.getClips(track);
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
    setSelectedTrack(lane);
    if (e.x < kHeaderWidth)
        return;
    const int clip = clipAt(lane, static_cast<float>(e.x));
    if (clip >= 0) {
        drag_ = Drag::Clip;
        dragTrack_ = lane;
        dragClip_ = clip;
        dragGrabOffset_ = xToSample(static_cast<float>(e.x)) -
                          static_cast<double>(session_.getClips(lane)[static_cast<size_t>(clip)].start);
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
    }
}

void ArrangementView::mouseUp(const juce::MouseEvent&) {
    drag_ = Drag::None;
    dragClip_ = -1;
    repaint();
}

void ArrangementView::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    if (e.mods.isCtrlDown() || e.mods.isCommandDown()) {
        zoom(wheel.deltaY > 0 ? 1.25 : 0.8, static_cast<float>(e.x));
    } else {
        const float delta = std::abs(wheel.deltaX) > std::abs(wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
        setView(viewStart_ - delta * 200.0 * samplesPerPixel_, samplesPerPixel_);
    }
}

} // namespace dawlab::app
