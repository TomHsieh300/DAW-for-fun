// Exercise 4 (solution): format 3 - an upgrade, and lines from a newer version.
//
// Build and run:  l15_ex4_solution   (ctest, label "solution")
// Your version:   exercises/starter/ex4_format3.cpp -> l15_ex4_starter
//
// Step 4's solution with three changes, each marked "Exercise 4": the version "N.M", the mute
// flag, and the kept lines.

#include "../exercises/ex4_format3.h"

#include <algorithm>
#include <cmath>
#include <locale>
#include <sstream>
#include <string>

namespace ex4 {

namespace {

// Exercise 4: "3" or "3.2". Both parts whole numbers, the major at least 1.
bool parseVersion(const Token& token, int& major, int& minor) {
    if (token.quoted)
        return false;
    const size_t dot = token.text.find('.');
    if (!parseNumber(Token{token.text.substr(0, dot), false}, major) || major < 1)
        return false;
    minor = 0;
    if (dot == std::string::npos)
        return true;
    return parseNumber(Token{token.text.substr(dot + 1), false}, minor) && minor >= 0;
}

} // namespace

std::string writeProject(const Project& project) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out.precision(std::numeric_limits<float>::max_digits10);
    // Exercise 4: kept lines come from a newer minor version; say so, or the next open refuses them.
    out << "DawLabProject " << kFormatMajor;
    if (!project.keptLines.empty() && project.keptMinor > kFormatMinor)
        out << '.' << project.keptMinor;
    out << '\n';
    out << "sampleRate " << project.sampleRate << '\n';
    out << "nextTrackId " << project.nextTrackId << '\n';
    for (const TrackState& t : project.tracks)
        out << "track " << t.id.value << ' ' << quote(t.name) << ' ' << t.gainDb << ' ' << t.pan << ' ' << (t.mute ? 1 : 0) << '\n';
    for (const ClipState& c : project.clips)
        out << "clip " << c.track.value << ' ' << c.start << ' ' << c.length << ' ' << quote(c.file) << '\n';
    for (const std::string& line : project.keptLines) // Exercise 4: as they were
        out << line << '\n';
    return out.str();
}

