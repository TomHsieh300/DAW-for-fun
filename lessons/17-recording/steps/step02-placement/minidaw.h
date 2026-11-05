#pragma once
// minidaw.h - Step 2 snapshot of chapter 17: where the take belongs.
//
// Step 1 got the frames off the audio thread. But the sound in the input block is OLD, and the
// musician was playing along with a sound that was already old when it reached the speakers:
//
//   rendered at P --L_mix--> leaves the mix --L_out--> speaker / musician --L_in--> input block
//
// The block that arrives while the transport is at P was played along with position
//
//   P - (L_in + L_out + L_mix)
//
// so that is where the take starts on the timeline.
//
//   - Only the audio thread knows the position of the first block, so it stamps it, together with
//     the mix latency of that moment. The latencies stay as stamped for the whole take.
//   - A take is one straight line on the timeline. A stop or a seek ends it. (Step 4: loops.)
//   - The start can be before the timeline's zero. Those frames are not part of any song: the clip
//     skips them (chapter 14's Clip::sourceOffset) and starts at 0.
//   - What a driver reports is only an estimate. A click sent out and recorded back measures the
//     real round trip, and the difference becomes a manual trim.
//   - The UI and the audio thread share one state. Only the audio thread ever makes it Idle, so Idle
//     means that it has left the take: no more frames are coming, and the UI owns everything again.
//
// YOUR TURN (Step 2): place(), measureRoundTrip(), Recorder::start(), stop() and process() in
// starter/step02.cpp.

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace minidaw {

// ---- Step 1 (solved) ----------------------------------------------------------------------------

struct Gap {
    std::uint64_t atFrame = 0;
    std::uint64_t frames = 0;
};

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
            return false;
        slots_[tail] = gap;
        tail_.store(next, std::memory_order_release);
        return true;
    }
    bool peek(Gap& gap) const noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire))
            return false;
        gap = slots_[head];
        return true;
    }
    void removeFirst() noexcept {
        head_.store((head_.load(std::memory_order_relaxed) + 1) % slots_.size(), std::memory_order_release);
    }

private:
    std::vector<Gap> slots_ = std::vector<Gap>(kCapacity + 1);
    std::atomic<std::size_t> head_{0}, tail_{0};
};

class InputRing {
public:
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
        droppedFrames_.store(0, std::memory_order_relaxed);
    }

    int numChannels() const noexcept { return static_cast<int>(channels_.size()); }

    bool push(const float* const* input, int numInputChannels, int n) noexcept {
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

    int pop(float* const* out, int maxFrames) noexcept {
        int done = 0;
        while (done < maxFrames) {
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
                for (std::size_t ch = 0; ch < channels_.size(); ++ch)
                    std::fill_n(out[ch] + done, silence, 0.0f);
                gapLeft_ -= silence;
                done += static_cast<int>(silence);
                continue;
            }

            std::uint64_t ready = written - read;
            if (hasGap && gap.atFrame > read)
                ready = std::min(ready, gap.atFrame - read);
            if (ready == 0)
                break;
            const int n = static_cast<int>(std::min(ready, room));
            for (std::size_t ch = 0; ch < channels_.size(); ++ch)
                copyOut(ch, out[ch] + done, read, n);
            read_.store(read + static_cast<std::uint64_t>(n), std::memory_order_release);
            done += n;
        }
        return done;
    }

    std::uint64_t droppedFrames() const noexcept { return droppedFrames_.load(std::memory_order_relaxed); }

private:
    void copyIn(std::size_t channel, const float* source, std::uint64_t frame, int n) noexcept {
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
    void copyOut(std::size_t channel, float* destination, std::uint64_t frame, int n) const noexcept {
        const float* storage = channels_[channel].data();
        const std::size_t start = static_cast<std::size_t>(frame) & mask_;
        const std::size_t first = std::min(static_cast<std::size_t>(n), capacity_ - start);
        std::copy_n(storage + start, first, destination);
        std::copy_n(storage, static_cast<std::size_t>(n) - first, destination + first);
    }
    void drop(int n) noexcept {
        pendingGap_ += static_cast<std::uint64_t>(n);
        droppedFrames_.fetch_add(static_cast<std::uint64_t>(n), std::memory_order_relaxed);
    }

    std::vector<std::vector<float>> channels_;
    std::size_t capacity_ = 0, mask_ = 0;
    std::atomic<std::uint64_t> written_{0}, read_{0};
    GapQueue gaps_;
    std::uint64_t pendingGap_ = 0, gapLeft_ = 0;
    std::atomic<std::uint64_t> droppedFrames_{0};
};

