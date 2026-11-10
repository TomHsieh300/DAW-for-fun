#include "MidiRecording.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace dawlab::app {

// ---- NoteRecorder: Step 6 -----------------------------------------------------------------------------------

void NoteRecorder::start(int64_t startTick, LoopTicks loop) {
    start_ = startTick;
    loop_ = loop;
    notes_.clear();
    passes_.clear();
    open_.clear();
}

int NoteRecorder::passOf(int64_t u) const noexcept {
    if (!loop_.on() || u < loop_.to)
        return 0;
    return static_cast<int>((u - loop_.to) / loop_.length()) + 1;
}

int64_t NoteRecorder::songTickOf(int64_t u) const noexcept {
    if (!loop_.on() || u < loop_.to)
        return u;
    return loop_.from + (u - loop_.to) % loop_.length();
}

int64_t NoteRecorder::passEnd(int pass) const noexcept {
    if (!loop_.on())
        return std::numeric_limits<int64_t>::max();
    return loop_.to + static_cast<int64_t>(pass) * loop_.length();
}

void NoteRecorder::noteOn(int64_t u, int pitch, int velocity, int channel) {
    if (velocity == 0) {
        noteOff(u, pitch, channel);
        return;
    }
    noteOff(u, pitch, channel); // the same key still down: it ends where it starts again
    if (open_.size() < 128)
        open_.push_back({u, pitch, velocity, channel});
}

void NoteRecorder::noteOff(int64_t u, int pitch, int channel) {
    const auto it = std::find_if(open_.begin(), open_.end(), [&](const Open& o) { return o.pitch == pitch && o.channel == channel; });
    if (it == open_.end())
        return;
    const int pass = passOf(it->on);
    const int64_t end = std::min(u, passEnd(pass));
    notes_.push_back({{songTickOf(it->on), std::max<int64_t>(end - it->on, 1), pitch, it->velocity, channel}, pass});
    open_.erase(it);
}

void NoteRecorder::stop(int64_t u) {
    while (!open_.empty())
        noteOff(u, open_.front().pitch, open_.front().channel);
    passes_.clear();
    const int last = passOf(u);
    for (int p = 0; p <= last; ++p) {
        const int64_t begin = p == 0 ? start_ : passEnd(p - 1);
        const int64_t end = std::min(u, passEnd(p));
        if (end > begin)
            passes_.push_back({p, songTickOf(begin), songTickOf(begin) + (end - begin)});
    }
}

bool NoteRecorder::covered(int64_t songTick, int laterThanPass) const noexcept {
    for (const Coverage& c : passes_)
        if (c.pass > laterThanPass && songTick >= c.from && songTick < c.to)
            return true;
    return false;
}

// ---- MidiRecording ----------------------------------------------------------------------------------------

bool MidiRecording::start(MidiRecordMode mode) {
    takes_.clear();
    engine::Engine& engine = session_.getEngine();
    const engine::Transport& tr = engine.getTransport();
    startTick_ = session_.tickOfSample(tr.getPosition());
    LoopTicks loop;
    loopStartSample_ = loopEndSample_ = 0;
    if (tr.isLoopEnabled() && tr.getLoopEnd() > tr.getLoopStart()) {
        loop = {session_.tickOfSample(tr.getLoopStart()), session_.tickOfSample(tr.getLoopEnd())};
        loopStartSample_ = tr.getLoopStart();
        loopEndSample_ = tr.getLoopEnd();
    }
    loopTicks_ = loop;
    for (int i = 0; i < session_.getNumTracks(); ++i) {
        const engine::TrackId id = session_.getTrackId(i);
        const engine::Track* t = engine.getTrack(id);
        if (session_.isInstrument(id) && t != nullptr && t->isArmed()) {
            NoteRecorder r;
            r.start(startTick_, loop);
            takes_.emplace_back(id, std::move(r));
        }
    }
    if (takes_.empty())
        return false;
    // What came before is not part of it.
    engine::TimedMidi m;
    while (host_.getRecordFifo().pop(m)) {
    }
    // The next block is the first of the recording: it starts at startTick_, after `renderedAfter` samples.
    AudioHost::Anchor last;
    const bool any = host_.getAnchor(last);
    startRendered_ = any ? last.renderedAfter() : 0;
    startBlock_ = any ? last.block : 0;
    startSample_ = tr.getPosition();
    mode_ = mode;
    recording_ = true;
    return true;
}

