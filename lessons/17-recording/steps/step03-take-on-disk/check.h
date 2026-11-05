#pragma once
// check.h - the self-check of Step 3 (the take on disk). Defines main(); included at the END of
// starter/step03.cpp and solution/step03.cpp, so both run exactly the same checks.
//
// A file copied while the writer still has it open is what a crash of this program leaves behind:
// whatever is still in the writer's memory is not in the copy. A DiskOps that stops writing after so
// many bytes is a full disk. The files are read back with dawlab::readWav, which knows nothing about
// takes, so a take that passes is a WAV that other programs can read. Ends with real threads; build it
// with -fsanitize=thread as well.

#include <dawlab/Common.h>
#include <dawlab/WavFile.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

namespace step03_check {

using namespace minidaw;
namespace fs = std::filesystem;

using ull = unsigned long long;
using Bytes = std::vector<char>;

// A folder of its own under the build folder, gone at the end.
class TempFolder {
public:
    TempFolder() {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::path("dawlab_output") / ("l17_step03_" + std::to_string(now));
        fs::create_directories(path_);
    }
    ~TempFolder() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    fs::path file(const std::string& name) const { return path_ / name; }
    const fs::path& path() const noexcept { return path_; }

private:
    fs::path path_;
};

Bytes readBytes(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return Bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

void writeBytes(const fs::path& path, const Bytes& bytes, std::size_t count) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(bytes.data(), static_cast<std::streamsize>(count));
}

fs::path recordingOf(fs::path path) {
    path += kRecordingExtension;
    return path;
}

// Exact in a float, different on every frame and channel, and never 0.
float sampleOf(std::int64_t frame, int channel) {
    const float value = static_cast<float>(frame % (1 << 22) + 1);
    return channel == 0 ? value : -value - static_cast<float>(channel) * 0.5f;
}

TakeInfo stereoInfo() {
    TakeInfo info;
    info.numChannels = 2;
    info.sampleRate = 48000;
    info.stampedPosition = 123456789012345LL;
    info.latencies = Latencies{240, 480, 752, -37};
    return info;
}

// Writes the frames [first, first + count) of the test signal, in chunks of `chunk`.
void writeFrames(TakeFile& file, std::int64_t first, int count, int numChannels, int chunk = 64) {
    std::vector<std::vector<float>> data(static_cast<std::size_t>(numChannels), std::vector<float>(static_cast<std::size_t>(chunk)));
    std::vector<const float*> pointers;
    for (const auto& channel : data)
        pointers.push_back(channel.data());
    for (int done = 0; done < count;) {
        const int n = std::min(chunk, count - done);
        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < n; ++i)
                data[static_cast<std::size_t>(ch)][static_cast<std::size_t>(i)] = sampleOf(first + done + i, ch);
        file.write(pointers.data(), n);
        done += n;
    }
}

// The file holds exactly `frames` frames of the test signal, for any reader, and says so correctly.
bool holdsSignal(const fs::path& path, int numChannels, std::int64_t frames) {
    const Bytes bytes = readBytes(path);
    if (bytes.size() < kHeaderBytes)
        return false;
    const auto* header = reinterpret_cast<const unsigned char*>(bytes.data());
    if (getU32(header + 4) != bytes.size() - 8 || getU32(header + 72) != bytes.size() - kHeaderBytes)
        return false;

    dawlab::WavData wav;
    if (!dawlab::readWav(path.string(), wav))
        return false;
    if (wav.buffer.getNumChannels() != numChannels || wav.buffer.getNumSamples() != frames || wav.bitDepth != 32)
        return false;
    for (int ch = 0; ch < numChannels; ++ch)
        for (int i = 0; i < wav.buffer.getNumSamples(); ++i)
            if (wav.buffer.getSample(ch, i) != sampleOf(i, ch))
                return false;
    return true;
}

// A disk with so many bytes left. What fits is really written, as far as it fits.
struct SmallDisk {
    std::uint64_t left = 0;
    int writesAfterFull = 0;
    bool full = false;

    DiskOps ops() {
        DiskOps result;
        result.write = [this](const void* bytes, std::size_t numBytes, std::FILE* file) {
            writesAfterFull += full ? 1 : 0;
            const auto n = static_cast<std::size_t>(std::min<std::uint64_t>(left, numBytes));
            const std::size_t written = std::fwrite(bytes, 1, n, file);
            left -= written;
            full = full || written < numBytes;
            return written;
        };
        return result;
    }
};

