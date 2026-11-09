// Chapter 18, Step 6 (solution): recording MIDI.
//
// Build and run:  l18_step06_solution   (ctest, label "solution")
// Your version:   starter/step06.cpp -> l18_step06_starter

#include "../minidaw.h"

namespace minidaw {

std::int64_t songSampleAt(double time, const Anchor& a) noexcept {
    return a.songSample + std::llround((time - a.blockTime) * a.sampleRate) - a.mixLatency;
}

std::int64_t tickAt(std::int64_t sample, const Tempo& tempo) noexcept {
    return std::llround(static_cast<double>(sample) / tempo.samplesPerTick());
}

void NoteRecorder::noteOn(std::int64_t u, int pitch, int velocity, int channel) {
    if (velocity == 0) {
        noteOff(u, pitch, channel);
        return;
    }
    noteOff(u, pitch, channel); // the same key still down: it ends where it starts again
    if (static_cast<int>(open_.size()) < kMaxOpen)
        open_.push_back({u, pitch, velocity, channel});
}

void NoteRecorder::noteOff(std::int64_t u, int pitch, int channel) {
    const auto it = std::find_if(open_.begin(), open_.end(), [&](const Open& o) { return o.pitch == pitch && o.channel == channel; });
    if (it == open_.end())
        return;
    const int pass = passOf(it->on);
    const std::int64_t end = std::min(u, passEnd(pass));
    notes_.push_back({{songTickOf(it->on), std::max<std::int64_t>(end - it->on, 1), pitch, it->velocity, channel}, pass});
    open_.erase(it);
}

void NoteRecorder::stop(std::int64_t u) {
    while (!open_.empty())
        noteOff(u, open_.front().pitch, open_.front().channel);
    passes_.clear();
    const int last = passOf(u);
    for (int p = 0; p <= last; ++p) {
        const std::int64_t begin = p == 0 ? start_ : passEnd(p - 1);                  // unwrapped
        const std::int64_t end = std::min(u, passEnd(p));                              // unwrapped
        if (end > begin)
            passes_.push_back({p, songTickOf(begin), songTickOf(begin) + (end - begin)});
    }
}

std::vector<Note> mergeTake(const std::vector<Note>& existing, const NoteRecorder& take, RecordMode mode) {
    // Erased: a later pass covered the start.
    auto erased = [&take](std::int64_t start, int pass) {
        for (const Coverage& c : take.passes())
            if (c.pass > pass && start >= c.from && start < c.to)
                return true;
        return false;
    };
    std::vector<Note> out;
    for (const Note& n : existing)
        if (mode == RecordMode::Overdub || !erased(n.start, -1))
            out.push_back(n);
    for (const TakeNote& t : take.notes())
        if (mode == RecordMode::Overdub || !erased(t.note.start, t.pass))
            out.push_back(t.note);
    std::stable_sort(out.begin(), out.end(), [](const Note& a, const Note& b) { return a.start < b.start; });
    return out;
}

std::vector<Note> quantize(const std::vector<Note>& played, const QuantizeSettings& q) {
    std::vector<Note> out = played;
    for (Note& n : out) {
        const std::int64_t line = std::llround(static_cast<double>(n.start) / static_cast<double>(q.grid));
        std::int64_t target = line * q.grid;
        if (line % 2 != 0)
            target += std::llround(q.swing * static_cast<double>(q.grid));
        n.start = std::max<std::int64_t>(0, std::llround(static_cast<double>(n.start) + q.strength * static_cast<double>(target - n.start)));
    }
    std::stable_sort(out.begin(), out.end(), [](const Note& a, const Note& b) { return a.start < b.start; });
    return out;
}

} // namespace minidaw

#include "../check.h"
