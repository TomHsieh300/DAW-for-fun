#pragma once
// minidaw.h - Step 1 snapshot of chapter 17: from the audio thread to the disk thread.
//
// The device hands the audio thread a block of input. That thread must not wait and must not
// allocate; a hard disk may stall for a long time and a growing take must allocate. So the two
// never meet: the audio thread copies the block into a ring, and another thread empties it.
//
//   audio thread                        disk thread (polls, nobody wakes it)
//   InputRing::push()  --> the ring -->  InputRing::pop()  --> Take::collect()
//
//   - One writer, one reader, two counters that only grow. written_ and read_ count FRAMES since
//     the start; a counter becomes an index with `& mask_`. Their difference is the fill level, so
//     no slot has to stay empty to tell "full" from "empty".
//   - A block goes in whole or not at all. When it does not fit, the audio thread drops it, counts
//     it and moves on. It never waits for the disk.
//   - A dropped block must not move what comes after it: the rest of the take would then play
//     early, for the whole song. So the ring remembers the gap, and pop() writes silence of exactly
//     that length in its place. The reader sees one unbroken stream that has as many frames as the
//     device delivered. Only a gap at the very end is lost, and it moves nothing.
//   - A gap is announced before the frames that follow it, in a small queue of its own. If that
//     queue is full the block is dropped too, and the gap simply grows.
//
// YOUR TURN (Step 1): InputRing::copyIn(), copyOut(), push(), pop() and Take::collect() in
// starter/step01.cpp.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace minidaw {

// ---- given ------------------------------------------------------------------------------------

// A stretch of frames that was dropped: `frames` of silence before the frame with number `atFrame`.
struct Gap {
    std::uint64_t atFrame = 0;
    std::uint64_t frames = 0;
};

// The same one-writer, one-reader queue as chapter 14's SpscQueue, small and with a look at the
// next item. The writer is the audio thread.
class GapQueue {
public:
    static constexpr std::size_t kCapacity = 16;

    void clear() noexcept {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }
    bool push(const Gap& gap) noexcept {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t next = (tail + 1) % slots_.size();
        if (next == head_.load(std::memory_order_acquire))
            return false; // full
        slots_[tail] = gap;
        tail_.store(next, std::memory_order_release);
        return true;
    }
    // The next gap, still in the queue. False when there is none.
    bool peek(Gap& gap) const noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire))
            return false;
        gap = slots_[head];
        return true;
    }
    // Removes the gap that peek() returned.
    void removeFirst() noexcept {
        head_.store((head_.load(std::memory_order_relaxed) + 1) % slots_.size(), std::memory_order_release);
    }

private:
    std::vector<Gap> slots_ = std::vector<Gap>(kCapacity + 1);
    std::atomic<std::size_t> head_{0}, tail_{0};
};

class InputRing {
public:
    // How much room `stallSeconds` of disk stall needs. Not a real-time function.
    static int framesForStall(double sampleRate, double stallSeconds) noexcept {
        return static_cast<int>(sampleRate * stallSeconds) + 1;
    }

    // Allocates room for at least `minFrames` frames (rounded up to a power of two, so that a
    // counter becomes an index with one AND). No other thread may be using the ring.
    void prepare(int numChannels, int minFrames) {
        std::size_t capacity = 1;
        while (capacity < static_cast<std::size_t>(minFrames > 0 ? minFrames : 1))
            capacity *= 2;
        channels_.assign(static_cast<std::size_t>(numChannels > 0 ? numChannels : 1), std::vector<float>(capacity, 0.0f));
        capacity_ = capacity;
        mask_ = capacity - 1;
        written_.store(0, std::memory_order_relaxed);
        read_.store(0, std::memory_order_relaxed);
        gaps_.clear();
        pendingGap_ = 0;
        gapLeft_ = 0;
        droppedBlocks_.store(0, std::memory_order_relaxed);
        droppedFrames_.store(0, std::memory_order_relaxed);
    }

    int numChannels() const noexcept { return static_cast<int>(channels_.size()); }
    std::size_t capacity() const noexcept { return capacity_; }

    // ---- audio thread -------------------------------------------------------------------------
    // Copies one device block into the ring, whole or not at all, and returns whether it did.
    // Never waits, never allocates. A ring channel without an input (more ring channels than
    // inputs, or a null pointer, which a device may send for an inactive channel) records silence.
    bool push(const float* const* input, int numInputChannels, int n) noexcept;

    // ---- disk thread --------------------------------------------------------------------------
    // Writes up to `maxFrames` frames to out[0 .. numChannels() - 1] and returns how many. Silence
    // takes the place of what was dropped. 0 means there is nothing more at the moment.
    int pop(float* const* out, int maxFrames) noexcept;

    // ---- any thread (for the warning in the UI) -------------------------------------------------
    // Frames waiting in the ring. read_ first: it can only fall behind written_, never overtake it.
    std::size_t fillLevel() const noexcept {
        const std::uint64_t read = read_.load(std::memory_order_acquire);
        return static_cast<std::size_t>(written_.load(std::memory_order_acquire) - read);
    }
    std::uint64_t droppedBlocks() const noexcept { return droppedBlocks_.load(std::memory_order_relaxed); }
    std::uint64_t droppedFrames() const noexcept { return droppedFrames_.load(std::memory_order_relaxed); }

private:
    // Writes n samples of one channel, starting at frame number `frame`, wrapping at the end of the
    // storage. A null source writes zeros.
    void copyIn(std::size_t channel, const float* source, std::uint64_t frame, int n) noexcept;
    // Reads n samples of one channel, starting at frame number `frame`, wrapping likewise.
    void copyOut(std::size_t channel, float* destination, std::uint64_t frame, int n) const noexcept;

    // Audio thread: the block does not fit. Counts it, and the gap grows.
    void drop(int n) noexcept {
        pendingGap_ += static_cast<std::uint64_t>(n);
        droppedBlocks_.fetch_add(1, std::memory_order_relaxed);
        droppedFrames_.fetch_add(static_cast<std::uint64_t>(n), std::memory_order_relaxed);
    }

    std::vector<std::vector<float>> channels_;
    std::size_t capacity_ = 0, mask_ = 0;
    std::atomic<std::uint64_t> written_{0}; // only push() changes it
    std::atomic<std::uint64_t> read_{0};    // only pop() changes it
    GapQueue gaps_;
    std::uint64_t pendingGap_ = 0; // audio thread only: dropped, not yet announced
    std::uint64_t gapLeft_ = 0;    // disk thread only: silence still to be written
    std::atomic<std::uint64_t> droppedBlocks_{0}, droppedFrames_{0};
};

// The take as it grows, on the disk thread: the one place in this picture that may allocate.
// (Step 3 also writes it to a file.)
class Take {
public:
    explicit Take(int numChannels)
        : channels_(static_cast<std::size_t>(numChannels)),
          scratch_(static_cast<std::size_t>(numChannels), std::vector<float>(kChunkFrames)) {}

    // Empties the ring into the take and returns the number of frames that arrived.
    int collect(InputRing& ring);

    const std::vector<std::vector<float>>& channels() const noexcept { return channels_; }
    std::size_t numFrames() const noexcept { return channels_.empty() ? 0 : channels_[0].size(); }

private:
    static constexpr int kChunkFrames = 4096;
    std::vector<std::vector<float>> channels_;
    std::vector<std::vector<float>> scratch_; // what pop() writes to, kChunkFrames per channel
};

} // namespace minidaw