void checkHeader() {
    const WavHeader header = makeHeader(stereoInfo());
    const unsigned char* h = header.data();
    const auto tag = [h](int at, const char* expected) { return std::memcmp(h + at, expected, 4) == 0; };

    DAWLAB_CHECK(tag(0, "RIFF"));
    DAWLAB_CHECK(tag(8, "WAVE"));
    DAWLAB_CHECK(tag(12, "fmt "));
    DAWLAB_CHECK(tag(36, "dlab"));
    DAWLAB_CHECK(tag(68, "data"));
    DAWLAB_CHECK(getU32(h + 4) == 0);
    DAWLAB_CHECK(getU32(h + 72) == 0);
    DAWLAB_CHECK(getU32(h + 16) == 16);
    DAWLAB_CHECK(getU16(h + 20) == 3);
    DAWLAB_CHECK(getU16(h + 22) == 2);
    DAWLAB_CHECK(getU32(h + 24) == 48000);
    DAWLAB_CHECK(getU32(h + 28) == 384000);
    DAWLAB_CHECK(getU16(h + 32) == 8);
    DAWLAB_CHECK(getU16(h + 34) == 32);
    DAWLAB_CHECK(getU32(h + 40) == 24);
    DAWLAB_CHECK(getU64(h + 44) == 123456789012345ULL);
    DAWLAB_CHECK(getU32(h + 52) == 240);
    DAWLAB_CHECK(getU32(h + 56) == 480);
    DAWLAB_CHECK(getU32(h + 60) == 752);

    // Through the header and back, negative numbers too.
    TakeInfo negative = stereoInfo();
    negative.stampedPosition = -1472;
    TakeInfo back;
    DAWLAB_CHECK(parseHeader(makeHeader(negative), back));
    DAWLAB_CHECK(back.numChannels == 2);
    DAWLAB_CHECK(back.sampleRate == 48000);
    DAWLAB_CHECK(back.stampedPosition == -1472);
    DAWLAB_CHECK(back.latencies.total() == negative.latencies.total());
    DAWLAB_CHECK(back.latencies.trim == -37);
}

void checkWriteAndFinish() {
    TempFolder folder;
    for (int numChannels : {1, 2, 3}) {
        const fs::path path = folder.file("take" + std::to_string(numChannels) + ".wav");
        TakeInfo info = stereoInfo();
        info.numChannels = numChannels;
        TakeFile file;
        DAWLAB_CHECK(file.open(path, info));
        DAWLAB_CHECK(file.isOpen());
        DAWLAB_CHECK(fs::exists(recordingOf(path)));
        DAWLAB_CHECK(!fs::exists(path));
        writeFrames(file, 0, 1000, numChannels, 64);
        writeFrames(file, 1000, 5000, numChannels, 4096);
        const SealedTake sealed = file.finish();

        DAWLAB_CHECK(sealed.ok);
        DAWLAB_CHECK(sealed.path == path);
        DAWLAB_CHECK(sealed.numFrames == 6000);
        DAWLAB_CHECK(sealed.info.numChannels == numChannels);
        DAWLAB_CHECK(sealed.info.stampedPosition == info.stampedPosition);
        DAWLAB_CHECK(sealed.info.latencies.total() == info.latencies.total());
        DAWLAB_CHECK(!file.isOpen());
        DAWLAB_CHECK(file.problem() == TakeProblem::None);
        DAWLAB_CHECK(!fs::exists(recordingOf(path)));
        DAWLAB_CHECK(holdsSignal(path, numChannels, 6000));
    }
    TakeFile never;
    DAWLAB_CHECK(!never.finish().ok);
}

// Any characters in the path, also where the system's "narrow" code page cannot show them.
void checkAnyName() {
    TempFolder folder;
    const fs::path path = folder.path() / fs::u8path(u8"錄音 αβ take.wav");
    TakeFile file;
    DAWLAB_CHECK(file.open(path, stereoInfo()));
    writeFrames(file, 0, 100, 2);
    DAWLAB_CHECK(file.finish().ok);
    DAWLAB_CHECK(fs::exists(path));
    DAWLAB_CHECK(fs::file_size(path) == kHeaderBytes + 100 * 8);
}

