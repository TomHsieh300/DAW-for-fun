#pragma once
// minidaw.h - Step 3 snapshot of chapter 17: the take on disk.
//
// Step 2 knows where a take belongs, but the take is still in memory, and an hour of stereo is 1.4 GB
// that a crash would lose. A recording is the one thing a DAW cannot ask the user to do again.
//
//   - The file grows while recording. Its header cannot know the final size, so the sizes stay 0
//     and the file's real length is the truth.
//   - Nothing waits in this process: every chunk goes to the operating system at once, and about
//     once a second to the disk.
//   - A crash leaves a `.recording` file. Recovering it and finishing a take normally are the same
//     function, sealTake(), so the path a crash needs is tested by every take.
//   - A full disk is a crash that you survive: the take ends where the file ends. Sealing only
//     overwrites bytes that already exist, so it needs no new space.
//   - The stamp goes into the file, so that a recovered take still knows where it belongs. The disk
//     thread may read it once a frame has come out of the ring: the push that made that frame visible
//     came after the stamp.
//
// YOUR TURN (Step 3): makeHeader(), TakeFile::open(), write() and finish(), sealTake() and
// TakeWriter::pump() in starter/step03.cpp.

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <io.h>
#include <share.h>
#else
#include <unistd.h>
#endif

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

// ---- Step 2 (solved) ----------------------------------------------------------------------------

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
inline Placement place(std::int64_t stampedPosition, const Latencies& latencies, std::int64_t numFrames) noexcept {
    const std::int64_t start = stampedPosition - latencies.total();
    // Frames from before the timeline's zero are not part of any song.
    const std::int64_t skip = std::min(start < 0 ? -start : 0, numFrames);
    return {start < 0 ? 0 : start, skip, numFrames - skip};
}

// A click was rendered at timeline position `clickPosition`, and the take that recorded it begins at
// `stampedPosition`. Returns the real round trip in samples: the distance from the click to the first
// frame that reaches half of the take's peak. -1 if the take's peak is below `minPeak` (no click came
// back: wrong input, or the volume is down) or the click arrived before it was sent.
inline std::int64_t measureRoundTrip(const float* take, std::int64_t numFrames, std::int64_t stampedPosition,
                              std::int64_t clickPosition, float minPeak = 0.01f) noexcept {
    float peak = 0.0f;
    for (std::int64_t i = 0; i < numFrames; ++i)
        peak = std::max(peak, std::abs(take[i]));
    if (peak < minPeak)
        return -1;
    for (std::int64_t i = 0; i < numFrames; ++i) {
        if (std::abs(take[i]) >= 0.5f * peak) {
            const std::int64_t roundTrip = stampedPosition + i - clickPosition;
            return roundTrip >= 0 ? roundTrip : -1;
        }
    }
    return -1;
}

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
    bool start(const Latencies& device, int numChannels, int ringFrames) {
        if (!isIdle())
            return false;
        // Idle: the audio thread stays away from everything below until it sees Armed.
        ring_.prepare(numChannels, ringFrames);
        latencies_ = device;
        started_ = false;
        state_.store(State::Armed, std::memory_order_release);
        return true;
    }
    // Ends the take, or a start that never played. The audio thread confirms it with its next block:
    // only when isIdle() is true are the last frames certain to be in the ring.
    void stop() noexcept {
        // The audio thread may be changing the state at this moment: Armed to Recording, or Recording to
        // Idle. In that order the two tries cannot miss a state that still needs stopping.
        State expected = State::Armed;
        if (!state_.compare_exchange_strong(expected, State::Stopping, std::memory_order_acq_rel)) {
            expected = State::Recording;
            state_.compare_exchange_strong(expected, State::Stopping, std::memory_order_acq_rel);
        }
    }
    bool isIdle() const noexcept { return state_.load(std::memory_order_acquire) == State::Idle; }
    State state() const noexcept { return state_.load(std::memory_order_acquire); }

    // Valid once isIdle() is true after a start(). wasStarted() is false if the transport never played.
    bool wasStarted() const noexcept { return started_; }
    // Where the last block ended. A tail that the ring dropped is not in the file, so a take can be
    // shorter than endPosition() - stampedPosition().
    std::int64_t endPosition() const noexcept { return nextPosition_; }
    // These two are also valid on the disk thread once a frame has come out of the ring: the audio
    // thread wrote them before the push, the push's release publishes them, and the pop's acquire
    // reads that. Until the take ends, the audio thread does not change them again.
    std::int64_t stampedPosition() const noexcept { return stampedPosition_; }
    const Latencies& latencies() const noexcept { return latencies_; }

    // ---- audio thread -------------------------------------------------------------------------
    // Called for every device block. `position` is the transport position of the block's first frame.
    void process(const float* const* input, int numInputChannels, int n, std::int64_t position, bool playing,
                           int mixLatency) noexcept {
        State state = state_.load(std::memory_order_acquire);
        if (state == State::Idle)
            return;
        if (state == State::Stopping) {
            state_.store(State::Idle, std::memory_order_release);
            return;
        }
        if (state == State::Armed) {
            if (!playing)
                return;
            if (!state_.compare_exchange_strong(state, State::Recording, std::memory_order_acq_rel))
                return; // stop() came first; the next block confirms it
            latencies_.mix = mixLatency;
            stampedPosition_ = position;
            nextPosition_ = position;
            started_ = true;
        } else if (!playing || position != nextPosition_) {
            // A take is one straight line: a stop or a seek ends it. If stop() made it Stopping in the
            // meantime, this store does what the next block would have done.
            state_.store(State::Idle, std::memory_order_release);
            return;
        }
        ring_.push(input, numInputChannels, n);
        nextPosition_ += n;
    }

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

