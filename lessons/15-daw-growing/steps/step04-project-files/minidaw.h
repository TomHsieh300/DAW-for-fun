#pragma once
// minidaw.h - Step 4 snapshot of chapter 15: saving and opening projects.
//
// A project file outlives the program that wrote it: it is opened by a later version, on another
// machine, at another sample rate, sometimes after a crash, sometimes after someone edited it by
// hand. Everything in it is OUTSIDE data. So this step keeps the file at the edge:
//
//   Project  ---- writeProject() ---->  text  ---- saveProjectFile() ---->  disk (.tmp, then rename)
//   Project  <---- readProject() -----  text  <---- openProjectFile() ----  disk
//
//   - writeProject / readProject are pure: text in, text out, no files. The tests feed them strings.
//   - readProject checks everything once, where the data comes in, and reports the line of the
//     first problem. Whatever it returns as ok can be trusted by the rest of the program.
//   - Tracks are saved with their ids (Step 1), so clips - and later sends, automation, undo -
//     find their track again. The id counter is saved too: ids are never reused, not even
//     after closing and opening the project.
//   - The file says which format version and which sample rate it was written with.
//
// This snapshot is the model only (the UI thread). The audio side of Steps 2 and 3 does not
// change; Step 6 builds the engine's tracks from an opened Project.
//
// The format (text, UTF-8, one item per line; '#' starts a comment outside quotes):
//
//   DawLabProject 2                       header: the format version
//   sampleRate 48000                      the rate all sample positions below are in
//   nextTrackId 5                         the id the next new track gets
//   track 2 "Bass" -6 0.25                id, name, gain in dB, pan (-1..1; format 1 had no pan)
//   clip 2 0 96000 "bass.wav"             track id, start and length in samples, audio file
//
// YOUR TURN (Step 4): write writeProject(), readProject() and convertSampleRate() in
// starter/step04.cpp. They are only declared here.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <vector>

namespace minidaw {

// ---- Step 1 (solved) ---------------------------------------------------------------------------

struct TrackId {
    std::uint32_t value = 0;

    constexpr bool isValid() const noexcept { return value != 0; }
    friend constexpr bool operator==(TrackId a, TrackId b) noexcept { return a.value == b.value; }
    friend constexpr bool operator!=(TrackId a, TrackId b) noexcept { return a.value != b.value; }
};

// ---- Step 4: what a project file holds ---------------------------------------------------------

constexpr int kFormatVersion = 2; // 1: no pan; 2: pan added

struct TrackState {
    TrackId id;
    std::string name;
    float gainDb = 0.0f;
    float pan = 0.0f; // -1 left .. +1 right (format 2)
};

struct ClipState {
    TrackId track;
    std::int64_t start = 0;  // samples, at Project::sampleRate
    std::int64_t length = 0; // samples, > 0
    std::string file;
};

struct Project {
    double sampleRate = 48000.0;
    std::uint32_t nextTrackId = 1;
    std::vector<TrackState> tracks; // in display order
    std::vector<ClipState> clips;
};

inline bool operator==(const TrackState& a, const TrackState& b) {
    return a.id == b.id && a.name == b.name && a.gainDb == b.gainDb && a.pan == b.pan;
}
inline bool operator==(const ClipState& a, const ClipState& b) {
    return a.track == b.track && a.start == b.start && a.length == b.length && a.file == b.file;
}
inline bool operator==(const Project& a, const Project& b) {
    return a.sampleRate == b.sampleRate && a.nextTrackId == b.nextTrackId && a.tracks == b.tracks && a.clips == b.clips;
}

struct LoadResult {
    bool ok = false;
    Project project;        // valid only if ok
    std::string error;      // if not ok: "line 4: ...", the first problem found
    int formatVersion = 0;  // the version the file was written in
};

// ---- YOUR TURN (starter/step04.cpp) ------------------------------------------------------------

// The text of the file, in the current format (kFormatVersion). Numbers in the classic "C" locale
// (a decimal point, whatever the user's language), floats with enough digits to read back exactly,
// names and file names quoted with quote().
std::string writeProject(const Project& project);

// Parses and checks a whole file; never throws, never trusts the text. On success the project's
// sample positions are converted to targetSampleRate (if > 0), and nextTrackId is above every
// track id. Accepts format versions 1..kFormatVersion; refuses newer ones with a clear message.
LoadResult readProject(std::string_view text, double targetSampleRate);

// Converts every sample position to newRate (if > 0 and different). Converts each clip's START
// and END, so clips that touched before still touch afterwards; a clip keeps at least 1 sample.
void convertSampleRate(Project& project, double newRate);

// ---- provided: the pieces a parser needs ----------------------------------------------------------

struct Token {
    std::string text; // without the quotes, escapes resolved
    bool quoted = false;
};

// Splits one line into tokens: words separated by spaces or tabs, or "quoted strings" with the
// escapes \" \\ and \n. A '#' outside quotes starts a comment. False (and error set) for an
// unterminated string or an unknown escape.
inline bool splitLine(std::string_view line, std::vector<Token>& out, std::string& error) {
    out.clear();
    size_t i = 0;
    while (i < line.size()) {
        const char c = line[i];
        if (c == ' ' || c == '\t') {
            ++i;
        } else if (c == '#') {
            break;
        } else if (c == '"') {
            Token t{std::string{}, true};
            ++i;
            bool closed = false;
            while (i < line.size()) {
                const char d = line[i++];
                if (d == '"') {
                    closed = true;
                    break;
                }
                if (d == '\\') {
                    if (i == line.size())
                        break;
                    const char e = line[i++];
                    if (e == '"' || e == '\\')
                        t.text += e;
                    else if (e == 'n')
                        t.text += '\n';
                    else {
                        error = std::string("unknown escape \\") + e;
                        return false;
                    }
                } else {
                    t.text += d;
                }
            }
            if (!closed) {
                error = "unterminated string";
                return false;
            }
            out.push_back(std::move(t));
        } else {
            const size_t start = i;
            while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '"' && line[i] != '#')
                ++i;
            out.push_back(Token{std::string(line.substr(start, i - start)), false});
        }
    }
    return true;
}

