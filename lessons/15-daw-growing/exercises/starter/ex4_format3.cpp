// Exercise 4 (starter): format 3 - an upgrade, and lines from a newer version.
//
// This is Step 4's reader and writer, moved onto the types of ../ex4_format3.h. It still reads only
// formats 1 and 2 and writes format 2. Make the changes marked TODO; until then the self-check
// prints FAIL (expected). The solution is solutions/ex4_format3.cpp.

#include "../ex4_format3.h"

#include <algorithm>
#include <cmath>
#include <locale>
#include <sstream>
#include <string>

namespace ex4 {

namespace {

// "3" or "3.2": the major and the minor version.
bool parseVersion(const Token& token, int& major, int& minor) {
    // TODO (Exercise 4): accept "N.M" too - both parts whole numbers (parseNumber on each part),
    //                    N >= 1, M >= 0. "3.", ".2", "3.x" and "3.1.1" are not versions.
    minor = 0;
    return parseNumber(token, major) && major >= 1;
}

} // namespace

std::string writeProject(const Project& project) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out.precision(std::numeric_limits<float>::max_digits10);
    // TODO (Exercise 4): format 3; "3.<keptMinor>" when there are kept lines.
    out << "DawLabProject " << 2 << '\n';
    out << "sampleRate " << project.sampleRate << '\n';
    out << "nextTrackId " << project.nextTrackId << '\n';
    for (const TrackState& t : project.tracks)
        out << "track " << t.id.value << ' ' << quote(t.name) << ' ' << t.gainDb << ' ' << t.pan << '\n'; // TODO (Exercise 4): the mute flag
    for (const ClipState& c : project.clips)
        out << "clip " << c.track.value << ' ' << c.start << ' ' << c.length << ' ' << quote(c.file) << '\n';
    // TODO (Exercise 4): the kept lines, as they were.
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
            if (!parseVersion(tok[1], major, minor))
                return fail(lineNo, "bad format version \"" + tok[1].text + "\"");
            if (major > 2) // TODO (Exercise 4): kFormatMajor
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
            const size_t expected = major >= 2 ? 5 : 4; // TODO (Exercise 4): 6 in format 3 (mute)
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
            // TODO (Exercise 4): format 3 - tok[5] is the mute flag, 0 or 1 ("mute must be 0 or 1").
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
        } else {
            // TODO (Exercise 4): in a file of a newer MINOR version (major == kFormatMajor, minor >
            //                    kFormatMinor) keep the line instead: p.keptLines gets `line` as it is,
            //                    p.keptMinor = minor, and a warning such as
            //                    "line 7: \"colour\" is from DawLab format 3.2, kept as it is".
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

#include "../ex4_check.h"