// ---- Step 3 -------------------------------------------------------------------------------------

// A take's file. All numbers are little-endian. The samples are too, on every machine this course
// runs on (x86-64 and ARM64), so they are written as they are in memory.
//
//   offset  bytes  what
//        0      4  "RIFF"
//        4      4  file size - 8                  0 while recording
//        8      4  "WAVE"
//       12      4  "fmt "
//       16      4  16 (the size of this chunk)
//       20      2  3 (IEEE float)
//       22      2  number of channels
//       24      4  sample rate
//       28      4  bytes per second (sample rate * bytes per frame)
//       32      2  bytes per frame (number of channels * 4)
//       34      2  32 (bits per sample)
//       36      4  "dlab"                         readers skip chunks they do not know
//       40      4  24 (the size of this chunk)
//       44      8  stamped position
//       52      4  input latency
//       56      4  output latency
//       60      4  mix latency
//       64      4  trim
//       68      4  "data"
//       72      4  bytes of samples               0 while recording
//       76         the samples, interleaved, 32-bit float
constexpr int kHeaderBytes = 76;
constexpr std::uint64_t kMaxDataBytes = 0xFFFFFFFFull - (kHeaderBytes - 8); // a RIFF size has 32 bits
constexpr const char* kRecordingExtension = ".recording";

struct TakeInfo {
    int numChannels = 1;
    int sampleRate = 48000;
    std::int64_t stampedPosition = 0;
    Latencies latencies;
};

using WavHeader = std::array<unsigned char, kHeaderBytes>;

