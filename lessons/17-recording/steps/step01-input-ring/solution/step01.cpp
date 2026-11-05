// Step 1 (solution): from the audio thread to the disk thread.
//
// Build and run:  l17_step01_solution   (ctest, label "solution")
// Your version:   starter/step01.cpp -> l17_step01_starter

#include "../minidaw.h"

#include <algorithm>

namespace minidaw {

void InputRing::copyIn(std::size_t channel, const float* source, std::uint64_t frame, int n) noexcept {
    float* storage = channels_[channel].data();
    const std::size_t start = static_cast<std::size_t>(frame) & mask_;
    const std::size_t first = std::min(static_cast<std::size_t>(n), capacity_ - start);
    const std::size_t second = static_cast<std::size_t>(n) - first;
    if (source != nullptr) {
        std::copy_n(source, first, storage + start);
        std::copy_n(source + first, second, storage);
    } else {
        std::fill_n(storage + start, first, 0.0f);
        std::fill_n(storage, second, 0.0f);
    }
}

void InputRing::copyOut(std::size_t channel, float* destination, std::uint64_t frame, int n) const noexcept {
    const float* storage = channels_[channel].data();
    const std::size_t start = static_cast<std::size_t>(frame) & mask_;
    const std::size_t first = std::min(static_cast<std::size_t>(n), capacity_ - start);
    std::copy_n(storage + start, first, destination);
    std::copy_n(storage, static_cast<std::size_t>(n) - first, destination + first);
}

bool InputRing::push(const float* const* input, int numInputChannels, int n) noexcept {
    if (n <= 0)
        return true;
    const std::uint64_t written = written_.load(std::memory_order_relaxed);
    const std::uint64_t read = read_.load(std::memory_order_acquire);
    if (static_cast<std::uint64_t>(n) > capacity_ - (written - read)) {
        drop(n);
        return false;
    }
    if (pendingGap_ > 0) {
        if (!gaps_.push({written, pendingGap_})) {
            drop(n);
            return false;
        }
        pendingGap_ = 0;
    }
    for (std::size_t ch = 0; ch < channels_.size(); ++ch)
        copyIn(ch, static_cast<int>(ch) < numInputChannels ? input[ch] : nullptr, written, n);
    written_.store(written + static_cast<std::uint64_t>(n), std::memory_order_release);
    return true;
}

int InputRing::pop(float* const* out, int maxFrames) noexcept {
    const std::size_t numChannels = channels_.size();
    int done = 0;
    while (done < maxFrames) {
        // In this order: whoever has seen the frames behind a gap will also see the gap.
        const std::uint64_t written = written_.load(std::memory_order_acquire);
        const std::uint64_t read = read_.load(std::memory_order_relaxed);
        const auto room = static_cast<std::uint64_t>(maxFrames - done);

        Gap gap;
        const bool hasGap = gaps_.peek(gap);
        if (gapLeft_ == 0 && hasGap && gap.atFrame == read) {
            gaps_.removeFirst();
            gapLeft_ = gap.frames;
        }
        if (gapLeft_ > 0) {
            const std::uint64_t silence = std::min(gapLeft_, room);
            for (std::size_t ch = 0; ch < numChannels; ++ch)
                std::fill_n(out[ch] + done, silence, 0.0f);
            gapLeft_ -= silence;
            done += static_cast<int>(silence);
            continue;
        }

        std::uint64_t ready = written - read;
        if (hasGap && gap.atFrame > read)
            ready = std::min(ready, gap.atFrame - read); // the frames before the next gap
        if (ready == 0)
            break;
        const int n = static_cast<int>(std::min(ready, room));
        for (std::size_t ch = 0; ch < numChannels; ++ch)
            copyOut(ch, out[ch] + done, read, n);
        read_.store(read + static_cast<std::uint64_t>(n), std::memory_order_release);
        done += n;
    }
    return done;
}

int Take::collect(InputRing& ring) {
    std::vector<float*> out;
    for (auto& channel : scratch_)
        out.push_back(channel.data());

    int total = 0;
    while (const int n = ring.pop(out.data(), kChunkFrames)) {
        for (std::size_t ch = 0; ch < channels_.size(); ++ch)
            channels_[ch].insert(channels_[ch].end(), scratch_[ch].begin(), scratch_[ch].begin() + n);
        total += n;
    }
    return total;
}

} // namespace minidaw

#include "../check.h"
