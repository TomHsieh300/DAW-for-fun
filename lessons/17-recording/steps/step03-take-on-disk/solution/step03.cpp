// Step 3 (solution): the take on disk.
//
// Build and run:  l17_step03_solution   (ctest, label "solution")
// Your version:   starter/step03.cpp -> l17_step03_starter

#include "../minidaw.h"

namespace minidaw {

WavHeader makeHeader(const TakeInfo& info) noexcept {
    const auto bytesPerFrame = static_cast<std::uint16_t>(info.numChannels * 4);
    const auto sampleRate = static_cast<std::uint32_t>(info.sampleRate);
    WavHeader header{};
    unsigned char* h = header.data();
    putTag(h, "RIFF");
    putTag(h + 8, "WAVE");
    putTag(h + 12, "fmt ");
    putU32(h + 16, 16);
    putU16(h + 20, 3);
    putU16(h + 22, static_cast<std::uint16_t>(info.numChannels));
    putU32(h + 24, sampleRate);
    putU32(h + 28, sampleRate * bytesPerFrame);
    putU16(h + 32, bytesPerFrame);
    putU16(h + 34, 32);
    putTag(h + 36, "dlab");
    putU32(h + 40, 24);
    putU64(h + 44, static_cast<std::uint64_t>(info.stampedPosition));
    putU32(h + 52, static_cast<std::uint32_t>(info.latencies.input));
    putU32(h + 56, static_cast<std::uint32_t>(info.latencies.output));
    putU32(h + 60, static_cast<std::uint32_t>(info.latencies.mix));
    putU32(h + 64, static_cast<std::uint32_t>(info.latencies.trim));
    putTag(h + 68, "data");
    return header; // both sizes are still 0
}

bool TakeFile::open(const std::filesystem::path& finalPath, const TakeInfo& info) {
    std::filesystem::path recording = finalPath;
    recording += kRecordingExtension;
    std::error_code ec;
    if (std::filesystem::exists(finalPath, ec)) {
        problem_.store(TakeProblem::CannotOpen, std::memory_order_relaxed);
        return false;
    }
    // "x": a `.recording` file that is already there is a take that has not been recovered yet.
    FilePtr file = openFile(recording, "wbx");
    if (!file) {
        problem_.store(TakeProblem::CannotOpen, std::memory_order_relaxed);
        return false;
    }
    std::setvbuf(file.get(), nullptr, _IONBF, 0); // nothing waits in this process

    const WavHeader header = makeHeader(info);
    if (ops_.write(header.data(), header.size(), file.get()) != header.size()) {
        file.reset();
        std::filesystem::remove(recording, ec); // a torn header holds nothing worth keeping
        problem_.store(TakeProblem::CannotOpen, std::memory_order_relaxed);
        return false;
    }
    file_ = std::move(file);
    recording_ = std::move(recording);
    info_ = info;
    return true;
}

void TakeFile::write(const float* const* channels, int n) {
    if (!file_ || problem() != TakeProblem::None || n <= 0)
        return;
    const auto numChannels = static_cast<std::size_t>(info_.numChannels);
    const std::uint64_t bytesPerFrame = numChannels * sizeof(float);

    const std::uint64_t room = (ops_.maxDataBytes - dataBytes_) / bytesPerFrame;
    const auto frames = static_cast<std::size_t>(std::min(room, static_cast<std::uint64_t>(n)));

    interleaved_.resize(frames * numChannels);
    for (std::size_t ch = 0; ch < numChannels; ++ch)
        for (std::size_t i = 0; i < frames; ++i)
            interleaved_[i * numChannels + ch] = channels[ch][i];

    const std::size_t numBytes = interleaved_.size() * sizeof(float);
    if (ops_.write(interleaved_.data(), numBytes, file_.get()) != numBytes) {
        problem_.store(TakeProblem::WriteFailed, std::memory_order_relaxed);
        return;
    }
    dataBytes_ += numBytes;

    framesSinceSync_ += static_cast<int>(frames);
    if (framesSinceSync_ >= info_.sampleRate) {
        framesSinceSync_ = 0;
        if (!ops_.sync(file_.get())) {
            problem_.store(TakeProblem::WriteFailed, std::memory_order_relaxed);
            return;
        }
    }
    if (frames < static_cast<std::size_t>(n))
        problem_.store(TakeProblem::TooLong, std::memory_order_relaxed);
}

SealedTake TakeFile::finish() {
    if (!file_)
        return {};
    file_.reset();
    // dataBytes_ does not count here: after a failed write, only the file knows how much it holds.
    SealedTake sealed = sealTake(recording_);
    if (!sealed.ok && problem() == TakeProblem::None)
        problem_.store(TakeProblem::CannotFinish, std::memory_order_relaxed);
    return sealed;
}

SealedTake sealTake(const std::filesystem::path& recording, std::uint64_t maxDataBytes) {
    namespace fs = std::filesystem;
    SealedTake sealed;
    TakeInfo info;
    WavHeader header{};
    {
        const FilePtr file = openFile(recording, "rb");
        if (!file || std::fread(header.data(), 1, header.size(), file.get()) != header.size() ||
            !parseHeader(header, info))
            return sealed;
    }

    std::error_code ec;
    const std::uint64_t fileBytes = fs::file_size(recording, ec);
    if (ec)
        return sealed;
    // The real length is the truth. A crash or a full disk can leave part of a frame at the end.
    const std::uint64_t bytesPerFrame = static_cast<std::uint64_t>(info.numChannels) * sizeof(float);
    const std::uint64_t numFrames = (fileBytes - kHeaderBytes) / bytesPerFrame;
    const std::uint64_t dataBytes = numFrames * bytesPerFrame;
    if (dataBytes > maxDataBytes)
        return sealed;
    if (kHeaderBytes + dataBytes != fileBytes) {
        fs::resize_file(recording, kHeaderBytes + dataBytes, ec);
        if (ec)
            return sealed;
    }

    putU32(header.data() + 4, static_cast<std::uint32_t>(kHeaderBytes - 8 + dataBytes));
    putU32(header.data() + 72, static_cast<std::uint32_t>(dataBytes));
    {
        const FilePtr file = openFile(recording, "r+b");
        if (!file || std::fwrite(header.data(), 1, header.size(), file.get()) != header.size() ||
            std::fflush(file.get()) != 0 || !syncToDisk(file.get()))
            return sealed;
    }

    fs::path finalPath = recording;
    finalPath.replace_extension();
    if (fs::exists(finalPath, ec)) // never over another take
        return sealed;
    fs::rename(recording, finalPath, ec);
    if (ec)
        return sealed;

    sealed.ok = true;
    sealed.path = std::move(finalPath);
    sealed.info = info;
    sealed.numFrames = numFrames;
    return sealed;
}

bool TakeWriter::pump() {
    if (done_)
        return true;
    // Before the pops: if it is Idle now, all the take's frames are in the ring by now.
    const bool over = recorder_.isIdle();
    while (const int n = recorder_.ring().pop(pointers_.data(), kChunkFrames)) {
        // No frame, no file: a take that never began leaves nothing behind. And the first frame is
        // what makes the stamp safe to read.
        if (!file_.isOpen() && file_.problem() == TakeProblem::None) {
            TakeInfo info;
            info.numChannels = static_cast<int>(scratch_.size());
            info.sampleRate = sampleRate_;
            info.stampedPosition = recorder_.stampedPosition();
            info.latencies = recorder_.latencies();
            file_.open(finalPath_, info);
        }
        file_.write(pointers_.data(), n);
    }
    if (over) {
        result_ = file_.finish();
        done_ = true;
    }
    return done_;
}

} // namespace minidaw

#include "../check.h"