// What a crash at any moment would leave, taken while the writer is still running.
void checkCrash() {
    TempFolder folder;
    const fs::path path = folder.file("take.wav");
    TakeFile file;
    DAWLAB_CHECK(file.open(path, stereoInfo()));

    // A reader without any idea sees an empty file: the sizes in the header are still 0.
    {
        fs::copy_file(recordingOf(path), folder.file("empty.wav"));
        dawlab::WavData wav;
        DAWLAB_CHECK(dawlab::readWav(folder.file("empty.wav").string(), wav));
        DAWLAB_CHECK(wav.buffer.getNumSamples() == 0);
    }

    int lost = 0, checked = 0;
    std::int64_t frames = 0;
    for (const int n : {1, 7, 64, 480, 4096, 1000, 3, 9000}) {
        writeFrames(file, frames, n, 2, n);
        frames += n;
        const fs::path crash = folder.file("crash" + std::to_string(++checked) + ".wav");
        const fs::path crashRecording = recordingOf(crash);
        fs::copy_file(recordingOf(path), crashRecording);
        const SealedTake sealed = sealTake(crashRecording);
        DAWLAB_CHECK(sealed.ok);
        DAWLAB_CHECK(sealed.path == crash);
        lost += static_cast<int>(frames - static_cast<std::int64_t>(sealed.numFrames));
        DAWLAB_CHECK(sealed.info.stampedPosition == stereoInfo().stampedPosition);
        DAWLAB_CHECK(sealed.info.latencies.trim == -37);
        DAWLAB_CHECK(holdsSignal(crash, 2, static_cast<std::int64_t>(sealed.numFrames)));
    }
    std::printf("  a crash after any write: %d copies recovered, %d frames lost (expected 0)\n", checked, lost);
    DAWLAB_CHECK(lost == 0);
    DAWLAB_CHECK(file.finish().ok);
}

// A crash can stop the file at any byte.
void checkEveryLength() {
    TempFolder folder;
    const fs::path path = folder.file("whole.wav");
    const fs::path cut = folder.file("cut.wav");
    const fs::path cutRecording = recordingOf(cut);
    Bytes whole;
    {
        TakeFile file;
        DAWLAB_CHECK(file.open(path, stereoInfo()));
        writeFrames(file, 0, 10, 2);
        whole = readBytes(recordingOf(path));
    }
    const std::size_t total = kHeaderBytes + 10 * 8;
    DAWLAB_CHECK(whole.size() == total);

    int wrong = 0;
    for (std::size_t length = 0; length <= whole.size(); ++length) {
        writeBytes(cutRecording, whole, length);
        const SealedTake sealed = sealTake(cutRecording);
        if (length < kHeaderBytes) {
            // Nothing can be said about it, so it stays, as it is, for someone to look at.
            wrong += !sealed.ok && fs::exists(cutRecording) && !fs::exists(cut) && fs::file_size(cutRecording) == length ? 0 : 1;
            continue;
        }
        const auto frames = static_cast<std::int64_t>((length - kHeaderBytes) / 8);
        wrong += sealed.ok && static_cast<std::int64_t>(sealed.numFrames) == frames ? 0 : 1;
        wrong += fs::exists(cut) && !fs::exists(cutRecording) && holdsSignal(cut, 2, frames) ? 0 : 1;
        wrong += fs::exists(cut) && fs::file_size(cut) == kHeaderBytes + static_cast<std::uint64_t>(frames) * 8 ? 0 : 1;

        // A crash while sealing: the header is done, the name is not. Again gives the same file.
        const Bytes once = readBytes(cut);
        fs::rename(cut, cutRecording);
        wrong += sealTake(cutRecording).ok && readBytes(cut) == once ? 0 : 1;
        fs::remove(cut);
    }
    std::printf("  the file cut at each of its %zu lengths: %d wrong (expected 0)\n", whole.size() + 1, wrong);
    DAWLAB_CHECK(wrong == 0);
}

// Files that are not takes are not touched.
void checkNotOurs() {
    TempFolder folder;
    const WavHeader good = makeHeader(stereoInfo());
    const auto rejected = [&](std::size_t at, unsigned char value) {
        Bytes bytes(good.begin(), good.end());
        bytes.resize(bytes.size() + 16, 'x');
        bytes[at] = static_cast<char>(value);
        const fs::path recording = recordingOf(folder.file("other.wav"));
        writeBytes(recording, bytes, bytes.size());
        const bool isRejected = !sealTake(recording).ok && readBytes(recording) == bytes && !fs::exists(folder.file("other.wav"));
        fs::remove(recording);
        fs::remove(folder.file("other.wav"));
        return isRejected;
    };
    DAWLAB_CHECK(rejected(0, 'X'));  // not RIFF
    DAWLAB_CHECK(rejected(36, 'X')); // a WAV without our chunk
    DAWLAB_CHECK(rejected(68, 'X')); // no samples there
    DAWLAB_CHECK(rejected(20, 1));   // integer samples
    DAWLAB_CHECK(rejected(22, 0));   // no channels: must not divide by zero
    DAWLAB_CHECK(rejected(32, 7));   // a frame size that does not fit

    // More than a WAV can say.
    const fs::path recording = recordingOf(folder.file("long.wav"));
    {
        TakeFile file;
        DAWLAB_CHECK(file.open(folder.file("long.wav"), stereoInfo()));
        writeFrames(file, 0, 10, 2);
    }
    DAWLAB_CHECK(!sealTake(recording, 79).ok);
    DAWLAB_CHECK(fs::exists(recording));
    DAWLAB_CHECK(sealTake(recording, 80).ok);

    DAWLAB_CHECK(!sealTake(folder.file("missing.wav.recording")).ok);
}

