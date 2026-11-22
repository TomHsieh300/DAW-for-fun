#pragma once
// Minimal WAV reading and writing (header-only, no external libraries).
//
// Reading supports: PCM 8/16/24/32-bit integer, 32/64-bit float, WAVE_FORMAT_EXTENSIBLE.
// Writing supports: 16/24-bit PCM integer, 32-bit float.
// Integer formats clip values beyond ±1.0 when written; use 32-bit float if you want to keep the overs.
//
// A file is outside data: readWav refuses a sample rate of 0 and a data chunk that says more bytes than the file
// holds. writeWav refuses a sample rate that a WAV cannot hold (below 1 Hz, NaN) and more than 4 GiB of samples,
// which its 32-bit sizes cannot say, before it opens the file.
//
// Usage:
//   dawlab::WavData wav;
//   if (!dawlab::readWav("in.wav", wav)) { ... }
//   dawlab::writeWav(dawlab::outputPath("out.wav"), wav.buffer, wav.sampleRate);

#include "Console.h"
#include "AudioBuffer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace dawlab {

struct WavData {
    AudioBuffer buffer;
    double sampleRate = 48000.0;
    int bitDepth = 0; // bit depth of the original file (for information only)
};

namespace detail {

inline uint32_t readU32(const uint8_t* p) noexcept {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

inline uint16_t readU16(const uint8_t* p) noexcept { return uint16_t(p[0] | (p[1] << 8)); }

inline void writeU32(std::ofstream& f, uint32_t v) {
    const char b[4] = {char(v & 0xff), char((v >> 8) & 0xff), char((v >> 16) & 0xff), char((v >> 24) & 0xff)};
    f.write(b, 4);
}

inline void writeU16(std::ofstream& f, uint16_t v) {
    const char b[2] = {char(v & 0xff), char((v >> 8) & 0xff)};
    f.write(b, 2);
}

inline void setError(std::string* error, const std::string& message) {
    if (error)
        *error = message;
}

// What a WAV can say, for reading and for writing. Its fmt chunk holds the sample rate in 32 bits, and 0 Hz (or
// NaN) is no rate: a file with one is damaged, and a rate that cannot be written is refused.
inline bool isWavSampleRate(double rate) noexcept { return std::isfinite(rate) && rate >= 1.0 && rate <= 4294967295.0; }

// The bytes of sample data of `numFrames` frames, counted in 64 bits so that no size wraps.
inline uint64_t wavDataBytes(uint64_t numFrames, uint64_t numChannels, uint64_t bytesPerSample) noexcept {
    return numFrames * numChannels * bytesPerSample;
}

// The RIFF header says the file's size less 8 in 32 bits: 36 header bytes, the data and its pad byte. More than
// that (some 4 GiB) is not a WAV file; writeWav refuses it before it writes anything.
inline bool fitsWav(uint64_t dataBytes) noexcept { return 36 + dataBytes + (dataBytes & 1) <= 0xFFFFFFFFull; }

// ---- readWav, one step per function ----

// What readWav finds in a file's chunks: the fmt fields and where the samples are
struct WavLayout {
    uint16_t format = 0, numChannels = 0, bitsPerSample = 0, blockAlign = 0;
    uint32_t sampleRate = 0;
    const uint8_t* data = nullptr;
    size_t dataSize = 0;
};

inline bool readFileBytes(const std::string& path, std::vector<uint8_t>& bytes, std::string* error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        setError(error, "Cannot open file: " + path);
        return false;
    }
    bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

inline bool isRiffWave(const std::vector<uint8_t>& bytes) {
    return bytes.size() >= 12 && std::string(bytes.begin(), bytes.begin() + 4) == "RIFF" &&
           std::string(bytes.begin() + 8, bytes.begin() + 12) == "WAVE";
}

// A fmt chunk body of `available` (>= 16) bytes
inline void readFmtChunk(const uint8_t* body, size_t available, WavLayout& wav) noexcept {
    wav.format = readU16(body);
    wav.numChannels = readU16(body + 2);
    wav.sampleRate = readU32(body + 4);
    wav.blockAlign = readU16(body + 12);
    wav.bitsPerSample = readU16(body + 14);
    if (wav.format == 0xFFFE && available >= 26) // WAVE_FORMAT_EXTENSIBLE: the real format is in the first two bytes of the SubFormat GUID
        wav.format = readU16(body + 24);
}

// Walks the chunks after "WAVE" and keeps the last fmt and data chunks. A file from outside may be cut short: a data
// chunk whose samples are not all there is refused.
inline bool findWavChunks(const std::vector<uint8_t>& bytes, WavLayout& wav, const std::string& path, std::string* error) {
    size_t pos = 12;
    while (pos + 8 <= bytes.size()) {
        const std::string id(bytes.begin() + std::ptrdiff_t(pos), bytes.begin() + std::ptrdiff_t(pos) + 4);
        const size_t size = readU32(&bytes[pos + 4]);
        const size_t body = pos + 8;
        const size_t available = std::min(size, bytes.size() - body);
        if (id == "data" && available < size) {
            setError(error, "The data chunk is cut short (" + std::to_string(size) + " bytes said, " +
                                std::to_string(available) + " there): " + path);
            return false;
        }
        if (id == "fmt " && available >= 16) {
            readFmtChunk(&bytes[body], available, wav);
        } else if (id == "data") {
            wav.data = bytes.data() + body; // for an empty data chunk body == size(), so bytes[body] must not be used
            wav.dataSize = available;
        }
        pos = body + size + (size & 1); // chunks are padded to an even length
    }
    return true;
}

// Channels, a frame (blockAlign) large enough to hold one sample of each, and a sample rate a WAV can say
inline bool hasValidFmt(const WavLayout& wav) noexcept {
    return wav.numChannels != 0 && wav.blockAlign != 0 && wav.blockAlign >= wav.numChannels * (wav.bitsPerSample / 8) &&
           isWavSampleRate(wav.sampleRate);
}

// PCM (format 1) 8/16/24/32-bit integer or IEEE float (format 3) 32/64-bit
inline bool isSupportedWav(const WavLayout& wav) noexcept {
    const int bits = wav.bitsPerSample;
    if (wav.format == 3)
        return bits == 32 || bits == 64;
    if (wav.format == 1)
        return bits == 8 || bits == 16 || bits == 24 || bits == 32;
    return false;
}

// One sample at p as a float: IEEE float as it is, integers scaled to [-1, 1)
inline float decodeWavSample(const uint8_t* p, bool isFloat, int bitsPerSample) noexcept {
    if (isFloat && bitsPerSample == 32) {
        uint32_t u = readU32(p);
        float f;
        std::memcpy(&f, &u, 4);
        return f;
    }
    if (isFloat) {
        uint64_t u = uint64_t(readU32(p)) | (uint64_t(readU32(p + 4)) << 32);
        double d;
        std::memcpy(&d, &u, 8);
        return float(d);
    }
    if (bitsPerSample == 8)
        return (float(p[0]) - 128.0f) / 128.0f; // 8-bit WAV is unsigned
    if (bitsPerSample == 16)
        return float(int16_t(readU16(p))) / 32768.0f;
    if (bitsPerSample == 24) {
        int32_t s = int32_t(uint32_t(p[0]) << 8 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 24) >> 8;
        return float(s) / 8388608.0f;
    }
    return float(double(int32_t(readU32(p))) / 2147483648.0);
}

// The data chunk's whole frames into out (a partial frame at the end is dropped)
inline void decodeWavSamples(const WavLayout& wav, WavData& out) {
    const int numFrames = int(wav.dataSize / wav.blockAlign);
    const int bytesPerSample = wav.bitsPerSample / 8;
    const bool isFloat = (wav.format == 3);
    out.buffer.setSize(wav.numChannels, numFrames);
    out.sampleRate = wav.sampleRate;
    out.bitDepth = wav.bitsPerSample;

    for (int i = 0; i < numFrames; ++i) {
        for (int ch = 0; ch < wav.numChannels; ++ch) {
            const uint8_t* p = wav.data + size_t(i) * wav.blockAlign + size_t(ch) * size_t(bytesPerSample);
            out.buffer.setSample(ch, i, decodeWavSample(p, isFloat, wav.bitsPerSample));
        }
    }
}

// ---- writeWav, one step per function ----

// The sizes writeWav puts in the header, worked out before the file is opened
struct WavWriteSizes {
    int numChannels = 0, bytesPerSample = 0;
    uint64_t rate = 0, byteRate = 0, dataBytes = 0;
};

// Works out the sizes and returns what makes the WAV impossible ("" = nothing): checked before the file is opened,
// so a refused WAV leaves nothing behind, not a truncated file.
inline std::string wavWriteProblem(const std::string& path, const AudioBuffer& buffer, double sampleRate, int bitDepth,
                                   WavWriteSizes& sizes) {
    if (bitDepth != 16 && bitDepth != 24 && bitDepth != 32)
        return "bitDepth must be 16, 24 or 32";
    sizes.numChannels = buffer.getNumChannels();
    sizes.bytesPerSample = bitDepth / 8;
    sizes.dataBytes = wavDataBytes(uint64_t(buffer.getNumSamples()), uint64_t(sizes.numChannels),
                                   uint64_t(sizes.bytesPerSample));
    sizes.rate = isWavSampleRate(sampleRate) ? uint64_t(std::llround(sampleRate)) : 0;
    sizes.byteRate = sizes.rate * uint64_t(sizes.numChannels) * uint64_t(sizes.bytesPerSample);
    if (sizes.rate == 0 || sizes.byteRate > 0xFFFFFFFFull)
        return "A WAV file cannot hold a sample rate of " + std::to_string(sampleRate) + " Hz: " + path;
    if (!fitsWav(sizes.dataBytes))
        return "Too large for a WAV file (4 GiB): " + path;
    return {};
}

// The 44-byte header: RIFF, the fmt chunk (format 3 = IEEE float for 32 bits, else 1 = PCM) and the data chunk's size
inline void writeWavHeader(std::ofstream& f, const WavWriteSizes& sizes, int bitDepth) {
    const auto dataSize = uint32_t(sizes.dataBytes);
    f.write("RIFF", 4);
    writeU32(f, 36 + dataSize + (dataSize & 1));
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    writeU32(f, 16);
    writeU16(f, bitDepth == 32 ? 3 : 1);
    writeU16(f, uint16_t(sizes.numChannels));
    writeU32(f, uint32_t(sizes.rate));
    writeU32(f, uint32_t(sizes.byteRate));
    writeU16(f, uint16_t(sizes.numChannels * sizes.bytesPerSample));
    writeU16(f, uint16_t(bitDepth));
    f.write("data", 4);
    writeU32(f, dataSize);
}

// One sample, little-endian: 32-bit float as it is; 16 / 24-bit integer clipped to ±1 and rounded
inline void encodeWavSample(char* p, float v, int bitDepth) noexcept {
    if (bitDepth == 32) {
        std::memcpy(p, &v, 4);
        return;
    }
    const float c = std::clamp(v, -1.0f, 1.0f);
    if (bitDepth == 16) {
        const auto s = int16_t(std::lround(c * 32767.0f));
        p[0] = char(s & 0xff);
        p[1] = char((s >> 8) & 0xff);
    } else {
        const auto s = int32_t(std::lround(double(c) * 8388607.0));
        p[0] = char(s & 0xff);
        p[1] = char((s >> 8) & 0xff);
        p[2] = char((s >> 16) & 0xff);
    }
}

} // namespace detail

// Reads the whole file, finds its fmt and data chunks, checks the format, then converts the samples to float
inline bool readWav(const std::string& path, WavData& out, std::string* error = nullptr) {
    std::vector<uint8_t> bytes;
    if (!detail::readFileBytes(path, bytes, error))
        return false;
    if (!detail::isRiffWave(bytes)) {
        detail::setError(error, "Not a RIFF/WAVE file: " + path);
        return false;
    }

    detail::WavLayout wav;
    if (!detail::findWavChunks(bytes, wav, path, error))
        return false;
    if (wav.format == 0 || wav.data == nullptr) {
        detail::setError(error, "fmt or data chunk not found: " + path);
        return false;
    }
    if (!detail::hasValidFmt(wav)) {
        detail::setError(error, "Invalid fmt chunk contents: " + path);
        return false;
    }
    if (!detail::isSupportedWav(wav)) {
        detail::setError(error, "Unsupported WAV format (format=" + std::to_string(wav.format) +
                                    ", bits=" + std::to_string(wav.bitsPerSample) + ")");
        return false;
    }

    detail::decodeWavSamples(wav, out);
    return true;
}

// bitDepth: 16, 24 (integer PCM) or 32 (IEEE float)
inline bool writeWav(const std::string& path, const AudioBuffer& buffer, double sampleRate, int bitDepth = 24,
                     std::string* error = nullptr) {
    detail::WavWriteSizes sizes;
    const std::string problem = detail::wavWriteProblem(path, buffer, sampleRate, bitDepth, sizes);
    if (!problem.empty()) {
        detail::setError(error, problem);
        return false;
    }
    const auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
    }
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        detail::setError(error, "Cannot write file: " + path);
        return false;
    }

    detail::writeWavHeader(f, sizes, bitDepth);
    const size_t bytesPerSample = size_t(sizes.bytesPerSample);
    std::vector<char> frame(size_t(sizes.numChannels) * bytesPerSample);
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        for (int ch = 0; ch < sizes.numChannels; ++ch)
            detail::encodeWavSample(frame.data() + size_t(ch) * bytesPerSample, buffer.getSample(ch, i), bitDepth);
        f.write(frame.data(), std::streamsize(frame.size()));
    }
    if (sizes.dataBytes & 1)
        f.put(0);
    return bool(f);
}

// Returns "dawlab_output/<filename>" (relative to the current working directory) and makes sure the folder exists.
// When run under ctest, the working directory is the build folder.
inline std::string outputPath(const std::string& filename) {
    const std::filesystem::path dir = "dawlab_output";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return (dir / filename).string();
}

} // namespace dawlab