// The inverse of splitLine for one string: "..." with \" \\ \n escaped.
inline std::string quote(std::string_view s) {
    std::string q = "\"";
    for (const char c : s) {
        if (c == '"' || c == '\\')
            q += '\\', q += c;
        else if (c == '\n')
            q += "\\n";
        else
            q += c;
    }
    return q + "\"";
}

// Number parsing for outside data: the whole token must be the number (no "12abc"), in the
// classic locale, finite, and within range. False otherwise.
template <typename T>
inline bool parseNumber(const Token& token, T& out) {
    if (token.quoted || token.text.empty())
        return false;
    std::istringstream in(token.text);
    in.imbue(std::locale::classic());
    T value{};
    in >> value;
    if (in.fail() || in.peek() != std::char_traits<char>::eof())
        return false;
    if constexpr (std::is_floating_point_v<T>) {
        if (!std::isfinite(value))
            return false;
    } else if constexpr (std::is_unsigned_v<T>) {
        if (token.text[0] == '-') // istream would wrap "-1" around to a huge unsigned value
            return false;
    }
    out = value;
    return true;
}

// ---- provided: the file system edge ----------------------------------------------------------------

// Writes the project next to `path` as "<path>.tmp", then renames it over `path`. A crash or a
// full disk while writing leaves the old file untouched. (A real app also flushes the file to the
// disk before the rename - FlushFileBuffers / fsync - which standard C++ cannot do.)
inline bool saveProjectFile(const Project& project, const std::filesystem::path& path, std::string& error) {
    const std::string text = writeProject(project);
    std::filesystem::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "cannot write " + tmp.string();
            return false;
        }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out) {
            out.close();
            std::error_code ignored;
            std::filesystem::remove(tmp, ignored);
            error = "write error on " + tmp.string();
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec); // replaces an existing file in one step
    if (ec) {
        std::error_code ignored;
        std::filesystem::remove(tmp, ignored);
        error = "cannot replace " + path.string() + ": " + ec.message();
        return false;
    }
    return true;
}

inline LoadResult openProjectFile(const std::filesystem::path& path, double targetSampleRate) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        LoadResult r;
        r.error = "cannot open " + path.string();
        return r;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return readProject(text, targetSampleRate);
}

} // namespace minidaw