void checkFindUnfinished() {
    TempFolder folder;
    {
        TakeFile crashed, finished;
        DAWLAB_CHECK(crashed.open(folder.file("b.wav"), stereoInfo()));
        DAWLAB_CHECK(finished.open(folder.file("a.wav"), stereoInfo()));
        DAWLAB_CHECK(finished.finish().ok);
    }
    const auto found = findUnfinishedTakes(folder.path());
    DAWLAB_CHECK(found.size() == 1);
    DAWLAB_CHECK(!found.empty() && found[0].filename() == "b.wav.recording");
    DAWLAB_CHECK(findUnfinishedTakes(folder.file("no such folder")).empty());
}

// The disk fills up at any byte: the take ends there, and it is still a good file.
void checkFullDisk() {
    TempFolder folder;
    int wrong = 0, cases = 0;
    const std::uint64_t kFrames = 1000;
    for (std::uint64_t space = 0; space < kHeaderBytes + kFrames * 8; space = space < 90 ? space + 1 : space + 397) {
        ++cases;
        const fs::path path = folder.file("full" + std::to_string(cases) + ".wav");
        SmallDisk disk;
        disk.left = space;
        TakeFile file(disk.ops());
        const bool opened = file.open(path, stereoInfo());
        if (space < kHeaderBytes) {
            // No room for a header: an error, and no file with half of one.
            wrong += !opened && file.problem() == TakeProblem::CannotOpen && !fs::exists(recordingOf(path)) ? 0 : 1;
            writeFrames(file, 0, 10, 2);
            wrong += !file.finish().ok && !fs::exists(path) ? 0 : 1;
            continue;
        }
        wrong += opened ? 0 : 1;
        writeFrames(file, 0, static_cast<int>(kFrames), 2, 64);
        wrong += file.problem() == TakeProblem::WriteFailed ? 0 : 1;
        wrong += disk.writesAfterFull == 0 ? 0 : 1; // once it failed, it leaves the disk alone

        const SealedTake sealed = file.finish();
        const auto frames = static_cast<std::int64_t>((space - kHeaderBytes) / 8);
        wrong += sealed.ok && static_cast<std::int64_t>(sealed.numFrames) == frames ? 0 : 1;
        wrong += holdsSignal(path, 2, frames) ? 0 : 1;
        wrong += file.problem() == TakeProblem::WriteFailed ? 0 : 1; // it was cut short: it still says so
    }
    std::printf("  the disk full at %d different bytes: %d wrong (expected 0)\n", cases, wrong);
    DAWLAB_CHECK(wrong == 0);
}

void checkTooLong() {
    TempFolder folder;
    const fs::path path = folder.file("take.wav");
    DiskOps ops;
    ops.maxDataBytes = 803; // 100 frames of stereo, and three bytes that hold no frame
    TakeFile file(ops);
    DAWLAB_CHECK(file.open(path, stereoInfo()));
    writeFrames(file, 0, 96, 2, 64);
    DAWLAB_CHECK(file.problem() == TakeProblem::None);
    writeFrames(file, 96, 64, 2, 64);
    DAWLAB_CHECK(file.problem() == TakeProblem::TooLong);
    const SealedTake sealed = file.finish();
    DAWLAB_CHECK(sealed.ok);
    DAWLAB_CHECK(sealed.numFrames == 100);
    DAWLAB_CHECK(holdsSignal(path, 2, 100));

    // Exactly full is not too long yet.
    DiskOps exact;
    exact.maxDataBytes = 800;
    TakeFile exactly(exact);
    DAWLAB_CHECK(exactly.open(folder.file("exact.wav"), stereoInfo()));
    writeFrames(exactly, 0, 100, 2, 100);
    DAWLAB_CHECK(exactly.problem() == TakeProblem::None);
    DAWLAB_CHECK(exactly.finish().ok);
}