inline void putTag(unsigned char* to, const char* tag) noexcept { std::memcpy(to, tag, 4); }
inline void putU16(unsigned char* to, std::uint16_t v) noexcept {
    to[0] = static_cast<unsigned char>(v & 0xff);
    to[1] = static_cast<unsigned char>(v >> 8);
}
inline void putU32(unsigned char* to, std::uint32_t v) noexcept {
    putU16(to, static_cast<std::uint16_t>(v & 0xffff));
    putU16(to + 2, static_cast<std::uint16_t>(v >> 16));
}
inline void putU64(unsigned char* to, std::uint64_t v) noexcept {
    putU32(to, static_cast<std::uint32_t>(v & 0xffffffffu));
    putU32(to + 4, static_cast<std::uint32_t>(v >> 32));
}
inline std::uint16_t getU16(const unsigned char* from) noexcept {
    return static_cast<std::uint16_t>(from[0] | (from[1] << 8));
}
inline std::uint32_t getU32(const unsigned char* from) noexcept {
    return getU16(from) | (static_cast<std::uint32_t>(getU16(from + 2)) << 16);
}
inline std::uint64_t getU64(const unsigned char* from) noexcept {
    return getU32(from) | (static_cast<std::uint64_t>(getU32(from + 4)) << 32);
}

// The header of a take with no samples yet: both sizes are 0.
WavHeader makeHeader(const TakeInfo& info) noexcept;

// False if this is not the header of a take: any other WAV, or a torn or damaged file.
inline bool parseHeader(const WavHeader& header, TakeInfo& info) noexcept {
    const unsigned char* h = header.data();
    const auto tag = [h](int at, const char* expected) { return std::memcmp(h + at, expected, 4) == 0; };
    if (!tag(0, "RIFF") || !tag(8, "WAVE") || !tag(12, "fmt ") || !tag(36, "dlab") || !tag(68, "data"))
        return false;
    const int numChannels = getU16(h + 22);
    const std::uint32_t sampleRate = getU32(h + 24);
    if (getU32(h + 16) != 16 || getU16(h + 20) != 3 || getU16(h + 34) != 32 || getU32(h + 40) != 24 ||
        numChannels == 0 || sampleRate == 0 || sampleRate > 0x7fffffffu || getU16(h + 32) != numChannels * 4)
        return false;
    info.numChannels = numChannels;
    info.sampleRate = static_cast<int>(sampleRate);
    info.stampedPosition = static_cast<std::int64_t>(getU64(h + 44));
    info.latencies.input = static_cast<std::int32_t>(getU32(h + 52));
    info.latencies.output = static_cast<std::int32_t>(getU32(h + 56));
    info.latencies.mix = static_cast<std::int32_t>(getU32(h + 60));
    info.latencies.trim = static_cast<std::int32_t>(getU32(h + 64));
    return true;
}

struct FileCloser {
    void operator()(std::FILE* file) const noexcept { std::fclose(file); }
};
using FilePtr = std::unique_ptr<std::FILE, FileCloser>;

// fopen with a path that may hold any characters, on Windows too. Other programs may read the file
// while it is open: `x` in the mode refuses a file that already exists.
inline FilePtr openFile(const std::filesystem::path& path, const char* mode) noexcept {
#ifdef _WIN32
    wchar_t wideMode[8] = {};
    for (int i = 0; i < 7 && mode[i] != '\0'; ++i)
        wideMode[i] = static_cast<wchar_t>(mode[i]);
    return FilePtr(_wfsopen(path.c_str(), wideMode, _SH_DENYNO));
#else
    return FilePtr(std::fopen(path.c_str(), mode));
#endif
}

// fwrite and fflush only hand the bytes to the operating system, which is enough to survive a crash
// of this program. This asks it to put them on the disk, which is what survives a power cut.
inline bool syncToDisk(std::FILE* file) noexcept {
#ifdef _WIN32
    return _commit(_fileno(file)) == 0;
#else
    return fsync(fileno(file)) == 0;
#endif
}

// What a take needs from the disk. The real thing by default; the self-check fills the disk with these.
struct DiskOps {
    std::function<std::size_t(const void* bytes, std::size_t numBytes, std::FILE* file)> write =
        [](const void* bytes, std::size_t numBytes, std::FILE* file) { return std::fwrite(bytes, 1, numBytes, file); };
    std::function<bool(std::FILE* file)> sync = syncToDisk;
    std::uint64_t maxDataBytes = kMaxDataBytes;
};

