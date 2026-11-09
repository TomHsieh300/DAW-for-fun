// Chapter 18, Step 4 (solution): polyphony.
//
// Build and run:  l18_step04_solution   (ctest, label "solution")
// Your version:   starter/step04.cpp -> l18_step04_starter

#include "../minidaw.h"

namespace minidaw {

int Synth::allocate(int pitch) noexcept {
    // 1. Played again: its own voice.
    for (int i = 0; i < numVoices_; ++i)
        if (voices_[static_cast<size_t>(i)].isActive() && voices_[static_cast<size_t>(i)].pitch() == pitch)
            return i;
    // 2. A free voice.
    for (int i = 0; i < numVoices_; ++i)
        if (!voices_[static_cast<size_t>(i)].isActive())
            return i;
    // 3. Steal what will be missed least: a released note, released longest ago; else the oldest note.
    int best = -1;
    for (int i = 0; i < numVoices_; ++i) {
        const Slot& s = slots_[static_cast<size_t>(i)];
        // Fading with its key up. (A sustained note does not fade; a voice fading for a stolen note has the new
        // note's key down.)
        if (voices_[static_cast<size_t>(i)].isReleasing() && !s.keyDown &&
            (best < 0 || s.released < slots_[static_cast<size_t>(best)].released))
            best = i;
    }
    if (best >= 0)
        return best;
    best = 0;
    for (int i = 1; i < numVoices_; ++i)
        if (slots_[static_cast<size_t>(i)].started < slots_[static_cast<size_t>(best)].started)
            best = i;
    return best;
}

void Synth::handle(const MidiMessage& m) noexcept {
    switch (m.kind()) {
    case Kind::NoteOn: {
        const int i = allocate(m.note());
        Voice& v = voices_[static_cast<size_t>(i)];
        if (v.isActive() && v.pitch() != m.note()) {
            v.steal(m.note(), m.velocity());
            ++stolen_;
        } else {
            v.start(m.note(), m.velocity());
        }
        slots_[static_cast<size_t>(i)] = {true, false, ++clock_, 0};
        break;
    }
    case Kind::NoteOff:
        for (int i = 0; i < numVoices_; ++i) {
            Voice& v = voices_[static_cast<size_t>(i)];
            Slot& s = slots_[static_cast<size_t>(i)];
            if (!v.isActive() || v.pitch() != m.note() || !s.keyDown)
                continue;
            s.keyDown = false;
            if (pedal_) {
                s.sustained = true;
            } else {
                v.release();
                s.released = ++clock_;
            }
        }
        break;
    case Kind::Controller:
        if (m.note() == kSustainPedal) {
            const bool down = m.velocity() >= 64;
            if (!down) // up: there are sustained notes only if it was down
                for (int i = 0; i < numVoices_; ++i) {
                    Slot& s = slots_[static_cast<size_t>(i)];
                    if (s.sustained) {
                        s.sustained = false;
                        voices_[static_cast<size_t>(i)].release();
                        s.released = ++clock_;
                    }
                }
            pedal_ = down;
        } else if (m.note() == kAllNotesOff) {
            for (int i = 0; i < numVoices_; ++i) {
                Slot& s = slots_[static_cast<size_t>(i)];
                const Voice& v = voices_[static_cast<size_t>(i)];
                // Release what still plays - and a voice fading for a stolen note, or that note starts after all.
                if (v.isActive() && (!v.isReleasing() || v.isStealing())) {
                    voices_[static_cast<size_t>(i)].release();
                    s.released = ++clock_;
                }
                s.keyDown = false;
                s.sustained = false;
            }
            pedal_ = false;
        }
        break;
    case Kind::PitchBend:
        bend_ = m.pitchBendValue() / 8192.0 * kBendRange;
        for (int i = 0; i < numVoices_; ++i)
            voices_[static_cast<size_t>(i)].setBend(bend_);
        break;
    case Kind::Other: break;
    }
}

void Synth::render(const std::vector<BlockEvent>& events, float* out, int numSamples) noexcept {
    std::fill(out, out + numSamples, 0.0f);
    int done = 0;
    auto renderTo = [this, out, &done](int to) {
        for (int i = 0; i < numVoices_; ++i)
            voices_[static_cast<size_t>(i)].render(out + done, to - done);
        done = to;
    };
    for (const BlockEvent& e : events) {
        renderTo(e.offset);
        handle(e.message);
    }
    renderTo(numSamples);
}

} // namespace minidaw

#include "../check.h"
