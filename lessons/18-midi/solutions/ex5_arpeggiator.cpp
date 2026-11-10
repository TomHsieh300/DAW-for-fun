// Exercise 5 (solution): an arpeggiator.
//
// Build and run:  l18_ex5_solution   (ctest, label "solution")
// Your version:   exercises/starter/ex5_arpeggiator.cpp -> l18_ex5_starter

#include "../exercises/ex5_arpeggiator.h"

namespace ex5 {

namespace {

// Appends `e` and moves it back past every event with a later offset. At one offset the order stays the
// order of emitting, and process() emits our note off before the input and the step of its offset - so the
// note offs come first without a rule of their own. No allocation as long as the capacity lasts.
void emit(std::vector<BlockEvent>& out, const BlockEvent& e) noexcept {
    if (out.size() == out.capacity())
        return; // the caller reserved too little: drop rather than allocate on the audio thread
    out.push_back(e);
    for (size_t i = out.size() - 1; i > 0 && out[i - 1].offset > e.offset; --i)
        std::swap(out[i - 1], out[i]);
}

} // namespace

void Arpeggiator::buildPattern() noexcept {
    patternSize_ = 0;
    std::array<Key, kMaxHeld> keys{};
    for (int i = 0; i < numHeld_; ++i)
        keys[static_cast<size_t>(i)] = held_[static_cast<size_t>(i)];
    if (settings_.mode != ArpMode::AsPlayed)
        std::sort(keys.begin(), keys.begin() + numHeld_, [](const Key& a, const Key& b) { return a.pitch < b.pitch; });
    std::array<Key, kMaxHeld * 4> up{};
    int n = 0;
    for (int o = 0; o < settings_.octaves; ++o)
        for (int i = 0; i < numHeld_; ++i) {
            Key k = keys[static_cast<size_t>(i)];
            k.pitch += 12 * o;
            if (k.pitch <= 127)
                up[static_cast<size_t>(n++)] = k;
        }
    switch (settings_.mode) {
    case ArpMode::Up:
    case ArpMode::AsPlayed:
        for (int i = 0; i < n; ++i)
            pattern_[static_cast<size_t>(patternSize_++)] = up[static_cast<size_t>(i)];
        break;
    case ArpMode::Down:
        for (int i = n - 1; i >= 0; --i)
            pattern_[static_cast<size_t>(patternSize_++)] = up[static_cast<size_t>(i)];
        break;
    case ArpMode::UpDown:
        for (int i = 0; i < n; ++i)
            pattern_[static_cast<size_t>(patternSize_++)] = up[static_cast<size_t>(i)];
        for (int i = n - 2; i >= 1; --i)
            pattern_[static_cast<size_t>(patternSize_++)] = up[static_cast<size_t>(i)];
        break;
    }
}

void Arpeggiator::process(std::int64_t blockStart, int numSamples, const Tempo& tempo, const std::vector<BlockEvent>& input,
                          std::vector<BlockEvent>& out) noexcept {
    out.clear();
    const std::int64_t from = minidaw::firstTickAtOrAfter(blockStart, tempo);
    const std::int64_t to = minidaw::firstTickAtOrAfter(blockStart + numSamples, tempo);
    const int rate = settings_.rate;
    auto offsetOf = [&](std::int64_t tick) { return static_cast<int>(minidaw::sampleOfTick(tick, tempo) - blockStart); };
    size_t next = 0;

    // Input up to and including `offset`: keys change, other events pass.
    auto takeInput = [&](int offset) {
        for (; next < input.size() && input[next].offset <= offset; ++next) {
            const MidiMessage& m = input[next].message;
            const Kind kind = m.kind();
            if (kind == Kind::NoteOn) {
                bool already = false;
                for (int i = 0; i < numHeld_; ++i)
                    already = already || held_[static_cast<size_t>(i)].pitch == m.note();
                if (already)
                    continue;
                if (numHeld_ == kMaxHeld) { // push out the oldest
                    for (int i = 1; i < numHeld_; ++i)
                        held_[static_cast<size_t>(i - 1)] = held_[static_cast<size_t>(i)];
                    --numHeld_;
                }
                held_[static_cast<size_t>(numHeld_++)] = {m.note(), m.velocity(), m.channel()};
            } else if (kind == Kind::NoteOff) {
                for (int i = 0; i < numHeld_; ++i)
                    if (held_[static_cast<size_t>(i)].pitch == m.note()) {
                        for (int j = i + 1; j < numHeld_; ++j)
                            held_[static_cast<size_t>(j - 1)] = held_[static_cast<size_t>(j)];
                        --numHeld_;
                        break;
                    }
            } else {
                emit(out, input[next]);
            }
        }
    };
    auto endNote = [&](std::int64_t upTo) { // our note ends if its tick is before `upTo`
        if (sounding_ && offTick_ < upTo) {
            emit(out, {offsetOf(offTick_), MidiMessage::noteOff(playing_.channel, playing_.pitch)});
            sounding_ = false;
        }
    };

    // The first step at or after `from`.
    std::int64_t step = from >= 0 ? (from + rate - 1) / rate * rate : -((-from) / rate) * rate;
    for (; step < to; step += rate) {
        const int at = offsetOf(step);
        endNote(step + 1); // a note ending at this step ends before it
        takeInput(at);
        if (numHeld_ == 0) {
            index_ = 0;
            continue;
        }
        buildPattern();
        if (patternSize_ == 0)
            continue;
        playing_ = pattern_[static_cast<size_t>(index_ % patternSize_)];
        ++index_;
        sounding_ = true;
        offTick_ = step + gateTicks();
        emit(out, {at, MidiMessage::noteOn(playing_.channel, playing_.pitch, playing_.velocity)});
    }
    endNote(to);
    takeInput(numSamples);
}

void Arpeggiator::reset(std::vector<BlockEvent>& out) noexcept {
    out.clear();
    if (sounding_)
        emit(out, {0, MidiMessage::noteOff(playing_.channel, playing_.pitch)});
    sounding_ = false;
    index_ = 0;
}

} // namespace ex5

#include "../exercises/ex5_check.h"