class Take {
public:
    explicit Take(int numChannels)
        : channels_(static_cast<std::size_t>(numChannels)),
          scratch_(static_cast<std::size_t>(numChannels), std::vector<float>(kChunkFrames)) {}

    int collect(InputRing& ring) {
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

    const std::vector<std::vector<float>>& channels() const noexcept { return channels_; }
    std::size_t numFrames() const noexcept { return channels_.empty() ? 0 : channels_[0].size(); }

private:
    static constexpr int kChunkFrames = 4096;
    std::vector<std::vector<float>> channels_;
    std::vector<std::vector<float>> scratch_;
};

// ---- Step 2 -------------------------------------------------------------------------------------

// All in samples, all of them >= 0 except the trim.
struct Latencies {
    int input = 0;  // what the device reports: from the converter to our callback
    int output = 0; // what the device reports: from our callback to the converter
    int mix = 0;    // the mix's plugin delay compensation (chapter 14, Step 8)
    int trim = 0;   // by hand: how much more (or, negative, less) the real round trip is

    std::int64_t total() const noexcept {
        return static_cast<std::int64_t>(input) + output + mix + trim;
    }
};

// Where a finished take goes: the frames [skipFrames, skipFrames + numFrames) of the take play from
// timelineStart on. These are a Clip's sourceOffset, length and start.
struct Placement {
    std::int64_t timelineStart = 0;
    std::int64_t skipFrames = 0;
    std::int64_t numFrames = 0;
};

// `stampedPosition` is the transport position of the take's first frame, `numFrames` its length.
Placement place(std::int64_t stampedPosition, const Latencies& latencies, std::int64_t numFrames) noexcept;

// A click was rendered at timeline position `clickPosition`, and the take that recorded it begins at
// `stampedPosition`. Returns the real round trip in samples: the distance from the click to the first
// frame that reaches half of the take's peak. -1 if the take's peak is below `minPeak` (no click came
// back: wrong input, or the volume is down) or the click arrived before it was sent.
std::int64_t measureRoundTrip(const float* take, std::int64_t numFrames, std::int64_t stampedPosition,
                              std::int64_t clickPosition, float minPeak = 0.01f) noexcept;

// The recording, from both threads' side.
//
//   Idle --start()--> Armed --first block while playing--> Recording --stop()--> Stopping --next block--> Idle
//                       |                                     |                      ^
//                       '-------------------stop()------------+----------------------'
//                                                             '---transport stopped or jumped---> Idle
//
// Everything except the state belongs to the UI in Idle and to the audio thread in all other states.
class Recorder {
public:
    enum class State { Idle, Armed, Recording, Stopping };

    // ---- UI thread ----------------------------------------------------------------------------
    // Starts a new take: only from Idle, and then the audio thread does not touch the ring, so it may
    // be prepared here (which allocates). `device` gives input, output and trim; the audio thread adds
    // the mix latency when the take begins. False if the state is not Idle.
    bool start(const Latencies& device, int numChannels, int ringFrames);
    // Ends the take, or a start that never played. The audio thread confirms it with its next block:
    // only when isIdle() is true are the last frames certain to be in the ring.
    void stop() noexcept;
    bool isIdle() const noexcept { return state_.load(std::memory_order_acquire) == State::Idle; }
    State state() const noexcept { return state_.load(std::memory_order_acquire); }

    // Valid once isIdle() is true after a start(). wasStarted() is false if the transport never played.
    bool wasStarted() const noexcept { return started_; }
    std::int64_t stampedPosition() const noexcept { return stampedPosition_; }
    const Latencies& latencies() const noexcept { return latencies_; }

    // ---- audio thread -------------------------------------------------------------------------
    // Called for every device block. `position` is the transport position of the block's first frame.
    void process(const float* const* input, int numInputChannels, int n, std::int64_t position, bool playing,
                 int mixLatency) noexcept;

    // ---- disk thread --------------------------------------------------------------------------
    InputRing& ring() noexcept { return ring_; }

private:
    std::atomic<State> state_{State::Idle};
    InputRing ring_;
    Latencies latencies_;              // the audio thread fills in the mix when the take begins
    bool started_ = false;             // the same
    std::int64_t stampedPosition_ = 0; // the same
    std::int64_t nextPosition_ = 0;    // where the next block has to start
};

} // namespace minidaw
