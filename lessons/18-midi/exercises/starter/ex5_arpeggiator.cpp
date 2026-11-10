// Exercise 5 (starter): an arpeggiator.
//
// Fill in the TODOs. Until then the self-check prints FAIL (expected). The solution is
// solutions/ex5_arpeggiator.cpp. What the functions must do is in ../ex5_arpeggiator.h.

#include "../ex5_arpeggiator.h"

namespace ex5 {

void Arpeggiator::buildPattern() noexcept {
    // TODO: the held keys (sorted by pitch unless AsPlayed), repeated an octave higher `octaves` times
    //       (leave out pitches above 127), then in the order of the mode.
    patternSize_ = 0;
}

void Arpeggiator::process(std::int64_t blockStart, int numSamples, const Tempo& tempo, const std::vector<BlockEvent>& input,
                          std::vector<BlockEvent>& out) noexcept {
    // TODO: the steps of the block are the multiples of `rate` in [firstTickAtOrAfter(blockStart),
    //       firstTickAtOrAfter(blockStart + numSamples)). For each one, in order: end our note if its off tick has
    //       come, take the input up to the step's offset (keys, and pass the rest), then play the step. After the
    //       last step: the note off if it falls in the block, and the rest of the input.
    //       Keep `out` in order as you add to it - an insertion from the back, no std::sort, no allocation.
    (void)blockStart;
    (void)numSamples;
    (void)tempo;
    (void)input;
    out.clear();
}

void Arpeggiator::reset(std::vector<BlockEvent>& out) noexcept {
    // TODO: a note off at offset 0 for the note that sounds, if any; the pattern starts again at the next step.
    out.clear();
}

} // namespace ex5

#include "../ex5_check.h"
