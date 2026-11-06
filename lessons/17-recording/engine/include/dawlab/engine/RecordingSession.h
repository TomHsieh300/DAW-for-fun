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

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "Constants.h"
#include "Engine.h"
#include "Recording.h"

namespace dawlab::engine {

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
