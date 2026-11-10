#pragma once
// SpscQueue: a single-producer / single-consumer lock-free ring buffer (Step 7).
//
// The DAW has exactly two parties talking to each other through a queue: the UI (or any
// non-audio) thread produces commands, the audio thread consumes them. With one producer and
// one consumer a ring buffer with two atomic indices is enough: the producer only ever writes
// `tail_`, the consumer only ever writes `head_`, and each reads the other's index with
// acquire semantics. No mutex, no allocation after construction, no blocking.
//
//   head_ -> next slot to pop  (written by the consumer only)
//   tail_ -> next slot to push (written by the producer only)
//   empty: head_ == tail_      full: (tail_ + 1) % size == head_
//
// One slot is always left unused so that "full" and "empty" are distinguishable, which is why
// the vector holds capacity + 1 slots.
//
// Memory ordering, in one sentence: the producer writes the slot, then publishes it with a
// release store of tail_; the consumer reads tail_ with an acquire load, so it is guaranteed
// to see the finished slot. The same pairing in the other direction (head_) tells the
// producer that a slot may be reused.
//
// T must be trivially copyable: a slot is overwritten with a plain assignment and nothing is
// ever destroyed, so T may not own resources (pass raw pointers and let the receiver adopt).

#include <atomic>
#include <cstddef>
#include <type_traits>
#include <vector>

namespace dawlab::engine {

template <typename T>
class SpscQueue {
    static_assert(std::is_trivially_copyable_v<T>, "SpscQueue<T> requires a trivially copyable T");

public:
    // Allocates capacity + 1 slots. Construct on a non-audio thread.
    explicit SpscQueue(std::size_t capacity) : slots_(capacity + 1) {}

    SpscQueue(const SpscQueue&) = delete;
    SpscQueue& operator=(const SpscQueue&) = delete;

    // Producer side. Returns false when the queue is full (the item is NOT stored).
    bool push(const T& item) noexcept {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t next = increment(tail);
        if (next == head_.load(std::memory_order_acquire))
            return false; // full
        slots_[tail] = item;
        tail_.store(next, std::memory_order_release); // publish the slot
        return true;
    }

    // Consumer side. Returns false when the queue is empty.
    bool pop(T& out) noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire))
            return false; // empty
        out = slots_[head];
        head_.store(increment(head), std::memory_order_release); // hand the slot back
        return true;
    }

    // Approximate when the other side is mid-operation; exact when the queue is idle.
    std::size_t size() const noexcept {
        const std::size_t head = head_.load(std::memory_order_acquire);
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        return tail >= head ? tail - head : slots_.size() - head + tail;
    }

    bool empty() const noexcept { return size() == 0; }
    std::size_t capacity() const noexcept { return slots_.size() - 1; }

private:
    std::size_t increment(std::size_t i) const noexcept { return (i + 1 == slots_.size()) ? 0 : i + 1; }

    static constexpr std::size_t kCacheLine = 64;

    std::vector<T> slots_;
    // The two indices live on separate cache lines so the producer core and the consumer core
    // do not keep stealing the same line from each other ("false sharing"). We pad by hand
    // instead of using alignas(64), which would make MSVC emit warning C4324 at /W4.
    [[maybe_unused]] char padBefore_[kCacheLine] = {};
    std::atomic<std::size_t> head_{0};
    [[maybe_unused]] char padBetween_[kCacheLine - sizeof(std::atomic<std::size_t>)] = {};
    std::atomic<std::size_t> tail_{0};
    [[maybe_unused]] char padAfter_[kCacheLine - sizeof(std::atomic<std::size_t>)] = {};
};

} // namespace dawlab::engine
