// Step 3 (starter): the take on disk.
//
// Build and run:  l17_step03_starter   (prints [FAIL] until all six TODOs are done)
// Reference:      ../solution/step03.cpp, docs/step-03-take-on-disk.md ("Your turn")
//
// The file layout, TakeInfo, the put/get helpers, parseHeader(), openFile(), syncToDisk() and the
// Recorder of Step 2 (solved) are in ../minidaw.h.

#include "../minidaw.h"

namespace minidaw {

WavHeader makeHeader(const TakeInfo& info) noexcept {
    // TODO 1: Fill in the header as in the table in minidaw.h, with putTag(), putU16(), putU32() and
    //         putU64(). Leave both sizes (offsets 4 and 72) at 0. A negative stamp or trim goes in
    //         as is: static_cast it to the unsigned type.
    (void)info;
    return WavHeader{};
}

bool TakeFile::open(const std::filesystem::path& finalPath, const TakeInfo& info) {
    // TODO 2: recording_ is finalPath + kRecordingExtension.
    //         If finalPath exists already, or openFile(recording, "wbx") fails: problem_ = CannotOpen,
    //         return false. ("x": a `.recording` file that is there already is a take to recover.)
    //         std::setvbuf(file, nullptr, _IONBF, 0): nothing may wait in this process.
    //         Write the header with ops_.write(). If that is short: close the file, remove it (a torn
    //         header holds nothing), problem_ = CannotOpen, return false.
    //         Otherwise keep the file in file_, and info in info_. Return true.
    (void)finalPath;
    (void)info;
    return false;
}

void TakeFile::write(const float* const* channels, int n) {
    // TODO 3: Nothing to do without a file, with a problem, or for n <= 0.
    //         How many of the n frames still fit? (ops_.maxDataBytes - dataBytes_) / bytes per frame.
    //         Interleave those into interleaved_ and write them, all at once, with ops_.write().
    //         Short: problem_ = WriteFailed, return.
    //         Add to dataBytes_ and framesSinceSync_. After a second of audio (info_.sampleRate frames),
    //         reset framesSinceSync_ and call ops_.sync(); if that fails: WriteFailed, return.
    //         If not all n frames fit: problem_ = TooLong.
    (void)channels;
    (void)n;
}

SealedTake TakeFile::finish() {
    // TODO 4: Without a file: return {}. Otherwise close it (file_.reset()), and sealTake(recording_).
    //         Do not use dataBytes_: after a failed write, only the file knows how much it holds. If
    //         that was not ok and there was no problem so far: problem_ = CannotFinish.
    return {};
}

SealedTake sealTake(const std::filesystem::path& recording, std::uint64_t maxDataBytes) {
    // TODO 5: Read the first kHeaderBytes with openFile(recording, "rb"), and close it. If there are
    //         fewer of them, or parseHeader() says no: return a SealedTake that is not ok.
    //         std::filesystem::file_size() (with an error_code): the frames are the whole frames after
    //         the header. More data bytes than maxDataBytes: not ok.
    //         If the file is longer than that, std::filesystem::resize_file() cuts off the rest.
    //         Put the two sizes into the header (offset 4: kHeaderBytes - 8 + data bytes, offset 72: data
    //         bytes). openFile(recording, "r+b"), std::fwrite the header over the old one, std::fflush,
    //         syncToDisk(). Close it; on any failure: not ok.
    //         The new name is recording with replace_extension(). If it exists: not ok. Never over
    //         another take. std::filesystem::rename(). Then fill in everything, with ok = true.
    (void)recording;
    (void)maxDataBytes;
    return {};
}

bool TakeWriter::pump() {
    // TODO 6: Once done_: true.
    //         First ask recorder_.isIdle(), and remember it. Why first? See the document.
    //         Then, while recorder_.ring().pop(pointers_.data(), kChunkFrames) gives n > 0 frames:
    //           if the file is not open and there is no problem: open it with a TakeInfo: the size of
    //           scratch_, sampleRate_, and the recorder's stamp and latencies.
    //           file_.write(pointers_.data(), n).
    //         If it was Idle: result_ = file_.finish(), done_ = true.
    //         Return done_.
    return false;
}

} // namespace minidaw

#include "../check.h"
