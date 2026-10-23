// Step 1 (solution): Engine::processBlock() and Engine::processChunk().
//
// Build and run:  l14_step01_solution   (ctest, label "solution")
// Your version:   starter/step01.cpp -> l14_step01_starter

#include "../minidaw.h"

namespace minidaw {

void Engine::processBlock(AudioBlock out) noexcept {
    if (!prepared_) { // a device may call us before the app has prepared the engine
        out.clear();
        return;
    }
    // Any block size: cut it into chunks the engine was prepared for. Later steps keep every
    // scratch buffer exactly maxBlockSize long, so no chunk may be longer.
    const int total = out.getNumSamples();
    int done = 0;
    while (done < total) {
        const int len = std::min(maxBlockSize_, total - done);
        processChunk(out.getSubBlock(done, len));
        done += len;
    }
}

void Engine::processChunk(AudioBlock out) noexcept {
    const int n = out.getNumSamples();
    out.clear();
    if (source_ && source_->getNumChannels() > 0) {
        const int64_t srcLen = source_->getNumSamples();
        for (int ch = 0; ch < out.getNumChannels(); ++ch) {
            const float* s = source_->getReadPointer(std::min(ch, source_->getNumChannels() - 1)); // mono -> both
            float* d = out.getChannelPointer(ch);
            for (int i = 0; i < n; ++i) {
                const int64_t si = position_ + i;
                if (si < srcLen)
                    d[i] = s[si];
            }
        }
    }
    position_ += n; // the "tape" moves on, also through the silence after the source
    ++chunksProcessed_;
}

} // namespace minidaw

#include "../check.h"