int64_t MidiRecording::unwrappedTick(double time) const noexcept {
    AudioHost::Anchor a;
    if (!host_.getAnchor(a) || a.block <= startBlock_)
        return std::numeric_limits<int64_t>::min(); // no block of this recording yet
    // Where the song was heard at `time`, counted from the start of the recording without wrapping.
    const double sample = static_cast<double>(startSample_ + (a.renderedBefore - startRendered_)) + (time - a.blockTime) * a.sampleRate -
                          static_cast<double>(a.mixLatency);
    // Chapter 19: the tempo may change along the way, so a pass of the loop is not a fixed number of ticks per
    // sample. Wrap the SAMPLE into its pass first, find its tick through the map, then count the passes on in ticks.
    const int64_t s = std::llround(sample);
    if (loopEndSample_ > loopStartSample_ && s >= loopEndSample_) {
        const int64_t length = loopEndSample_ - loopStartSample_;
        const int64_t pass = (s - loopEndSample_) / length + 1;
        const int64_t songSample = loopStartSample_ + (s - loopEndSample_) % length;
        return session_.tickOfSample(songSample) + pass * loopTicks_.length();
    }
    return session_.tickOfSample(s);
}

void MidiRecording::poll() {
    if (!recording_)
        return;
    AudioHost::Anchor a;
    if (!host_.getAnchor(a) || a.block <= startBlock_)
        return; // the keys wait in the FIFO until the recording's first block has been rendered
    engine::TimedMidi m;
    while (host_.getRecordFifo().pop(m)) {
        const int64_t u = unwrappedTick(m.time);
        if (u < startTick_)
            continue; // before the recording started: a key held from before, or the count-in
        for (auto& [track, recorder] : takes_) {
            if (m.message.kind() == engine::MidiKind::NoteOn)
                recorder.noteOn(u, m.message.note(), m.message.velocity(), m.message.channel());
            else if (m.message.kind() == engine::MidiKind::NoteOff)
                recorder.noteOff(u, m.message.note(), m.message.channel());
        }
    }
}

int MidiRecording::stop() {
    if (!recording_)
        return 0;
    poll();
    recording_ = false;
    const int64_t end = std::max(unwrappedTick(host_.now()), startTick_);
    std::vector<Edit> edits;
    int numNotes = 0;
    for (auto& [track, recorder] : takes_) {
        recorder.stop(end);
        if (!session_.hasTrack(track) || recorder.notes().empty() || recorder.passes().empty())
            continue;
        // The take: in Replace, a later pass erases what an earlier one played there.
        std::vector<engine::Note> notes;
        for (const TakeNote& n : recorder.notes())
            if (mode_ == MidiRecordMode::Overdub || !recorder.covered(n.note.start, n.pass))
                notes.push_back(n.note);
        if (notes.empty())
            continue;
        int64_t from = std::numeric_limits<int64_t>::max(), to = 0;
        for (const Coverage& c : recorder.passes()) {
            from = std::min(from, c.from);
            to = std::max(to, c.to);
        }
        MidiClipModel clip;
        clip.id = session_.newMidiClipId();
        clip.start = from;
        clip.length = std::max<int64_t>(1, to - from);
        clip.name = "Take " + juce::String(clip.id);
        for (engine::Note n : notes) {
            n.start -= from;
            clip.played.push_back(n);
        }
        sortNotes(clip.played);
        numNotes += static_cast<int>(clip.played.size());

        // The old clips: in Replace, the notes of the places the take played over are gone.
        std::vector<MidiClipModel> list = session_.getMidiClips(track);
        if (mode_ == MidiRecordMode::Replace)
            for (MidiClipModel& old : list) {
                std::vector<engine::Note> kept;
                for (const engine::Note& n : old.played)
                    if (!recorder.covered(old.start + n.start - old.offset, -1))
                        kept.push_back(n);
                old.played = std::move(kept);
            }
        list.push_back(std::move(clip));
        edits.push_back(edit::SetMidiClips{track, std::move(list)});
    }
    if (!edits.empty())
        session_.perform(std::move(edits), "Record MIDI");
    takes_.clear();
    return numNotes;
}

} // namespace dawlab::app