LoadResult readProject(std::string_view text, double targetSampleRate) {
    auto fail = [](int line, const std::string& message) {
        LoadResult r;
        r.error = "line " + std::to_string(line > 0 ? line : 1) + ": " + message;
        return r;
    };
    int lineNo = 0;

    if (text.substr(0, 3) == "\xEF\xBB\xBF")
        text.remove_prefix(3);

    Project p;
    p.sampleRate = 0.0;
    p.nextTrackId = 0;
    int major = 0, minor = 0;
    bool haveRate = false;
    std::vector<int> clipLines;
    std::vector<std::string> warnings;
    std::vector<Token> tok;
    std::string error;

    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string_view::npos)
            end = text.size();
        std::string_view line = text.substr(pos, end - pos);
        pos = end + 1;
        ++lineNo;
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        if (!splitLine(line, tok, error))
            return fail(lineNo, error);
        if (tok.empty())
            continue;
        const std::string& kw = tok[0].text;
        if (tok[0].quoted)
            return fail(lineNo, "a line must start with a keyword");

        if (major == 0) {
            if (kw != "DawLabProject" || tok.size() != 2)
                return fail(lineNo, "not a DawLab project (no \"DawLabProject <version>\" header)");
            if (!parseVersion(tok[1], major, minor)) // Exercise 4
                return fail(lineNo, "bad format version \"" + tok[1].text + "\"");
            if (major > kFormatMajor)
                return fail(lineNo, "made by a newer DawLab (format " + tok[1].text + ", this one reads up to " +
                                        std::to_string(kFormatMajor) + ".x)");
            continue;
        }
        if (!haveRate && kw != "sampleRate")
            return fail(lineNo, "sampleRate must come right after the header");

        if (kw == "sampleRate") {
            if (haveRate)
                return fail(lineNo, "sampleRate given twice");
            if (tok.size() != 2 || !parseNumber(tok[1], p.sampleRate) || p.sampleRate <= 0.0)
                return fail(lineNo, "sampleRate needs one positive number");
            haveRate = true;
        } else if (kw == "nextTrackId") {
            if (tok.size() != 2 || !parseNumber(tok[1], p.nextTrackId))
                return fail(lineNo, "nextTrackId needs one whole number");
        } else if (kw == "track") {
            const size_t expected = major >= 3 ? 6 : (major >= 2 ? 5 : 4); // Exercise 4: mute in format 3
            if (tok.size() != expected)
                return fail(lineNo, "track needs " + std::to_string(expected - 1) + " values in format " + std::to_string(major));
            TrackState t;
            if (!parseNumber(tok[1], t.id.value) || !t.id.isValid())
                return fail(lineNo, "bad track id \"" + tok[1].text + "\"");
            if (!tok[2].quoted)
                return fail(lineNo, "the track name must be in quotes");
            t.name = tok[2].text;
            if (!parseNumber(tok[3], t.gainDb))
                return fail(lineNo, "bad gain \"" + tok[3].text + "\"");
            if (major >= 2 && (!parseNumber(tok[4], t.pan) || t.pan < -1.0f || t.pan > 1.0f))
                return fail(lineNo, "pan must be a number from -1 to 1");
            if (major >= 3) { // Exercise 4
                int mute = -1;
                if (!parseNumber(tok[5], mute) || (mute != 0 && mute != 1))
                    return fail(lineNo, "mute must be 0 or 1");
                t.mute = mute == 1;
            }
            for (const TrackState& other : p.tracks)
                if (other.id == t.id)
                    return fail(lineNo, "track id " + tok[1].text + " used twice");
            p.tracks.push_back(std::move(t));
        } else if (kw == "clip") {
            if (tok.size() != 5)
                return fail(lineNo, "clip needs 4 values");
            ClipState c;
            if (!parseNumber(tok[1], c.track.value) || !c.track.isValid())
                return fail(lineNo, "bad track id \"" + tok[1].text + "\"");
            if (!parseNumber(tok[2], c.start) || c.start < 0)
                return fail(lineNo, "clip start must be 0 or more samples");
            if (!parseNumber(tok[3], c.length) || c.length <= 0)
                return fail(lineNo, "clip length must be at least 1 sample");
            if (!tok[4].quoted)
                return fail(lineNo, "the file name must be in quotes");
            c.file = tok[4].text;
            p.clips.push_back(std::move(c));
            clipLines.push_back(lineNo);
        } else if (major == kFormatMajor && minor > kFormatMinor) {
            // Exercise 4: a newer minor version only adds lines; keep this one for the next save.
            p.keptLines.emplace_back(line);
            p.keptMinor = minor;
            warnings.push_back("line " + std::to_string(lineNo) + ": \"" + kw + "\" is from DawLab format " +
                               std::to_string(major) + "." + std::to_string(minor) + ", kept as it is");
        } else {
            return fail(lineNo, "unknown keyword \"" + kw + "\"");
        }
    }

    if (major == 0)
        return fail(lineNo, "not a DawLab project (empty file)");
    if (!haveRate)
        return fail(lineNo, "no sampleRate");
    std::uint32_t maxId = 0;
    for (const TrackState& t : p.tracks)
        maxId = std::max(maxId, t.id.value);
    for (size_t i = 0; i < p.clips.size(); ++i) {
        const TrackId owner = p.clips[i].track;
        const bool found = std::any_of(p.tracks.begin(), p.tracks.end(), [owner](const TrackState& t) { return t.id == owner; });
        if (!found)
            return fail(clipLines[i], "clip on track " + std::to_string(owner.value) + ", which does not exist");
    }
    if (maxId == std::numeric_limits<std::uint32_t>::max())
        return fail(lineNo, "no track ids left");
    p.nextTrackId = std::max(p.nextTrackId, maxId + 1);

    convertSampleRate(p, targetSampleRate);
    LoadResult r;
    r.ok = true;
    r.project = std::move(p);
    r.major = major;
    r.minor = minor;
    r.warnings = std::move(warnings);
    return r;
}

} // namespace ex4

#include "../exercises/ex4_check.h"
