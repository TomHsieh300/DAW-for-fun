// Chapter 18, Step 5 (solution): a keyboard.
//
// Build and run:  l18_step05_solution   (ctest, label "solution")
// Your version:   starter/step05.cpp -> l18_step05_starter

#include "../minidaw.h"

namespace minidaw {

bool MidiParser::feed(std::uint8_t byte, MidiMessage& out) noexcept {
    if (byte >= 0xF8)
        return false; // real time: between any two bytes, touches nothing
    if (byte >= 0xF0) {
        sysex_ = byte == 0xF0; // 0xF7 ends it; other system common messages are not SysEx
        running_ = 0;
        have_ = 0;
        return false;
    }
    if (byte >= 0x80) {
        sysex_ = false;
        running_ = byte;
        have_ = 0;
        return false;
    }
    if (sysex_)
        return false;
    if (running_ == 0) {
        ++skipped_;
        return false;
    }
    data_[have_++] = byte;
    if (have_ < dataBytesOf(running_))
        return false;
    out = {running_, data_[0], have_ == 2 ? data_[1] : std::uint8_t{0}};
    have_ = 0; // the running status stays
    return true;
}

bool MidiFifo::push(const TimedMidi& m) noexcept {
    const std::uint64_t w = written_.load(std::memory_order_relaxed);
    if (w - read_.load(std::memory_order_acquire) == kCapacity) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    items_[w & (kCapacity - 1)] = m;
    written_.store(w + 1, std::memory_order_release); // after the item: the consumer sees it whole
    return true;
}

bool MidiFifo::pop(TimedMidi& m) noexcept {
    const std::uint64_t r = read_.load(std::memory_order_relaxed);
    if (r == written_.load(std::memory_order_acquire))
        return false;
    m = items_[r & (kCapacity - 1)];
    read_.store(r + 1, std::memory_order_release); // after the copy: the producer may reuse the slot
    return true;
}

const TimedMidi* MidiFifo::peek() const noexcept {
    const std::uint64_t r = read_.load(std::memory_order_relaxed);
    if (r == written_.load(std::memory_order_acquire))
        return nullptr;
    return &items_[r & (kCapacity - 1)];
}

void drain(MidiFifo& fifo, const BlockClock& clock, Timing timing, double delay, std::vector<BlockEvent>& out, long& late) noexcept {
    out.clear();
    TimedMidi m;
    if (timing == Timing::AsSoonAsPossible) {
        while (fifo.pop(m))
            out.push_back({0, m.message});
        return;
    }
    while (const TimedMidi* next = fifo.peek()) {
        const auto offset = static_cast<long long>(std::floor((next->time + delay - clock.blockTime) * clock.sampleRate));
        if (offset >= clock.numSamples)
            break; // for a later block
        if (offset < 0)
            ++late;
        out.push_back({static_cast<int>(std::max<long long>(offset, 0)), next->message});
        fifo.pop(m);
    }
}

} // namespace minidaw

#include "../check.h"
