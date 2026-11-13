#include "TakeRecorder.h"

#include "MediaLoader.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace dawlab::app {

using namespace dawlab::engine;

namespace {

juce::String describe(TakeProblem problem) {
    switch (problem) {
    case TakeProblem::None: return {};
    case TakeProblem::CannotOpen: return "no take file could be created. Is the disk full, or the folder gone?";
    case TakeProblem::WriteFailed: return "the disk is full or failing. The take ends where the file ends";
    case TakeProblem::TooLong: return "the take reached the 4 GiB a WAV file can hold, and ends there";
    case TakeProblem::CannotFinish: return "the take could not be finished, but its samples are safe in its .recording file";
    }
    return {};
}

std::filesystem::path pathOf(const juce::File& file) {
    return std::filesystem::u8path(file.getFullPathName().toStdString());
}

juce::File fileOf(const std::filesystem::path& path) {
    return juce::File(juce::String::fromUTF8(path.u8string().c_str()));
}

} // namespace

TakeRecorder::TakeRecorder(Session& session, AudioHost& audioHost)
    : juce::Thread("DawLab disk"), session_(session), audioHost_(audioHost), folder_(defaultFolder()) {}

TakeRecorder::~TakeRecorder() {
    // A take still running now stays a .recording file, and the next start recovers it.
    stopThread(4000);
}

juce::File TakeRecorder::defaultFolder() {
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("DawLab 20 Takes");
}

juce::String TakeRecorder::start() {
    if (isBusy())
        return "A recording is still running";
    Engine& engine = session_.getEngine();
    bool anyArmed = false;
    for (int i = 0; i < session_.getNumTracks(); ++i) // an instrument track records MIDI (chapter 18)
        anyArmed = anyArmed || (engine.getTrack(session_.getTrackId(i))->isArmed() && !session_.isInstrument(session_.getTrackId(i)));
    if (!anyArmed)
        return "Nothing to record: arm a track first (R in its header)";
    if (const juce::Result made = folder_.createDirectory(); made.failed())
        return "Cannot create " + folder_.getFullPathName() + ": " + made.getErrorMessage();

    const Transport& tr = engine.getTransport();
    RecordRange range;
    if (tr.isLoopEnabled() && tr.getLoopEnd() > tr.getLoopStart()) {
        range.loopStart = tr.getLoopStart();
        range.loopEnd = tr.getLoopEnd();
    }
    if (punch_ && tr.getLoopEnd() > tr.getLoopStart()) {
        range.punchIn = tr.getLoopStart();
        range.punchOut = tr.getLoopEnd();
    }

    auto recording = std::make_unique<RecordingSession>();
    const juce::String name = juce::File::createLegalFileName(session_.getProjectName());
    if (recording->begin(engine, pathOf(folder_), name.toStdString(), audioHost_.getDeviceLatencies(trimSamples_), range,
                         ringSeconds_, diskOps_) == 0)
        return "The last recording is still ending";
    recording_ = std::move(recording);
    results_.clear();
    finished_.store(false);
    droppedShown_ = 0;
    stopAsked_ = false;
    phase_ = Phase::Recording;
    startThread(juce::Thread::Priority::high);
    return {};
}

void TakeRecorder::stop() {
    if (phase_ == Phase::Recording && !stopAsked_) {
        stopAsked_ = true;
        session_.getEngine().stopRecording();
    }
}

void TakeRecorder::run() {
    while (!recording_->pump()) {
        if (threadShouldExit())
            return; // the program is closing: the files stay, and recover() finds them
        wait(10);
    }
    for (const RecordingSession::Result& result : recording_->results()) {
        Loaded loaded;
        loaded.track = result.track;
        loaded.take = result.take;
        loaded.problem = result.problem;
        loaded.droppedFrames = result.droppedFrames;
        if (result.take.ok) {
            // At the file's own rate: tick() converts it, if the clips are at another one by then.
            const LoadedMedia media = MediaLoader::readAudioFile(fileOf(result.take.path), result.take.info.sampleRate);
            loaded.audio = media.audio;
            loaded.audioRate = result.take.info.sampleRate;
            loaded.error = media.error;
        }
        results_.push_back(std::move(loaded));
    }
    finished_.store(true, std::memory_order_release);
}

bool TakeRecorder::getLive(TrackId track, LiveTake& live) const {
    return phase_ == Phase::Recording && recording_ != nullptr && recording_->getLive(track, live);
}

void TakeRecorder::tick() {
    if (phase_ == Phase::Idle)
        return;
    Engine& engine = session_.getEngine();
    if (phase_ == Phase::Recording) {
        // A problem ends the recording: the song plays on, and the user hears about it now.
        if (const TakeProblem problem = recording_->problem(); problem != TakeProblem::None && !stopAsked_) {
            say("Recording stopped: " + describe(problem), true);
            stop();
        }
        const std::uint64_t dropped = recording_->droppedFrames();
        if (dropped > droppedShown_) {
            droppedShown_ = dropped;
            say("The disk cannot keep up: " + juce::String(static_cast<double>(dropped) * 1000.0 / engine.getSampleRate(), 0) +
                    " ms of this take are silence. Its timing stays right.",
                true);
        }
        if (engine.isRecordingOver())
            phase_ = Phase::Finishing;
    }
    if (phase_ == Phase::Finishing && finished_.load(std::memory_order_acquire)) {
        stopThread(4000);
        applyResults();
        recording_.reset();
        phase_ = Phase::Idle;
    }
}