// The file that `<name>.wav.recording` becomes.
struct SealedTake {
    bool ok = false;
    std::filesystem::path path; // <name>.wav
    TakeInfo info;
    std::uint64_t numFrames = 0;
};

// Makes a finished take out of a `.recording` file, whatever state it is in: makes it a whole number
// of frames, writes the real sizes into the header, and renames it to the name without `.recording`.
// A finished take and one that a crash left behind go through this same function.
//
// Not ok, and the file stays what it is, if the header is not a take's, if it holds more than
// maxDataBytes, or if the new name is taken. Doing it twice is safe, so a crash in here is too.
SealedTake sealTake(const std::filesystem::path& recording, std::uint64_t maxDataBytes = kMaxDataBytes);

// The `.recording` files in a folder: takes that a crash left behind.
inline std::vector<std::filesystem::path> findUnfinishedTakes(const std::filesystem::path& folder) {
    std::vector<std::filesystem::path> found;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
        if (it->path().extension() == kRecordingExtension)
            found.push_back(it->path());
    std::sort(found.begin(), found.end());
    return found;
}

enum class TakeProblem {
    None,
    CannotOpen,   // no file at all: the folder is missing, the name is taken, the disk is full
    WriteFailed,  // the disk is full or failing: the take ends where the file ends
    TooLong,      // the 4 GiB of a WAV file are used up: the take ends there
    CannotFinish, // the samples are safe in the `.recording` file, but it could not be sealed
};

// One take's file. Disk thread only, except problem(), which the UI can ask at any time.
class TakeFile {
public:
    explicit TakeFile(DiskOps ops = {}) : ops_(std::move(ops)) {}

    // Creates `<finalPath>.recording` with its header, and never over an existing file.
    bool open(const std::filesystem::path& finalPath, const TakeInfo& info);
    // Appends n frames, and forgets them if there is a problem. Nothing stays in this process.
    void write(const float* const* channels, int n);
    // Closes the file, and seals what has reached the disk. Not ok if the file was never opened.
    SealedTake finish();

    bool isOpen() const noexcept { return file_ != nullptr; }
    TakeProblem problem() const noexcept { return problem_.load(std::memory_order_relaxed); }

private:
    DiskOps ops_;
    FilePtr file_;
    std::filesystem::path recording_;
    TakeInfo info_;
    std::uint64_t dataBytes_ = 0;
    int framesSinceSync_ = 0;
    std::vector<float> interleaved_;
    std::atomic<TakeProblem> problem_{TakeProblem::None};
};

// The disk thread's side of one take: from the ring to the file. Make one after Recorder::start(),
// and call pump() on the disk thread until it returns true.
//
// A TakeWriter destroyed before that leaves a `.recording` file, exactly as a crash would.
class TakeWriter {
public:
    TakeWriter(Recorder& recorder, std::filesystem::path finalPath, int sampleRate, DiskOps ops = {})
        : recorder_(recorder), finalPath_(std::move(finalPath)), sampleRate_(sampleRate), file_(std::move(ops)) {
        const auto numChannels = static_cast<std::size_t>(recorder_.ring().numChannels());
        scratch_.assign(numChannels, std::vector<float>(kChunkFrames));
        for (auto& channel : scratch_)
            pointers_.push_back(channel.data());
    }

    // Moves what is in the ring to the file. True once the take is over and sealed.
    bool pump();

    // The UI can ask at any time, and should stop the recording when there is a problem.
    TakeProblem problem() const noexcept { return file_.problem(); }
    // After pump() returned true. Not ok if the take never began, or there was no file.
    const SealedTake& result() const noexcept { return result_; }

private:
    static constexpr int kChunkFrames = 4096;
    Recorder& recorder_;
    std::filesystem::path finalPath_;
    int sampleRate_;
    TakeFile file_;
    std::vector<std::vector<float>> scratch_;
    std::vector<float*> pointers_;
    SealedTake result_;
    bool done_ = false;
};

} // namespace minidaw