// About once a second of audio to the disk, and a failure there counts.
void checkSync() {
    TempFolder folder;
    int syncs = 0;
    DiskOps counting;
    counting.sync = [&syncs](std::FILE*) {
        ++syncs;
        return true;
    };
    TakeInfo info = stereoInfo();
    info.sampleRate = 1000;
    TakeFile file(counting);
    DAWLAB_CHECK(file.open(folder.file("sync.wav"), info));
    writeFrames(file, 0, 999, 2, 100);
    DAWLAB_CHECK(syncs == 0);
    writeFrames(file, 999, 1, 2, 100);
    DAWLAB_CHECK(syncs == 1);
    writeFrames(file, 1000, 2500, 2, 100);
    DAWLAB_CHECK(syncs == 3);
    DAWLAB_CHECK(file.problem() == TakeProblem::None);
    DAWLAB_CHECK(file.finish().ok);

    DiskOps failing;
    failing.sync = [](std::FILE*) { return false; };
    TakeFile failed(failing);
    DAWLAB_CHECK(failed.open(folder.file("failed.wav"), info));
    writeFrames(failed, 0, 1000, 2, 100);
    DAWLAB_CHECK(failed.problem() == TakeProblem::WriteFailed);
    const SealedTake sealed = failed.finish();
    DAWLAB_CHECK(sealed.ok);
    DAWLAB_CHECK(sealed.numFrames == 1000);
}

// Other takes are never overwritten, finished or not.
void checkNeverOverwrites() {
    TempFolder folder;
    const Bytes precious{'t', 'a', 'k', 'e'};

    // A finished take has this name already.
    const fs::path finished = folder.file("finished.wav");
    writeBytes(finished, precious, precious.size());
    TakeFile second;
    DAWLAB_CHECK(!second.open(finished, stereoInfo()));
    DAWLAB_CHECK(second.problem() == TakeProblem::CannotOpen);
    DAWLAB_CHECK(readBytes(finished) == precious);
    DAWLAB_CHECK(!fs::exists(recordingOf(finished)));

    // A crashed take, not recovered yet, has this name.
    const fs::path crashed = folder.file("crashed.wav");
    writeBytes(recordingOf(crashed), precious, precious.size());
    TakeFile third;
    DAWLAB_CHECK(!third.open(crashed, stereoInfo()));
    DAWLAB_CHECK(third.problem() == TakeProblem::CannotOpen);
    DAWLAB_CHECK(readBytes(recordingOf(crashed)) == precious);

    // The name is taken while recording: the samples stay safe in the `.recording` file.
    const fs::path late = folder.file("late.wav");
    TakeFile fourth;
    DAWLAB_CHECK(fourth.open(late, stereoInfo()));
    writeFrames(fourth, 0, 100, 2);
    writeBytes(late, precious, precious.size());
    DAWLAB_CHECK(!fourth.finish().ok);
    DAWLAB_CHECK(fourth.problem() == TakeProblem::CannotFinish);
    DAWLAB_CHECK(readBytes(late) == precious);
    DAWLAB_CHECK(fs::exists(recordingOf(late)));
    fs::remove(late);
    const SealedTake sealed = sealTake(recordingOf(late));
    DAWLAB_CHECK(sealed.ok);
    DAWLAB_CHECK(holdsSignal(late, 2, 100));

    // No folder.
    TakeFile fifth;
    DAWLAB_CHECK(!fifth.open(folder.file("no such folder") / "take.wav", stereoInfo()));
    DAWLAB_CHECK(fifth.problem() == TakeProblem::CannotOpen);
}

constexpr int kEvery = 1000;
constexpr Latencies kDevice{240, 480, 0, 11};
constexpr int kMix = 752;
constexpr std::int64_t kTotal = 240 + 480 + 752 + 11;

// The clicks of a loop-back: what was rendered at a multiple of kEvery comes back kTotal later.
void fillClicks(float* input, std::int64_t position, int n) {
    for (int i = 0; i < n; ++i) {
        const std::int64_t rendered = position + i - kTotal;
        input[i] = rendered >= 0 && rendered % kEvery == 0 ? 1.0f : 0.0f;
    }
}

// Reads a sealed take, and counts the clicks and those that are not where they belong.
void countClicks(const fs::path& path, const SealedTake& sealed, int& clicks, int& misplaced) {
    dawlab::WavData wav;
    clicks = misplaced = 0;
    if (!dawlab::readWav(path.string(), wav) || wav.buffer.getNumChannels() < 1)
        return;
    const Placement p = place(sealed.info.stampedPosition, sealed.info.latencies, wav.buffer.getNumSamples());
    for (std::int64_t i = 0; i < p.numFrames; ++i) {
        if (wav.buffer.getSample(0, static_cast<int>(p.skipFrames + i)) != 0.0f) {
            ++clicks;
            misplaced += (p.timelineStart + i) % kEvery == 0 ? 0 : 1;
        }
    }
}

