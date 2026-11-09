#pragma once
// ArrangementView: the timeline, one lane per track (chapter 14, Step 14; chapter 15, Step 7).
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
// Lanes (chapter 15): the lanes share the height below the ruler, but none gets lower than
// kMinLaneHeight. When the tracks do not fit any more, the lanes scroll under the ruler (a
// scroll bar appears on the right); lane i starts at y = rulerHeight + i * laneHeight - scrollY.
//
// Automation (chapter 16): a track may show one automation lane under its clips, so rows have two
// heights and a row's top is a sum, not a product. The lane draws one parameter's curve:
//
//   | Drums    | [drums][drums]   [drums]        the track's row
//   | Gain     |     *-------*                   its automation: value 0 at the bottom, 1 at the top
//   | Touch    | ---*         \-----*            header: click the name or the mode for a menu
//
//   lane:  click on nothing   add a point and drag it (snapped to the beat; Alt = free)
//          drag a point       move it; one drag is one undo step
//          right click        remove the point under the mouse
//   "A" in a track's header (or the key A) shows and hides the lane.
//
// Recording (chapter 17): "R" in a track's header arms it, "I" turns its monitor through Off, Auto and
// Always; a right click on either has the input and the monitor's path. While it records, a track shows
// what has come in so far: a red clip with the peaks that the disk thread has seen. A right click on a
// clip that came from a loop lets you choose its take.
//
// Mouse:
//   ruler: click / drag      seek (snapped to the beat grid; hold Alt for no snapping)
//   clip:  drag              move the clip along its lane (snapped; Alt = free).
//                            Session::moveClip() builds a new clip list and sends it to the engine.
//   lane:  click             select the track (File > Load audio..., M / S shortcuts act on it)
//   wheel                    scroll the lanes up and down when they do not all fit, otherwise
//                            in time; Shift + wheel (or a sideways wheel): in time;
//                            Ctrl + wheel: zoom around the mouse
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

#include <dawlab/engine/RecordingSession.h>

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
                              private juce::ChangeListener,
                              private juce::ScrollBar::Listener {
public:
    static constexpr int kRulerHeight = 26;
    static constexpr int kHeaderWidth = 132;
    static constexpr int kMinLaneHeight = 36;
    static constexpr int kScrollBarWidth = 10;
    static constexpr int kAutomationHeight = 72;
    static constexpr int kPointRadius = 4;

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
    // Lanes are counted in display order (Session::getTrackId(lane) is the track in it).
    int laneAt(int y) const noexcept; // -1 outside the lanes
    juce::Rectangle<int> getLaneBounds(int lane) const noexcept;
    juce::Rectangle<int> getClipBounds(int lane, int clipIndex) const;
    int getLaneHeight() const noexcept;

    // ---- vertical scrolling (chapter 15) --------------------------------------------------------
    int getScrollY() const noexcept { return scrollY_; }
    int getMaxScrollY() const noexcept;
    void setScrollY(int pixels); // clamped to 0 .. getMaxScrollY()
    void scrollToTrack(engine::TrackId track); // the least scrolling that shows the whole lane

    // ---- automation lanes (chapter 16) -----------------------------------------------------------
    bool isAutomationShown(engine::TrackId track) const noexcept;
    void showAutomation(engine::TrackId track, const LaneAddress& address);
    void hideAutomation(engine::TrackId track);
    void toggleAutomation(engine::TrackId track); // first time: the track's Gain
    const LaneAddress* getShownLane(engine::TrackId track) const noexcept; // nullptr: not shown
    juce::Rectangle<int> getAutomationBounds(int lane) const noexcept;     // empty: not shown
    juce::Rectangle<int> getAutomationToggleBounds(int lane) const noexcept;
    int automationLaneAt(int y) const noexcept;                            // -1: not in an automation lane
    int pointAt(int lane, float x, float y) const;                         // -1: no point there

    // ---- recording (chapter 17) -----------------------------------------------------------------
    juce::Rectangle<int> getArmBounds(int lane) const noexcept;
    juce::Rectangle<int> getMonitorBounds(int lane) const noexcept;
    std::function<void(engine::TrackId track)> onArmClicked;
    std::function<void(engine::TrackId track)> onMonitorClicked;
    std::function<void(engine::TrackId track)> onRecordMenu;
    std::function<void(engine::TrackId track, int clipIndex)> onClipMenu;
    // What a track has recorded so far (TakeRecorder::getLive); false if nothing.
    std::function<bool(engine::TrackId track, engine::LiveTake& live)> liveTakeOf;
    float valueToY(int lane, float value) const noexcept;
    float yToValue(int lane, float y) const noexcept;

    // Snaps to the nearest beat (when `snap`), never below 0.
    int64_t snapToBeat(double sample, bool snap) const noexcept;

    // ---- selection -----------------------------------------------------------------------------
    // The selected track (invalid only in a project without tracks). When it is deleted, the
    // track that takes its place (or the one above, at the end) is selected.
    engine::TrackId getSelectedTrack() const noexcept { return selectedTrack_; }
    void setSelectedTrack(engine::TrackId track);
    void selectLane(int lane); // clamped to the existing lanes
    std::function<void(engine::TrackId track)> onTrackSelected;

    // ---- drag and drop from the file browser ---------------------------------------------------
    // Called with the lane the files were dropped on and the readable audio files among them (in
    // the order they were dragged). Unreadable files are filtered out before the call.
    std::function<void(engine::TrackId track, const juce::StringArray& files)> onFilesDropped;
    // Invalid when nothing is dragged over.
    engine::TrackId getDropTargetTrack() const noexcept { return session_.getTrackId(dropLane_); }

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
    void scrollBarMoved(juce::ScrollBar*, double start) override { setScrollY(static_cast<int>(start)); }
    void updateScrollBar();
    void paintRuler(juce::Graphics&, juce::Rectangle<int> area);
    void paintLane(juce::Graphics&, int lane, juce::Rectangle<int> area);
    void paintClip(juce::Graphics&, engine::TrackId track, const engine::Clip& clip, juce::Rectangle<int> bounds,
                   bool selected);
    void paintAutomation(juce::Graphics&, int lane, juce::Rectangle<int> area);
    void paintLiveTake(juce::Graphics&, engine::TrackId track, juce::Rectangle<int> area);
    void mouseDownInAutomation(const juce::MouseEvent&, int lane);
    void showParameterMenu(engine::TrackId track);
    void showModeMenu(engine::TrackId track);
    int rowTop(int lane) const noexcept; // in the lanes' own coordinates: without the ruler and the scrolling
    int rowsHeight() const noexcept;
    int clipAt(int lane, float x) const;
    void seekToX(float x, bool snap);

    Session& session_;
    const AudioHost* audioHost_;
    WaveformCache waveforms_;
    double viewStart_ = 0.0;
    double samplesPerPixel_ = 1000.0;
    engine::TrackId selectedTrack_;
    int selectedLane_ = 0; // where the selected track was shown: its successor is picked from here
    int dropLane_ = -1;    // lane highlighted while files are dragged over the view
    int scrollY_ = 0;
    juce::ScrollBar scrollBar_{true};
    float lastPlayheadX_ = -1.0f;

    // drag state
    std::vector<std::pair<engine::TrackId, LaneAddress>> shownLanes_;
    enum class Drag { None, Seek, Clip, Point };
    Drag drag_ = Drag::None;
    engine::TrackId dragTrack_;
    int dragClip_ = -1;
    double dragGrabOffset_ = 0.0;
    int dragPoint_ = -1;
    int dragLane_ = -1;
    std::uint32_t dragPointKey_ = 0;
};

} // namespace dawlab::app