std::vector<Placement> TakeRecorder::placementsOf(const Loaded& loaded, std::shared_ptr<const AudioBuffer>& audio) const {
    const SealedTake& take = loaded.take;
    const double fileRate = take.info.sampleRate;
    const double clipRate = session_.getClipSampleRate();
    const auto minFrames = static_cast<std::int64_t>(kMinTakeSeconds * fileRate);
    std::vector<Placement> takes = takesOf(take.info.stampedPosition, take.info.latencies, take.info.range,
                                           static_cast<std::int64_t>(take.numFrames), minFrames);
    audio = loaded.audio;
    if (fileRate == clipRate)
        return takes;
    // The device changed its rate during the take. The audio follows, as all clips did.
    audio = std::make_shared<const AudioBuffer>(resampleBuffer(*loaded.audio, fileRate, clipRate));
    const double ratio = clipRate / fileRate;
    const auto scale = [ratio](std::int64_t v) { return static_cast<std::int64_t>(std::llround(static_cast<double>(v) * ratio)); };
    for (Placement& p : takes) {
        p.timelineStart = scale(p.timelineStart);
        p.skipFrames = scale(p.skipFrames);
        p.numFrames = std::min(scale(p.numFrames), audio->getNumSamples() - p.skipFrames);
    }
    return takes;
}

void TakeRecorder::applyResults() {
    std::vector<Edit> edits;
    int numTakes = 0;
    juce::StringArray notes;
    for (const Loaded& loaded : results_) {
        if (const juce::String problem = describe(loaded.problem); problem.isNotEmpty() && !stopAsked_)
            notes.add(problem);
        if (!loaded.take.ok)
            continue;
        const juce::String path = fileOf(loaded.take.path).getFullPathName();
        if (loaded.audio == nullptr) {
            notes.add("could not read " + path + ": " + loaded.error);
            continue;
        }
        if (!session_.hasTrack(loaded.track)) {
            notes.add("the track is gone; its take is kept in " + path);
            continue;
        }
        std::shared_ptr<const AudioBuffer> audio;
        const std::vector<Placement> takes = placementsOf(loaded, audio);
        if (takes.empty())
            continue; // a punch that was never reached
        edits.push_back(session_.makeTakeEdit(loaded.track, audio, takes, fileOf(loaded.take.path).getFileNameWithoutExtension()));
        numTakes += static_cast<int>(takes.size());
    }
    if (!edits.empty())
        session_.perform(std::move(edits), "Record");
    juce::String message = numTakes == 0 ? juce::String("Nothing recorded") : "Recorded " + juce::String(numTakes) + (numTakes == 1 ? " take" : " takes");
    if (!notes.isEmpty())
        message << ": " << notes.joinIntoString("; ");
    if (droppedShown_ > 0)
        message << ". The disk could not keep up: "
                << juce::String(static_cast<double>(droppedShown_) * 1000.0 / session_.getEngine().getSampleRate(), 0)
                << " ms are silence";
    say(message, !notes.isEmpty() || droppedShown_ > 0);
}

int TakeRecorder::recover() {
    if (isBusy() || !folder_.isDirectory())
        return 0;
    int found = 0;
    for (const std::filesystem::path& recording : findUnfinishedTakes(pathOf(folder_))) {
        ++found;
        const SealedTake sealed = sealTake(recording);
        if (!sealed.ok) {
            say("Could not recover " + fileOf(recording).getFullPathName() + ": the file stays as it is", true);
            continue;
        }
        const LoadedMedia media = MediaLoader::readAudioFile(fileOf(sealed.path), sealed.info.sampleRate);
        if (media.audio == nullptr) {
            say("Could not read " + fileOf(sealed.path).getFullPathName() + ": " + media.error, true);
            continue;
        }
        Loaded loaded;
        loaded.take = sealed;
        loaded.audio = media.audio;
        std::shared_ptr<const AudioBuffer> audio;
        const std::vector<Placement> takes = placementsOf(loaded, audio);
        if (takes.empty())
            continue;
        const juce::String name = fileOf(sealed.path).getFileNameWithoutExtension();
        const TrackId track = session_.addTrack(sealed.info.numChannels > 1 ? TrackFormat::Stereo : TrackFormat::Mono, "Recovered " + name);
        if (!track.isValid()) {
            say("No free track for the recovered take " + name + "; it is kept in " + folder_.getFullPathName(), true);
            continue;
        }
        session_.applyEdit(session_.makeTakeEdit(track, audio, takes, name));
        session_.getHistory().markModified(); // the take is in no project file yet: closing must ask to save
    }
    return found;
}

} // namespace dawlab::app
