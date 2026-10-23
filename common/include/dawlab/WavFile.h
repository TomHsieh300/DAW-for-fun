#pragma once
// 極簡 WAV 讀寫（header-only，不依賴任何外部函式庫）。
//
// 支援讀取：PCM 8/16/24/32-bit 整數、32/64-bit float、WAVE_FORMAT_EXTENSIBLE。
// 支援寫入：16/24-bit PCM 整數、32-bit float。
// 整數格式寫出時會把超過 ±1.0 的值截斷（clip）；想保留 overs 請用 32-bit float。
//
// 用法：
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
    int bitDepth = 0; // 原始檔案的 bit depth（僅供參考）
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

} // namespace detail

inline bool readWav(const std::string& path, WavData& out, std::string* error = nullptr) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        detail::setError(error, "無法開啟檔案: " + path);
        return false;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (bytes.size() < 12 || std::string(bytes.begin(), bytes.begin() + 4) != "RIFF" ||
        std::string(bytes.begin() + 8, bytes.begin() + 12) != "WAVE") {
        detail::setError(error, "不是 RIFF/WAVE 檔案: " + path);
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
        if (id == "fmt " && available >= 16) {
            format = detail::readU16(&bytes[body]);
            numChannels = detail::readU16(&bytes[body + 2]);
            sampleRate = detail::readU32(&bytes[body + 4]);
            blockAlign = detail::readU16(&bytes[body + 12]);
            bitsPerSample = detail::readU16(&bytes[body + 14]);
            if (format == 0xFFFE && available >= 26) // WAVE_FORMAT_EXTENSIBLE：真正格式在 SubFormat GUID 前兩個 byte
                format = detail::readU16(&bytes[body + 24]);
        } else if (id == "data") {
            data = bytes.data() + body; // 空的 data chunk 時 body == size()，不能用 bytes[body]
            dataSize = available;
        }
        pos = body + size + (size & 1); // chunk 會補齊到偶數長度
    }

    if (format == 0 || data == nullptr) {
        detail::setError(error, "找不到 fmt 或 data chunk: " + path);
        return false;
    }
    if (numChannels == 0 || blockAlign == 0 || blockAlign < numChannels * (bitsPerSample / 8)) {
        detail::setError(error, "fmt chunk 內容不合理: " + path);
        return false;
    }
    const bool isFloat = (format == 3);
    if (!(format == 1 || isFloat) || (isFloat && bitsPerSample != 32 && bitsPerSample != 64) ||
        (!isFloat && bitsPerSample != 8 && bitsPerSample != 16 && bitsPerSample != 24 && bitsPerSample != 32)) {
        detail::setError(error, "不支援的 WAV 格式（format=" + std::to_string(format) +
                                    ", bits=" + std::to_string(bitsPerSample) + "）");
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
                v = (float(p[0]) - 128.0f) / 128.0f; // 8-bit WAV 是 unsigned
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

// bitDepth: 16、24（整數 PCM）或 32（IEEE float）
inline bool writeWav(const std::string& path, const AudioBuffer& buffer, double sampleRate, int bitDepth = 24,
                     std::string* error = nullptr) {
    if (bitDepth != 16 && bitDepth != 24 && bitDepth != 32) {
        detail::setError(error, "bitDepth 只能是 16、24 或 32");
        return false;
    }
    const auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
    }
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        detail::setError(error, "無法寫入檔案: " + path);
        return false;
    }

    const int numChannels = buffer.getNumChannels();
    const int numFrames = buffer.getNumSamples();
    const int bytesPerSample = bitDepth / 8;
    const uint32_t dataSize = uint32_t(numFrames) * uint32_t(numChannels) * uint32_t(bytesPerSample);

    f.write("RIFF", 4);
    detail::writeU32(f, 36 + dataSize + (dataSize & 1));
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    detail::writeU32(f, 16);
    detail::writeU16(f, bitDepth == 32 ? 3 : 1);
    detail::writeU16(f, uint16_t(numChannels));
    detail::writeU32(f, uint32_t(std::lround(sampleRate)));
    detail::writeU32(f, uint32_t(std::lround(sampleRate)) * uint32_t(numChannels * bytesPerSample));
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

// 回傳 "dawlab_output/<filename>"（相對於目前工作目錄），並確保資料夾存在。
// 用 ctest 執行時，工作目錄是 build 資料夾。
inline std::string outputPath(const std::string& filename) {
    const std::filesystem::path dir = "dawlab_output";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return (dir / filename).string();
}

} // namespace dawlab
