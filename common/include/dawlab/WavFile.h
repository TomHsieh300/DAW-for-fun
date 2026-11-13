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

} // namespace detail

inline bool readWav(const std::string& path, WavData& out, std::string* error = nullptr) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        detail::setError(error, "Cannot open file: " + path);
        return false;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (bytes.size() < 12 || std::string(bytes.begin(), bytes.begin() + 4) != "RIFF" ||
        std::string(bytes.begin() + 8, bytes.begin() + 12) != "WAVE") {
        detail::setError(error, "Not a RIFF/WAVE file: " + path);
        return false;
    }

    uint16_t format = 0, numChannels = 0, bitsPerSample = 0, blockAlign = 0;
    uint32_t sampleRate = 0;
    const uint8_t* data = nullptr;
    size_t dataSize = 0;

    size_t pos = 12;
    while (pos + 8 <= bytes.size()) {
        const std::string id(bytes.begin() + std::ptrdiff_t(pos), bytes.begin() + std::ptrdiff_t(pos) + 4);
        const size_t size = detail::readU32(&bytes[pos + 4]);
        const size_t body = pos + 8;
        const size_t available = std::min(size, bytes.size() - body);
        if (id == "data" && available < size) { // a file from outside: its samples are not all there
            detail::setError(error, "The data chunk is cut short (" + std::to_string(size) + " bytes said, " +
                                        std::to_string(available) + " there): " + path);
            return false;
        }
        if (id == "fmt " && available >= 16) {
            format = detail::readU16(&bytes[body]);
            numChannels = detail::readU16(&bytes[body + 2]);
            sampleRate = detail::readU32(&bytes[body + 4]);
            blockAlign = detail::readU16(&bytes[body + 12]);
            bitsPerSample = detail::readU16(&bytes[body + 14]);
            if (format == 0xFFFE && available >= 26) // WAVE_FORMAT_EXTENSIBLE: the real format is in the first two bytes of the SubFormat GUID
                format = detail::readU16(&bytes[body + 24]);
        } else if (id == "data") {
            data = bytes.data() + body; // for an empty data chunk body == size(), so bytes[body] must not be used
            dataSize = available;
        }
        pos = body + size + (size & 1); // chunks are padded to an even length
    }

    if (format == 0 || data == nullptr) {
        detail::setError(error, "fmt or data chunk not found: " + path);
        return false;
    }
    if (numChannels == 0 || blockAlign == 0 || blockAlign < numChannels * (bitsPerSample / 8) || !detail::isWavSampleRate(sampleRate)) {
        detail::setError(error, "Invalid fmt chunk contents: " + path);
        return false;
    }
    const bool isFloat = (format == 3);
    if (!(format == 1 || isFloat) || (isFloat && bitsPerSample != 32 && bitsPerSample != 64) ||
        (!isFloat && bitsPerSample != 8 && bitsPerSample != 16 && bitsPerSample != 24 && bitsPerSample != 32)) {
        detail::setError(error, "Unsupported WAV format (format=" + std::to_string(format) +
                                    ", bits=" + std::to_string(bitsPerSample) + ")");
        return false;
    }

    const int numFrames = int(dataSize / blockAlign);
    const int bytesPerSample = bitsPerSample / 8;
    out.buffer.setSize(numChannels, numFrames);
    out.sampleRate = sampleRate;
    out.bitDepth = bitsPerSample;

    for (int i = 0; i < numFrames; ++i) {
        for (int ch = 0; ch < numChannels; ++ch) {
            const uint8_t* p = data + size_t(i) * blockAlign + size_t(ch) * size_t(bytesPerSample);
            float v = 0.0f;
            if (isFloat && bitsPerSample == 32) {
                uint32_t u = detail::readU32(p);
                float f;
                std::memcpy(&f, &u, 4);
                v = f;
            } else if (isFloat) {
                uint64_t u = uint64_t(detail::readU32(p)) | (uint64_t(detail::readU32(p + 4)) << 32);
                double d;
                std::memcpy(&d, &u, 8);
                v = float(d);
            } else if (bitsPerSample == 8) {
                v = (float(p[0]) - 128.0f) / 128.0f; // 8-bit WAV is unsigned
            } else if (bitsPerSample == 16) {
                v = float(int16_t(detail::readU16(p))) / 32768.0f;
            } else if (bitsPerSample == 24) {
                int32_t s = int32_t(uint32_t(p[0]) << 8 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 24) >> 8;
                v = float(s) / 8388608.0f;
            } else {
                v = float(double(int32_t(detail::readU32(p))) / 2147483648.0);
            }
            out.buffer.setSample(ch, i, v);
        }
    }
    return true;
}

// bitDepth: 16, 24 (integer PCM) or 32 (IEEE float)
inline bool writeWav(const std::string& path, const AudioBuffer& buffer, double sampleRate, int bitDepth = 24,
                     std::string* error = nullptr) {
    if (bitDepth != 16 && bitDepth != 24 && bitDepth != 32) {
        detail::setError(error, "bitDepth must be 16, 24 or 32");
        return false;
    }
    const int numChannels = buffer.getNumChannels();
    const int numFrames = buffer.getNumSamples();
    const int bytesPerSample = bitDepth / 8;
    const uint64_t dataBytes = detail::wavDataBytes(uint64_t(numFrames), uint64_t(numChannels), uint64_t(bytesPerSample));
    // Checked before the file is opened: a refused WAV leaves nothing behind, not a truncated file.
    const uint64_t rate = detail::isWavSampleRate(sampleRate) ? uint64_t(std::llround(sampleRate)) : 0;
    const uint64_t byteRate = rate * uint64_t(numChannels) * uint64_t(bytesPerSample);
    if (rate == 0 || byteRate > 0xFFFFFFFFull) {
        detail::setError(error, "A WAV file cannot hold a sample rate of " + std::to_string(sampleRate) + " Hz: " + path);
        return false;
    }
    if (!detail::fitsWav(dataBytes)) {
        detail::setError(error, "Too large for a WAV file (4 GiB): " + path);
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

    const auto dataSize = uint32_t(dataBytes);
    f.write("RIFF", 4);
    detail::writeU32(f, 36 + dataSize + (dataSize & 1));
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    detail::writeU32(f, 16);
    detail::writeU16(f, bitDepth == 32 ? 3 : 1);
    detail::writeU16(f, uint16_t(numChannels));
    detail::writeU32(f, uint32_t(rate));
    detail::writeU32(f, uint32_t(byteRate));
    detail::writeU16(f, uint16_t(numChannels * bytesPerSample));
    detail::writeU16(f, uint16_t(bitDepth));
    f.write("data", 4);
    detail::writeU32(f, dataSize);

    std::vector<char> frame(size_t(numChannels) * size_t(bytesPerSample));
    for (int i = 0; i < numFrames; ++i) {
        for (int ch = 0; ch < numChannels; ++ch) {
            const float v = buffer.getSample(ch, i);
            char* p = frame.data() + size_t(ch) * size_t(bytesPerSample);
            if (bitDepth == 32) {
                std::memcpy(p, &v, 4);
            } else {
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
        }
        f.write(frame.data(), std::streamsize(frame.size()));
    }
    if (dataSize & 1)
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