// All of it on one thread: audio block, disk thread's turn, audio block, ...
void checkFromRingToFile() {
    TempFolder folder;
    constexpr int kBlock = 256;
    float input[kBlock];
    const float* inputs[] = {input};

    // A take that never began leaves no file.
    {
        Recorder recorder;
        DAWLAB_CHECK(recorder.start(kDevice, 1, 1 << 14));
        TakeWriter writer(recorder, folder.file("never.wav"), 48000);
        fillClicks(input, 0, kBlock);
        recorder.process(inputs, 1, kBlock, 0, false, kMix);
        DAWLAB_CHECK(!writer.pump());
        recorder.stop();
        recorder.process(inputs, 1, kBlock, 0, false, kMix);
        DAWLAB_CHECK(writer.pump());
        DAWLAB_CHECK(!writer.result().ok);
        DAWLAB_CHECK(writer.problem() == TakeProblem::None);
        DAWLAB_CHECK(!fs::exists(folder.file("never.wav")));
        DAWLAB_CHECK(!fs::exists(recordingOf(folder.file("never.wav"))));
    }

    // A whole take. The file exists as soon as there is a frame, with the stamp in it.
    {
        const fs::path path = folder.file("take.wav");
        Recorder recorder;
        DAWLAB_CHECK(recorder.start(kDevice, 1, 1 << 14));
        TakeWriter writer(recorder, path, 44100);
        std::int64_t position = 96000;
        const std::int64_t begin = position;
        for (int block = 0; block < 200; ++block, position += kBlock) {
            fillClicks(input, position, kBlock);
            recorder.process(inputs, 1, kBlock, position, true, kMix);
            DAWLAB_CHECK(!writer.pump());
        }
        DAWLAB_CHECK(fs::exists(recordingOf(path)));
        recorder.process(inputs, 1, kBlock, position, false, kMix); // the transport stops
        DAWLAB_CHECK(writer.pump());
        DAWLAB_CHECK(writer.pump()); // and stays done

        const SealedTake& sealed = writer.result();
        DAWLAB_CHECK(sealed.ok);
        DAWLAB_CHECK(sealed.numFrames == 200 * kBlock);
        DAWLAB_CHECK(sealed.info.sampleRate == 44100);
        DAWLAB_CHECK(sealed.info.stampedPosition == begin);
        DAWLAB_CHECK(sealed.info.latencies.total() == kTotal);
        DAWLAB_CHECK(recorder.endPosition() == position);
        int clicks = 0, misplaced = 0;
        countClicks(path, sealed, clicks, misplaced);
        DAWLAB_CHECK(clicks >= 51);
        DAWLAB_CHECK(misplaced == 0);
    }

    // The disk thread stalls: frames are dropped, and the clicks after that are still in place.
    {
        const fs::path path = folder.file("stall.wav");
        Recorder recorder;
        DAWLAB_CHECK(recorder.start(kDevice, 1, 1024));
        TakeWriter writer(recorder, path, 48000);
        std::int64_t position = 5000;
        for (int block = 0; block < 300; ++block, position += kBlock) {
            fillClicks(input, position, kBlock);
            recorder.process(inputs, 1, kBlock, position, true, kMix);
            if (block < 100 || block > 150)
                DAWLAB_CHECK(!writer.pump());
        }
        recorder.process(inputs, 1, kBlock, position, false, kMix);
        DAWLAB_CHECK(writer.pump());
        const SealedTake& sealed = writer.result();
        DAWLAB_CHECK(recorder.ring().droppedFrames() > 0);
        DAWLAB_CHECK(sealed.ok);
        DAWLAB_CHECK(static_cast<std::int64_t>(sealed.numFrames) == position - 5000);
        int clicks = 0, misplaced = 0;
        countClicks(path, sealed, clicks, misplaced);
        DAWLAB_CHECK(clicks > 40);
        DAWLAB_CHECK(misplaced == 0);
    }

    // No way to write: the recording goes on, frames go nowhere, and the UI can see it.
    {
        Recorder recorder;
        DAWLAB_CHECK(recorder.start(kDevice, 1, 1 << 14));
        TakeWriter writer(recorder, folder.file("no such folder") / "take.wav", 48000);
        std::int64_t position = 0;
        for (int block = 0; block < 100; ++block, position += kBlock) {
            fillClicks(input, position, kBlock);
            recorder.process(inputs, 1, kBlock, position, true, kMix);
            DAWLAB_CHECK(!writer.pump());
            // The folder appears too late: a file without the start of the take would be worse than none.
            if (block == 50)
                fs::create_directories(folder.file("no such folder"));
        }
        DAWLAB_CHECK(recorder.ring().droppedFrames() == 0); // the ring is not left to overflow
        DAWLAB_CHECK(writer.problem() == TakeProblem::CannotOpen);
        recorder.stop();
        recorder.process(inputs, 1, kBlock, position, true, kMix);
        DAWLAB_CHECK(writer.pump());
        DAWLAB_CHECK(!writer.result().ok);
        DAWLAB_CHECK(!fs::exists(folder.file("no such folder") / "take.wav"));
        DAWLAB_CHECK(!fs::exists(recordingOf(folder.file("no such folder") / "take.wav")));
    }
}

