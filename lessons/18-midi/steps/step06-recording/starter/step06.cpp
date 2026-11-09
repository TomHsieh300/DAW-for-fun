// Step 6 (starter): recording MIDI.
//
// Build and run:  l18_step06_starter   (prints [FAIL] until all seven TODOs are done)
// Reference:      ../solution/step06.cpp, docs/step-06-recording.md ("Your turn")
//
// Anchor, NoteRecorder (with passOf(), songTickOf(), passEnd()), RecordMode and QuantizeSettings are in
// ../minidaw.h.

#include "../minidaw.h"

namespace minidaw {

std::int64_t songSampleAt(double time, const Anchor& anchor) noexcept {
    // TODO 1: songSample + (time - blockTime) x sampleRate (std::llround) - mixLatency.
    (void)time;
    (void)anchor;
    return 0;
}

std::int64_t tickAt(std::int64_t sample, const Tempo& tempo) noexcept {
    // TODO 2: the nearest tick: std::llround(sample / samplesPerTick).
    (void)sample;
    (void)tempo;
    return 0;
}

void NoteRecorder::noteOn(std::int64_t u, int pitch, int velocity, int channel) {
    // TODO 3: Velocity 0: a note off. Otherwise end the same key if it is still down (noteOff), and open a new
    //         note (open_, at most kMaxOpen).
    (void)u;
    (void)pitch;
    (void)velocity;
    (void)channel;
}

void NoteRecorder::noteOff(std::int64_t u, int pitch, int channel) {
    // TODO 4: Find the open note of this pitch and channel. Its pass is passOf(on); it ends at u, or at
    //         passEnd(pass) if that comes first; at least 1 tick. Push it to notes_ at songTickOf(on), and
    //         remove it from open_.
    (void)u;
    (void)pitch;
    (void)channel;
}

void NoteRecorder::stop(std::int64_t u) {
    // TODO 5: Close every open note at u. Then for each pass from 0 to passOf(u): it begins at start_ (pass
    //         0) or at passEnd(pass - 1), it ends at u or passEnd(pass); if it is not empty, its song ticks go
    //         into passes_ (songTickOf(begin), plus its length).
    (void)u;
    (void)start_;
}

std::vector<Note> mergeTake(const std::vector<Note>& existing, const NoteRecorder& take, RecordMode mode) {
    // TODO 6: Overdub: all of them. Replace: drop a note whose start lies in the coverage of a later pass
    //         (existing notes are pass -1). Then std::stable_sort by start.
    (void)take;
    (void)mode;
    return existing;
}

std::vector<Note> quantize(const std::vector<Note>& played, const QuantizeSettings& settings) {
    // TODO 7: A copy of played; for each note the nearest line, the swing on odd lines, the strength, not
    //         below 0. Then std::stable_sort by start.
    (void)settings;
    return played;
}

} // namespace minidaw

#include "../check.h"
