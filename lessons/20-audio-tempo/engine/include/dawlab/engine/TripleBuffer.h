#pragma once
// TripleBuffer: hands a value of several fields from ONE writer thread to ONE reader thread, always whole.
//
// A value such as a tempo map or a loop range (start, end, unit) cannot travel in one std::atomic: written
// and read field by field, the reader can get half the old value and half the new one. A triple buffer keeps
// three copies, each owned by one side at a time:
//
//        writer                 middle                  reader
//        back  --- write() --->  [new]  --- take() --->  front
//
//   write(value)  writer: copies the value into its back slot and swaps that slot with the middle one,
//                 marked new. The writer never touches the reader's slot.
//   take()        reader: if the middle slot is new, swaps it with its front slot; front() is then the newest
//                 value. One atomic exchange, no copy, no allocation, no lock: real-time safe.
//
// A value the reader has not taken yet is replaced by a newer write (the reader only ever needs the newest).
// Only one thread may write, and only one may read at a time (the reader's role may pass to another thread
// while the first one is stopped, as the audio thread's does to an offline render).

#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

namespace dawlab::engine {

template <typename T>
class TripleBuffer {
public:
    TripleBuffer() = default;
    explicit TripleBuffer(const T& initial) { slots_.fill(initial); }
    TripleBuffer(const TripleBuffer&) = delete;
    TripleBuffer& operator=(const TripleBuffer&) = delete;

    // Writer: publishes `value` whole.
    void write(const T& value) noexcept(std::is_nothrow_copy_assignable_v<T>) {
        slots_[static_cast<std::size_t>(back_)] = value;
        back_ = middle_.exchange(back_ | kNew, std::memory_order_acq_rel) & kIndex;
    }

    // Reader: whether a value was written since the last take().
    bool hasNew() const noexcept { return (middle_.load(std::memory_order_acquire) & kNew) != 0; }
    // Reader: makes the newest written value front(). Returns false (front() unchanged) if there was none.
    bool take() noexcept {
        if (!hasNew())
            return false;
        front_ = middle_.exchange(front_, std::memory_order_acq_rel) & kIndex;
        return true;
    }
    // Reader: the value it took last (the initial value before the first take()).
    const T& front() const noexcept { return slots_[static_cast<std::size_t>(front_)]; }

private:
    static constexpr int kIndex = 3, kNew = 4; // a slot's index, and the mark of a middle slot not taken yet
    std::array<T, 3> slots_{};
    int back_ = 0;               // writer
    std::atomic<int> middle_{1}; // either thread, exchanged
    int front_ = 2;              // reader
};

} // namespace dawlab::engine