// The whole story: a take is being recorded and the program dies. The next run finds it, and the
// clicks land where they were played.
void checkRecoveredTakeLands() {
    TempFolder folder;
    constexpr int kBlock = 480;
    float input[kBlock];
    const float* inputs[] = {input};
    const fs::path path = folder.file("take.wav");
    {
        Recorder recorder;
        DAWLAB_CHECK(recorder.start(kDevice, 1, 1 << 14));
        TakeWriter writer(recorder, path, 48000);
        std::int64_t position = 1000000;
        for (int block = 0; block < 100; ++block, position += kBlock) {
            fillClicks(input, position, kBlock);
            recorder.process(inputs, 1, kBlock, position, true, kMix);
            DAWLAB_CHECK(!writer.pump());
        }
    } // never sealed

    const auto found = findUnfinishedTakes(folder.path());
    DAWLAB_CHECK(found.size() == 1);
    if (found.size() != 1)
        return;
    const SealedTake sealed = sealTake(found[0]);
    DAWLAB_CHECK(sealed.ok);
    DAWLAB_CHECK(sealed.numFrames == 100 * kBlock);
    int clicks = 0, misplaced = 0;
    countClicks(path, sealed, clicks, misplaced);
    std::printf("  a crash during a take: %llu frames recovered, %d clicks, %d in the wrong place (expected 0)\n",
                static_cast<ull>(sealed.numFrames), clicks, misplaced);
    DAWLAB_CHECK(clicks >= 48);
    DAWLAB_CHECK(misplaced == 0);
}

