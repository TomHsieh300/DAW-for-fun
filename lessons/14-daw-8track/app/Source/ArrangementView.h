#pragma once
// ArrangementView: the timeline with 8 lanes (Step 14).
//
//   +----------+----1--------2--------3--------4------ ruler (click / drag = seek)
//   | Drums    | [drums][drums]   [drums]  [drums]       one lane per track,
//   | Bass     | [b][b][b][b][b][b][b][b][b]...           clips with waveform thumbnails
//   | ...      |        |                                 playhead (cyan line) at the position
//                                                       heard now (latency-compensated)
//
// Coordinates: the view shows timeline samples [viewStart, viewStart + width * samplesPerPixel).
//
//   x(s) = headerWidth + (s - viewStart) / samplesPerPixel
//   s(x) = viewStart + (x - headerWidth) * samplesPerPixel
//
// Mouse:
//   ruler: click / drag      seek (snapped to the beat grid; hold Alt for no snapping)
//   clip:  drag              move the clip along its lane (snapped; Alt = free).
//                            Session::moveClip() builds a new clip list and sends it to the engine.
//   lane:  click             select the track (File > Load audio..., M / S shortcuts act on it)
//   wheel                    scroll in time;  Ctrl + wheel: zoom around the mouse
//
// Drag and drop: audio files dragged in from the file browser (Explorer / Finder) onto a lane
// load into that track; several files fill that track and the ones below it. The lane under the
// mouse is highlighted while dragging. Only the routing happens here: onFilesDropped hands the
// files to MainComponent, which loads them like File > Load audio... does.
//
// Waveform thumbnails: computing min/max over thousands of samples per pixel in paint() would
// be slow, so a WaveformCache reduces every source buffer once to min/max pairs per
// kSamplesPerBucket samples (a "peak file", like every DAW keeps next to its audio). paint()
// only combines buckets.

#include "Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>
#include <memory>
#include <vector>

namespace dawlab::app {

class AudioHost;

class WaveformCache {
public:
    static constexpr int kSamplesPerBucket = 128;

    struct Peaks {
        std::shared_ptr<const AudioBuffer> source; // keeps the buffer alive while cached
        std::vector<float> minimum, maximum;       // per bucket, over all channels
    };

    // Peaks of `buffer` (computed on first use).
    const Peaks& get(const std::shared_ptr<const AudioBuffer>& buffer);

    // Forgets buffers that are not used by any clip of `session` any more.
    void prune(const Session& session);

    std::size_t size() const noexcept { return cache_.size(); }

private:
    std::map<const AudioBuffer*, Peaks> cache_;
};

class ArrangementView final : public juce::Component,
                              public juce::FileDragAndDropTarget,
                              private juce::ChangeListener {
public:
    static constexpr int kRulerHeight = 26;
    static constexpr int kHeaderWidth = 132;

    // With an AudioHost the playhead shows what is heard (AudioHost::getAudiblePosition: the
    // transport position minus the mix latency and the device output latency while playing);
    // without one (exercise test benches) it shows the transport position itself.
    explicit ArrangementView(Session& session, const AudioHost* audioHost = nullptr);
    ~ArrangementView() override;

    // Follows the transport (message thread, 30 Hz): moves the playhead, scrolls while playing.
    void update();

    // The timeline sample under the playhead (see the constructor).
    int64_t getPlayheadPosition() const noexcept;

    // ---- view ----------------------------------------------------------------------------------
    double getSamplesPerPixel() const noexcept { return samplesPerPixel_; }
    double getViewStart() const noexcept { return viewStart_; }
    void setView(double viewStartSamples, double samplesPerPixel);
    void zoomToFit();               // whole song (+1 bar) in view
    void zoom(double factor, float aroundX);
    float sampleToX(double sample) const noexcept;
    double xToSample(float x) const noexcept;
    int laneAt(int y) const noexcept; // -1 outside the lanes
    juce::Rectangle<int> getLaneBounds(int track) const noexcept;
    juce::Rectangle<int> getClipBounds(int track, int clipIndex) const;

    // Snaps to the nearest beat (when `snap`), never below 0.
    int64_t snapToBeat(double sample, bool snap) const noexcept;

    // ---- selection -----------------------------------------------------------------------------
    int getSelectedTrack() const noexcept { return selectedTrack_; }
    void setSelectedTrack(int track);
    std::function<void(int track)> onTrackSelected;

    // ---- drag and drop from the file browser ---------------------------------------------------
    // Called with the lane the files were dropped on and the readable audio files among them (in
    // the order they were dragged). Unreadable files are filtered out before the call.
    std::function<void(int track, const juce::StringArray& files)> onFilesDropped;
    int getDropTargetTrack() const noexcept { return dropTrack_; } // -1 when nothing is dragged over

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    const WaveformCache& getWaveformCache() const noexcept { return waveforms_; }

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void paintRuler(juce::Graphics&, juce::Rectangle<int> area);
    void paintLane(juce::Graphics&, int track, juce::Rectangle<int> area);
    void paintClip(juce::Graphics&, int track, const engine::Clip& clip, juce::Rectangle<int> bounds, bool selected);
    int clipAt(int track, float x) const;
    void seekToX(float x, bool snap);

    Session& session_;
    const AudioHost* audioHost_;
    WaveformCache waveforms_;
    double viewStart_ = 0.0;
    double samplesPerPixel_ = 1000.0;
    int selectedTrack_ = 0;
    int dropTrack_ = -1; // lane highlighted while files are dragged over the view
    float lastPlayheadX_ = -1.0f;

    // drag state
    enum class Drag { None, Seek, Clip };
    Drag drag_ = Drag::None;
    int dragTrack_ = -1;
    int dragClip_ = -1;
    double dragGrabOffset_ = 0.0;
};

} // namespace dawlab::app
