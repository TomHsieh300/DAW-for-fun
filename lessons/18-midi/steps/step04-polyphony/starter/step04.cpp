// Step 4 (starter): polyphony.
//
// Build and run:  l18_step04_starter   (prints [FAIL] until all three TODOs are done)
// Reference:      ../solution/step04.cpp, docs/step-04-polyphony.md ("Your turn")
//
// Voice (Step 3, with steal() and setBend()) and Synth are in ../minidaw.h.

#include "../minidaw.h"

namespace minidaw {

int Synth::allocate(int pitch) noexcept {
    // TODO 1: Three loops over the first numVoices_ voices: an active voice on this pitch; an idle voice;
    //         then the released voice (isReleasing(), its slot's key not down) with the smallest
    //         `released`, or failing that the voice with the smallest `started`.
    (void)pitch;
    return 0;
}

void Synth::handle(const MidiMessage& message) noexcept {
    // TODO 2: switch on message.kind(): note on, note off, controller (64 and 123), pitch bend - the rules in
    //         the header. Keep the slots up to date: they are what allocate() decides by.
    (void)message;
    (void)clock_;
    (void)pedal_;
    (void)bend_;
    (void)stolen_;
}

void Synth::render(const std::vector<BlockEvent>& events, float* out, int numSamples) noexcept {
    // TODO 3: Clear out. Then, as Step 1's renderBlock(): every voice renders up to each event's offset,
    //         handle() the event, and so on to the end of the block.
    (void)events;
    std::fill(out, out + numSamples, 0.0f);
}

} // namespace minidaw

#include "../check.h"