// A real audio thread, a real disk thread, and the UI (this thread).
//
// Every frame of the signal says which position it came in at, so a take checks itself: frame i is
// either that value for stamp + i, or silence where the ring dropped something. Three ways to end a
// take: stop(), a seek, and a full disk that the UI reacts to. Some takes have a disk that keeps
// stalling, and those are the ones that drop frames.
//
// The test's own counters are relaxed: they must not synchronize anything for the code under test.
void checkThreads() {
    TempFolder folder;
    constexpr int kBlock = 128;
    constexpr int kRing = 1 << 15;
    constexpr std::int64_t kCycle = 1 << 23;
    const auto valueAt = [](std::int64_t position) { return static_cast<float>(position % kCycle + 1); };

    Recorder recorder;
    std::atomic<bool> quit{false}, seek{false};
    std::atomic<std::int64_t> transport{0}, allowed{0};
    std::thread audio([&] {
        float input[kBlock];
        const float* inputs[] = {input};
        std::int64_t position = 0;
        while (!quit.load(std::memory_order_relaxed)) {
            if (position >= allowed.load(std::memory_order_relaxed)) {
                std::this_thread::yield();
                continue;
            }
            if (seek.exchange(false, std::memory_order_relaxed))
                position += 7777;
            for (int i = 0; i < kBlock; ++i)
                input[i] = valueAt(position + i);
            recorder.process(inputs, 1, kBlock, position, true, 0);
            position += kBlock;
            transport.store(position, std::memory_order_relaxed);
        }
    });

    const auto begin = std::chrono::steady_clock::now();
    const auto running = [&] { return std::chrono::steady_clock::now() - begin < std::chrono::seconds(40); };

    int takes = 0, empty = 0, fullDisks = 0, withDrops = 0;
    ull framesOnDisk = 0, wrong = 0;
    for (int round = 0; round < 150 && running(); ++round) {
        enum Kind { Stop, Seek, FullDisk, Stalling, StopAtOnce };
        const auto kind = static_cast<Kind>(round % 5);

        std::atomic<std::uint64_t> left{kind == FullDisk ? 2000u + static_cast<std::uint64_t>(round) * 13u : ~std::uint64_t{0}};
        DiskOps ops;
        ops.write = [&left, kind](const void* bytes, std::size_t numBytes, std::FILE* file) {
            if (kind == Stalling)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            const std::uint64_t space = left.load(std::memory_order_relaxed);
            const auto n = static_cast<std::size_t>(std::min<std::uint64_t>(space, numBytes));
            const std::size_t written = std::fwrite(bytes, 1, n, file);
            left.store(space - written, std::memory_order_relaxed);
            return written;
        };

        const fs::path path = folder.file("t" + std::to_string(round) + ".wav");
        if (!recorder.start(Latencies{10, 20, 0, 0}, 1, kRing)) {
            ++wrong;
            break;
        }
        TakeWriter writer(recorder, path, 48000, std::move(ops));

        // A real audio thread gets ahead only so far. A stalling disk has to fall behind.
        const std::int64_t ahead = kind == Stalling ? kRing * 3 : 1 << 12;
        std::atomic<bool> diskDone{false};
        std::thread disk([&] {
            while (!writer.pump()) {
                allowed.store(transport.load(std::memory_order_relaxed) + ahead, std::memory_order_relaxed);
                std::this_thread::yield();
            }
            diskDone.store(true, std::memory_order_relaxed);
        });

        const std::int64_t until = transport.load(std::memory_order_relaxed) + (kind == Stalling ? kRing * 6 : 3000 + round * 17);
        bool asked = kind == StopAtOnce;
        if (asked)
            recorder.stop();
        while (!diskDone.load(std::memory_order_relaxed) && running()) {
            allowed.store(transport.load(std::memory_order_relaxed) + ahead, std::memory_order_relaxed);
            const bool time = transport.load(std::memory_order_relaxed) >= until;
            const bool diskFull = writer.problem() != TakeProblem::None;
            if (!asked && (diskFull || (kind != FullDisk && time))) {
                asked = true;
                if (kind == Seek && !diskFull)
                    seek.store(true, std::memory_order_relaxed);
                else
                    recorder.stop();
            }
            std::this_thread::yield();
        }
        if (!diskDone.load(std::memory_order_relaxed)) {
            // Out of time: the state machine must end it now, or the disk thread never comes back.
            recorder.stop();
            allowed.store(std::int64_t{1} << 62, std::memory_order_relaxed);
        }
        disk.join();

        const SealedTake& sealed = writer.result();
        if (!sealed.ok) {
            // Only a take that never began has no file.
            wrong += recorder.wasStarted() && recorder.endPosition() > recorder.stampedPosition() ? 1 : 0;
            ++empty;
            continue;
        }
        ++takes;
        if (writer.problem() == TakeProblem::WriteFailed)
            ++fullDisks;
        const bool dropped = recorder.ring().droppedFrames() > 0;
        withDrops += dropped ? 1 : 0;

        dawlab::WavData wav;
        if (!dawlab::readWav(path.string(), wav) || wav.buffer.getNumSamples() != static_cast<std::int64_t>(sealed.numFrames)) {
            ++wrong;
            continue;
        }
        wrong += sealed.info.stampedPosition == recorder.stampedPosition() ? 0 : 1;
        for (int i = 0; i < wav.buffer.getNumSamples(); ++i) {
            const float got = wav.buffer.getSample(0, i);
            const bool right = got == valueAt(sealed.info.stampedPosition + i);
            wrong += right || (dropped && got == 0.0f) ? 0 : 1;
        }
        framesOnDisk += sealed.numFrames;

        // Without a problem and without a drop, every frame reached the file, up to the last one.
        if (writer.problem() == TakeProblem::None && !dropped)
            wrong += static_cast<std::int64_t>(sealed.numFrames) == recorder.endPosition() - recorder.stampedPosition() ? 0 : 1;
        // A full disk ends the take, but it holds everything that fitted before.
        if (kind == FullDisk && writer.problem() == TakeProblem::WriteFailed)
            wrong += sealed.numFrames > 0 ? 0 : 1;
        std::error_code ec;
        fs::remove(path, ec);
    }
    quit.store(true, std::memory_order_relaxed);
    audio.join();

    std::printf("  three threads: %d takes (%d never began), %d disks full, %d with dropped frames, %llu frames, %llu wrong (expected 0)\n",
                takes, empty, fullDisks, withDrops, framesOnDisk, wrong);
    DAWLAB_CHECK(takes > 0);
    DAWLAB_CHECK(fullDisks > 0);
    DAWLAB_CHECK(withDrops > 0);
    DAWLAB_CHECK(wrong == 0);
}

} // namespace step03_check

int main() {
    using namespace step03_check;
    std::printf("Step 3: the take on disk\n");
    checkHeader();
    checkWriteAndFinish();
    if (dawlab::test::counters().failures > 0)
        return dawlab::test::summary("l17_step03"); // the rest needs a file that works
    checkAnyName();
    checkCrash();
    checkEveryLength();
    checkNotOurs();
    checkFindUnfinished();
    checkFullDisk();
    checkTooLong();
    checkSync();
    checkNeverOverwrites();
    checkFromRingToFile();
    checkRecoveredTakeLands();
    checkThreads();
    return dawlab::test::summary("l17_step03");
}
