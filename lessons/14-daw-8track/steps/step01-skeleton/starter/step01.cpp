// Step 1 (starter): write Engine::processBlock() and Engine::processChunk().
//
// Build and run:  l14_step01_starter   (prints [FAIL] until both TODOs are done)
// Reference:      ../solution/step01.cpp, docs/step-01-skeleton.md ("Your turn")
//
// The class is declared in ../minidaw.h; you only write the two function bodies below.

#include "../minidaw.h"

namespace minidaw {

void Engine::processBlock(AudioBlock out) noexcept {
    // TODO 1: if the engine is not prepared, clear `out` and return.
    // TODO 2: cut `out` into chunks of at most maxBlockSize_ samples
    //         (out.getSubBlock(start, length)) and call processChunk() for each one.
    (void)out;
}

void Engine::processChunk(AudioBlock out) noexcept {
    // TODO 3: clear `out`; then copy source_ samples [position_, position_ + n) into every
    //         channel of `out` (a mono source feeds both channels; beyond the end of the source
    //         stays silent). Then advance position_ by n and increment chunksProcessed_.
    (void)out;
}

} // namespace minidaw

#include "../check.h"
