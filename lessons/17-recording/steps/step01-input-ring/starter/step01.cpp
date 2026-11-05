// Step 1 (starter): from the audio thread to the disk thread.
//
// Build and run:  l17_step01_starter   (prints [FAIL] until all five TODOs are done)
// Reference:      ../solution/step01.cpp, docs/step-01-input-ring.md ("Your turn")
//
// InputRing, GapQueue and Take are in ../minidaw.h.

#include "../minidaw.h"

namespace minidaw {

void InputRing::copyIn(std::size_t channel, const float* source, std::uint64_t frame, int n) noexcept {
    // TODO 1: The frame's place in the storage is frame & mask_. Write n samples from there, and
    //         go on at the start of the storage when you reach the end (two pieces at most). A null
    //         source writes zeros.
    (void)channel;
    (void)source;
    (void)frame;
    (void)n;
}

void InputRing::copyOut(std::size_t channel, float* destination, std::uint64_t frame, int n) const noexcept {
    // TODO 2: The same in the other direction.
    (void)channel;
    (void)destination;
    (void)frame;
    (void)n;
}

bool InputRing::push(const float* const* input, int numInputChannels, int n) noexcept {
    // TODO 3: n <= 0: true, nothing to do.
    //         Load written_ (relaxed: only this function changes it) and read_ (acquire). The free
    //         room is capacity_ - (written - read). If n is more than that: drop(n) and return false.
    //         If pendingGap_ > 0, announce it first: gaps_.push({written, pendingGap_}). If that
    //         fails: drop(n) and return false. If it works: pendingGap_ = 0.
    //         copyIn() every channel of the ring: input[ch] if ch < numInputChannels, else nullptr.
    //         Only then store written + n in written_ (release): this is what shows the frames
    //         to the reader, so they must be complete by now. Return true.
    (void)input;
    (void)numInputChannels;
    (void)n;
    return false;
}

int InputRing::pop(float* const* out, int maxFrames) noexcept {
    // TODO 4: Loop while fewer than maxFrames frames are done:
    //         Load written_ (acquire) BEFORE you look at the gaps. (push() announces a gap before
    //         it shows the frames behind it, so whoever has seen those frames will see the gap.)
    //         Load read_ (relaxed).
    //         If gapLeft_ is 0 and gaps_.peek() gives a gap at exactly the read position: remove it
    //         and gapLeft_ = its frames.
    //         If gapLeft_ > 0: write as much of it as still fits as zeros to every channel, count
    //         it as done, take it off gapLeft_ and start the loop again. (read_ does not move:
    //         these frames were never in the ring.)
    //         Otherwise the frames you may read are written - read, but not beyond the next gap,
    //         if there is one. None: leave the loop.
    //         copyOut() as many as still fit to every channel, count them as done, and store the new
    //         read position in read_ (release): this gives the room back to the writer, so the
    //         copying must be finished by now.
    //         Return the number of frames done.
    (void)out;
    (void)maxFrames;
    return 0;
}

int Take::collect(InputRing& ring) {
    // TODO 5: Pop into scratch_ (kChunkFrames per channel) until pop() returns 0, appending what
    //         arrives to channels_ each time. Return the total.
    (void)ring;
    return 0;
}

} // namespace minidaw

#include "../check.h"
