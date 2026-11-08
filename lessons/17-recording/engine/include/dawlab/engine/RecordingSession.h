#pragma once
// RecordingSession: one recording of all armed tracks, from the UI's click to the takes on disk
// (chapter 17, Step 6).
//
//   UI thread      begin()                     starts the Recorders, one take file per track
//   disk thread    pump(), until it is true    moves the rings to the files, seals them at the end
//   UI thread      engine.stopRecording()      (or the transport stops, or the device goes away)
//   UI thread      results(), after that       what became of each track
//
// Takes get a name that is not used yet: <folder>/<baseName>-<track id>-<number>.wav. A file is
// only created when its first frame arrives, so a recording that never began leaves nothing.
//
// One session per recording. The folder must exist.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "Constants.h"
#include "Engine.h"
#include "Recording.h"

namespace dawlab::engine {

// The peaks of a take so far, for a display. Appended by the disk thread, read by the UI: each entry is
// written once, and `count_` publishes it. The room is set aside at the start, so that nothing moves; a
// take longer than that stops growing here, not on disk.
class PeakTrail {
public:
    static constexpr int kFramesPerPeak = 256;

    explicit PeakTrail(std::size_t maxPeaks) : peaks_(maxPeaks, 0.0f) {}

    void add(const float* const* channels, int numChannels, int n) noexcept {
        for (int i = 0; i < n; ++i) {
            for (int ch = 0; ch < numChannels; ++ch)
                peak_ = std::max(peak_, std::abs(channels[ch][i]));
            if (++inPeak_ == kFramesPerPeak) {
                const std::size_t count = count_.load(std::memory_order_relaxed);
                if (count < peaks_.size()) {
                    peaks_[count] = peak_;
                    count_.store(count + 1, std::memory_order_release);
                }
                peak_ = 0.0f;
                inPeak_ = 0;
            }
        }
    }
    std::size_t size() const noexcept { return count_.load(std::memory_order_acquire); }
    float at(std::size_t index) const noexcept { return peaks_[index]; } // below size()

private:
    std::vector<float> peaks_;
    std::atomic<std::size_t> count_{0};
    float peak_ = 0.0f; // the disk thread's
    int inPeak_ = 0;    // the disk thread's
};

// What a track has recorded so far.
struct LiveTake {
    std::int64_t stampedPosition = 0;
    Latencies latencies;
    RecordRange range;
    const PeakTrail* trail = nullptr;
};

class RecordingSession {
public:
    struct Result {
        TrackId track;
        SealedTake take;     // take.ok is false if the recording never began or there was no file
        TakeProblem problem = TakeProblem::None;
        uint64_t droppedFrames = 0; // the disk could not keep up: silence in the take, and the time is right
    };

    // UI thread. Returns the number of tracks recording: 0 if none is armed, or if another recording
    // is still ending, and then nothing has started. Allocates.
    int begin(Engine& engine, const std::filesystem::path& folder, const std::string& baseName, const Latencies& device,
              const RecordRange& range = {}, double ringSeconds = 10.0, const DiskOps& ops = {}) {
        if (!entries_.empty() || engine.startRecording(device, range, ringSeconds) == 0)
            return 0;
        for (int slot = 0; slot < kMaxTracks; ++slot) {
            const TrackId id = engine.ownerOfSlot(slot);
            Track& track = engine.getMixer().getTrack(slot);
            if (!id.isValid() || !track.isArmed())
                continue;
            track.getMonitor().setRange(range);
            Entry entry;
            entry.track = id;
            entry.recorder = &track.getRecorder();
            entry.writer = std::make_unique<TakeWriter>(track.getRecorder(), freeName(folder, baseName, id),
                                                        static_cast<int>(engine.getSampleRate()), ops);
            // Three hours at 48 kHz: a little over 2.5 MB per track.
            entry.trail = std::make_unique<PeakTrail>(static_cast<std::size_t>(engine.getSampleRate() * 3600.0 * 3.0 / PeakTrail::kFramesPerPeak));
            entry.writer->onFrames = [trail = entry.trail.get()](const float* const* channels, int numChannels, int n) {
                trail->add(channels, numChannels, n);
            };
            entries_.push_back(std::move(entry));
        }
        return static_cast<int>(entries_.size());
    }

    // Disk thread. True once every take is over and sealed.
    bool pump() {
        bool allDone = true;
        for (Entry& entry : entries_)
            allDone = entry.writer->pump() && allDone;
        return allDone;
    }

    // Any thread: the first problem of any track, for the UI to stop the recording and say so.
    TakeProblem problem() const noexcept {
        for (const Entry& entry : entries_)
            if (entry.writer->problem() != TakeProblem::None)
                return entry.writer->problem();
        return TakeProblem::None;
    }

    // Any thread: how much the rings have dropped so far, all tracks together.
    uint64_t droppedFrames() const noexcept {
        uint64_t dropped = 0;
        for (const Entry& entry : entries_)
            dropped += entry.recorder->ring().droppedFrames();
        return dropped;
    }

    // UI thread: what a track has recorded so far. False if nothing yet. The first peak came out of the
    // ring, so the stamp is there, for the same reason as for the file (Step 3).
    bool getLive(TrackId track, LiveTake& live) const noexcept {
        for (const Entry& entry : entries_) {
            if (entry.track == track && entry.trail->size() > 0) {
                live.stampedPosition = entry.recorder->stampedPosition();
                live.latencies = entry.recorder->latencies();
                live.range = entry.recorder->range();
                live.trail = entry.trail.get();
                return true;
            }
        }
        return false;
    }

    // After pump() returned true.
    std::vector<Result> results() const {
        std::vector<Result> all;
        for (const Entry& entry : entries_)
            all.push_back({entry.track, entry.writer->result(), entry.writer->problem(), entry.recorder->ring().droppedFrames()});
        return all;
    }

private:
    struct Entry {
        TrackId track;
        Recorder* recorder = nullptr;
        std::unique_ptr<TakeWriter> writer;
        std::unique_ptr<PeakTrail> trail;
    };

    static std::filesystem::path freeName(const std::filesystem::path& folder, const std::string& baseName, TrackId id) {
        for (int number = 1;; ++number) {
            char tail[64];
            std::snprintf(tail, sizeof tail, "-%u-%03d.wav", static_cast<unsigned>(id.value), number);
            std::filesystem::path path = folder / (baseName + tail);
            std::filesystem::path recording = path;
            recording += kRecordingExtension;
            std::error_code ec;
            if (!std::filesystem::exists(path, ec) && !std::filesystem::exists(recording, ec))
                return path;
        }
    }

    std::vector<Entry> entries_;
};

} // namespace dawlab::engine
